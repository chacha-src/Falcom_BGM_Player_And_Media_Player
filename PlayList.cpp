// PlayList.cpp : 実装ファイル
//

#include "stdafx.h"
#include "direct.h"
#include "dshow.h"
#include "ogg.h"
#include "oggDlg.h"
#include "ListCtrlA.h"
#include "PlayList.h"
#include "CProToolsDlg.h"
#include "SongParams.h"
#include "ListSyosai.h"
#include "TranscodeExport.h"
#include "TagEditDlg.h"
#include <vector>
#include <algorithm>
#include "Douga.h"
#include "mp3image.h"
#include "CMediaPlayerDlg.h"
#include "CMissingFilesDlg.h"
#include "MpPlayerAddons.h"

static CWnd* GetPlaylistModalOwner(CPlayList* plDlg)
{
	extern CMediaPlayerDlg* mp;
	if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()) && ::IsWindowVisible(mp->GetSafeHwnd()))
		return mp;
	if (plDlg && ::IsWindow(plDlg->GetSafeHwnd()))
		return plDlg;
	extern COggDlg* og;
	if (og && ::IsWindow(og->GetSafeHwnd()))
		return og;
	return AfxGetMainWnd();
}

enum { kPlJakN = 48, kPlJakPx = 24 };
#ifndef WM_PL_MISS_DONE
#define WM_PL_MISS_DONE (WM_APP + 67)
#endif
#ifndef WM_PL_JAK_DONE
#define WM_PL_JAK_DONE (WM_APP + 66)
#endif
static void PlJakMemInit();
static HBITMAP PlJacketGetCb(void* ctx, int row);
static int PlNoteIconGetCb(void* ctx, int row);
static BOOL PlJacketLoadVisible(CPlayList* self, BOOL allowExtract, BOOL bulkDisk = FALSE);
static void PlMissTickScan(CPlayList* self);

static bool DeserializeLogFont(const TCHAR* str, LOGFONT* lf)
{
	if (!str || !lf || _tcslen(str) == 0) return false;
	if (_tcschr(str, '|') == NULL) {
		memset(lf, 0, sizeof(LOGFONT));
		_tcsncpy(lf->lfFaceName, str, LF_FACESIZE - 1);
		lf->lfFaceName[LF_FACESIZE - 1] = 0;
		return false;
	}
	memset(lf, 0, sizeof(LOGFONT));
	TCHAR faceName[LF_FACESIZE] = { 0 };
	int height = 0, width = 0, escapement = 0, orientation = 0, weight = 0;
	int italic = 0, underline = 0, strikeOut = 0, charSet = 0, outPrecision = 0, clipPrecision = 0, quality = 0, pitchAndFamily = 0;
	int parsed = _stscanf(str, _T("%[^|]|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
		faceName, &height, &width, &escapement, &orientation, &weight,
		&italic, &underline, &strikeOut, &charSet, &outPrecision, &clipPrecision, &quality, &pitchAndFamily);
	if (parsed >= 1) {
		_tcsncpy(lf->lfFaceName, faceName, LF_FACESIZE - 1);
		lf->lfFaceName[LF_FACESIZE - 1] = 0;
	}
	if (parsed >= 14) {
		lf->lfHeight = height;
		lf->lfWidth = width;
		lf->lfEscapement = escapement;
		lf->lfOrientation = orientation;
		lf->lfWeight = weight;
		lf->lfItalic = (BYTE)italic;
		lf->lfUnderline = (BYTE)underline;
		lf->lfStrikeOut = (BYTE)strikeOut;
		lf->lfCharSet = (BYTE)charSet;
		lf->lfOutPrecision = (BYTE)outPrecision;
		lf->lfClipPrecision = (BYTE)clipPrecision;
		lf->lfQuality = (BYTE)quality;
		lf->lfPitchAndFamily = (BYTE)pitchAndFamily;
		return true;
	}
	return false;
}
#include "CImageBase.h"
#include "CPlayListNew.h"

// CPlayList ダイアログ

IMPLEMENT_DYNAMIC(CPlayList, CCustomBlurDialogBase)

extern 	CString ext[150][300];
extern 	CString kpif[400];
extern  BOOL kpichk[200];
extern 	int kpicnt;
extern COggDlg *og;
extern BOOL plw;

extern BYTE kvar[150][300];
extern BYTE kvver;

namespace {

static void WavListInfoBytesToTchar(const char* val, TCHAR* out, int outCount)
{
	if (!val || !out || outCount <= 0)
		return;
	out[0] = 0;
	// MB_ERR_INVALID_CHARS = 8: try UTF-8 first, then system ANSI (RIFF INFO is often CP932 on JP Windows)
	if (MultiByteToWideChar(CP_UTF8, 8, val, -1, out, outCount) > 0)
		return;
	MultiByteToWideChar(CP_ACP, 0, val, -1, out, outCount);
}

static void WavReadRiffListInfoTags(const CString& fname, TCHAR* nameOut, TCHAR* artOut, TCHAR* albOut)
{
	CFile f;
	if (!f.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL))
		return;
	ULONGLONG fileLen = f.GetLength();
	BYTE riff[12];
	if (f.Read(riff, 12) != 12) {
		f.Close();
		return;
	}
	if (riff[0] != 'R' || riff[1] != 'I' || riff[2] != 'F' || riff[3] != 'F' ||
	    riff[8] != 'W' || riff[9] != 'A' || riff[10] != 'V' || riff[11] != 'E') {
		f.Close();
		return;
	}
	ULONGLONG pos = 12ULL;
	while (pos + 8ULL <= fileLen) {
		f.Seek((LONGLONG)pos, CFile::begin);
		DWORD chunkId = 0, chunkSize = 0;
		if (f.Read(&chunkId, 4) != 4)
			break;
		if (f.Read(&chunkSize, 4) != 4)
			break;
		DWORD pad = (chunkSize + 1) & ~1u;
		ULONGLONG nextPos = pos + 8ULL + (ULONGLONG)pad;
		if (nextPos > fileLen)
			break;

		if (chunkId == 0x5453494C) { // 'LIST'
			if (chunkSize < 4) {
				pos = nextPos;
				continue;
			}
			DWORD listType = 0;
			if (f.Read(&listType, 4) != 4)
				break;
			if (listType != 0x4F464E49) { // 'INFO'
				pos = nextPos;
				continue;
			}
			ULONGLONG innerEnd = pos + 8ULL + (ULONGLONG)chunkSize;
			ULONGLONG k = (ULONGLONG)f.GetPosition();
			while (k + 8ULL <= innerEnd && k <= fileLen) {
				f.Seek((LONGLONG)k, CFile::begin);
				DWORD subid = 0, subsize = 0;
				if (f.Read(&subid, 4) != 4)
					break;
				if (f.Read(&subsize, 4) != 4)
					break;
				if (subsize > 0x100000u)
					break;
				char val[1024];
				UINT toRead = subsize < (sizeof(val) - 1) ? subsize : (UINT)(sizeof(val) - 1);
				if (toRead > 0) {
					if (f.Read(val, toRead) != toRead)
						break;
					if (subsize > toRead) {
						f.Seek((LONGLONG)(subsize - toRead), CFile::current);
					}
					val[toRead] = 0;
					TCHAR t[1024];
					WavListInfoBytesToTchar(val, t, 1024);
					if (t[0] != 0) {
						if (subid == 0x4D414E49) // INAM
							_tcscpy(nameOut, t);
						else if (subid == 0x54524149) // IART
							_tcscpy(artOut, t);
						else if (subid == 0x44525049 || subid == 0x424C4149) // IPRD or IALB
							_tcscpy(albOut, t);
					}
				}
				k += 8ULL + (ULONGLONG)((subsize + 1u) & ~1u);
			}
		}
		pos = nextPos;
	}
	f.Close();
}

} // namespace

namespace {

class CPlHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_PL_HELP };
	explicit CPlHelpDlg(CPlayList* owner, CWnd* pParent = nullptr)
		: CDialog(IDD, pParent), m_owner(owner) {}
protected:
	CPlayList* m_owner;
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CPlHelpDlg* g_plHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CPlHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CPlHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"プレイリスト操作ガイド", L"Playlist Guide", L"Guide de la liste", L"Guida playlist",
		L"Guía de lista", L"재생 목록 가이드", L"播放列表指南", L"دليل قائمة التشغيل",
		L"Руководство плейлиста", L"Playlist-Anleitung", L"Guia da lista", L"Afspeellijstgids",
		L"Przewodnik listy", L"Çalma listesi kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	(void)m_owner;
	return TRUE;
}

void CPlHelpDlg::OnOK() { DestroyWindow(); }
void CPlHelpDlg::OnCancel() { DestroyWindow(); }
void CPlHelpDlg::OnClose() { DestroyWindow(); }

void CPlHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_plHelpDlg == this)
		g_plHelpDlg = nullptr;
	delete this;
}

BOOL CPlHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CPlHelpDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int footerH = 26;
	rc.bottom -= footerH;
	dc.FillSolidRect(CRect(0, 0, rc.right, rc.bottom + footerH), RGB(248, 248, 252));
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 1;
	CBrush frameBrush(RGB(130, 130, 150));

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
	title(L, y, LL14(L"プレイリスト操作ガイド", L"Playlist — Guide", L"Guide liste", L"Guida playlist",
		L"Guía lista", L"재생 목록 가이드", L"播放列表指南", L"دليل القائمة",
		L"Руководство плейлиста", L"Playlist-Guide", L"Guia lista", L"Afspeellijstgids",
		L"Przewodnik listy", L"Liste kılavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"曲の並び替え・連続/ループ再生・検索・リスト管理をここで行います。",
		L"Reorder tracks, continuous/loop play, search, and manage lists here.",
		L"Réordonnez, lecture continue/boucle, recherche et gestion des listes.",
		L"Riordina, continua/loop, ricerca e gestione liste.",
		L"Reordene, continua/bucle, búsqueda y gestión de listas.",
		L"곡 정렬·연속/루프 재생·검색·목록 관리를 여기서 합니다.",
		L"在此重排曲目、连续/循环播放、搜索并管理列表。",
		L"أعد الترتيب، التشغيل المتواصل/الحلقي، البحث وإدارة القوائم.",
		L"Здесь порядок треков, непрерывный/цикл, поиск и списки.",
		L"Hier sortieren, fortlaufend/Schleife, Suche und Listen verwalten.",
		L"Reordene, contínua/loop, pesquisa e gerencie listas aqui.",
		L"Hier herschikken, doorlopend/lus, zoeken en lijsten beheren.",
		L"Tu przestawiaj, ciągłe/pętla, szukaj i zarządzaj listami.",
		L"Burada sırala, sürekli/döngü, ara ve listeleri yönet."));
	y += lh + 4;

	title(L, y, LL14(L"再生チェック", L"Playback checks", L"Cases lecture", L"Opzioni riproduzione",
		L"Casillas de reproducción", L"재생 체크", L"播放选项", L"خيارات التشغيل",
		L"Флажки воспроизведения", L"Wiedergabe-Optionen", L"Opções de reprodução", L"Afspeelopties",
		L"Opcje odtwarzania", L"Çalma seçenekleri"));
	y += titleLh;
	body(L, y, LL14(L"・連続再生 …… リスト順に次曲へ進みます。再生中の追加でも現在曲は続きます。",
		L"· Continuous …… play next in list order. Adding files keeps current track.",
		L"· Continue …… piste suivante. Ajout en cours = piste actuelle continue.",
		L"· Continua …… brano successivo. Aggiunte non interrompono il corrente.",
		L"· Continua …… siguiente en orden. Añadir no corta la pista actual.",
		L"· 연속 재생 …… 목록 순으로 다음 곡. 재생 중 추가해도 현재 곡 유지.",
		L"· 连续播放 …… 按列表顺序下一曲。播放中添加仍继续当前曲。",
		L"· متواصل …… التالي بالترتيب. الإضافة لا توقف المسار الحالي.",
		L"· Непрерывное …… следующий по списку. Добавление не рвёт текущий трек.",
		L"· Fortlaufend …… nächster Titel. Hinzufügen unterbricht nicht.",
		L"· Contínua …… próxima da lista. Adicionar mantém a faixa atual.",
		L"· Doorlopend …… volgende in volgorde. Toevoegen houdt huidig nummer.",
		L"· Ciągłe …… następny z listy. Dodawanie nie przerywa bieżącego.",
		L"· Sürekli …… listedeki sıradaki. Ekleme mevcut parçayı kesmez.")); y += lh;
	body(L, y, LL14(L"・ループ再生 …… 再生前にチェック。ループ点0の曲(mp3等)が対象です。",
		L"· Loop …… check before play. For tracks with loop point 0 (e.g. mp3).",
		L"· Boucle …… cocher avant lecture. Pistes avec point de boucle 0.",
		L"· Loop …… spunta prima di riprodurre. Tracce con punto loop 0.",
		L"· Bucle …… marcar antes. Pistas con punto de bucle 0.",
		L"· 루프 …… 재생 전 체크. 루프 포인트 0인 곡(mp3 등).",
		L"· 循环 …… 播放前勾选。循环点为0的曲目（如 mp3）。",
		L"· حلقي …… حدد قبل التشغيل. مسارات بنقطة تكرار 0.",
		L"· Цикл …… отметить до старта. Треки с точкой цикла 0.",
		L"· Schleife …… vor Wiedergabe aktivieren. Loop-Punkt 0.",
		L"· Loop …… marcar antes. Faixas com ponto de loop 0.",
		L"· Lus …… vooraf aanvinken. Nummers met luspunt 0.",
		L"· Pętla …… zaznacz przed startem. Utwory z punktem pętli 0.",
		L"· Döngü …… çalmadan önce işaretle. Döngü noktası 0 olanlar.")); y += lh + 4;

	title(L, y, LL14(L"移動・検索", L"Move / search", L"Déplacer / chercher", L"Sposta / cerca",
		L"Mover / buscar", L"이동·검색", L"移动/搜索", L"نقل / بحث",
		L"Перемещение / поиск", L"Verschieben / Suche", L"Mover / pesquisar", L"Verplaatsen / zoeken",
		L"Przenoszenie / szukaj", L"Taşı / ara"));
	y += titleLh;
	body(L, y, LL14(L"・ファイル移動 …… ↑/↓ で選択行を上下、最上/最下へ一括移動。",
		L"· File move …… ↑/↓ reorder selection; top/bottom jump buttons.",
		L"· Déplacer …… ↑/↓ réordonner ; boutons haut/bas absolus.",
		L"· Sposta …… ↑/↓ riordina; pulsanti cima/fondo.",
		L"· Mover …… ↑/↓ reordenar; botones inicio/final.",
		L"· 파일 이동 …… ↑/↓로 선택 행 이동, 맨 위/아래 버튼.",
		L"· 文件移动 …… ↑/↓ 调整顺序；置顶/置底按钮。",
		L"· نقل …… ↑/↓ لإعادة الترتيب؛ أعلى/أسفل.",
		L"· Перемещение …… ↑/↓ порядок; кнопки вверх/вниз списка.",
		L"· Verschieben …… ↑/↓ sortieren; Ganz oben/unten.",
		L"· Mover …… ↑/↓ reordenar; topo/fim.",
		L"· Verplaatsen …… ↑/↓ herschikken; top/onder.",
		L"· Przenieś …… ↑/↓ kolejność; góra/dół listy.",
		L"· Taşı …… ↑/↓ sırala; en üst/en alt.")); y += lh;
	body(L, y, LL14(L"・あいまい検索 …… キーワード入力後、上下検索で名前/アーティスト等を探します。",
		L"· Fuzzy search …… type a keyword, then search up/down in name/artist/etc.",
		L"· Recherche floue …… mot-clé puis chercher haut/bas (nom/artiste…).",
		L"· Fuzzy …… parola chiave, poi cerca su/giù (nome/artista…).",
		L"· Difusa …… palabra clave y buscar arriba/abajo (nombre/artista…).",
		L"· 퍼지 검색 …… 키워드 입력 후 위/아래로 이름·아티스트 등 검색.",
		L"· 模糊搜索 …… 输入关键字后用上下搜索名称/艺术家等。",
		L"· بحث غامض …… أدخل كلمة ثم ابحث أعلى/أسفل.",
		L"· Нечёткий поиск …… ключ, затем вверх/вниз по полям.",
		L"· Fuzzy …… Stichwort, dann auf/ab in Name/Interpret usw.",
		L"· Fuzzy …… palavra-chave e buscar cima/baixo.",
		L"· Fuzzy …… zoekterm, dan omhoog/omlaag in velden.",
		L"· Rozmyte …… słowo kluczowe, potem góra/dół w polach.",
		L"· Bulanık …… anahtar kelime, sonra yukarı/aşağı ara.")); y += lh + 4;

	title(L, y, LL14(L"リスト操作", L"List actions", L"Actions liste", L"Azioni lista",
		L"Acciones de lista", L"목록 조작", L"列表操作", L"عمليات القائمة",
		L"Действия со списком", L"Listenaktionen", L"Ações da lista", L"Lijstacties",
		L"Akcje listy", L"Liste işlemleri"));
	y += titleLh;
	body(L, y, LL14(L"・D&D …… ファイル/フォルダをリストへドロップで追加。行ドラッグで並べ替え。",
		L"· D&D …… drop files/folders onto the list. Drag rows to reorder.",
		L"· D&D …… déposer fichiers/dossiers. Glisser les lignes pour trier.",
		L"· D&D …… rilascia file/cartelle. Trascina le righe per riordinare.",
		L"· D&D …… soltar archivos/carpetas. Arrastrar filas para ordenar.",
		L"· D&D …… 파일/폴더를 목록에 드롭. 행 드래그로 정렬.",
		L"· 拖放 …… 将文件/文件夹拖到列表添加。拖行可重排。",
		L"· سحب وإفلات …… أضف بالسحب. اسحب الصفوف للترتيب.",
		L"· D&D …… перетащите файлы/папки. Строки — для порядка.",
		L"· D&D …… Dateien/Ordner ablegen. Zeilen ziehen zum Sortieren.",
		L"· D&D …… solte arquivos/pastas. Arraste linhas para ordenar.",
		L"· D&D …… sleep bestanden/mappen. Rijen slepen om te ordenen.",
		L"· D&D …… upuść pliki/foldery. Przeciągnij wiersze.",
		L"· Sürükle-bırak …… dosya/klasör ekle. Satır sürükleyerek sırala.")); y += lh;
	body(L, y, LL14(L"・名前変更 / リスト削除 …… 表示中のプレイリスト名変更・削除（削除は復元不可）。",
		L"· Rename / Delete list …… rename or remove the current playlist (no undo).",
		L"· Renommer / Supprimer …… liste affichée (suppression définitive).",
		L"· Rinomina / Elimina …… playlist corrente (eliminazione definitiva).",
		L"· Renombrar / Eliminar …… lista actual (sin recuperación).",
		L"· 이름 변경 / 목록 삭제 …… 현재 목록 (삭제 후 복구 불가).",
		L"· 重命名/删除列表 …… 当前列表（删除不可恢复）。",
		L"· إعادة تسمية / حذف …… القائمة الحالية (لا استرداد).",
		L"· Переименовать / Удалить …… текущий список (без отмены).",
		L"· Umbenennen / Löschen …… aktuelle Liste (kein Undo).",
		L"· Renomear / Excluir …… lista atual (sem desfazer).",
		L"· Hernoemen / Verwijderen …… huidige lijst (geen herstel).",
		L"· Zmień nazwę / Usuń …… bieżąca lista (bez cofania).",
		L"· Yeniden adlandır / Sil …… geçerli liste (geri alınamaz).")); y += lh;
	body(L, y, LL14(L"・簡易ピアノロール …… 再生中の音程を鍵盤表示で確認します。",
		L"· Simple Piano Roll …… show live pitch on a keyboard layout.",
		L"· Rouleau piano …… hauteur en direct sur un clavier.",
		L"· Piano roll …… altezza live su tastiera.",
		L"· Piano roll …… tono en vivo en teclado.",
		L"· 간이 피아노 롤 …… 재생 중 음정을 건반으로 표시.",
		L"· 简易钢琴卷帘 …… 以键盘显示正在播放的音高。",
		L"· لوحة بيانو …… طبقة الصوت على مفاتيح.",
		L"· Пианоролл …… высота звука на клавиатуре.",
		L"· Klavierrolle …… Tonhöhe live als Tastatur.",
		L"· Piano roll …… altura ao vivo no teclado.",
		L"· Pianorol …… toonhoogte live op toetsenbord.",
		L"· Rolka pianina …… wysokość dźwięku na klawiaturze.",
		L"· Piyano rulosu …… canlı perdeyi klavyede gösterir.")); y += lh + 4;

	title(L, y, LL14(L"途中保存・表示", L"Resume / display", L"Reprise / affichage", L"Ripresa / display",
		L"Reanudar / pantalla", L"이어듣기·표시", L"续播/显示", L"استئناف / عرض",
		L"Возобновление / вид", L"Fortsetzen / Anzeige", L"Retomar / exibição", L"Hervatten / weergave",
		L"Wznów / widok", L"Sürdür / görünüm"));
	y += titleLh;
	body(L, y, LL14(L"・再生位置を保存 + 音源 / DShow …… 内蔵音源・動画で停止位置を記憶。",
		L"· Save position + Audio / DShow …… remember stop point for built-in & video.",
		L"· Position + Audio / DShow …… mémoriser l'arrêt (intégré & vidéo).",
		L"· Posizione + Audio / DShow …… ricorda lo stop (interno & video).",
		L"· Posición + Audio / DShow …… recordar parada (interno y vídeo).",
		L"· 재생 위치 저장 + 음원/DShow …… 내장·동영상 정지 위치 기억.",
		L"· 保存位置 + 音源/DShow …… 记住内置与视频的停止位置。",
		L"· حفظ الموضع + صوت/DShow …… تذكر موضع الإيقاف.",
		L"· Позиция + Аудио / DShow …… помнить стоп для встроенных и видео.",
		L"· Position + Audio / DShow …… Stopp für eingebaut & Video merken.",
		L"· Posição + Áudio / DShow …… lembrar parada (interno e vídeo).",
		L"· Positie + Audio / DShow …… onthoud stop (ingebouwd & video).",
		L"· Pozycja + Audio / DShow …… zapamiętaj stop (wbudowane i wideo).",
		L"· Konum + Ses / DShow …… dahili ve video durma noktasını kaydet.")); y += lh;
	body(L, y, LL14(L"・ツールチップ表示 …… コントロールとリスト行の説明を出します。",
		L"· Show tooltips …… tips for controls and list rows.",
		L"· Info-bulles …… pour contrôles et lignes de liste.",
		L"· Suggerimenti …… per controlli e righe.",
		L"· Sugerencias …… para controles y filas.",
		L"· 도구 설명 …… 컨트롤·목록 행 설명 표시.",
		L"· 工具提示 …… 显示控件与列表行说明。",
		L"· تلميحات …… لعناصر التحكم وصفوف القائمة.",
		L"· Подсказки …… для элементов и строк списка.",
		L"· Tooltips …… für Steuerelemente und Zeilen.",
		L"· Dicas …… para controles e linhas.",
		L"· Tooltips …… voor bediening en rijen.",
		L"· Etykiety …… dla kontrolek i wierszy.",
		L"· İpuçları …… denetimler ve satırlar için.")); y += lh;
	body(L, y, LL14(L"・最小化、復帰 …… オン時はメイン画面とプレイリストを同時に最小化/復帰。",
		L"· Minimize, restore …… when on, main + playlist minimize/restore together.",
		L"· Réduire/restaurer …… principal + liste ensemble si coché.",
		L"· Riduci/ripristina …… principale + playlist insieme.",
		L"· Minimizar/restaurar …… principal + lista juntos.",
		L"· 최소화·복원 …… 켜면 메인과 목록을 함께.",
		L"· 最小化/还原 …… 开启时主窗口与列表同步。",
		L"· تصغير/استعادة …… الرئيسية والقائمة معاً.",
		L"· Свернуть/восстановить …… главное и список вместе.",
		L"· Minimieren …… Hauptfenster + Playlist gemeinsam.",
		L"· Minimizar …… principal + lista juntos.",
		L"· Minimaliseren …… hoofd + lijst samen.",
		L"· Minimalizuj …… główne + lista razem.",
		L"· Küçült …… ana pencere + liste birlikte.")); y += lh + 6;

	// mini toolbar region diagram
	title(L, y, LL14(L"画面レイアウト", L"Layout map", L"Plan de la fenêtre", L"Mappa finestra",
		L"Mapa de ventana", L"화면 레이아웃", L"界面布局", L"تخطيط النافذة",
		L"Схема окна", L"Fensterlayout", L"Mapa da janela", L"Vensterindeling",
		L"Układ okna", L"Pencere düzeni"));
	y += titleLh;
	const int gx = L, gy = y, gw = min(420, rc.Width() - L * 2), gh = lh * 3 + 16;
	dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
	const int pad = 4;
	const int bw1 = (gw - pad * 5) / 4;
	const int bh = gh - pad * 2 - lh - 2;
	const int by = gy + pad;
	dc.FillSolidRect(gx + pad, by, bw1, bh, RGB(70, 140, 90));
	dc.FillSolidRect(gx + pad * 2 + bw1, by, bw1, bh, RGB(180, 140, 60));
	dc.FillSolidRect(gx + pad * 3 + bw1 * 2, by, bw1, bh, RGB(55, 90, 140));
	dc.FillSolidRect(gx + pad * 4 + bw1 * 3, by, bw1, bh, RGB(150, 70, 70));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + pad + 6, by + 4, LL14(L"移動", L"Move", L"Dépl.", L"Sposta", L"Mover", L"이동", L"移动", L"نقل", L"Перем.", L"Versch.", L"Mover", L"Verpl.", L"Przen.", L"Taşı"));
	dc.TextOut(gx + pad * 2 + bw1 + 6, by + 4, LL14(L"検索", L"Find", L"Rech.", L"Cerca", L"Buscar", L"검색", L"搜索", L"بحث", L"Поиск", L"Suche", L"Pesq.", L"Zoek", L"Szukaj", L"Ara"));
	dc.TextOut(gx + pad * 3 + bw1 * 2 + 6, by + 4, LL14(L"リスト", L"List", L"Liste", L"Lista", L"Lista", L"목록", L"列表", L"قائمة", L"Список", L"Liste", L"Lista", L"Lijst", L"Lista", L"Liste"));
	dc.TextOut(gx + pad * 4 + bw1 * 3 + 6, by + 4, LL14(L"保存", L"Save", L"Sauver", L"Salva", L"Guardar", L"저장", L"保存", L"حفظ", L"Сохр.", L"Speich.", L"Salvar", L"Opslaan", L"Zapisz", L"Kaydet"));
	dc.SetTextColor(RGB(80, 80, 95));
	dc.TextOut(gx + pad, gy + pad + bh + 2, LL14(
		L"↑↓移動 | あいまい検索 | コンボ/改名/削除/ピアノ | 位置保存+音源",
		L"↑↓ move | fuzzy find | combo/rename/del/piano | resume+audio",
		L"↑↓ | recherche | combo/renommer/suppr./piano | reprise",
		L"↑↓ | fuzzy | combo/rinomina/elim./piano | ripresa",
		L"↑↓ | difusa | combo/renombrar/elim./piano | reanudar",
		L"↑↓ 이동 | 퍼지 검색 | 콤보/이름/삭제/피아노 | 이어듣기",
		L"↑↓移动 | 模糊搜索 | 组合/改名/删除/钢琴 | 续播",
		L"↑↓ | بحث | قائمة/إعادة تسمية | استئناف",
		L"↑↓ | поиск | комбо/имя/удал./piano | позиция",
		L"↑↓ | Suche | Kombo/Name/Lösch./Piano | Position",
		L"↑↓ | fuzzy | combo/renomear/excl./piano | retomar",
		L"↑↓ | zoeken | combo/hernoemen/verw./piano | hervatten",
		L"↑↓ | szukaj | combo/nazwa/usuń/piano | wznów",
		L"↑↓ | ara | kombo/ad/sil/piyano | sürdür"));
	dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
	y = gy + gh + 4;
	if (y < rc.bottom - 4) {
		muted(L, y, LL14(
			L"右クリックメニューからも詳細操作や「操作ガイド」を開けます。",
			L"Right-click the list for more actions and this guide.",
			L"Clic droit sur la liste pour plus d'actions et ce guide.",
			L"Tasto destro sulla lista per altre azioni e questa guida.",
			L"Clic derecho en la lista para más acciones y esta guía.",
			L"목록 우클릭으로 상세 조작과 이 가이드를 열 수 있습니다.",
			L"右键列表可进行更多操作并打开本指南。",
			L"انقر يميناً على القائمة لمزيد من الإجراءات وهذا الدليل.",
			L"ПКМ по списку — доп. действия и это руководство.",
			L"Rechtsklick auf die Liste für mehr Aktionen und diesen Guide.",
			L"Clique direito na lista para mais ações e este guia.",
			L"Rechtsklik op de lijst voor meer acties en deze gids.",
			L"PPM na liście — więcej akcji i ten przewodnik.",
			L"Listede sağ tık ile daha fazla işlem ve bu kılavuz."));
	}

	dc.SelectObject(oldFont);
}

} // namespace

CPlayList::CPlayList(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CPlayList::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDI_PL);
	pc=NULL;
	plw=0;
	playcnt=0;
//	pc = new playlistdata0[60000];
}

CPlayList::~CPlayList()
{
	if (pc) {
		free(pc);
		pc = NULL;
	}
	m_tooltip.DestroyWindow();
}

void CPlayList::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_BUTTON1, m_lsup);
	DDX_Control(pDX, IDC_BUTTON5, m_lup);
	DDX_Control(pDX, IDC_BUTTON10, m_lsdown);
	DDX_Control(pDX, IDC_BUTTON11, m_ldown);
	DDX_Control(pDX, IDC_LIST1, m_lc);
	DDX_Control(pDX, IDC_EDIT1, m_e);
	DDX_Control(pDX, IDC_CHECK1, m_renzoku);
	DDX_Control(pDX, IDC_CHECK4, m_loop);
	DDX_Control(pDX, IDC_CHECK28, m_tool);
	DDX_Control(pDX, IDC_CHECK29, m_saisyo);
	DDX_Control(pDX, IDC_EDIT2, m_find);
	DDX_Control(pDX, IDC_BUTTON16, m_findup);
	DDX_Control(pDX, IDC_BUTTON20, m_finddown);
	DDX_Control(pDX, IDC_CHECK5, m_savecheck);
	DDX_Control(pDX, IDC_CHECK6, m_save_mp3);
	DDX_Control(pDX, IDC_CHECK7, m_save_kpi);
	DDX_Control(pDX, IDC_COMBO1, m_listchange);
	DDX_Control(pDX, IDC_BUTTON3, m_namechage);
	DDX_Control(pDX, IDC_PLAYDELETE, m_listdelete);
	DDX_Control(pDX, IDC_PIANOROLL, m_pianorollBtn);
	DDX_Control(pDX, IDC_PL_HELP, m_help);
}


BEGIN_MESSAGE_MAP(CPlayList, CCustomBlurDialogBase)
	ON_WM_NCDESTROY()
	ON_WM_CREATE()
	ON_WM_CLOSE()
	ON_BN_CLICKED(IDOK, &CPlayList::OnBnClickedOk)
	ON_BN_CLICKED(IDC_BUTTON1, &CPlayList::OnUP)
	ON_BN_CLICKED(IDC_BUTTON5, &CPlayList::OnSUP)
	ON_BN_CLICKED(IDC_BUTTON10, &CPlayList::OnSDOWN)
	ON_BN_CLICKED(IDC_BUTTON11, &CPlayList::OnDOWN)
	ON_NOTIFY(LVN_KEYDOWN, IDC_LIST1, &CPlayList::OnLvnKeydownList1)
	ON_WM_DROPFILES()
	ON_NOTIFY(NM_DBLCLK, IDC_LIST1, &CPlayList::OnNMDblclkList1)
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_KEYDOWN()
	ON_BN_CLICKED(IDC_CHECK4, &CPlayList::OnBnClickedCheck4)
	ON_BN_CLICKED(IDC_CHECK1, &CPlayList::OnBnClickedCheck1)
	ON_NOTIFY(LVN_BEGINDRAG, IDC_LIST1, &CPlayList::OnLvnBegindragList1)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST1, &CPlayList::OnLvnGetdispinfoList1)
	ON_NOTIFY(NM_RCLICK, IDC_LIST1, &CPlayList::OnNMRclickList1)
	ON_COMMAND(ID_POP_32776, OnList)
	ON_COMMAND(ID_POP_32777,Del)
	ON_COMMAND(ID_POP_WAVEXPORT, &CPlayList::OnPopWavExport)
	ON_WM_ACTIVATE()
	ON_COMMAND(ID_POP_32787, &CPlayList::OnPop32787)
	ON_BN_CLICKED(IDC_BUTTON16, &CPlayList::OnFindUp)
	ON_BN_CLICKED(IDC_BUTTON20, &CPlayList::OnFindDown)
	ON_BN_CLICKED(IDC_CHECK6, &CPlayList::OnBnClickedCheck6mp3)
	ON_BN_CLICKED(IDC_CHECK7, &CPlayList::OnBnClickedCheck7dshow)
	ON_WM_CTLCOLOR()
	ON_WM_SHOWWINDOW()
	ON_WM_MOVING()
	ON_WM_SIZING()
	ON_WM_SETFOCUS()
	ON_WM_NCACTIVATE()
	ON_CBN_SELCHANGE(IDC_COMBO1, &CPlayList::OnCbnSelchangeCombo1)
	ON_BN_CLICKED(IDC_BUTTON3, &CPlayList::OnBnClickedButton3)
	ON_BN_CLICKED(IDC_PLAYDELETE, &CPlayList::OnBnClickedPlaydelete)
	ON_BN_CLICKED(IDC_PIANOROLL, &CPlayList::OnBnClickedPianoroll)
	ON_BN_CLICKED(IDC_PL_HELP, &CPlayList::OnBnClickedHelp)
	ON_WM_DESTROY()
#if CCUSTOM_AERO_SUPPORT
	ON_MESSAGE(CCC_MSG_REAPPLY_OPAQUE_FIXERS, OnReapplyOpaqueFixers)
#endif
	ON_MESSAGE(WM_PL_MISS_DONE, &CPlayList::OnPlMissDone)
	ON_MESSAGE(WM_PL_JAK_DONE, &CPlayList::OnPlJakDone)
END_MESSAGE_MAP()

static const UINT_PTR kPlayListNavRefreshTimer = 4945;

static void ClampPlaylistSelectionIndices(CPlayList* pl)
{
	if (!pl || pl->playcnt <= 0) {
		if (pl) {
			pl->pnt = -1;
			pl->pnt1 = -1;
		}
		return;
	}
	if (pl->pnt < 0 || pl->pnt >= pl->playcnt)
		pl->pnt = -1;
	if (pl->pnt1 < 0 || pl->pnt1 >= pl->playcnt)
		pl->pnt1 = -1;
}

static bool PlaylistItemMatchesKeyword(const playlistdata0& item, const CString& keywordLower)
{
	const TCHAR* fields[] = {
		item.name, item.art, item.alb, item.fol, item.game
	};
	for (const TCHAR* field : fields) {
		if (!field || !*field)
			continue;
		CString ssl(field);
		ssl.MakeLower();
		if (ssl.Find(keywordLower) != -1)
			return true;
	}
	return false;
}

static int GetFuzzySearchAnchor(const CPlayList* pl)
{
	if (!pl || pl->playcnt <= 0)
		return -1;
	int anchor = pl->pnt;
	if (pl->pnt1 != -1)
		anchor = pl->pnt1;
	if (anchor < 0 || anchor >= pl->playcnt)
		return -1;
	return anchor;
}

#include <eh.h>
class SE_Exception1
{
private:
    unsigned int nSE;
public:
    SE_Exception1() {}
    SE_Exception1( unsigned int n ) : nSE( n ) {}
    ~SE_Exception1() {}
    unsigned int getSeNumber() { return nSE; }
};
void trans_func1( unsigned int, EXCEPTION_POINTERS* );
void trans_func1( unsigned int u, EXCEPTION_POINTERS* pExp )
{
    throw SE_Exception1();
}


float hD2;
int syo;
int syomode;
CString syos;
extern TCHAR karento2[1024];
extern int fade1;
extern IMediaPosition *pMediaPosition;
extern int mode,videoonly,playf,plf;
extern int plcnt;
extern int playy;
extern save savedata;
extern CPlayList* pl;
CImageBase* playbase;
int ogpl = 0;

BOOL CPlayList::OnInitDialog()
{
	// AddTool(NULL) / ImageList 失敗は ENSURE→「引数が正しくありません」。外側へ上げない。
	try {
	CCustomBlurDialogBase::OnInitDialog();

	CDC *desktopDc = GetDC();
	// Get native resolution
	int horizontalDPI = GetDeviceCaps(desktopDc->m_hDC, LOGPIXELSX);
	hD2 = (float)(horizontalDPI) / (96.0f);
	ReleaseDC(desktopDc);

	playcnt=0;
	w_flg=TRUE;
	pnt=0;
	SetIcon(m_hIcon, TRUE);			// 大きいアイコンを設定
	SetIcon(m_hIcon, FALSE);		// 小さいアイコンを設定
	SetWindowText(LL14(L"プレイリスト", L"Playlist", L"Liste de lecture", L"Playlist", L"Lista de reproduccion", L"재생 목록", L"播放列表", L"قائمة التشغيل", L"Плейлист", L"Wiedergabeliste", L"Lista de reproducao", L"Afspeellijst", L"Lista odtwarzania", L"Calma listesi"));
	SetDlgItemText(IDC_CHECK1, LL14(L"連続再生", L"Continuous play", L"Lecture continue", L"Riproduzione continua", L"Reproduccion continua", L"연속 재생", L"连续播放", L"تشغيل متواصل", L"Непрерывное воспроизведение", L"Fortlaufende Wiedergabe", L"Reproducao continua", L"Doorlopend afspelen", L"Ciągłe odtwarzanie", L"Surekli calma"));
	SetDlgItemText(IDC_CHECK4, LL14(L"ループ再生", L"Loop play", L"Lecture en boucle", L"Riproduzione in loop", L"Reproduccion en bucle", L"루프 재생", L"循环播放", L"تشغيل حلقي", L"Зацикленное воспроизведение", L"Schleifenwiedergabe", L"Reproducao em loop", L"Herhalend afspelen", L"Odtwarzanie w pętli", L"Dongude calma"));
	SetDlgItemText(IDC_CHECK28, LL14(L"ツールチップ表示", L"Show tooltips", L"Afficher les info-bulles", L"Mostra suggerimenti", L"Mostrar sugerencias", L"도구 설명 표시", L"显示工具提示", L"إظهار تلميحات الأدوات", L"Показывать подсказки", L"Tooltips anzeigen", L"Mostrar dicas", L"Tooltips tonen", L"Pokaż etykiety", L"İpuçlarını göster"));
	SetDlgItemText(IDC_CHECK29, LL14(L"最小化、復帰", L"Minimize, restore", L"Reduire, restaurer", L"Riduci, ripristina", L"Minimizar, restaurar", L"최소화, 복원", L"最小化、还原", L"تصغير، استعادة", L"Свернуть, восстановить", L"Minimieren, wiederherstellen", L"Minimizar, restaurar", L"Minimaliseren, herstellen", L"Minimalizuj, przywróć", L"Kucult, geri yukle"));
	SetDlgItemText(IDC_CHECK5, LL14(L"再生位置\nを保存", L"Save\nplayback position", L"Enregistrer la\nposition de lecture", L"Salva posizione\ndi riproduzione", L"Guardar posicion\nde reproduccion", L"재생 위치\n저장", L"保存\n播放位置", L"حفظ موضع\nالتشغيل", L"Сохранить позицию\nвоспроизведения", L"Wiedergabeposition\nspeichern", L"Salvar posicao\nde reproducao", L"Afspeelpositie\nopslaan", L"Zapisz pozycję\nodtwarzania", L"Oynatma konumunu\nkaydet"));
	SetDlgItemText(IDC_CHECK6, LL14(L"音源", L"Audio", L"Audio", L"Audio", L"Audio", L"음원", L"音源", L"صوت", L"Аудио", L"Audio", L"Audio", L"Audio", L"Audio", L"Ses"));
	SetDlgItemText(IDC_STATICido, LL14(L"ファイル移動", L"File move", L"Deplacer fichier", L"Sposta file", L"Mover archivo", L"파일 이동", L"文件移动", L"نقل الملف", L"Переместить файл", L"Datei verschieben", L"Mover arquivo", L"Bestand verplaatsen", L"Przenieś plik", L"Dosya taşı"));
	SetDlgItemText(IDC_STATICken, LL14(L"あいまい検索", L"Fuzzy search", L"Recherche floue", L"Ricerca fuzzy", L"Busqueda difusa", L"퍼지 검색", L"模糊搜索", L"بحث غامض", L"Нечеткий поиск", L"Fuzzy-Suche", L"Pesquisa fuzzy", L"Fuzzy zoeken", L"Wyszukiwanie rozmyte", L"Bulanık arama"));
	SetDlgItemText(IDC_BUTTON3, LL14(L"名前変更", L"Rename", L"Renommer", L"Rinomina", L"Cambiar nombre", L"이름 바꾸기", L"重命名", L"إعادة التسمية", L"Переименовать", L"Umbenennen", L"Renomear", L"Hernoemen", L"Zmień nazwę", L"Yeniden adlandır"));
	SetDlgItemText(IDC_PLAYDELETE, LL14(L"リスト削除", L"Delete list", L"Supprimer la liste", L"Elimina lista", L"Eliminar lista", L"목록 삭제", L"删除列表", L"حذف القائمة", L"Удалить список", L"Liste loschen", L"Excluir lista", L"Lijst verwijderen", L"Usuń listę", L"Listeyi sil"));
	SetDlgItemText(IDC_PIANOROLL, LL14(L"簡易ピアノロール", L"Simple Piano Roll", L"Rouleau piano simple", L"Rotolo pianoforte semplice", L"Rollo de piano simple", L"간이 피아노 롤", L"简易钢琴卷帘", L"لوحة بيانو بسيطة", L"Простой пианоролл", L"Einfache Klavierrolle", L"Rolo de piano simples", L"Eenvoudige pianorol", L"Prosta rolka pianina", L"Basit piyano rulosu"));
	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	auto addTip = [this](UINT id, LPCTSTR text) {
		if (!m_tooltip.GetSafeHwnd() || !text) return;
		CWnd* w = GetDlgItem(id);
		HWND hw = w ? w->GetSafeHwnd() : NULL;
		if (!hw || !::IsWindow(hw)) return;
		m_tooltip.AddTool(w, text);
	};
	addTip(IDOK, LL14(L"プレイリストを閉じます。", L"Close the playlist.", L"Fermer la liste de lecture.", L"Chiudi la playlist.", L"Cerrar la lista de reproduccion.", L"재생 목록을 닫습니다.", L"关闭播放列表。", L"إغلاق قائمة التشغيل.", L"Закрыть плейлист.", L"Wiedergabeliste schliesen.", L"Fechar lista de reproducao.", L"Afspeellijst sluiten.", L"Zamknij listę odtwarzania.", L"Calma listesini kapat."));
	addTip(IDC_BUTTON1, LL14(L"選択項目を一番上に持って行きます。", L"Move selected item to the top.", L"Deplacer l'element selectionne tout en haut.", L"Sposta l'elemento selezionato in cima.", L"Mover elemento seleccionado al inicio.", L"선택한 항목을 맨 위로 이동.", L"将所选项目移至顶部。", L"نقل العنصر المحدد إلى الأعلى.", L"Переместить выбранный элемент вверх.", L"Gewahltes Element nach oben verschieben.", L"Mover item selecionado para o topo.", L"Geselecteerd item naar boven verplaatsen.", L"Przenieś zaznaczony element na górę.", L"Seçili öğeyi en üste taşı."));
	addTip(IDC_BUTTON5, LL14(L"選択項目を上に持って行きます。", L"Move selected item up.", L"Deplacer l'element selectionne vers le haut.", L"Sposta l'elemento selezionato in alto.", L"Mover elemento seleccionado arriba.", L"선택한 항목을 위로 이동.", L"将所选项目上移。", L"نقل العنصر المحدد لأعلى.", L"Переместить выбранный элемент вверх.", L"Gewahltes Element nach oben verschieben.", L"Mover item selecionado para cima.", L"Geselecteerd item omhoog verplaatsen.", L"Przenieś zaznaczony element w górę.", L"Seçili öğeyi yukarı taşı."));
	addTip(IDC_BUTTON10, LL14(L"選択項目を一番下に持って行きます。", L"Move selected item to the bottom.", L"Deplacer l'element selectionne tout en bas.", L"Sposta l'elemento selezionato in fondo.", L"Mover elemento seleccionado al final.", L"선택한 항목을 맨 아래로 이동.", L"将所选项目移至底部。", L"نقل العنصر المحدد إلى الأسفل.", L"Переместить выбранный элемент вниз.", L"Gewahltes Element nach unten verschieben.", L"Mover item selecionado para o final.", L"Geselecteerd item naar beneden verplaatsen.", L"Przenieś zaznaczony element na dół.", L"Seçili öğeyi en alta taşı."));
	addTip(IDC_BUTTON11, LL14(L"選択項目を下に持って行きます。", L"Move selected item down.", L"Deplacer l'element selectionne vers le bas.", L"Sposta l'elemento selezionato in basso.", L"Mover elemento seleccionado abajo.", L"선택한 항목을 아래로 이동.", L"将所选项目下移。", L"نقل العنصر المحدد لأسفل.", L"Переместить выбранный элемент вниз.", L"Gewahltes Element nach unten verschieben.", L"Mover item selecionado para baixo.", L"Geselecteerd item omlaag verplaatsen.", L"Przenieś zaznaczony element w dół.", L"Seçili öğeyi aşağı taşı."));
	addTip(IDC_BUTTON16, LL14(L"現在の位置から下に検索します。", L"Search downward from current position.", L"Rechercher vers le bas a partir de la position actuelle.", L"Cerca verso il basso dalla posizione corrente.", L"Buscar hacia abajo desde la posicion actual.", L"현재 위치부터 아래로 검색.", L"从当前位置向下搜索。", L"البحث للأسفل من الموضع الحالي.", L"Искать вниз от текущей позиции.", L"Ab aktueller Position nach unten suchen.", L"Pesquisar para baixo a partir da posicao atual.", L"Zoek naar beneden vanaf de huidige positie.", L"Szukaj w dół od bieżącej pozycji.", L"Mevcut konumdan aşağı doğru ara."));
	addTip(IDC_BUTTON20, LL14(L"現在の位置から上に検索します。", L"Search upward from current position.", L"Rechercher vers le haut a partir de la position actuelle.", L"Cerca verso l'alto dalla posizione corrente.", L"Buscar hacia arriba desde la posicion actual.", L"현재 위치부터 위로 검색.", L"从当前位置向上搜索。", L"البحث للأعلى من الموضع الحالي.", L"Искать вверх от текущей позиции.", L"Ab aktueller Position nach oben suchen.", L"Pesquisar para cima a partir da posicao atual.", L"Zoek naar boven vanaf de huidige positie.", L"Szukaj w górę od bieżącej pozycji.", L"Mevcut konumdan yukarı doğru ara."));
	addTip(IDC_CHECK1, LL14(L"プレイリストの順番に再生を行います。\n再生中にファイルドロップして追加しても演奏中の曲はそのまま鳴り続けます。", L"Playback in playlist order.\nEven if files are added during playback, the currently playing track continues.", L"Lecture dans l'ordre de la liste.\nLa piste en cours continue meme si des fichiers sont ajoutes pendant la lecture.", L"Riproduzione nell'ordine della playlist.\nAnche se aggiungi file durante la riproduzione, la traccia corrente continua.", L"Reproduccion en orden de la lista.\nAunque se anadan archivos durante la reproduccion, la pista actual continua.", L"재생 목록 순서대로 재생.\n재생 중 파일 추가해도 현재 트랙은 계속 재생됨.", L"按播放列表顺序播放。\n播放期间添加文件时，当前曲目仍会播放。", L"تشغيل بترتيب القائمة.\nعند إضافة ملفات أثناء التشغيل، يستمر المسار الحالي.", L"Воспроизведение по порядку плейлиста.\nДаже при добавлении файлов текущий трек продолжает воспроизводиться.", L"Wiedergabe in Playlist-Reihenfolge.\nBei zusatzlichen Dateien wahrend der Wiedergabe lauft der aktuelle Titel weiter.", L"Reproducao na ordem da lista.\nMesmo ao adicionar arquivos durante a reproducao, a faixa atual continua.", L"Afspeel in playlistvolgorde.\nBij toevoegen van bestanden tijdens afspelen gaat het huidige nummer door.", L"Odtwarzaj w kolejności listy.\nPrzy dodawaniu plików podczas odtwarzania aktualny utwór kontynuuje.", L"Liste sırasına göre çalma.\nÇalma sırasında dosya eklense bile çalınan parça devam eder."));
	addTip(IDC_CHECK4, LL14(L"選択した曲をループさせます。\n再生する前にチェックを入れる必要があります。\nそうでないとループはかかりません。\nループポイントが0のもの(mp3やループしない曲)が対象です。", L"Loop selected track.\nCheck before playback to enable looping.\nOtherwise, looping will not work.\nApplies to tracks with loop point 0 (mp3 or non-looping tracks).", L"Boucler la piste selectionnee.\nVous devez cocher cette case avant la lecture.\nSinon, la boucle ne fonctionnera pas.\nS'applique aux pistes dont le point de boucle est 0 (mp3 ou pistes sans boucle).", L"Ripeti la traccia selezionata.\nDevi spuntare questa casella prima della riproduzione.\nAltrimenti il loop non funzionera.\nSi applica alle tracce con punto di loop 0 (mp3 o tracce senza loop).", L"Repetir la pista seleccionada.\nDebe marcar esta casilla antes de reproducir.\nDe lo contrario, el bucle no funcionara.\nSe aplica a pistas con punto de bucle 0 (mp3 o pistas sin bucle).", L"선택한 곡을 반복 재생합니다.\n재생하기 전에 체크해야 합니다.\n그렇지 않으면 반복되지 않습니다.\n루프 포인트가 0인 것(mp3나 루프하지 않는 곡)이 대상입니다.", L"循环播放所选曲目。\n播放前必须勾选。\n否则不会循环。\n适用于循环点为0的曲目（mp3或不循环的曲目）。", L"تكرار المسار المحدد.\nيجب تحديد هذا الخيار قبل التشغيل.\nوإلا فلن يعمل التكرار.\nينطبق على المسارات التي نقطة التكرار فيها 0 (mp3 أو مسارات بلا تكرار).", L"Зациклить выбранный трек.\nНужно установить флажок до начала воспроизведения.\nИначе зацикливание не сработает.\nПрименяется к трекам с точкой цикла 0 (mp3 или треки без цикла).", L"Gewahlten Titel wiederholen.\nSie mussen dies vor der Wiedergabe aktivieren.\nAndernfalls funktioniert die Schleife nicht.\nGilt fur Titel mit Loop-Punkt 0 (mp3 oder Titel ohne Schleife).", L"Repetir a faixa selecionada.\nVoce deve marcar esta opcao antes de reproduzir.\nCaso contrario, o loop nao funcionara.\nAplica-se a faixas com ponto de loop 0 (mp3 ou faixas sem loop).", L"Herhaal het geselecteerde nummer.\nU moet dit voor het afspelen aanvinken.\nAnders werkt het herhalen niet.\nGeldt voor nummers met lus-punt 0 (mp3 of nummers zonder lus).", L"Zapętl zaznaczony utwór.\nMusisz zaznaczyć to przed odtwarzaniem.\nW przeciwnym razie pętla nie zadziała.\nDotyczy utworów z punktem pętli 0 (mp3 lub utwory bez pętli).", L"Seçili parçayı döngüye alır.\nÇalmadan önce bunu işaretlemelisiniz.\nAksi halde döngü çalışmaz.\nDöngü noktası 0 olan parçalar içindir (mp3 veya dönmeyen parçalar)."));
	addTip(IDC_CHECK28, LL14(L"ツールチップを表示します。", L"Show tooltips.", L"Afficher les info-bulles.", L"Mostra suggerimenti.", L"Mostrar sugerencias.", L"도구 설명 표시.", L"显示工具提示。", L"إظهار تلميحات الأدوات.", L"Показывать подсказки.", L"Tooltips anzeigen.", L"Mostrar dicas.", L"Tooltips tonen.", L"Pokaż etykiety.", L"İpuçlarını göster."));
	addTip(IDC_CHECK29, LL14(L"最小化、最小化からの復帰時、メイン画面とプレイリスト画面も同時に最小化、最小化からの復帰を行います。", L"When minimizing/restoring, main window and playlist window minimize/restore together.", L"Lors de la minimisation/restauration, les fenetres principale et playlist font de meme.", L"Alla minimizzazione/ripristino, finestra principale e playlist si minimizzano/ripristinano insieme.", L"Al minimizar/restaurar, ventana principal y lista se minimizan/restauran juntas.", L"최소화/복원 시 메인과 재생 목록 창도 함께 최소화/복원.", L"最小化/还原时，主窗口和播放列表窗口同时最小化/还原。", L"عند التصغير/الاستعادة، تُصغَّر النوافذ أو تُستعاد معاً.", L"При сворачивании/восстановлении окна сворачиваются вместе.", L"Beim Minimieren/Wiederherstellen werden beide Fenster zusammen behandelt.", L"Ao minimizar/restaurar, as janelas fazem o mesmo juntas.", L"Bij minimaliseren/herstellen gaan beide vensters mee.", L"Przy minimalizowaniu/przywracaniu okna zmieniają się razem.", L"Küçültme/geri yüklemede ana pencere ve liste birlikte değişir."));
	addTip(IDC_CHECK5, LL14(L"途中で演奏を停止した位置を自動保存します。\n内蔵音源と動画(avi,mp4など)に対応。\n停止ボタンもしくは終了したときのみ保存します。\n再生中に違う曲を選んだ時は位置は保存しません。", L"Auto-save playback position when stopped.\nSupports built-in audio and video (avi, mp4, etc.).\nSaves only when stop button is pressed or when exiting.\nPosition is not saved when selecting a different track during playback.", L"Enregistre automatiquement la position ou la lecture a ete arretee.\nPris en charge pour l audio integre et la video (avi, mp4, etc.).\nEnregistre uniquement au bouton d arret ou a la fermeture.\nLa position n est pas enregistree si vous choisissez une autre piste pendant la lecture.", L"Salva automaticamente la posizione in cui la riproduzione e stata fermata.\nSupporta audio interno e video (avi, mp4, ecc.).\nSalva solo con il pulsante di stop o all uscita.\nLa posizione non viene salvata se scegli un altra traccia durante la riproduzione.", L"Guarda automaticamente la posicion donde se detuvo la reproduccion.\nCompatible con audio interno y video (avi, mp4, etc.).\nGuarda solo al pulsar el boton de parada o al salir.\nNo se guarda la posicion si elige otra pista durante la reproduccion.", L"재생을 중간에 멈춘 위치를 자동으로 저장합니다.\n내장 음원과 동영상(avi, mp4 등)을 지원합니다.\n정지 버튼을 누르거나 종료할 때만 저장합니다.\n재생 중에 다른 곡을 선택하면 위치는 저장되지 않습니다.", L"自动保存中途停止播放的位置。\n支持内置音源和视频(avi、mp4等)。\n仅在按下停止按钮或退出时保存。\n播放中选择其他曲目时不保存位置。", L"يحفظ تلقائياً الموضع الذي توقف عنده التشغيل.\nيدعم الصيغ الصوتية المدمجة والفيديو (avi وmp4 وغيرها).\nيحفظ فقط عند الضغط على زر الإيقاف أو عند الخروج.\nلا يُحفظ الموضع عند اختيار مقطع آخر أثناء التشغيل.", L"Автоматически сохраняет позицию, на которой было остановлено воспроизведение.\nПоддерживаются встроенные аудиоформаты и видео (avi, mp4 и др.).\nСохраняет только при нажатии кнопки Стоп или при выходе.\nПозиция не сохраняется, если во время воспроизведения выбрать другой трек.", L"Speichert automatisch die Position, an der die Wiedergabe gestoppt wurde.\nUnterstutzt eingebaute Audioformate und Video (avi, mp4 usw.).\nSpeichert nur beim Stopp-Knopf oder beim Beenden.\nDie Position wird nicht gespeichert, wenn wahrend der Wiedergabe ein anderer Titel gewahlt wird.", L"Salva automaticamente a posicao onde a reproducao foi parada.\nCompativel com audio interno e video (avi, mp4, etc.).\nSalva apenas ao pressionar o botao de parar ou ao sair.\nA posicao nao e salva ao escolher outra faixa durante a reproducao.", L"Slaat automatisch de positie op waar het afspelen werd gestopt.\nOndersteunt ingebouwde audio en video (avi, mp4, enz.).\nSlaat alleen op bij de stopknop of bij afsluiten.\nDe positie wordt niet opgeslagen als u tijdens het afspelen een ander nummer kiest.", L"Automatycznie zapisuje pozycję, w której zatrzymano odtwarzanie.\nObsługuje wbudowane formaty audio i wideo (avi, mp4 itp.).\nZapisuje tylko po naciśnięciu przycisku stop lub przy wyjściu.\nPozycja nie jest zapisywana, gdy podczas odtwarzania wybierzesz inny utwór.", L"Çalmanın durdurulduğu konumu otomatik olarak kaydeder.\nDahili ses formatları ve video (avi, mp4 vb.) desteklenir.\nYalnızca durdur düğmesine basıldığında veya çıkışta kaydeder.\nÇalma sırasında başka bir parça seçilirse konum kaydedilmez."));
	addTip(IDC_CHECK6, LL14(L"内蔵音源の再生時に途中保存を有効にします。", L"Enable mid-playback save for built-in audio formats.", L"Activer l enregistrement de position pour l audio integre.", L"Abilita salvataggio posizione per audio interno.", L"Habilitar guardado de posicion para audio interno.", L"내장 음원 재생 시 위치 저장 활성화.", L"内置音源播放时启用位置保存。", L"تفعيل حفظ الموضع للصيغ الصوتية المدمجة.", L"Включить сохранение позиции для встроенных форматов.", L"Positionsspeicherung fur eingebaute Audioformate aktivieren.", L"Habilitar salvamento para audio interno.", L"Positieopslag voor ingebouwde audio inschakelen.", L"Włącz zapisywanie pozycji dla wbudowanych formatów.", L"Dahili ses formatları için konum kaydını etkinleştir."));
	addTip(IDC_CHECK7, LL14(L"動画などのDirectShow使用時に途中保存を有効にします。", L"Enable mid-playback save for DirectShow (videos, etc.).", L"Activer l'enregistrement pour DirectShow (videos, etc.).", L"Abilita salvataggio per DirectShow (video).", L"Habilitar guardado para DirectShow (videos).", L"DirectShow(동영상 등) 재생 시 위치 저장 활성화.", L"DirectShow（视频等）启用位置保存。", L"تفعيل حفظ الموضع لـ DirectShow (الفيديو).", L"Включить сохранение для DirectShow (видео).", L"Fur DirectShow (Videos) aktivieren.", L"Habilitar para DirectShow (videos).", L"Voor DirectShow (video's) inschakelen.", L"Włącz dla DirectShow (wideo).", L"DirectShow (videolar) için etkinleştir."));
	addTip(IDC_COMBO1, LL14(L"プレイリストを変更または追加します。", L"Change or add playlists.", L"Modifier ou ajouter des listes.", L"Cambia o aggiungi playlist.", L"Cambiar o anadir listas.", L"재생 목록 변경 또는 추가.", L"更改或添加播放列表。", L"تغيير أو إضافة قوائم التشغيل.", L"Изменить или добавить плейлисты.", L"Playlists andern oder hinzufugen.", L"Alterar ou adicionar listas.", L"Playlists wijzigen of toevoegen.", L"Zmień lub dodaj listy.", L"Listeleri değiştir veya ekle."));
	addTip(IDC_BUTTON3, LL14(L"プレイリスト名を変更します。", L"Rename playlist.", L"Renommer la liste.", L"Rinomina playlist.", L"Cambiar nombre de lista.", L"재생 목록 이름 변경.", L"重命名播放列表。", L"إعادة تسمية قائمة التشغيل.", L"Переименовать плейлист.", L"Playlist umbenennen.", L"Renomear lista.", L"Playlist hernoemen.", L"Zmień nazwę listy.", L"Liste adını değiştir."));
	addTip(IDC_PLAYDELETE, LL14(L"表示されているプレイリストを削除します。\n※削除したものは復活できないので注意ください。", L"Delete the displayed playlist.\n*Deleted playlists cannot be recovered.", L"Supprimer la liste affichee.\n*Les listes supprimees ne peuvent pas etre recuperees.", L"Elimina la playlist visualizzata.\n*Le playlist eliminate non possono essere recuperate.", L"Eliminar la lista mostrada.\n*Las listas eliminadas no se pueden recuperar.", L"표시된 재생 목록 삭제.\n*삭제 후 복구 불가.", L"删除显示的播放列表。\n*删除后无法恢复。", L"حذف قائمة التشغيل المعروضة.\n*لا يمكن استرداد المحذوفة.", L"Удалить отображаемый плейлист.\n*Удалённые плейлисты восстановить нельзя.", L"Angezeigte Playlist loschen.\n*Geloschte Playlists konnen nicht wiederhergestellt werden.", L"Excluir lista exibida.\n*Listas excluidas nao podem ser recuperadas.", L"Getoonde playlist verwijderen.\n*Verwijderde playlists kunnen niet worden hersteld.", L"Usuń wyświetlaną listę.\n*Usuniętych list nie można odzyskać.", L"Gösterilen listeyi sil.\n*Silinen listeler geri alınamaz."));
	addTip(IDC_PIANOROLL, LL14(L"簡易ピアノロール表示を開きます。\n再生中の音程を鍵盤状に表示します。", L"Open simple piano roll view.\nShows pitch of playing audio on a keyboard layout.", L"Ouvrir le rouleau piano simple.\nAffiche la hauteur du son en cours sur un clavier.", L"Apri rotolo pianoforte semplice.\nMostra l'altezza dell'audio in riproduzione su tastiera.", L"Abrir rollo de piano simple.\nMuestra el tono del audio en reproduccion en un teclado.", L"간이 피아노 롤 창을 엽니다.\n재생 중 음정을 건반 형태로 표시합니다.", L"打开简易钢琴卷帘。\n以键盘形式显示正在播放的音频音高。", L"فتح لوحة البيانو البسيطة.\nيعرض طبقة الصوت على شكل لوحة مفاتيح.", L"Открыть простой пианоролл.\nПоказывает высоту звука на клавиатуре.", L"Einfache Klavierrolle offnen.\nZeigt Tonhohe als Tastatur.", L"Abrir rolo de piano simples.\nMostra altura do audio em teclado.", L"Eenvoudige pianorol openen.\nToont toonhoogte op een toetsenbord.", L"Otworz prosta rolke pianina.\nPokazuje wysokosc dzwieku na klawiaturze.", L"Basit piyano rulosunu ac.\nCalan sesin perdesini klavye duzeninde gosterir."));
	addTip(IDC_EDIT2, LL14(L"あいまい検索のキーワードを入力します。\n上下の検索ボタンでリスト内を検索します。", L"Enter fuzzy search keyword.\nUse search buttons above/below to find in list.", L"Saisir le mot-cle de recherche floue.\nUtilisez les boutons pour chercher dans la liste.", L"Inserisci parola chiave ricerca fuzzy.\nUsa i pulsanti per cercare nella lista.", L"Introduzca palabra clave de busqueda difusa.\nUse los botones para buscar en la lista.", L"퍼지 검색 키워드를 입력합니다.\n위/아래 검색 버튼으로 목록을 검색합니다.", L"输入模糊搜索关键字。\n用上下搜索按钮在列表中查找。", L"أدخل كلمة البحث الغامض.\nاستخدم أزرار البحث للعثور في القائمة.", L"Введите ключевое слово нечеткого поиска.\nКнопками ищите в списке.", L"Suchbegriff fur Fuzzy-Suche eingeben.\nMit Suchtasten in der Liste suchen.", L"Digite palavra-chave de pesquisa fuzzy.\nUse os botoes para buscar na lista.", L"Voer fuzzy-zoekterm in.\nGebruik zoekknoppen in de lijst.", L"Wpisz slowo kluczowe wyszukiwania rozmytego.\nPrzyciskami szukaj na liscie.", L"Bulanik arama anahtar kelimesini girin.\nListede aramak icin arama dugmelerini kullanin."));
	addTip(IDC_PL_HELP, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
//	m_lc.SetMaxTipWidth(500)
	DWORD dwExStyle = m_lc.GetExtendedStyle();
	dwExStyle |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES;//|LVS_EX_INFOTIP;
	m_lc.SetExtendedStyle(dwExStyle);
	if (il.Create(16, 16, ILC_COLOR32 | ILC_MASK, 0, 3)) {
		HICON h1 = ::AfxGetApp()->LoadIcon(IDI_ICON1);
		HICON h2 = ::AfxGetApp()->LoadIcon(IDI_ICON2);
		HICON h3 = ::AfxGetApp()->LoadIcon(IDI_ICON3);
		if (h1) il.Add(h1);
		if (h2) il.Add(h2);
		if (h3) il.Add(h3);
		if (il.GetImageCount() > 0)
			m_lc.SetImageList(&il, LVSIL_SMALL);
	}
	// リストがダイアログの大部分を覆うため、リスト自身もファイルドロップを受け付ける。
	// 受け取ったドロップは CCustomListCtrl::OnDropFiles が親(本ダイアログ)へ中継する。
	m_lc.DragAcceptFiles(TRUE);
	m_lc.ModifyStyle ( 0, LVS_REPORT );
	m_lc.InsertColumn ( 0, LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 1, LL14(L"印", L"Mark", L"Marq.", L"Segno", L"Marca", L"표시", L"标记", L"علامة", L"Метка", L"Zchn", L"Marca", L"Teken", L"Znak", L"Isaret"), LVCFMT_CENTER, (int)(88 * hD2), 0 ); // [SAV]=曲ごと保存 / [LRC]=歌詞(.lrc)
	m_lc.InsertColumn ( 2, LL14(L"ゲーム", L"Game", L"Jeu", L"Gioco", L"Juego", L"게임", L"游戏", L"لعبة", L"Игра", L"Spiel", L"Jogo", L"Spel", L"Gra", L"Oyun"), LVCFMT_LEFT, 50, 0 );
	m_lc.InsertColumn ( 3, LL14(L"時間", L"Time", L"Duree", L"Durata", L"Duracion", L"시간", L"时间", L"الوقت", L"Время", L"Zeit", L"Duracao", L"Tijd", L"Czas", L"Sure"), LVCFMT_RIGHT, 72, 0 );
	m_lc.InsertColumn ( 4, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Kunstler", L"Artista", L"Artiest", L"Artysta", L"Sanatçı"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 5, LL14(L"アルバム/コメント", L"Album/Comment", L"Album/Commentaire", L"Album/Commento", L"Album/Comentario", L"앨범/댓글", L"专辑/注释", L"الألبوم/التعليق", L"Альбом/Комментарий", L"Album/Kommentar", L"Album/Comentario", L"Album/Opmerking", L"Album/Komentarz", L"Album/Yorum"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 6, LL14(L"フォルダ", L"Folder", L"Dossier", L"Cartella", L"Carpeta", L"폴더", L"文件夹", L"المجلد", L"Папка", L"Ordner", L"Pasta", L"Map", L"Folder", L"Klasor"), LVCFMT_LEFT, 50, 0 );
	m_lc.pc = pc;
	m_lc.m_bSongParamTip = true; // 曲ごと保存パラメータをツールチップに付記
	m_lc.m_mpJacketPx = kPlJakPx;
	m_lc.m_mpJacketGet = PlJacketGetCb;
	m_lc.m_mpJacketCtx = this;
	m_lc.m_mpNoteIconGet = PlNoteIconGetCb;
	PlJakMemInit();
//	pc=NULL;
//	pc = (playlistdata0*)malloc(sizeof(playlistdata0)*50000);
//	if(pc==NULL)
//		EndDialog(0);
	m_lc.SetFocus();
	pnt=pnt1=-1;
	nnn=1;
	pc=NULL;

	// サブONなのにマスターが0の古い設定を補正（途中保存が一切動かない原因になり得る）
	if ((savedata.savecheck_mp3 || savedata.savecheck_dshow) && !savedata.savecheck)
		savedata.savecheck = 1;
	m_savecheck.SetCheck(savedata.savecheck);
	m_save_mp3.SetCheck(savedata.savecheck_mp3);
	m_save_kpi.SetCheck(savedata.savecheck_dshow);

	Load();
	loadplaylistname();
	// 配布済 AudioData.dat の旧キー(pathのみ)を mode+ret2 付きへ一度だけ移行
	SongParams_ConvertKeysIfNeeded();
	if(pc==NULL){
		pc = (playlistdata0*)malloc(sizeof(playlistdata0));
	}
	// m_lc はダイアログ再作成のたびに新しい HWND になるが、グローバル tlg は残るため
	// タイマー側の tl!=tlg だけだと EnableToolTips が一度も呼ばれず行ツールチップが出ない。
	{
		const int on = m_tool.GetCheck();
		m_lc.EnableToolTips(on ? TRUE : FALSE);
		DWORD ex = m_lc.GetExtendedStyle();
		if (on)
			ex &= ~LVS_EX_INFOTIP;
		else
			ex |= LVS_EX_INFOTIP;
		m_lc.SetExtendedStyle(ex);
		extern int tlg;
		tlg = on;
	}
	SetTimer(20,20,NULL);
	SetTimer(3000,1200,NULL);
	SetTimer(40,500,NULL);   // 起動ワンショット(plw)。timerpl1 が Kill する
	SetTimer(41,125,NULL);   // ミニジャケ+欠損。温いと500msに間引き
	SetTimer(5000,100,NULL);
	SIcon(pnt1);

	CCustomControlUtility::SetControlBackgroundColor(&m_listchange, COLOR_COMBO_BG);

	if (m_fontList.GetSafeHandle()) {
		m_fontList.DeleteObject();
	}
	BOOL retfont = FALSE;
	LOGFONT logFont;
	if (_tcslen(savedata.font2) > 0 && DeserializeLogFont(savedata.font2, &logFont)) {
		retfont = m_fontList.CreateFontIndirect(&logFont);
	} else if (_tcslen(savedata.font2) > 0) {
		retfont = m_fontList.CreateFont(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, DRAFT_QUALITY, DEFAULT_PITCH | FF_SWISS, savedata.font2);
	}
	if (!retfont) {
		retfont = m_fontList.CreateFont(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, DRAFT_QUALITY, DEFAULT_PITCH | FF_SWISS, _T("メイリオ"));
	}
	if (retfont) {
		m_lc.SetFont(&m_fontList, TRUE);
		m_find.SetFont(&m_fontList, TRUE);
		m_e.SetFont(&m_fontList, TRUE);
	}
	Invalidate();
	playbase = NULL;
	if (savedata.aero == 2) {
		playbase = new CImageBase;
		playbase->Create(pl);
		playbase->oya = pl;
	}
	CRect r;
	GetWindowRect(&r);
	if(playbase)
		playbase->MoveWindow(&r);

	plw = 1;

	m_lsup.SetIcon(IDR_SUP);
	m_lsup.SetFlat(TRUE);
	m_lup.SetIcon(IDR_UP);
	m_lup.SetFlat(TRUE);
	m_lsdown.SetIcon(IDR_SDOWN);
	m_lsdown.SetFlat(TRUE);
	m_ldown.SetIcon(IDR_DOWN);
	m_ldown.SetFlat(TRUE);
	m_findup.SetIcon(IDR_DOWN);
	m_findup.SetFlat(TRUE);
	m_finddown.SetIcon(IDR_UP);
	m_finddown.SetFlat(TRUE);
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	ScheduleRefreshNavControls();

	EnableMainWindowLock(&savedata.playlistMainLock);
	CCC_MainLockBringToFront(m_hWnd);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
	}
	catch (CException* e)
	{
		{
			TCHAR msg[512] = {};
			e->GetErrorMessage(msg, _countof(msg) - 1);
			CStdioFile f;
			if (f.Open(_T("pl_init_exception.log"),
				CFile::modeCreate | CFile::modeWrite | CFile::typeText))
			{
				CString line;
				line.Format(_T("CPlayList::OnInitDialog caught %hs: %s\n"),
					e->GetRuntimeClass()->m_lpszClassName, msg);
				f.WriteString(line);
				f.Close();
			}
		}
		e->Delete();
		return TRUE;
	}
}
extern int killw1;

void CPlayList::OnNcDestroy()
{
	CCustomBlurDialogBase::OnNcDestroy();

	// TODO: ここにメッセージ ハンドラ コードを追加します。
	killw1=1;
}

void CPlayList::OnDestroy()
{
	if (g_plHelpDlg && ::IsWindow(g_plHelpDlg->GetSafeHwnd()))
		g_plHelpDlg->DestroyWindow();
	CCustomBlurDialogBase::OnDestroy();
}

void CPlayList::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CPlayList::ShowHelpSheet()
{
	if (g_plHelpDlg && ::IsWindow(g_plHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_plHelpDlg, this);
		return;
	}
	if (g_plHelpDlg && !::IsWindow(g_plHelpDlg->GetSafeHwnd()))
		g_plHelpDlg = nullptr;
	CPlHelpDlg* dlg = new CPlHelpDlg(this, this);
	if (!dlg->Create(IDD_PL_HELP, this)) {
		delete dlg;
		return;
	}
	g_plHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CPlayList::OnBnClickedHelp()
{
	ShowHelpSheet();
}

BOOL CPlayList::DestroyWindow()
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
	Save();
//	free(pc);
//	pc=NULL;
//	KillTimer(20);
//	KillTimer(30);
	BOOL rr=CCustomBlurDialogBase::DestroyWindow();
	pl=NULL;
//	if(nnn)
//		delete this;
	plw=0;
	if(playbase) delete playbase;
	playbase = NULL;
	return rr;
}

int CPlayList::Create(CWnd *pWnd)
{
	 m_pParent = NULL;
	// 親は呼び出し側(og)。this(未生成)を渡すと親子が壊れる
	BOOL bret = CCustomBlurDialogBase::Create(CPlayList::IDD, pWnd);
	if (savedata.aero == 2) {
		ModifyStyleEx(0, WS_EX_LAYERED);

		// レイヤードウィンドウの不透明度と透明のカラーキー
		SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY);

		// 赤色のブラシを作成する．
		m_brDlg.CreateSolidBrush(RGB(255, 0, 0));
	}
	if (bret == TRUE) {
		// メディアプレイヤーモード中は単独表示しない(裏で生かすのみ)
		if (savedata.playerMode != 1) {
			ShowWindow(SW_SHOW);
			EnsureOnScreen();
		}
		ScheduleRefreshNavControls();
	}
	return bret;
}

void CPlayList::EnsureOnScreen()
{
	if (!::IsWindow(GetSafeHwnd()))
		return;
	CRect r;
	GetWindowRect(&r);
	const BOOL wayOff = (r.left < -5000 || r.top < -5000);
	HMONITOR hMon = ::MonitorFromRect(&r, MONITOR_DEFAULTTONULL);
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	BOOL offWork = FALSE;
	if (hMon && ::GetMonitorInfo(hMon, &mi)) {
		if (r.right < mi.rcWork.left + 8 || r.left > mi.rcWork.right - 8
			|| r.bottom < mi.rcWork.top + 8 || r.top > mi.rcWork.bottom - 8)
			offWork = TRUE;
	}
	else {
		offWork = TRUE;
	}
	if (!wayOff && !offWork)
		return;

	CRect anchor(80, 80, 80, 80);
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->GetWindowRect(&anchor);
	else if (savedata.playerMode == 1) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->GetWindowRect(&anchor);
	}

	int x = anchor.left;
	int y = anchor.bottom + 8;
	const int w = r.Width();
	const int h = r.Height();
	HMONITOR hA = ::MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO ami = {};
	ami.cbSize = sizeof(ami);
	if (hA && ::GetMonitorInfo(hA, &ami)) {
		if (x + w > ami.rcWork.right) x = ami.rcWork.right - w;
		if (y + h > ami.rcWork.bottom) y = ami.rcWork.bottom - h;
		if (x < ami.rcWork.left) x = ami.rcWork.left;
		if (y < ami.rcWork.top) y = ami.rcWork.top;
	}
	SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

BOOL CPlayList::PreCreateWindow(CREATESTRUCT& cs)
{
	BOOL r = CCustomBlurDialogBase::PreCreateWindow(cs);
	// メディアプレイヤーモードでは最初から非表示・画面外で生成(ちらつき防止)
	if (savedata.playerMode == 1) {
		cs.style &= ~WS_VISIBLE;
		cs.x = -32000;
		cs.y = -32000;
	}
	return r;
}

void CPlayList::OnClose()
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	nnn=0;
	DestroyWindow();

	CCustomBlurDialogBase::OnClose();
}

void CPlayList::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
//	DestroyWindow();
}

BOOL CPlayList::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN) {
		CWnd* pFocus = GetFocus();
		if (pFocus && pFocus->GetSafeHwnd() == m_find.GetSafeHwnd()) {
			OnFindUp();  // Enter = 次の候補へ（親ダイアログの IDOK/終了を防ぐ）
			return TRUE;
		}
	}
	if (m_lc.GetSafeHwnd() && m_tool.GetCheck())
	{
		if (m_lc.PreTranslateMessage(pMsg))
			return TRUE;
	}
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);

	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

int pnt1=-1;

static bool PlIsAbsoluteMediaPath(LPCTSTR p)
{
	if (!p || !*p) return false;
	if (p[0] == _T('\\') && p[1] == _T('\\')) return true;
	return ((int)_tcslen(p) >= 3 && p[1] == _T(':') && (p[2] == _T('\\') || p[2] == _T('/')));
}

CString NormalizePlaylistPath(LPCTSTR fol)
{
	if (!fol || !*fol) return CString();
	TCHAR full[MAX_PATH];
	DWORD n = GetFullPathName(fol, MAX_PATH, full, NULL);
	CString s = (n > 0 && n < MAX_PATH) ? CString(full) : CString(fol);
	s.Replace(_T('/'), _T('\\'));
	return s;
}

CString PlPhysicalMediaPath(LPCTSTR fol)
{
	if (!fol || !*fol) return CString();
	CString in(fol);
	const int len = in.GetLength();
	// KPI subsong: path::0001 (4-digit track number)
	if (len >= 6 && in.GetAt(len - 5) == L':') {
		const CString tail = in.Right(4);
		bool allDigits = true;
		for (int i = 0; i < 4; ++i) {
			if (tail[i] < L'0' || tail[i] > L'9') { allDigits = false; break; }
		}
		if (allDigits)
			return in.Left(len - 6);
	}
	// Other virtual paths (e.g. .pac::..., archive entries)
	const int cor = in.Find(L':', 6);
	if (cor != -1)
		return in.Left(cor);
	return in;
}

BOOL PlIsFalcomGameBgmMode(int sub)
{
	return (sub >= 1 && sub <= 21) || sub == 30 ||
		sub == -11 || sub == -12 || sub == -13 || sub == -14 || sub == -15;
}

BOOL PlTrackLooksMissing(int sub, LPCTSTR fol)
{
	if (!fol || !fol[0]) return TRUE;
	// Falcom game BGM (mode1-21等): fol は basename のみ。play() が savedata で解決する。
	// mode 30 はフルパス保存なので存在チェックする。
	if (PlIsFalcomGameBgmMode(sub) && sub != 30)
		return FALSE;
	return !PathFileExists(PlPhysicalMediaPath(fol));
}

// %LOCALAPPDATA%\oggYSED\ 配下。resume/libroots と同じ場所。
static ULONGLONG PlCacheHashPath(LPCTSTR path)
{
	ULONGLONG h = 14695981039346656037ULL;
	if (!path) return h;
	for (const TCHAR* p = path; *p; ++p) {
		TCHAR c = *p;
		if (c >= _T('A') && c <= _T('Z')) c = (TCHAR)(c - _T('A') + _T('a'));
		if (c == _T('/')) c = _T('\\');
		h ^= (ULONGLONG)(unsigned)(TCHAR)c;
		h *= 1099511628211ULL;
	}
	return h;
}

static CString PlYsedCacheDir(LPCTSTR sub)
{
	// 毎回 GetEnvironmentVariable+CreateDirectory しない(欠品/ジャケで大量呼び出しされる)
	static TCHAR s_missDir[MAX_PATH] = {};
	static TCHAR s_jakDir[MAX_PATH] = {};
	TCHAR* slot = NULL;
	if (sub && _tcscmp(sub, _T("miss")) == 0) slot = s_missDir;
	else if (sub && _tcscmp(sub, _T("jak")) == 0) slot = s_jakDir;
	if (slot && slot[0])
		return CString(slot);

	TCHAR base[MAX_PATH] = {};
	DWORD n = ::GetEnvironmentVariable(_T("LOCALAPPDATA"), base, MAX_PATH);
	CString root;
	if (n > 0 && n < MAX_PATH)
		root = base;
	else {
		TCHAR tmp[MAX_PATH] = {};
		if (::GetTempPath(MAX_PATH, tmp) == 0 || tmp[0] == 0)
			return CString();
		root = tmp;
		while (!root.IsEmpty() && (root[root.GetLength() - 1] == _T('\\') || root[root.GetLength() - 1] == _T('/')))
			root = root.Left(root.GetLength() - 1);
	}
	CString app = root + _T("\\oggYSED");
	CString dir = app + _T("\\") + sub;
	::CreateDirectory(app, NULL);
	::CreateDirectory(dir, NULL);
	if (slot)
		_tcsncpy_s(slot, MAX_PATH, dir, _TRUNCATE);
	return dir;
}

// -1=未キャッシュ / 0=存在 / 1=欠損
int PlMissDiskGet(LPCTSTR fol)
{
	if (!fol || !fol[0]) return 1;
	CString dir = PlYsedCacheDir(_T("miss"));
	if (dir.IsEmpty()) return -1;
	CString path;
	path.Format(_T("%s\\%016I64X"), (LPCTSTR)dir, PlCacheHashPath(fol));
	HANDLE h = ::CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return -1;
	BYTE b = 0;
	DWORD rd = 0;
	const BOOL ok = ::ReadFile(h, &b, 1, &rd, NULL);
	::CloseHandle(h);
	if (!ok || rd != 1) return -1;
	return b ? 1 : 0;
}

void PlMissDiskSet(LPCTSTR fol, int miss)
{
	if (!fol || !fol[0]) return;
	CString dir = PlYsedCacheDir(_T("miss"));
	if (dir.IsEmpty()) return;
	CString path;
	path.Format(_T("%s\\%016I64X"), (LPCTSTR)dir, PlCacheHashPath(fol));
	HANDLE h = ::CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	BYTE b = miss ? 1 : 0;
	DWORD wr = 0;
	::WriteFile(h, &b, 1, &wr, NULL);
	::CloseHandle(h);
}

void PlMissDiskForget(LPCTSTR fol)
{
	if (!fol || !fol[0]) return;
	CString dir = PlYsedCacheDir(_T("miss"));
	if (dir.IsEmpty()) return;
	CString path;
	path.Format(_T("%s\\%016I64X"), (LPCTSTR)dir, PlCacheHashPath(fol));
	::DeleteFile(path);
}

// ジャケットサムネ: .bmp=有り / .none=無し(負キャッシュ)
CString PlJakDiskPath(LPCTSTR fol, BOOL noneSentinel)
{
	CString dir = PlYsedCacheDir(_T("jak"));
	if (dir.IsEmpty() || !fol || !fol[0]) return CString();
	CString path;
	// 拡張子は必ず CImage が認識する .bmp/.none。
	// (.b2 だと Save が形式不明で失敗し、ディスクに残らず毎回抽出になっていた)
	path.Format(_T("%s\\%016I64X%s"), (LPCTSTR)dir, PlCacheHashPath(fol),
		noneSentinel ? _T(".none") : _T(".bmp"));
	return path;
}

void PlJakDiskForget(LPCTSTR fol)
{
	CString a = PlJakDiskPath(fol, FALSE);
	CString b = PlJakDiskPath(fol, TRUE);
	if (!a.IsEmpty()) ::DeleteFile(a);
	if (!b.IsEmpty()) ::DeleteFile(b);
	// 旧版(.b2/.n2)も掃除
	if (!a.IsEmpty()) {
		CString old = a;
		old.Replace(_T(".bmp"), _T(".b2"));
		::DeleteFile(old);
	}
	if (!b.IsEmpty()) {
		CString old = b;
		old.Replace(_T(".none"), _T(".n2"));
		::DeleteFile(old);
	}
}

CString PlLrcSidecarPath(LPCTSTR fol)
{
	if (!fol || !fol[0]) return CString();
	CString s = fol;
	const int slash = (s.ReverseFind(_T('\\')) > s.ReverseFind(_T('/')))
		? s.ReverseFind(_T('\\')) : s.ReverseFind(_T('/'));
	const int dot = s.ReverseFind(_T('.'));
	if (dot > slash)
		return s.Left(dot) + _T(".lrc");
	return s + _T(".lrc");
}

int PlLrcDiskGet(LPCTSTR fol)
{
	if (!fol || !fol[0]) return 1;
	CString dir = PlYsedCacheDir(_T("lrcflag"));
	if (dir.IsEmpty()) return -1;
	CString path;
	path.Format(_T("%s\\%016I64X"), (LPCTSTR)dir, PlCacheHashPath(fol));
	HANDLE h = ::CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return -1;
	BYTE b = 0;
	DWORD rd = 0;
	const BOOL ok = ::ReadFile(h, &b, 1, &rd, NULL);
	::CloseHandle(h);
	if (!ok || rd != 1) return -1;
	return b ? 1 : 0;
}

void PlLrcDiskSet(LPCTSTR fol, int none)
{
	if (!fol || !fol[0]) return;
	CString dir = PlYsedCacheDir(_T("lrcflag"));
	if (dir.IsEmpty()) return;
	CString path;
	path.Format(_T("%s\\%016I64X"), (LPCTSTR)dir, PlCacheHashPath(fol));
	HANDLE h = ::CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	BYTE b = none ? 1 : 0;
	DWORD wr = 0;
	::WriteFile(h, &b, 1, &wr, NULL);
	::CloseHandle(h);
}

void PlLrcDiskForget(LPCTSTR fol)
{
	if (!fol || !fol[0]) return;
	CString dir = PlYsedCacheDir(_T("lrcflag"));
	if (dir.IsEmpty()) return;
	CString path;
	path.Format(_T("%s\\%016I64X"), (LPCTSTR)dir, PlCacheHashPath(fol));
	::DeleteFile(path);
}

int PlLrcProbe(LPCTSTR fol)
{
	if (!fol || !fol[0]) return -1;
	const int cached = PlLrcDiskGet(fol);
	if (cached >= 0) return cached;
	const CString lrc = PlLrcSidecarPath(fol);
	const int none = (!lrc.IsEmpty() && ::PathFileExists(lrc)) ? 0 : 1;
	PlLrcDiskSet(fol, none);
	return none;
}

void PlFormatRowMarks(int row, LPCTSTR fol, CString& out)
{
	out.Empty();
	const BOOL sav = SongParams_HasEntryForRow(row);
	BOOL lrc = FALSE;
	if (fol && fol[0]) {
		const int c = PlLrcDiskGet(fol);
		lrc = (c == 0);
	}
	// [SAV]=曲ごと保存パラメータあり / [LRC]=歌詞(.lrc)あり
	if (sav) out += _T("[SAV]");
	if (lrc) out += _T("[LRC]");
}

// プレイリスト窓用ジャケ(メモリLRU)。ディスクは PlJakDiskPath と共有。
static HBITMAP s_plJakBmp[kPlJakN];
static TCHAR   s_plJakKey[kPlJakN][1024];
static DWORD   s_plJakTick[kPlJakN];
static BOOL    s_plJakInited = FALSE;
// 無し確定 / ディスク解決済み(AttrEx 済みで再監視不要)。リングで溢れても mark 可能。
static const int kPlJakNegCap = 512;
static TCHAR   s_plJakNoneDone[kPlJakNegCap][1024];
static int     s_plJakNoneDoneN = 0;
static int     s_plJakNoneDoneW = 0;
static TCHAR   s_plJakDiskSettled[kPlJakNegCap][1024]; // .b2/.n2 確認済→prefetch で AttrEx しない
static int     s_plJakDiskSettledN = 0;
static int     s_plJakDiskSettledW = 0;
static UINT    s_plJakTimerMs = 125;

static BOOL PlJakIsInRing(const TCHAR (*ring)[1024], int n, int cap, const TCHAR* path)
{
	if (!path || !path[0]) return FALSE;
	const int lim = (n < cap) ? n : cap;
	for (int i = 0; i < lim; ++i) {
		if (_tcsicmp(ring[i], path) == 0) return TRUE;
	}
	return FALSE;
}
static void PlJakRingAdd(TCHAR (*ring)[1024], int& n, int& w, int cap, const TCHAR* path)
{
	if (!path || !path[0] || PlJakIsInRing(ring, n, cap, path)) return;
	_tcsncpy_s(ring[w], path, _TRUNCATE);
	w = (w + 1) % cap;
	if (n < cap) ++n;
}
static BOOL PlJakIsKnownNone(const TCHAR* path)
{
	return PlJakIsInRing(s_plJakNoneDone, s_plJakNoneDoneN, kPlJakNegCap, path);
}
static void PlJakMarkNone(const TCHAR* path)
{
	PlJakRingAdd(s_plJakNoneDone, s_plJakNoneDoneN, s_plJakNoneDoneW, kPlJakNegCap, path);
	PlJakRingAdd(s_plJakDiskSettled, s_plJakDiskSettledN, s_plJakDiskSettledW, kPlJakNegCap, path);
}
static BOOL PlJakIsDiskSettled(const TCHAR* path)
{
	return PlJakIsInRing(s_plJakDiskSettled, s_plJakDiskSettledN, kPlJakNegCap, path);
}
static void PlJakMarkDiskSettled(const TCHAR* path)
{
	PlJakRingAdd(s_plJakDiskSettled, s_plJakDiskSettledN, s_plJakDiskSettledW, kPlJakNegCap, path);
}

static void PlJakMemInit()
{
	if (s_plJakInited) return;
	ZeroMemory(s_plJakBmp, sizeof(s_plJakBmp));
	ZeroMemory(s_plJakKey, sizeof(s_plJakKey));
	ZeroMemory(s_plJakTick, sizeof(s_plJakTick));
	s_plJakInited = TRUE;
}

static HBITMAP PlJacketGetCb(void* ctx, int row)
{
	CPlayList* self = (CPlayList*)ctx;
	if (!self || !self->pc) return NULL;
	if (row < 0 || row >= self->playcnt) return NULL;
	const TCHAR* path = self->pc[row].fol;
	if (!path || !path[0]) return NULL;
	PlJakMemInit();
	// 描画中はメモリキャッシュのみ。mtime/ディスク読込は PlJacketLoadVisible 側。
	DWORD now = GetTickCount();
	for (int i = 0; i < kPlJakN; ++i) {
		if (s_plJakKey[i][0] && _tcsicmp(s_plJakKey[i], path) == 0) {
			s_plJakTick[i] = now;
			return s_plJakBmp[i];
		}
	}
	return NULL;
}

static int PlNoteIconGetCb(void* ctx, int row)
{
	CPlayList* self = (CPlayList*)ctx;
	if (!self || !self->pc || row < 0 || row >= self->playcnt) return 1;
	return self->pc[row].icon;
}

// ディスクにキャッシュが無いと分かったパス(毎tickの PathFileExists 連打を止める)
static TCHAR s_plJakNoDisk[96][1024];
static int s_plJakNoDiskN = 0;
static TCHAR s_plJakPend[1024];
static volatile LONG s_plJakBusy = 0;
static volatile LONG s_plJakGen = 0;
static int s_plJakPrefetch = 0;

struct PlJakJob {
	HWND hwnd;
	LONG gen;
	TCHAR path[1024];
	int row;
	HBITMAP hb;
	char noneFlag;
	char bgOnly;
};

static UINT AFX_CDECL PlJakLoadThread(LPVOID p)
{
	PlJakJob* job = (PlJakJob*)p;
	if (!job) return 0;
	job->hb = NULL;
	job->noneFlag = 1;
	::CoInitialize(NULL);
	extern COggDlg* og;
	CString diskBmp = PlJakDiskPath(job->path, FALSE);
	CString diskNone = PlJakDiskPath(job->path, TRUE);
	WIN32_FILE_ATTRIBUTE_DATA fad = {};
	if (!diskBmp.IsEmpty() && ::GetFileAttributesEx(diskBmp, GetFileExInfoStandard, &fad)) {
		CImage disk;
		if (disk.Load(diskBmp) == S_OK && !disk.IsNull() && disk.GetWidth() > 0) {
			HDC screen = ::GetDC(NULL);
			if (screen) {
				HDC mem = ::CreateCompatibleDC(screen);
				HBITMAP hbD = ::CreateCompatibleBitmap(screen, kPlJakPx, kPlJakPx);
				if (hbD && mem) {
					HGDIOBJ old = ::SelectObject(mem, hbD);
					RECT rc = { 0, 0, kPlJakPx, kPlJakPx };
					HBRUSH br = ::CreateSolidBrush(RGB(240, 240, 245));
					::FillRect(mem, &rc, br);
					::DeleteObject(br);
					::SetStretchBltMode(mem, COLORONCOLOR);
					disk.StretchBlt(mem, 0, 0, kPlJakPx, kPlJakPx, SRCCOPY);
					::SelectObject(mem, old);
					job->hb = hbD;
					job->noneFlag = 0;
					hbD = NULL;
				}
				if (hbD) ::DeleteObject(hbD);
				if (mem) ::DeleteDC(mem);
				::ReleaseDC(NULL, screen);
			}
			disk.Destroy();
		}
	}
	if (!job->hb && og) {
		CImage tmp;
		og->LoadJacket(job->path, &tmp);
		if (!tmp.IsNull() && tmp.GetWidth() > 0 && tmp.GetHeight() > 0) {
			HDC screen = ::GetDC(NULL);
			if (screen) {
				HDC mem = ::CreateCompatibleDC(screen);
				HBITMAP hb = ::CreateCompatibleBitmap(screen, kPlJakPx, kPlJakPx);
				if (hb && mem) {
					HGDIOBJ old = ::SelectObject(mem, hb);
					RECT rc = { 0, 0, kPlJakPx, kPlJakPx };
					HBRUSH br = ::CreateSolidBrush(RGB(240, 240, 245));
					::FillRect(mem, &rc, br);
					::DeleteObject(br);
					::SetStretchBltMode(mem, COLORONCOLOR);
					tmp.StretchBlt(mem, 0, 0, kPlJakPx, kPlJakPx, SRCCOPY);
					::SelectObject(mem, old);
					job->hb = hb;
					job->noneFlag = 0;
					hb = NULL;
					// フル解像度を共有ディスクへ(リスト用24pxを書くと本編ジャケットが潰れる)
					if (!diskBmp.IsEmpty()) {
						try { tmp.Save(diskBmp, Gdiplus::ImageFormatBMP); } catch (...) {}
						if (!diskNone.IsEmpty()) ::DeleteFile(diskNone);
					}
				}
				if (hb) ::DeleteObject(hb);
				if (mem) ::DeleteDC(mem);
				::ReleaseDC(NULL, screen);
			}
		}
		tmp.Destroy();
	}
	if (job->noneFlag) {
		if (!diskNone.IsEmpty()) {
			HANDLE hf = ::CreateFile(diskNone, GENERIC_WRITE, FILE_SHARE_READ, NULL,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (hf != INVALID_HANDLE_VALUE) ::CloseHandle(hf);
		}
		if (!diskBmp.IsEmpty()) ::DeleteFile(diskBmp);
	}
	::CoUninitialize();
	if (job->hwnd && ::IsWindow(job->hwnd))
		::PostMessage(job->hwnd, WM_PL_JAK_DONE, (WPARAM)job->gen, (LPARAM)job);
	else {
		if (job->hb) ::DeleteObject(job->hb);
		free(job);
	}
	return 0;
}

static BOOL PlJacketLoadVisible(CPlayList* self, BOOL allowExtract, BOOL bulkDisk)
{
	if (!self || !::IsWindow(self->m_lc.GetSafeHwnd()) || !self->pc)
		return FALSE;
	PlJakMemInit();
	const int nDisp = self->m_lc.GetItemCount();
	if (nDisp <= 0) return FALSE;
	int top = self->m_lc.GetTopIndex();
	if (top < 0) top = 0;
	int page = self->m_lc.GetCountPerPage() + 2;
	if (page < 4) page = 4;
	const DWORD now = GetTickCount();
	BOOL anyDisk = FALSE;
	BOOL visibleCold = FALSE;

	if (bulkDisk) {
		InterlockedIncrement(&s_plJakGen);
		s_plJakPend[0] = 0;
		InterlockedExchange(&s_plJakBusy, 0);
		s_plJakPrefetch = 0;
		s_plJakNoDiskN = 0;
		for (int i = 0; i < kPlJakN; ++i) {
			if (s_plJakBmp[i]) { ::DeleteObject(s_plJakBmp[i]); s_plJakBmp[i] = NULL; }
			s_plJakKey[i][0] = 0;
			s_plJakTick[i] = 0;
		}
	}

	CDC* pDCBatch = self->GetDC();
	const int passN = bulkDisk ? 2 : 1;
	for (int pass = 0; pass < passN; ++pass) {
		int r0, r1;
		BOOL allowLru;
		if (!bulkDisk) {
			r0 = top; r1 = top + page; allowLru = TRUE;
		}
		else if (pass == 0) {
			r0 = top; r1 = top + page; allowLru = TRUE;
		}
		else {
			r0 = 0; r1 = nDisp; allowLru = FALSE;
		}
		for (int row = r0; row < r1 && row < nDisp && row < self->playcnt; ++row) {
			const TCHAR* path = self->pc[row].fol;
			if (!path || !path[0]) continue;
			int hit = -1, freeSlot = -1, lruSlot = 0;
			DWORD lruTick = 0xFFFFFFFF;
			for (int i = 0; i < kPlJakN; ++i) {
				if (s_plJakKey[i][0] && _tcsicmp(s_plJakKey[i], path) == 0) { hit = i; break; }
				if (!s_plJakKey[i][0] && freeSlot < 0) freeSlot = i;
				if (s_plJakTick[i] < lruTick) { lruTick = s_plJakTick[i]; lruSlot = i; }
			}
			if (hit >= 0) {
				if (!s_plJakBmp[hit]) {
					s_plJakKey[hit][0] = 0;
					s_plJakTick[hit] = 0;
					PlJakMarkNone(path);
					continue;
				}
				s_plJakTick[hit] = now;
				continue;
			}
			if (s_plJakPend[0] && _tcsicmp(s_plJakPend, path) == 0) {
				visibleCold = TRUE;
				continue;
			}
			if (PlJakIsKnownNone(path))
				continue;
			BOOL knownNoDisk = FALSE;
			for (int ni = 0; ni < s_plJakNoDiskN; ++ni) {
				if (_tcsicmp(s_plJakNoDisk[ni], path) == 0) { knownNoDisk = TRUE; break; }
			}
			if (knownNoDisk) continue;

			// 可視の未メモリのみ AttrEx/Load。settled でも BMP はメモリへ載せる。
			int slot = freeSlot;
			if (slot < 0) {
				if (!allowLru) continue;
				slot = lruSlot;
			}
			const CString diskBmp = PlJakDiskPath(path, FALSE);
			const CString diskNone = PlJakDiskPath(path, TRUE);
			WIN32_FILE_ATTRIBUTE_DATA fadBmp = {}, fadNone = {};
			const BOOL hasBmp = !diskBmp.IsEmpty() && ::GetFileAttributesEx(diskBmp, GetFileExInfoStandard, &fadBmp);
			const BOOL hasNone = !diskNone.IsEmpty() && ::GetFileAttributesEx(diskNone, GetFileExInfoStandard, &fadNone);
			if (!hasBmp && !hasNone) {
				if (s_plJakNoDiskN < 96)
					_tcsncpy_s(s_plJakNoDisk[s_plJakNoDiskN++], path, _TRUNCATE);
				PlJakMarkDiskSettled(path);
				continue;
			}
			{
				WIN32_FILE_ATTRIBUTE_DATA fadM = {};
				const CString media = PlPhysicalMediaPath(path);
				if (!media.IsEmpty() && ::GetFileAttributesEx(media, GetFileExInfoStandard, &fadM)) {
					ULARGE_INTEGER um = {}, uc = {};
					um.LowPart = fadM.ftLastWriteTime.dwLowDateTime; um.HighPart = fadM.ftLastWriteTime.dwHighDateTime;
					WIN32_FILE_ATTRIBUTE_DATA& fadC = hasBmp ? fadBmp : fadNone;
					uc.LowPart = fadC.ftLastWriteTime.dwLowDateTime; uc.HighPart = fadC.ftLastWriteTime.dwHighDateTime;
					if (um.QuadPart > uc.QuadPart) {
						PlJakDiskForget(path);
						for (int ci = 0; ci < kPlJakN; ++ci) {
							if (s_plJakKey[ci][0] && _tcsicmp(s_plJakKey[ci], path) == 0) {
								if (s_plJakBmp[ci]) { ::DeleteObject(s_plJakBmp[ci]); s_plJakBmp[ci] = NULL; }
								s_plJakKey[ci][0] = 0;
								s_plJakTick[ci] = 0;
							}
						}
						visibleCold = TRUE;
						continue;
					}
				}
			}
			if (hasBmp && pDCBatch) {
				CImage disk;
				if (disk.Load(diskBmp) == S_OK && !disk.IsNull() && disk.GetWidth() > 0) {
					CDC mem0; mem0.CreateCompatibleDC(pDCBatch);
					HBITMAP hbD = ::CreateCompatibleBitmap(pDCBatch->GetSafeHdc(), kPlJakPx, kPlJakPx);
					if (hbD) {
						if (s_plJakBmp[slot]) { ::DeleteObject(s_plJakBmp[slot]); s_plJakBmp[slot] = NULL; }
						HGDIOBJ oldD = mem0.SelectObject(hbD);
						mem0.FillSolidRect(0, 0, kPlJakPx, kPlJakPx, RGB(240, 240, 245));
						mem0.SetStretchBltMode(COLORONCOLOR);
						disk.StretchBlt(mem0.GetSafeHdc(), 0, 0, kPlJakPx, kPlJakPx, SRCCOPY);
						mem0.SelectObject(oldD);
						mem0.DeleteDC();
						_tcsncpy_s(s_plJakKey[slot], path, _TRUNCATE);
						s_plJakBmp[slot] = hbD;
						s_plJakTick[slot] = now;
						anyDisk = TRUE;
						PlJakMarkDiskSettled(path);
						disk.Destroy();
						continue;
					}
					mem0.DeleteDC();
					disk.Destroy();
				}
			}
			if (hasNone) {
				// 空 HBITMAP をスロットに載せない(無色サムネの原因)
				PlJakMarkNone(path);
				continue;
			}
			PlJakMarkDiskSettled(path);
			visibleCold = TRUE; // 抽出待ち
		}
	}
	if (pDCBatch) self->ReleaseDC(pDCBatch);
	if (anyDisk) {
		CRect rc0, rc1;
		if (self->m_lc.GetItemRect(top, &rc0, LVIR_BOUNDS)) {
			int last = top + page - 1;
			if (last >= nDisp) last = nDisp - 1;
			if (last >= self->playcnt) last = self->playcnt - 1;
			if (self->m_lc.GetItemRect(last, &rc1, LVIR_BOUNDS))
				rc0.bottom = rc1.bottom;
			rc0.right = rc0.left + kPlJakPx + 28;
			self->m_lc.RedrawWindow(&rc0, NULL, RDW_INVALIDATE | RDW_NOERASE);
		}
	}
	if (!allowExtract || !og) return visibleCold;
	if (InterlockedCompareExchange(&s_plJakBusy, 1, 0) != 0) return TRUE;

	auto startExtractAt = [&](int row, BOOL bgOnly) -> BOOL {
		if (row < 0 || row >= self->playcnt || row >= nDisp) return FALSE;
		const TCHAR* path = self->pc[row].fol;
		if (!path || !path[0]) return FALSE;
		for (int i = 0; i < kPlJakN; ++i) {
			if (s_plJakKey[i][0] && _tcsicmp(s_plJakKey[i], path) == 0)
				return FALSE;
		}
		if (s_plJakPend[0] && _tcsicmp(s_plJakPend, path) == 0)
			return FALSE;
		if (PlJakIsKnownNone(path) || PlJakIsDiskSettled(path))
			return FALSE;
		for (int ni = 0; ni < s_plJakNoDiskN; ++ni) {
			if (_tcsicmp(s_plJakNoDisk[ni], path) == 0) return FALSE;
		}

		const CString diskBmp = PlJakDiskPath(path, FALSE);
		const CString diskNone = PlJakDiskPath(path, TRUE);
		WIN32_FILE_ATTRIBUTE_DATA fadBmp = {}, fadNone = {};
		const BOOL hasBmp = !diskBmp.IsEmpty() && ::GetFileAttributesEx(diskBmp, GetFileExInfoStandard, &fadBmp);
		const BOOL hasNone = !diskNone.IsEmpty() && ::GetFileAttributesEx(diskNone, GetFileExInfoStandard, &fadNone);
		if (hasBmp) {
			PlJakMarkDiskSettled(path);
			return FALSE;
		}
		if (hasNone) {
			PlJakMarkNone(path);
			return FALSE;
		}

		PlJakJob* job = (PlJakJob*)malloc(sizeof(PlJakJob));
		if (!job) return FALSE;
		ZeroMemory(job, sizeof(*job));
		job->hwnd = self->m_hWnd;
		job->gen = InterlockedIncrement(&s_plJakGen);
		job->row = row;
		job->bgOnly = bgOnly ? 1 : 0;
		_tcsncpy_s(job->path, path, _TRUNCATE);
		_tcsncpy_s(s_plJakPend, path, _TRUNCATE);
		if (!AfxBeginThread(PlJakLoadThread, job, THREAD_PRIORITY_BELOW_NORMAL)) {
			s_plJakPend[0] = 0;
			free(job);
			return FALSE;
		}
		return TRUE;
	};

	if (s_plJakPend[0]) {
		InterlockedExchange(&s_plJakBusy, 0);
		return TRUE;
	}
	for (int row = top; row < top + page && row < nDisp && row < self->playcnt; ++row) {
		if (startExtractAt(row, FALSE))
			return TRUE;
	}
	// 全曲 AttrEx 巡回はやめる。画面外は1件/tickだけ。
	if (s_plJakPrefetch < 0 || s_plJakPrefetch >= nDisp)
		s_plJakPrefetch = 0;
	const int lim = (nDisp < self->playcnt) ? nDisp : self->playcnt;
	if (lim > 0 && plf == 0) {
		for (int n = 0; n < lim; ++n) {
			const int row = (s_plJakPrefetch + n) % lim;
			s_plJakPrefetch = (row + 1) % lim;
			if (row >= top && row < top + page) continue;
			if (startExtractAt(row, TRUE))
				return TRUE;
			break; // 1候補だけ見て終わる(解決済みなら次tickへ)
		}
	}
	InterlockedExchange(&s_plJakBusy, 0);
	return visibleCold;
}

// 欠損表示用メモリ(描画中に PlMissDiskGet しない)。MP があるときは mp->m_miss を優先参照。
static char* s_plMiss = NULL;
static int s_plMissCap = 0;
static int s_plMissDoneCnt = -1;
static volatile LONG s_plMissBusy = 0;
static volatile LONG s_plMissGen = 0;
static HWND s_plMissHwnd = NULL;

struct PlMissJob {
	HWND hwnd;
	LONG gen;
	int count;
	int* indices;
	int* subs;
	TCHAR* fols;
	char* result;
};

static UINT AFX_CDECL PlMissScanThread(LPVOID p)
{
	PlMissJob* job = (PlMissJob*)p;
	if (!job) return 0;
	for (int i = 0; i < job->count; ++i) {
		const TCHAR* fol = job->fols + (size_t)i * 1024;
		const int cached = PlMissDiskGet(fol);
		if (cached == 0) {
			job->result[i] = 0;
			continue;
		}
		job->result[i] = PlTrackLooksMissing(job->subs[i], fol) ? 1 : 0;
		PlMissDiskSet(fol, job->result[i] ? 1 : 0);
	}
	if (job->hwnd && ::IsWindow(job->hwnd))
		::PostMessage(job->hwnd, WM_PL_MISS_DONE, (WPARAM)job->gen, (LPARAM)job);
	else {
		free(job->indices);
		free(job->subs);
		free(job->fols);
		free(job->result);
		free(job);
	}
	return 0;
}

// 欠損キャッシュの遅延確定。UI では PathFileExists / PlMissDiskGet しない。
static void PlMissTickScan(CPlayList* self)
{
	if (!self || !self->pc || self->playcnt <= 0) return;
	extern CMediaPlayerDlg* mp;
	s_plMissHwnd = self->GetSafeHwnd();
	// MP 側 KickMissScan が本体。完了後に PL を再描画するだけ。
	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
		if (mp->m_missScan >= self->playcnt && s_plMissDoneCnt != self->playcnt) {
			s_plMissDoneCnt = self->playcnt;
			if (::IsWindow(self->m_lc.GetSafeHwnd()))
				self->m_lc.Invalidate(FALSE);
		}
		return;
	}
	if (s_plMissDoneCnt == self->playcnt) return;
	if (InterlockedCompareExchange(&s_plMissBusy, 1, 0) != 0) return;

	const int n = self->playcnt;
	if (s_plMissCap < n) {
		char* np = (char*)realloc(s_plMiss, (size_t)n);
		if (!np) {
			InterlockedExchange(&s_plMissBusy, 0);
			return;
		}
		if (s_plMissCap > 0)
			memset(np + s_plMissCap, 0, (size_t)(n - s_plMissCap));
		else
			memset(np, 0, (size_t)n);
		s_plMiss = np;
		s_plMissCap = n;
	}

	PlMissJob* job = (PlMissJob*)malloc(sizeof(PlMissJob));
	if (!job) {
		InterlockedExchange(&s_plMissBusy, 0);
		return;
	}
	ZeroMemory(job, sizeof(*job));
	job->hwnd = self->m_hWnd;
	job->gen = InterlockedIncrement(&s_plMissGen);
	job->count = n;
	job->indices = (int*)malloc(sizeof(int) * (size_t)n);
	job->subs = (int*)malloc(sizeof(int) * (size_t)n);
	job->fols = (TCHAR*)malloc(sizeof(TCHAR) * 1024 * (size_t)n);
	job->result = (char*)malloc((size_t)n);
	if (!job->indices || !job->subs || !job->fols || !job->result) {
		free(job->indices); free(job->subs); free(job->fols); free(job->result); free(job);
		InterlockedExchange(&s_plMissBusy, 0);
		return;
	}
	for (int i = 0; i < n; ++i) {
		job->indices[i] = i;
		job->subs[i] = self->pc[i].sub;
		_tcsncpy_s(job->fols + (size_t)i * 1024, 1024, self->pc[i].fol, _TRUNCATE);
	}
	if (!AfxBeginThread(PlMissScanThread, job, THREAD_PRIORITY_BELOW_NORMAL)) {
		free(job->indices); free(job->subs); free(job->fols); free(job->result); free(job);
		InterlockedExchange(&s_plMissBusy, 0);
	}
}

static void PlSelectItemAtScreenPoint(CListCtrl& lc, CPoint screenPt)
{
	CPoint pt = screenPt;
	lc.ScreenToClient(&pt);
	const int hit = lc.HitTest(pt, NULL);
	if (hit < 0) return;
	if (!(lc.GetItemState(hit, LVIS_SELECTED) & LVIS_SELECTED)) {
		const int n = lc.GetItemCount();
		for (int i = 0; i < n; ++i)
			lc.SetItemState(i, 0, LVIS_SELECTED);
		lc.SetItemState(hit, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
}

int CPlayList::FindByPath(LPCTSTR fol)
{
	if (!pc || playcnt <= 0 || !fol || !*fol) return -1;
	if (!PlIsAbsoluteMediaPath(fol)) return -1;
	const CString want = NormalizePlaylistPath(fol);
	if (want.IsEmpty()) return -1;
	for (int j = 0; j < playcnt; j++) {
		if (!pc[j].fol[0]) continue;
		if (NormalizePlaylistPath(pc[j].fol).CompareNoCase(want) == 0)
			return j;
	}
	return -1;
}

static CString PlPlaylistFileName(int idx)
{
	if (idx == 0) return _T("playlistu.dat");
	CString s; s.Format(_T("playlistu%d.dat"), idx);
	return s;
}

static void PlPcFromPld(const playlistdata& pld, playlistdata0& out)
{
	ZeroMemory(&out, sizeof(out));
	_tcscpy(out.alb, pld.alb);
	_tcscpy(out.art, pld.art);
	_tcscpy(out.fol, pld.fol);
	_tcscpy(out.name, pld.name);
	out.loop1 = pld.loop1;
	out.loop2 = pld.loop2;
	out.sub = pld.sub;
	out.ret2 = pld.ret2;
	out.time = pld.time;
	out.icon = 1;
}

static bool PlReadPlaylistTracks(int plIdx, std::vector<playlistdata0>& tracks)
{
	tracks.clear();
	TCHAR tmp[1024];
	_tgetcwd(tmp, 1000);
	_tchdir(karento2);
	CString fname = PlPlaylistFileName(plIdx);
	CFile f;
	if (!f.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL)) {
		_tchdir(tmp);
		return false;
	}
	int cnt = 0;
	if (f.Read(&cnt, 4) != 4 || cnt < 0 || cnt > 100000) {
		f.Close(); _tchdir(tmp); return false;
	}
	int skip[9];
	for (int i = 0; i < 9; ++i)
		if (f.Read(&skip[i], 4) != 4) { f.Close(); _tchdir(tmp); return false; }
	tracks.reserve((size_t)cnt);
	playlistdata pld;
	for (int i = 0; i < cnt; ++i) {
		if (f.Read(&pld, sizeof(pld)) != sizeof(pld)) { f.Close(); _tchdir(tmp); return false; }
		playlistdata0 pc0;
		PlPcFromPld(pld, pc0);
		tracks.push_back(pc0);
	}
	for (int i = 0; i < 6; ++i) {
		int c = 0;
		if (f.Read(&c, 4) != 4) break;
	}
	f.Close();
	_tchdir(tmp);
	return true;
}

static bool PlWritePlaylistTracks(int plIdx, const std::vector<playlistdata0>& tracks)
{
	TCHAR tmp[1024];
	_tgetcwd(tmp, 1000);
	_tchdir(karento2);
	CString fname = PlPlaylistFileName(plIdx);

	int x = -10000, y = 0, cx = 800, cy = 600;
	int col[7] = { 120, 80, 72, 80, 80, 80, 80 };
	int loop = 0, renzoku = 0, tool = 1, saisyo = 1, pntSaved = -1;

	CFile rf;
	if (rf.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL)) {
		int cntOld = 0;
		if (rf.Read(&cntOld, 4) == 4) {
			rf.Read(&x, 4); rf.Read(&y, 4); rf.Read(&cx, 4); rf.Read(&cy, 4);
			for (int i = 0; i < 5; ++i) rf.Read(&col[i], 4);
			if (cntOld > 0 && cntOld < 100000) {
				const ULONGLONG skipBytes = (ULONGLONG)cntOld * (ULONGLONG)sizeof(playlistdata);
				rf.Seek(skipBytes, CFile::current);
			}
			rf.Read(&loop, 4);
			rf.Read(&renzoku, 4);
			rf.Read(&tool, 4);
			rf.Read(&saisyo, 4);
			rf.Read(&col[5], 4);
			rf.Read(&col[6], 4);
			rf.Read(&pntSaved, 4);
		}
		rf.Close();
	}

	const int cnt = (int)tracks.size();
	CFile wf;
	if (!wf.Open(fname, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL)) {
		_tchdir(tmp);
		return false;
	}
	wf.Write(&cnt, 4);
	wf.Write(&x, 4); wf.Write(&y, 4); wf.Write(&cx, 4); wf.Write(&cy, 4);
	for (int i = 0; i < 5; ++i) wf.Write(&col[i], 4);
	playlistdata pld;
	for (int i = 0; i < cnt; ++i) {
		ZeroMemory(&pld, sizeof(pld));
		_tcscpy(pld.alb, tracks[i].alb);
		_tcscpy(pld.art, tracks[i].art);
		_tcscpy(pld.fol, tracks[i].fol);
		_tcscpy(pld.name, tracks[i].name);
		pld.loop1 = tracks[i].loop1;
		pld.loop2 = tracks[i].loop2;
		pld.sub = tracks[i].sub;
		pld.ret2 = tracks[i].ret2;
		pld.time = tracks[i].time;
		wf.Write(&pld, sizeof(pld));
	}
	wf.Write(&loop, 4);
	wf.Write(&renzoku, 4);
	wf.Write(&tool, 4);
	wf.Write(&saisyo, 4);
	wf.Write(&col[5], 4);
	wf.Write(&col[6], 4);
	wf.Write(&pntSaved, 4);
	wf.Close();
	_tchdir(tmp);
	return true;
}

static int PlAdjustIndexAfterRemovals(int idx, const std::vector<int>& removedAsc)
{
	if (idx < 0) return idx;
	for (int r : removedAsc) {
		if (idx == r) return -1;
	}
	int delta = 0;
	for (int r : removedAsc) {
		if (idx > r) ++delta;
	}
	return idx - delta;
}

int CPlayList::GetPlaylistFileCount()
{
	int lcnt = 0;
	for (;; ++lcnt) {
		if (!PathFileExists(GetModulePath() + PlPlaylistFileName(lcnt)))
			break;
		if (lcnt >= 999) break;
	}
	return lcnt;
}

CString CPlayList::GetPlaylistDisplayName(int ii)
{
	if (ii < 0) return _T("");
	CString ss = savedata.playlistname[ii];
	if (ss == _T("")) {
		ss.Format(LL14(
			L"プレイリスト：%d", L"Playlist: %d", L"Liste de lecture : %d", L"Playlist: %d",
			L"Lista de reproducción: %d", L"플레이리스트: %d", L"播放列表：%d", L"قائمة التشغيل: %d",
			L"Плейлист: %d", L"Wiedergabeliste: %d", L"Lista de reprodução: %d", L"Afspeellijst: %d",
			L"Lista odtwarzania: %d", L"Oynatma Listesi: %d"), ii + 1);
	}
	return ss;
}

static bool IsDougaVideoFile(const CString& path);

static void PlMicLevSliderCb(void* /*ctx*/, int value)
{
	if (value < 0) value = 0;
	if (value > 200) value = 200;
	savedata.mic_mix_level = value;
	extern COggDlg* og;
	extern CMediaPlayerDlg* mp;
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_miclev.GetSafeHwnd())
		mp->m_miclev.SetPos(value);
	if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_miclev.GetSafeHwnd()) {
		og->m_miclev.SetPos(value);
		og->ApplyMicMixLevelLabel();
	}
	MpPersistSavedataQuick();
}

int CPlayList::ShowTrackContextMenu(CPoint pt, CWnd* pOwner)
{
	int Lindex = -1;
	Lindex = m_lc.GetNextItem(Lindex, LVNI_ALL | LVNI_SELECTED);
	if (Lindex < 0) return 0;

	CCustomPopupMenu menu;
	menu.AddCommand(PL_CTX_INFO,
		LL14(L"ファイル情報", L"File Info", L"Infos fichier", L"Info file",
			L"Info. de archivo", L"파일 정보", L"文件信息", L"معلومات الملف",
			L"Сведения о файле", L"Dateiinfo", L"Info. do arquivo", L"Bestandsinfo",
			L"Informacje o pliku", L"Dosya bilgisi"),
		LL14(L"選択曲の詳細情報を表示", L"Show details for the selection", L"Afficher les infos", L"Mostra dettagli",
			L"Mostrar detalles", L"선택 곡 상세 정보", L"显示所选详细信息", L"عرض تفاصيل التحديد",
			L"Показать сведения", L"Details anzeigen", L"Mostrar detalhes", L"Details tonen",
			L"Pokaz szczegoly", L"Secimin ayrintilarini goster"));
	menu.AddCommand(PL_CTX_WAV,
		LL14(L"音声書き出し…", L"Audio export...", L"Export audio...", L"Esporta audio...",
			L"Exportar audio...", L"오디오 내보내기...", L"音频导出…", L"تصدير الصوت...",
			L"Экспорт аудио...", L"Audio exportieren...", L"Exportar audio...", L"Audio exporteren...",
			L"Eksport audio...", L"Ses disa aktar..."));
	menu.AddCommand(PL_CTX_TRANSCODE,
		LL14(L"mp3/FLAC 変換…", L"Convert to mp3/FLAC…", L"Convertir en mp3/FLAC…", L"Converti in mp3/FLAC…",
			L"Convertir a mp3/FLAC…", L"mp3/FLAC 변환…", L"转换为 mp3/FLAC…", L"تحويل إلى mp3/FLAC…",
			L"Конвертировать в mp3/FLAC…", L"Nach mp3/FLAC konvertieren…", L"Converter para mp3/FLAC…", L"Converteren naar mp3/FLAC…",
			L"Konwertuj do mp3/FLAC…", L"mp3/FLAC donustur…"),
		LL14(L"選択曲を mp3 または FLAC に変換して書き出し", L"Convert selection to mp3 or FLAC", L"Convertir la selection en mp3/FLAC", L"Converti la selezione in mp3/FLAC",
			L"Convertir la seleccion a mp3/FLAC", L"선택 곡을 mp3/FLAC로 변환", L"将所选转为 mp3/FLAC", L"تحويل التحديد إلى mp3/FLAC",
			L"Конвертировать выбор в mp3/FLAC", L"Auswahl nach mp3/FLAC konvertieren", L"Converter selecao para mp3/FLAC", L"Selectie naar mp3/FLAC converteren",
			L"Konwertuj wybor do mp3/FLAC", L"Secimi mp3/FLAC'a donustur"));
	if (Lindex >= 0 && Lindex < playcnt && (pc[Lindex].sub == -2 || IsDougaVideoFile(pc[Lindex].fol))) {
		menu.AddCommand(PL_CTX_VIDEO_EXTRACT,
			LL14(L"動画→音声抽出…", L"Extract audio from video…", L"Extraire audio de la video…", L"Estrai audio dal video…",
				L"Extraer audio del video…", L"동영상→오디오 추출…", L"从视频提取音频…", L"استخراج صوت من الفيديو…",
				L"Извлечь аудио из видео…", L"Audio aus Video extrahieren…", L"Extrair audio do video…", L"Audio uit video…",
				L"Wyodrebnij audio z wideo…", L"Videodan ses cikar…"));
	}
	menu.AddCheck(PL_CTX_MICMIX,
		LL14(L"WAV保存時にマイクをミックス", L"Mix mic when saving WAV", L"Mixer le micro à l'enregistrement WAV", L"Mix microfono al salvataggio WAV",
			L"Mezclar micro al guardar WAV", L"WAV 저장 시 마이크 믹스", L"保存WAV时混合麦克风", L"مزج الميكروفون عند حفظ WAV",
			L"Микшировать микрофон при сохранении WAV", L"Mikrofon beim WAV-Speichern mischen", L"Misturar microfone ao salvar WAV", L"Microfoon mixen bij WAV-opslag",
			L"Miksuj mikrofon przy zapisie WAV", L"WAV kaydında mikrofonu karıştır"),
		savedata.mic_mix ? TRUE : FALSE);
	{
		int lv = savedata.mic_mix_level;
		if (lv < 0) lv = 0;
		if (lv > 200) lv = 200;
		menu.AddSlider(
			LL14(L"マイクレベル", L"Mic level", L"Niveau micro", L"Livello microfono", L"Nivel micro",
				L"마이크 레벨", L"麦克风电平", L"مستوى الميكروفون", L"Уровень микрофона", L"Mikrofonpegel",
				L"Nivel do microfone", L"Mic-niveau", L"Poziom mikrofonu", L"Mikrofon seviyesi"),
			0, 200, lv, PlMicLevSliderCb, NULL,
			LL14(L"マイクミックスの音量 0〜200（ドラッグ中に反映）", L"Mic mix level 0–200 (live)", L"Niveau mix micro 0–200 (direct)", L"Livello mix microfono 0–200 (live)", L"Nivel mezcla micro 0–200 (en vivo)",
				L"마이크 믹스 볼륨 0–200(즉시)", L"麦克风混音音量 0–200（即时）", L"مستوى مزج الميكروفون 0–200 (مباشر)", L"Громкость микса микрофона 0–200 (сразу)", L"Mikrofon-Mix-Pegel 0–200 (live)",
				L"Nivel do mix de microfone 0–200 (ao vivo)", L"Mic-mixniveau 0–200 (live)", L"Poziom mixu mikrofonu 0–200 (na zywo)", L"Mikrofon mix seviyesi 0–200 (anlik)"));
	}
	{
		int selCount = 0;
		int idx = -1;
		while ((idx = m_lc.GetNextItem(idx, LVNI_ALL | LVNI_SELECTED)) >= 0) {
			if (idx < playcnt) ++selCount;
		}
		if (selCount >= 2) {
			menu.AddCommand(PL_CTX_XFADE,
				LL14(L"クロスフェード書き出し…", L"Crossfade export...", L"Export fondu enchaine...", L"Esporta con crossfade...",
					L"Exportar con fundido cruzado...", L"크로스페이드 내보내기...", L"交叉淡入淡出导出…", L"تصدير بتلاشي متقاطع...",
					L"Экспорт с кроссфейдом...", L"Ueberblend-Export...", L"Exportar com crossfade...", L"Crossfade-export...",
					L"Eksport z przejsciem...", L"Capraz solma disa aktar..."),
				LL14(L"選択した複数曲をクロスフェードで書き出し", L"Export selection with crossfade", L"Exporter avec fondu", L"Esporta con crossfade",
					L"Exportar con fundido", L"선택 곡을 크로스페이드로 내보내기", L"交叉淡入淡出导出所选", L"تصدير بتلاشي متقاطع",
					L"Экспорт с кроссфейдом", L"Mit Ueberblendung exportieren", L"Exportar com crossfade", L"Exporteren met crossfade",
					L"Eksport z przejsciem", L"Capraz solma ile aktar"));
		}
	}
	menu.AddCommand(PL_CTX_TAG_EDIT,
		LL14(L"タグ編集…", L"Edit tags...", L"Modifier les tags...", L"Modifica tag...",
			L"Editar etiquetas...", L"태그 편집...", L"编辑标签…", L"تحرير الوسوم...",
			L"Редактировать теги...", L"Tags bearbeiten...", L"Editar tags...", L"Tags bewerken...",
			L"Edytuj tagi...", L"Etiketleri duzenle..."));
	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
		menu.AddCommand(PL_CTX_MB_AUTOTAG,
			LL14(L"MusicBrainz 自動タグ", L"MusicBrainz auto-tag", L"Auto-tag MusicBrainz", L"Auto-tag MusicBrainz",
				L"Auto-etiqueta MusicBrainz", L"MusicBrainz 자동 태그", L"MusicBrainz 自动标签", L"وسم تلقائي MusicBrainz",
				L"Автотег MusicBrainz", L"MusicBrainz Auto-Tag", L"Auto-tag MusicBrainz", L"MusicBrainz auto-tag",
				L"Auto-tag MusicBrainz", L"MusicBrainz otomatik etiket"));
		CString bpmLbl;
		if (MpBpmIsMeasuring())
			bpmLbl = LL14(L"BPM 計測中…（再クリックで確定）", L"Measuring BPM… (click again)", L"Mesure BPM…", L"Misura BPM…", L"Midiendo BPM…", L"BPM 측정 중…", L"正在测 BPM…", L"قياس BPM…", L"Измерение BPM…", L"BPM messen…", L"Medindo BPM…", L"BPM meten…", L"Pomiar BPM…", L"BPM olculuyor…");
		else if (savedata.mpDetectedBpm > 0)
			bpmLbl.Format(LL14(L"BPM 計測（現在 %d）", L"Measure BPM (now %d)", L"Mesurer BPM (%d)", L"Misura BPM (%d)", L"Medir BPM (%d)", L"BPM 측정 (현재 %d)", L"测量 BPM（当前 %d）", L"قياس BPM (%d)", L"BPM (%d)", L"BPM messen (%d)", L"Medir BPM (%d)", L"BPM meten (%d)", L"Mierz BPM (%d)", L"BPM olc (%d)"), savedata.mpDetectedBpm);
		else
			bpmLbl = LL14(L"BPM 計測", L"Measure BPM", L"Mesurer BPM", L"Misura BPM", L"Medir BPM", L"BPM 측정", L"测量 BPM", L"قياس BPM", L"Измерить BPM", L"BPM messen", L"Medir BPM", L"BPM meten", L"Mierz BPM", L"BPM olc");
		menu.AddCheck(PL_CTX_BPM, bpmLbl, MpBpmIsMeasuring() ? TRUE : FALSE,
			LL14(L"再生中のテンポを計測／確定", L"Measure / confirm tempo while playing", L"Mesurer / confirmer le tempo", L"Misura / conferma il tempo",
				L"Medir / confirmar tempo", L"재생 중 템포 측정/확정", L"测量/确认播放中的速度", L"قياس/تأكيد الإيقاع",
				L"Измерить / подтвердить темп", L"Tempo messen / bestaetigen", L"Medir / confirmar o tempo", L"Tempo meten / bevestigen",
				L"Zmierz / potwierdz tempo", L"Temposu olc / onayla"));
		if (!MpBpmIsMeasuring() && (savedata.mpDetectedBpm > 0 || savedata.mpBpmCand[0] > 0)) {
			MpBpmEnsureCandList();
			int nSub = 0;
			for (int ci = 0; ci < 3; ++ci) {
				if (savedata.mpBpmCand[ci] > 0) ++nSub;
			}
			if (nSub > 0) {
				CCustomPopupMenu* candSub = menu.AddSubMenu(
					LL14(L"BPM 候補", L"BPM candidates", L"Candidats BPM", L"Candidati BPM",
						L"Candidatos BPM", L"BPM 후보", L"BPM 候选", L"مرشحو BPM",
						L"Кандидаты BPM", L"BPM-Kandidaten", L"Candidatos BPM", L"BPM-kandidaten",
						L"Kandydaci BPM", L"BPM adaylari"),
					LL14(L"検出された候補BPMを選択", L"Pick a detected BPM candidate", L"Choisir un candidat BPM", L"Scegli un candidato BPM",
						L"Elegir candidato BPM", L"감지된 BPM 후보 선택", L"选择检测到的 BPM 候选", L"اختر مرشح BPM",
						L"Выбрать кандидата BPM", L"BPM-Kandidat waehlen", L"Escolher candidato BPM", L"BPM-kandidaat kiezen",
						L"Wybierz kandydata BPM", L"BPM adayi sec"));
				if (candSub) {
					for (int ci = 0; ci < 3; ++ci) {
						const int cb = savedata.mpBpmCand[ci];
						if (cb <= 0) continue;
						CString candItem;
						candItem.Format(L"%d", cb);
						const UINT id = (ci == 0) ? PL_CTX_BPM_CAND1 : (ci == 1) ? PL_CTX_BPM_CAND2 : PL_CTX_BPM_CAND3;
						candSub->AddCheck(id, candItem, savedata.mpDetectedBpm == cb ? TRUE : FALSE);
					}
				}
			}
		}
		menu.AddCommand(PL_CTX_NORM_SCAN,
			LL14(L"ラウドネス計測", L"Measure loudness", L"Mesurer loudness", L"Misura loudness", L"Medir loudness", L"라우드니스 측정", L"响度测量", L"قياس الجهارة", L"Измерить громкость", L"Lautheit messen", L"Medir loudness", L"Loudness meten", L"Zmierz glosnosc", L"Loudness olc"));
		menu.AddCommand(PL_CTX_EXPORT_AB,
			LL14(L"A-B を WAV 書き出し", L"Export A-B to WAV", L"Exporter A-B en WAV", L"Esporta A-B in WAV", L"Exportar A-B a WAV", L"A-B를 WAV로 내보내기", L"将 A-B 导出为 WAV", L"تصدير A-B إلى WAV", L"Экспорт A-B в WAV", L"A-B als WAV exportieren", L"Exportar A-B para WAV", L"A-B naar WAV", L"Eksport A-B do WAV", L"A-B WAV aktar"));
		menu.AddCommand(PL_CTX_SSVIZ,
			LL14(L"SS ビジュアライザ", L"SS visualizer", L"Visualiseur SS", L"Visualizzatore SS", L"Visualizador SS", L"SS 비주얼", L"SS 可视化", L"عارض SS", L"SS-визуализатор", L"SS-Visualizer", L"Visual SS", L"SS-visualizer", L"Wizual SS", L"SS gorsel"));
		menu.AddCheck(PL_CTX_DESK_LRC,
			LL14(L"歌詞ウィンドウを表示", L"Show lyrics window", L"Afficher fenetre paroles", L"Mostra finestra testi", L"Mostrar ventana de letra",
				L"가사 창 표시", L"显示歌词窗口", L"عرض نافذة الكلمات", L"Показать окно текста", L"Textfenster anzeigen",
				L"Mostrar janela de letra", L"Songtekstvenster tonen", L"Pokaz okno tekstu", L"Soz penceresini goster"),
			savedata.deskLrcOn ? TRUE : FALSE);
		menu.AddCommand(PL_CTX_DUPES,
			LL14(L"重複スキャン", L"Scan duplicates", L"Detecter doublons", L"Scansiona duplicati", L"Buscar duplicados", L"중복 스캔", L"扫描重复", L"فحص التكرار", L"Поиск дублей", L"Duplikate scannen", L"Procurar duplicatas", L"Duplicaten scannen", L"Skanuj duplikaty", L"Kopyalari tara"));
		menu.AddCommand(PL_CTX_FOLDER_SYNC,
			LL14(L"フォルダ差分", L"Folder sync diff", L"Diff dossier", L"Diff cartella", L"Diff carpeta", L"폴더 차이", L"文件夹差异", L"فرق المجلد", L"Разница папки", L"Ordner-Diff", L"Diff pasta", L"Mapverschil", L"Roznica folderu", L"Klasor farki"));
	}
	menu.AddSeparator();
	menu.AddCommand(PL_CTX_REMOVE_MISSING,
		LL14(L"存在しないファイルを一覧から削除", L"Remove missing files from list",
			L"Supprimer les fichiers manquants", L"Rimuovi file mancanti", L"Eliminar archivos inexistentes",
			L"없는 파일을 목록에서 삭제", L"从列表中删除不存在的文件", L"إزالة الملفات المفقودة",
			L"Удалить отсутствующие файлы", L"Fehlende Dateien entfernen", L"Remover arquivos ausentes",
			L"Ontbrekende bestanden verwijderen", L"Usuń brakujące pliki", L"Eksik dosyalari kaldir"));
	menu.AddCommand(PL_CTX_RESCAN_MISS,
		LL14(L"欠損マークを再スキャン", L"Rescan missing marks", L"Rescanner les manquants", L"Ricontrolla mancanti",
			L"Reexaminar faltantes", L"결손 표시 재스캔", L"重新扫描缺失标记", L"إعادة فحص المفقود",
			L"Пересканировать отсутствующие", L"Fehlend-Markierungen erneut pruefen", L"Verificar ausentes de novo",
			L"Ontbrekende markeringen opnieuw scannen", L"Ponownie skanuj brakujace", L"Eksik isaretlerini yeniden tara"));
	menu.AddCommand(PL_CTX_REFRESH_JAK,
		LL14(L"選択曲のジャケを再取得", L"Refresh jacket for selection", L"Rafraichir la pochette", L"Aggiorna copertina",
			L"Actualizar caratula", L"선택 곡 재킷 다시 가져오기", L"重新获取所选封面", L"تحديث الغلاف للتحديد",
			L"Обновить обложку выбранного", L"Cover der Auswahl neu laden", L"Atualizar capa da selecao",
			L"Hoes van selectie vernieuwen", L"Odswiez okladke zaznaczenia", L"Secimin kapagini yenile"));
	menu.AddCommand(PL_CTX_DEL,
		LL14(L"削除", L"Delete", L"Supprimer", L"Elimina",
			L"Eliminar", L"삭제", L"删除", L"حذف",
			L"Удалить", L"Löschen", L"Excluir", L"Verwijderen",
			L"Usuń", L"Sil"),
		LL14(L"一覧から選択曲を削除（ファイルは残る）", L"Remove selection from list (files kept)", L"Retirer de la liste", L"Rimuovi dall'elenco",
			L"Quitar de la lista", L"목록에서 선택 곡 삭제(파일 유지)", L"从列表删除所选（保留文件）", L"إزالة من القائمة",
			L"Удалить из списка", L"Aus Liste entfernen", L"Remover da lista", L"Uit lijst verwijderen",
			L"Usun z listy", L"Listeden kaldir (dosya kalir)"));
	menu.AddCommand(PL_CTX_CLEAR_SONGPARAM,
		LL14(L"選択曲の記憶パラメータを削除", L"Clear saved params for selection",
			L"Effacer les parametres enregistres de la selection", L"Cancella parametri salvati della selezione",
			L"Borrar parametros guardados de la seleccion", L"선택 곡의 저장 파라미터 삭제",
			L"删除所选曲目的已存参数", L"مسح المعلمات المحفوظة للتحديد",
			L"Удалить сохранённые параметры выбранного", L"Gespeicherte Parameter der Auswahl loeschen",
			L"Limpar parametros salvos da selecao", L"Opgeslagen parameters van selectie wissen",
			L"Usun zapisane parametry zaznaczenia", L"Secimin kayitli parametrelerini sil"));
	menu.AddCommand(PL_CTX_PROTOOLS,
		LL14(L"再生詳細...", L"Playback details...", L"Details lecture...", L"Dettagli riproduzione...",
			L"Detalles de reproduccion...", L"재생 상세...", L"播放详情...", L"تفاصيل التشغيل...",
			L"Параметры воспроизведения...", L"Wiedergabedetails...", L"Detalhes de reproducao...", L"Afspeeldetails...",
			L"Szczegoly odtwarzania...", L"Oynatma ayrintilari..."));
	menu.AddCheck(PL_CTX_EQ,
		LL14(L"イコライザーを開く", L"Open Equalizer", L"Ouvrir l'egaliseur", L"Apri equalizzatore",
			L"Abrir ecualizador", L"이퀄라이저 열기", L"打开均衡器", L"فتح المعادل",
			L"Открыть эквалайзер", L"Equalizer offnen", L"Abrir equalizador", L"Equalizer openen",
			L"Otworz equalizer", L"Equalizeri ac"),
		savedata.eqwindow ? TRUE : FALSE);
	menu.AddCheck(PL_CTX_ANALYZER,
		LL14(L"アナライザーを開く", L"Open Analyzer", L"Ouvrir l'analyseur", L"Apri analizzatore",
			L"Abrir analizador", L"애널라이저 열기", L"打开分析器", L"فتح المحلل",
			L"Открыть анализатор", L"Analyzer offnen", L"Abrir analisador", L"Analyser openen",
			L"Otworz analizator", L"Analizoru ac"),
		savedata.analyzerwindow ? TRUE : FALSE);
	menu.AddCheck(PL_CTX_PIANOROLL,
		LL14(L"ピアノロールを開く", L"Open Piano Roll", L"Ouvrir le piano roll", L"Apri piano roll",
			L"Abrir piano roll", L"피아노 롤 열기", L"打开钢琴卷帘", L"فتح لفة البيانو",
			L"Открыть пианоролл", L"Piano Roll offnen", L"Abrir piano roll", L"Piano roll openen",
			L"Otworz piano roll", L"Piano roll'u ac"),
		savedata.pianorollwindow ? TRUE : FALSE);
	menu.AddSeparator();
	menu.AddCommand(PL_CTX_COPY_TITLEART,
		LL14(L"タイトル - アーティストをコピー", L"Copy Title - Artist", L"Copier Titre - Artiste", L"Copia Titolo - Artista",
			L"Copiar Titulo - Artista", L"제목 - 아티스트 복사", L"复制 标题 - 艺术家", L"نسخ العنوان - الفنان",
			L"Копировать Название - Исполнитель", L"Titel - Interpret kopieren", L"Copiar Titulo - Artista", L"Kopieer Titel - Artiest",
			L"Kopiuj Tytul - Artysta", L"Baslik - Sanatciyi kopyala"));
	menu.AddCommand(PL_CTX_ADD_FOLDER,
		LL14(L"フォルダから追加...", L"Add from folder...", L"Ajouter depuis un dossier...", L"Aggiungi da cartella...",
			L"Anadir desde carpeta...", L"폴더에서 추가...", L"从文件夹添加...", L"إضافة من مجلد...",
			L"Добавить из папки...", L"Aus Ordner hinzufugen...", L"Adicionar da pasta...", L"Toevoegen uit map...",
			L"Dodaj z folderu...", L"Klasorden ekle..."));
	menu.AddCommand(PL_CTX_ADD_SAME_FOLDER,
		LL14(L"同じフォルダの曲を追加", L"Add tracks from same folder", L"Ajouter les pistes du meme dossier",
			L"Aggiungi brani dalla stessa cartella", L"Anadir pistas de la misma carpeta",
			L"같은 폴더의 곡 추가", L"添加同一文件夹的曲目", L"إضافة مقاطع من نفس المجلد",
			L"Добавить треки из этой же папки", L"Titel aus demselben Ordner hinzufugen",
			L"Adicionar faixas da mesma pasta", L"Nummers uit dezelfde map toevoegen",
			L"Dodaj utwory z tego samego folderu", L"Ayni klasordeki parcalari ekle"));
	menu.AddCommand(PL_CTX_OPEN_FOLDER,
		LL14(L"エクスプローラーで開く", L"Open containing folder", L"Ouvrir le dossier du fichier",
			L"Apri cartella del file", L"Abrir la carpeta del archivo",
			L"파일 위치 열기", L"打开所在文件夹", L"فتح المجلد الحاوي",
			L"Открыть папку с файлом", L"Ordner im Explorer offnen",
			L"Abrir a pasta do arquivo", L"Map van bestand openen",
			L"Otworz folder pliku", L"Dosya klasorunu ac"));
	{
		CCustomPopupMenu* subSort = menu.AddSubMenu(
			LL14(L"並べ替え", L"Sort", L"Trier", L"Ordina", L"Ordenar", L"정렬", L"排序", L"ترتيب", L"Сортировка", L"Sortieren", L"Ordenar", L"Sorteren", L"Sortuj", L"Sirala"));
		if (subSort) {
			subSort->AddCommand(PL_CTX_SORT_NAME,
				LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"));
			subSort->AddCommand(PL_CTX_SORT_ART,
				LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Interpret", L"Artista", L"Artiest", L"Artysta", L"Sanatci"));
			subSort->AddCommand(PL_CTX_SORT_ALB,
				LL14(L"アルバム", L"Album", L"Album", L"Album", L"Album", L"앨범", L"专辑", L"الألبوم", L"Альбом", L"Album", L"Album", L"Album", L"Album", L"Album"));
			subSort->AddCommand(PL_CTX_SORT_TIME,
				LL14(L"時間", L"Time", L"Duree", L"Durata", L"Duracion", L"시간", L"时间", L"الوقت", L"Время", L"Zeit", L"Duracao", L"Tijd", L"Czas", L"Sure"));
		}
	}
	menu.AddSeparator();
	menu.AddCommand(PL_CTX_AB_SET_A,
		LL14(L"A-B: 現在位置をAに", L"A-B: Set A at position", L"A-B: Definir A", L"A-B: Imposta A", L"A-B: Fijar A", L"A-B: 현재 위치를 A로", L"A-B: 将当前位置设为A", L"A-B: تعيين A", L"A-B: Задать A", L"A-B: A setzen", L"A-B: Definir A", L"A-B: Stel A in", L"A-B: Ustaw A", L"A-B: A ayarla"));
	menu.AddCommand(PL_CTX_AB_SET_B,
		LL14(L"A-B: 現在位置をBに", L"A-B: Set B at position", L"A-B: Definir B", L"A-B: Imposta B", L"A-B: Fijar B", L"A-B: 현재 위치를 B로", L"A-B: 将当前位置设为B", L"A-B: تعيين B", L"A-B: Задать B", L"A-B: B setzen", L"A-B: Definir B", L"A-B: Stel B in", L"A-B: Ustaw B", L"A-B: B ayarla"));
	menu.AddCommand(PL_CTX_AB_CLEAR,
		LL14(L"A-Bリピート解除", L"Clear A-B repeat", L"Effacer A-B", L"Cancella A-B", L"Borrar A-B", L"A-B 반복 해제", L"清除A-B重复", L"مسح تكرار A-B", L"Сбросить A-B", L"A-B aufheben", L"Limpar A-B", L"A-B wissen", L"Wyczysc A-B", L"A-B temizle"));
	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
		menu.AddSeparator();
		menu.AddCommand(PL_CTX_QUEUE_ADD,
			LL14(L"Up Next に追加", L"Add to Up Next", L"Ajouter a Up Next", L"Aggiungi a Up Next", L"Anadir a Up Next", L"Up Next에 추가", L"加入 Up Next", L"إضافة إلى Up Next", L"В Up Next", L"Zu Up Next", L"Adicionar a Up Next", L"Toevoegen aan Up Next", L"Dodaj do Up Next", L"Up Next'e ekle"));
		menu.AddCommand(PL_CTX_QUEUE_PLAYNEXT,
			LL14(L"次に再生", L"Play Next", L"Lire ensuite", L"Riproduci dopo", L"Reproducir despues", L"다음에 재생", L"下一首播放", L"تشغيل التالي", L"Играть следующим", L"Als Nachstes", L"Tocar a seguir", L"Speel hierna", L"Odtworz nastepnie", L"Sonraki oynat"));
		CString qclr;
		qclr.Format(LL14(L"キューをクリア (%d)", L"Clear Queue (%d)", L"Vider la file (%d)", L"Svuota coda (%d)", L"Vaciar cola (%d)", L"큐 비우기 (%d)", L"清空队列 (%d)", L"مسح الطابور (%d)", L"Очистить очередь (%d)", L"Warteschlange leeren (%d)", L"Limpar fila (%d)", L"Wachtrij wissen (%d)", L"Wyczysc kolejke (%d)", L"Kuyrugu temizle (%d)"),
			mp->m_queueN);
		menu.AddCommand(PL_CTX_QUEUE_CLEAR, qclr, NULL, mp->m_queueN > 0 ? TRUE : FALSE);
	}
	menu.AddSeparator();

	const int plCnt = GetPlaylistFileCount();
	if (plCnt > 1) {
		int otherN = 0;
		for (int i = 0; i < plCnt; ++i) {
			if (i != savedata.playlistnum) ++otherN;
		}
		if (otherN > 0) {
			CCustomPopupMenu* subMove = menu.AddSubMenu(
				LL14(L"他のリストへ移動", L"Move to another playlist", L"Deplacer vers une autre liste",
					L"Sposta in un'altra playlist", L"Mover a otra lista", L"다른 리스트로 이동", L"移动到其他列表",
					L"نقل إلى قائمة أخرى", L"Переместить в другой плейлист", L"In andere Liste verschieben",
					L"Mover para outra lista", L"Verplaatsen naar andere lijst", L"Przenieś do innej listy",
					L"Baska listeye tasi"));
			CCustomPopupMenu* subCopy = menu.AddSubMenu(
				LL14(L"他のリストへコピー", L"Copy to another playlist", L"Copier vers une autre liste",
					L"Copia in un'altra playlist", L"Copiar a otra lista", L"다른 리스트로 복사", L"复制到其他列表",
					L"نسخ إلى قائمة أخرى", L"Копировать в другой плейлист", L"In andere Liste kopieren",
					L"Copiar para outra lista", L"Kopiëren naar andere lijst", L"Kopiuj do innej listy",
					L"Baska listeye kopyala"));
			for (int i = 0; i < plCnt; ++i) {
				if (i == savedata.playlistnum) continue;
				CString name = GetPlaylistDisplayName(i);
				if (subMove) subMove->AddCommand(PL_CTX_MOVE_BASE + i, name);
				if (subCopy) subCopy->AddCommand(PL_CTX_COPY_BASE + i, name);
			}
		}
	}

	menu.AddSeparator();
	menu.AddCommand(ID_HELP_SHOWSHEET,
		LL14(L"操作ガイド", L"Operation guide", L"Guide d'utilisation", L"Guida operativa",
			L"Guía de operación", L"조작 가이드", L"操作指南", L"دليل التشغيل",
			L"Руководство", L"Bedienungsanleitung", L"Guia de operação", L"Handleiding",
			L"Przewodnik", L"İşlem kılavuzu"));

	return (int)menu.Track(pt, pOwner ? pOwner : this);
}

void CPlayList::HandleTrackContextCmd(int cmd)
{
	if (cmd == ID_HELP_SHOWSHEET) { ShowHelpSheet(); return; }
	if (cmd == PL_CTX_INFO) OnList();
	else if (cmd == PL_CTX_WAV) OnPopWavExport();
	else if (cmd == PL_CTX_VIDEO_EXTRACT) {
		extern CMediaPlayerDlg* mp;
		MpOnVideoExtract(mp);
	}
	else if (cmd == PL_CTX_MICMIX) {
		savedata.mic_mix = savedata.mic_mix ? 0 : 1;
		extern COggDlg* og;
		extern CMediaPlayerDlg* mp;
		if (og && ::IsWindow(og->GetSafeHwnd())) {
			if (og->m_micmix.GetSafeHwnd())
				og->m_micmix.SetCheck(savedata.mic_mix ? BST_CHECKED : BST_UNCHECKED);
			og->OnMicMixCheck();
		} else {
			MpPersistSavedataQuick();
		}
		if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_micmix.GetSafeHwnd())
			mp->m_micmix.SetCheck(savedata.mic_mix ? BST_CHECKED : BST_UNCHECKED);
	}
	else if (cmd == PL_CTX_XFADE) OnPopXfadeExport();
	else if (cmd == PL_CTX_TRANSCODE) OnPopTranscode();
	else if (cmd == PL_CTX_TAG_EDIT) OnPopTagEdit();
	else if (cmd == PL_CTX_DEL) Del();
	else if (cmd == PL_CTX_REMOVE_MISSING) RemoveMissingFiles();
	else if (cmd == PL_CTX_RESCAN_MISS) {
		if (pc && playcnt > 0) {
			for (int i = 0; i < playcnt; ++i)
				PlMissDiskForget(pc[i].fol);
			s_plMissDoneCnt = -1;
			InterlockedIncrement(&s_plMissGen);
			extern CMediaPlayerDlg* mp;
			if (mp && ::IsWindow(mp->GetSafeHwnd())) {
				mp->StopMissScan();
				if (mp->m_miss && mp->m_missCap > 0)
					memset(mp->m_miss, 0, (size_t)mp->m_missCap);
				mp->m_missScan = 0;
			}
			if (s_plMissCap < playcnt) {
				char* np = (char*)realloc(s_plMiss, (size_t)playcnt);
				if (np) {
					if (s_plMissCap > 0)
						memset(np + s_plMissCap, 0, (size_t)(playcnt - s_plMissCap));
					else
						memset(np, 0, (size_t)playcnt);
					s_plMiss = np;
					s_plMissCap = playcnt;
				}
			}
			for (int i = 0; i < playcnt; ++i) {
				const int m = PlTrackLooksMissing(pc[i].sub, pc[i].fol) ? 1 : 0;
				PlMissDiskSet(pc[i].fol, m);
				if (s_plMiss && i < s_plMissCap)
					s_plMiss[i] = (char)m;
				if (mp && mp->m_miss && i < mp->m_missCap)
					mp->m_miss[i] = (char)m;
			}
			if (mp && ::IsWindow(mp->GetSafeHwnd()))
				mp->m_missScan = playcnt;
			s_plMissDoneCnt = playcnt;
			m_lc.Invalidate(FALSE);
		}
	}
	else if (cmd == PL_CTX_REFRESH_JAK) {
		extern CMediaPlayerDlg* mp;
		int idx = -1;
		while ((idx = m_lc.GetNextItem(idx, LVNI_ALL | LVNI_SELECTED)) >= 0) {
			if (!pc || idx < 0 || idx >= playcnt) continue;
			PlJakDiskForget(pc[idx].fol);
			PlJakMemInit();
			for (int j = 0; j < kPlJakN; ++j) {
				if (s_plJakKey[j][0] && _tcsicmp(s_plJakKey[j], pc[idx].fol) == 0) {
					if (s_plJakBmp[j]) { ::DeleteObject(s_plJakBmp[j]); s_plJakBmp[j] = NULL; }
					s_plJakKey[j][0] = 0;
					s_plJakTick[j] = 0;
					break;
				}
			}
			if (mp) {
				for (int j = 0; j < CMediaPlayerDlg::kMpJakN; ++j) {
					if (mp->m_jakKey[j][0] && _tcsicmp(mp->m_jakKey[j], pc[idx].fol) == 0) {
						if (mp->m_jakBmp[j]) { ::DeleteObject(mp->m_jakBmp[j]); mp->m_jakBmp[j] = NULL; }
						mp->m_jakKey[j][0] = 0;
						mp->m_jakTick[j] = 0;
						mp->m_jakRow[j] = -1;
						break;
					}
				}
			}
		}
		m_lc.Invalidate(FALSE);
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->m_list.Invalidate(FALSE);
		s_plJakNoDiskN = 0;
		InterlockedIncrement(&s_plJakGen);
		s_plJakPend[0] = 0;
		if (mp) {
			InterlockedIncrement(&mp->m_jakGen);
			mp->m_jakPend[0] = 0;
		}
	}
	else if (cmd == PL_CTX_PROTOOLS) {
		extern void OpenProToolsForSelection();
		OpenProToolsForSelection();
	}
	else if (cmd == PL_CTX_EQ) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_OPEN_EQ, 0);
	}
	else if (cmd == PL_CTX_CLEAR_SONGPARAM) {
		std::vector<playlistdata0> items;
		int idx = -1;
		while ((idx = m_lc.GetNextItem(idx, LVNI_ALL | LVNI_SELECTED)) >= 0) {
			if (idx >= 0 && idx < playcnt && pc)
				items.push_back(pc[idx]);
		}
		if (!items.empty()) {
			CString msg = LL14(
				L"選択した曲の記憶パラメータ(音量・EQ等)を削除します。よろしいですか？",
				L"Clear saved parameters (volume, EQ, etc.) for the selected tracks?",
				L"Effacer les parametres enregistres (volume, EQ, etc.) des morceaux selectionnes ?",
				L"Cancellare i parametri salvati (volume, EQ, ecc.) dei brani selezionati?",
				L"Borrar los parametros guardados (volumen, EQ, etc.) de las pistas seleccionadas?",
				L"선택한 곡의 저장 파라미터(볼륨·EQ 등)를 삭제할까요?",
				L"删除所选曲目的已存参数（音量、EQ等）？",
				L"مسح المعلمات المحفوظة (الصوت، EQ، إلخ) للمسارات المحددة؟",
				L"Удалить сохранённые параметры (громкость, EQ и т.д.) выбранных треков?",
				L"Gespeicherte Parameter (Lautstaerke, EQ usw.) der Auswahl loeschen?",
				L"Limpar parametros salvos (volume, EQ etc.) das faixas selecionadas?",
				L"Opgeslagen parameters (volume, EQ enz.) van selectie wissen?",
				L"Usunąć zapisane parametry (głośność, EQ itd.) zaznaczonych utworów?",
				L"Seçili parçaların kayıtlı parametreleri (ses, EQ vb.) silinsin mi?");
			if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION) == IDYES) {
				CString listKey = SongParams_CurrentListName();
				SongParams_RebindEntries(listKey, NULL, items.data(), (int)items.size(), false);
			}
		}
	}
	else if (cmd == PL_CTX_ANALYZER) {
		if (og && ::IsWindow(og->GetSafeHwnd())) og->ToggleAnalyzer();
	}
	else if (cmd == PL_CTX_PIANOROLL) {
		if (og && ::IsWindow(og->GetSafeHwnd())) og->TogglePianoRoll();
	}
	else if (cmd == PL_CTX_COPY_TITLEART) {
		const int idx = m_lc.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
		if (pc && idx >= 0 && idx < playcnt) {
			CString s;
			s.Format(_T("%s - %s"), pc[idx].name, pc[idx].art);
			if (OpenClipboard()) {
				EmptyClipboard();
				const size_t bytes = ((size_t)s.GetLength() + 1) * sizeof(TCHAR);
				HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
				if (h) {
					memcpy(GlobalLock(h), (LPCTSTR)s, bytes);
					GlobalUnlock(h);
#ifdef _UNICODE
					SetClipboardData(CF_UNICODETEXT, h);
#else
					SetClipboardData(CF_TEXT, h);
#endif
				}
				CloseClipboard();
			}
		}
	}
	else if (cmd == PL_CTX_ADD_FOLDER) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd())) {
			mp->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_ADDFOLDER, BN_CLICKED), 0);
		}
		else {
			BROWSEINFO bi; ZeroMemory(&bi, sizeof(bi));
			TCHAR path[MAX_PATH] = { 0 };
			bi.hwndOwner = GetSafeHwnd();
			bi.pszDisplayName = path;
			bi.lpszTitle = LL14(L"追加するフォルダを選んでください", L"Select a folder to add", L"Choisir un dossier", L"Scegli una cartella", L"Elija una carpeta", L"추가할 폴더 선택", L"选择要添加的文件夹", L"اختر مجلدًا للإضافة", L"Выберите папку", L"Ordner zum Hinzufugen", L"Selecione uma pasta", L"Selecteer een map", L"Wybierz folder", L"Eklenecek klasoru secin");
			bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
			LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
			if (pidl) {
				if (SHGetPathFromIDList(pidl, path)) {
					TCHAR cwd[1024];
					_tgetcwd(cwd, 1000);
					syo = 0; syos = _T(""); syomode = 0;
					m_lc.SetRedraw(FALSE);
					Fol(path);
					m_lc.SetRedraw(TRUE);
					m_lc.Invalidate();
					m_lc.UpdateWindow();
					_tchdir(cwd);
					Save();
				}
				CoTaskMemFree(pidl);
			}
		}
	}
	else if (cmd == PL_CTX_SORT_NAME || cmd == PL_CTX_SORT_ART || cmd == PL_CTX_SORT_ALB || cmd == PL_CTX_SORT_TIME) {
		extern CMediaPlayerDlg* mp;
		const int key = (cmd == PL_CTX_SORT_NAME) ? 1
			: (cmd == PL_CTX_SORT_ART) ? 2
			: (cmd == PL_CTX_SORT_ALB) ? 3 : 4;
		MpSortPlaylistByKey(key);
		m_lc.Invalidate(FALSE);
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->RefreshList(TRUE);
		MpPersistSavedataQuick();
	}
	else if (cmd == PL_CTX_AB_SET_A) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_SEEK_SETA, 0);
	}
	else if (cmd == PL_CTX_AB_SET_B) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_SEEK_SETB, 0);
	}
	else if (cmd == PL_CTX_AB_CLEAR) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_SEEK_ABCLR, 0);
	}
	else if (cmd == PL_CTX_QUEUE_ADD) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_QUEUE_ADD, 0);
	}
	else if (cmd == PL_CTX_QUEUE_PLAYNEXT) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_QUEUE_PLAYNEXT, 0);
	}
	else if (cmd == PL_CTX_QUEUE_CLEAR) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_QUEUE_CLEAR, 0);
	}
	else if (cmd == PL_CTX_MB_AUTOTAG) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_MB_AUTOTAG, 0);
	}
	else if (cmd == PL_CTX_BPM) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_BPM_DETECT, 0);
	}
	else if (cmd == PL_CTX_BPM_CAND1) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_BPM_CAND1, 0);
	}
	else if (cmd == PL_CTX_BPM_CAND2) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_BPM_CAND2, 0);
	}
	else if (cmd == PL_CTX_BPM_CAND3) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_BPM_CAND3, 0);
	}
	else if (cmd == PL_CTX_NORM_SCAN) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_NORM_SCAN, 0);
	}
	else if (cmd == PL_CTX_EXPORT_AB) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_EXPORT_AB_NOW, 0);
	}
	else if (cmd == PL_CTX_SSVIZ) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_SSVIZ, 0);
	}
	else if (cmd == PL_CTX_DESK_LRC) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_DESK_LRC, 0);
	}
	else if (cmd == PL_CTX_DUPES) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_DUPES, 0);
	}
	else if (cmd == PL_CTX_FOLDER_SYNC) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->SendMessage(WM_COMMAND, ID_MP_FOLDER_SYNC, 0);
	}
	else if (cmd == PL_CTX_OPEN_FOLDER || cmd == PL_CTX_ADD_SAME_FOLDER) {
		const int idx = m_lc.GetNextItem(-1, LVNI_ALL | LVNI_SELECTED);
		CString path;
		if (pc && idx >= 0 && idx < playcnt)
			path = PlPhysicalMediaPath(pc[idx].fol);
		const int sep = path.ReverseFind(_T('\\'));
		if (!path.IsEmpty() && sep > 0 && PathFileExists(path)) {
			if (cmd == PL_CTX_OPEN_FOLDER) {
				CString arg;
				arg.Format(_T("/select,\"%s\""), (LPCTSTR)path);
				ShellExecute(NULL, _T("open"), _T("explorer.exe"), arg, NULL, SW_SHOWNORMAL);
			}
			else {
				// Fol() は _tchdir するので OnDropFiles と同じくカレントを退避・復元する
				TCHAR cwd[1024];
				_tgetcwd(cwd, 1000);
				syo = 0; syos = _T(""); syomode = 0;
				m_lc.SetRedraw(FALSE);
				Fol(path.Left(sep));
				m_lc.SetRedraw(TRUE);
				m_lc.Invalidate();
				m_lc.UpdateWindow();
				_tchdir(cwd);
				Save();
			}
		}
	}
	else if (cmd >= PL_CTX_MOVE_BASE && cmd <= PL_CTX_MOVE_MAX)
		TransferSelectedToPlaylist(cmd - PL_CTX_MOVE_BASE, true);
	else if (cmd >= PL_CTX_COPY_BASE && cmd <= PL_CTX_COPY_MAX)
		TransferSelectedToPlaylist(cmd - PL_CTX_COPY_BASE, false);

	extern CMediaPlayerDlg* mp;
	if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->RefreshList(TRUE);
}

void CPlayList::TransferSelectedToPlaylist(int targetIdx, bool moveNotCopy)
{
	if (targetIdx < 0 || targetIdx >= GetPlaylistFileCount()) return;

	std::vector<int> sel;
	int idx = -1;
	while ((idx = m_lc.GetNextItem(idx, LVNI_ALL | LVNI_SELECTED)) >= 0) {
		if (idx < playcnt) sel.push_back(idx);
	}
	if (sel.empty()) return;

	std::vector<playlistdata0> toXfer;
	toXfer.reserve(sel.size());
	for (int i : sel)
		toXfer.push_back(pc[i]);

	std::vector<playlistdata0> targetTracks;
	if (!PlReadPlaylistTracks(targetIdx, targetTracks)) {
		targetTracks.clear();
	}
	for (const auto& t : toXfer)
		targetTracks.push_back(t);

	if (!PlWritePlaylistTracks(targetIdx, targetTracks)) {
		MessageBox(LL14(L"プレイリストへの書き込みに失敗しました。", L"Failed to write playlist.",
			L"Echec ecriture liste.", L"Scrittura playlist fallita.", L"Error al escribir lista.",
			L"플레이리스트 쓰기 실패.", L"写入播放列表失败。", L"فشل كتابة القائمة.", L"Ошибка записи плейлиста.",
			L"Schreiben fehlgeschlagen.", L"Falha ao gravar playlist.", L"Schrijven mislukt.",
			L"Zapis playlisty nieudany.", L"Listeye yazma basarisiz."),
			LL14(L"ogg簡易プレイヤ", L"ogg Simple Player", L"ogg Lecteur simple", L"ogg Lettore semplice",
				L"ogg Reproductor simple", L"ogg 간이 플레이어", L"ogg简易播放器", L"ogg مشغل بسيط",
				L"ogg Простой плеер", L"ogg Einfacher Player", L"ogg Player simples", L"ogg Eenvoudige speler",
				L"ogg Prosty odtwarzacz", L"ogg Basit oynatıcı"), MB_ICONWARNING);
		return;
	}

	// AudioData.dat の listName キーも移動/コピーに追従
	{
		CString srcList = SongParams_CurrentListName();
		CString dstList = savedata.playlistname[targetIdx];
		if (dstList.IsEmpty()) dstList.Format(L"#%d", targetIdx);
		SongParams_RebindEntries(srcList, dstList, toXfer.data(), (int)toXfer.size(), !moveNotCopy);
	}

	if (moveNotCopy) {
		std::sort(sel.begin(), sel.end(), std::greater<int>());
		for (int i : sel) {
			if (i < 0 || i >= playcnt) continue;
			for (int j = i + 1; j < playcnt; ++j)
				memcpy(&pc[j - 1], &pc[j], sizeof(playlistdata0));
			playcnt--;
		}
		playlistdata0* newPc = (playlistdata0*)realloc(pc, (size_t)sizeof(playlistdata0) * (playcnt + 2));
		if (newPc) pc = newPc;
		extern int plcnt;
		std::vector<int> selAsc = sel;
		std::sort(selAsc.begin(), selAsc.end());
		plcnt = PlAdjustIndexAfterRemovals(plcnt, selAsc);
		pnt = PlAdjustIndexAfterRemovals(pnt, selAsc);
		pnt1 = PlAdjustIndexAfterRemovals(pnt1, selAsc);
		m_lc.SetItemCount(playcnt);
		for (int j = 0; j < playcnt; j++) pc[j].icon = 1;
		m_lc.RedrawWindow();
		Save();
	}
}

BOOL CPlayList::UpdateTrackPath(int index, LPCTSTR newPath)
{
	if (!pc || index < 0 || index >= playcnt)
		return FALSE;
	CString stored = PlStorePlaylistFol(newPath, pc[index].sub);
	if (stored.IsEmpty() && newPath && *newPath)
		stored = newPath;
	_tcsncpy_s(pc[index].fol, _countof(pc[index].fol), stored, _TRUNCATE);
	if (::IsWindow(m_lc.GetSafeHwnd())) {
		m_lc.RedrawItems(index, index);
		m_lc.UpdateWindow();
	}
	const BOOL exists = (pc[index].fol[0] != 0) &&
		PathFileExists(PlPhysicalMediaPath(pc[index].fol));
	Save();
	extern CMediaPlayerDlg* mp;
	if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->RefreshList(TRUE);
	return exists;
}

void CPlayList::DeleteTracksByIndices(std::vector<int> indices)
{
	if (!pc || playcnt <= 0 || indices.empty())
		return;
	std::sort(indices.begin(), indices.end());
	indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
	std::vector<int> desc = indices;
	std::sort(desc.begin(), desc.end(), std::greater<int>());
	for (int i : desc) {
		if (i < 0 || i >= playcnt) continue;
		for (int j = i + 1; j < playcnt; ++j)
			memcpy(&pc[j - 1], &pc[j], sizeof(playlistdata0));
		playcnt--;
	}
	playlistdata0* newPc = (playlistdata0*)realloc(pc, (size_t)sizeof(playlistdata0) * (playcnt + 2));
	if (newPc) pc = newPc;
	extern int plcnt;
	plcnt = PlAdjustIndexAfterRemovals(plcnt, indices);
	pnt = PlAdjustIndexAfterRemovals(pnt, indices);
	pnt1 = PlAdjustIndexAfterRemovals(pnt1, indices);
	m_lc.SetItemCount(playcnt);
	for (int j = 0; j < playcnt; j++) pc[j].icon = 1;
	m_lc.RedrawWindow();
	Save();
	extern CMediaPlayerDlg* mp;
	if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->RefreshList(TRUE);
}

void CPlayList::RemoveMissingFiles()
{
	if (!pc || playcnt <= 0) return;
	std::vector<int> missing;
	for (int i = 0; i < playcnt; ++i) {
		if (PlTrackLooksMissing(pc[i].sub, pc[i].fol))
			missing.push_back(i);
	}
	if (missing.empty()) {
		MessageBox(LL14(L"存在しないファイルはありません。", L"No missing files found.",
			L"Aucun fichier manquant.", L"Nessun file mancante.", L"No hay archivos inexistentes.",
			L"없는 파일이 없습니다.", L"没有不存在的文件。", L"لا توجد ملفات مفقودة.", L"Отсутствующих файлов нет.",
			L"Keine fehlenden Dateien.", L"Nenhum arquivo ausente.", L"Geen ontbrekende bestanden.",
			L"Brak brakujących plików.", L"Eksik dosya yok."),
			LL14(L"ogg簡易プレイヤ", L"ogg Simple Player", L"ogg Lecteur simple", L"ogg Lettore semplice",
				L"ogg Reproductor simple", L"ogg 간이 플레이어", L"ogg简易播放器", L"ogg مشغل بسيط",
				L"ogg Простой плеер", L"ogg Einfacher Player", L"ogg Player simples", L"ogg Eenvoudige speler",
				L"ogg Prosty odtwarzacz", L"ogg Basit oynatıcı"), MB_ICONINFORMATION);
		return;
	}

	CMissingFilesDlg dlg(this, missing, this);
	if (dlg.DoModal() != IDOK)
		return;
	if (!dlg.m_toDelete.empty())
		DeleteTracksByIndices(dlg.m_toDelete);
}

CString PlStorePlaylistFol(LPCTSTR fol, int sub)
{
	if (!fol || !*fol)
		return CString();
	// mode 30 (空の軌跡 The 1st) は savedata のゲームフォルダ chdir が無く、
	// wavread が .pac をフルパスで Open する。basename 化すると Open 失敗→Seek で落ちる。
	if (PlIsFalcomGameBgmMode(sub) && sub != 30) {
		CString s = fol;
		int slash = s.ReverseFind(_T('\\'));
		if (slash >= 0)
			s = s.Mid(slash + 1);
		slash = s.ReverseFind(_T('/'));
		if (slash >= 0)
			s = s.Mid(slash + 1);
		return s;
	}
	if (sub == 30) {
		CString in(fol);
		const int cor = in.Find(L':', 6);
		const CString phys = (cor != -1) ? in.Left(cor) : in;
		const CString suffix = (cor != -1) ? in.Mid(cor) : CString();
		// 絶対パスだけ正規化。旧 basename 行を cwd 付き偽フルパスにしない。
		const bool absPath = (phys.GetLength() >= 3 &&
			((phys[1] == _T(':') && (phys[2] == _T('\\') || phys[2] == _T('/'))) ||
				phys.Left(2) == _T("\\\\")));
		if (absPath) {
			const CString nf = NormalizePlaylistPath(phys);
			if (!nf.IsEmpty())
				return nf + suffix;
		}
		return in;
	}
	CString nf = NormalizePlaylistPath(fol);
	return nf.IsEmpty() ? CString(fol) : nf;
}

int CPlayList::chk(CString name,int sub,CString art,CString fol,int ret)
{
	if(!pc || playcnt<=0) return -1;
	int i=m_lc.GetItemCount(),c=0;
	pnt1=-1;
	CString s,s1;
	// 単体メディアファイルはパス+形式(sub)で同一判定(タグ名とプレイリスト表示名の差異を吸収)
	const bool pathKeyOnly = (sub == -1 || sub == -6 || sub == 33 || sub == 34 || sub == 35 ||
		sub == -7 || sub == -8 || sub == -9 ||
		sub == -10 || sub == 999 || sub == -2 || sub == -3);
	for(int j=0;j<i;j++){
		if (pathKeyOnly) {
			if (_tcsicmp(pc[j].fol, fol) == 0 && pc[j].sub == sub)
				return j;
			continue;
		}
		c=0;
		if ((pc[j].sub == -10) || (pc[j].sub == -2) || (pc[j].sub == -3 || pc[j].sub == 30) || (pc[j].sub == 999)) {
			if (_tcscmp(pc[j].fol, fol) == 0 && pc[j].sub == sub && _tcscmp(pc[j].name, name) == 0)
				return j;
		}else{
			if(_tcscmp(pc[j].fol,fol)==0 && pc[j].sub==sub && (pc[j].ret2==ret))
				return j;
		}
	}
	return -1;
}

CString CPlayList::UTF8toSJIS(const char* a)
{
	WCHAR f[1024];
	char ff[1024];
	int rr=MultiByteToWideChar(CP_UTF8,0,a,-1,f,1024);
	int rr2=WideCharToMultiByte(CP_ACP,0,f,rr,ff,0,NULL,NULL);
	WideCharToMultiByte(CP_ACP,0,f,rr,ff,rr2,NULL,NULL);
	CString s; s=f;
	return s;
//	return _T("");
}

CString CPlayList::UTF8toUNI(const TCHAR* a)
{
//	WCHAR f[1024];
//	char ff[1024];
//	int rr2=WideCharToMultiByte(CP_ACP, 0, a,1024,ff,1024,NULL,NULL);
//	int rr= MultiByteToWideChar(CP_UTF8,0,ff,-1,f ,1024);
//	WideCharToMultiByte(CP_ACP,0,f,rr,ff,rr2,NULL,NULL);
	CString s; s=a;
	return s;
//	return _T("");
}

// CDouga (DirectShow) で再生する動画形式。
// KPI が同じ拡張子を表明していても、動画は KPI に渡さない。
static bool IsDougaVideoFile(const CString& path)
{
	const int dot = path.ReverseFind(_T('.'));
	if (dot < 0)
		return false;

	CString fileExt = path.Mid(dot);
	fileExt.MakeLower();

	static const LPCTSTR videoExts[] = {
		_T(".avi"), _T(".mp4"), _T(".m4v"), _T(".mkv"),
		_T(".wmv"), _T(".asf"), _T(".mov"), _T(".qt"),
		_T(".mpg"), _T(".mpeg"), _T(".mpe"), _T(".m1v"),
		_T(".m2v"), _T(".mpv"), _T(".vob"), _T(".ts"),
		_T(".m2ts"), _T(".mts"), _T(".webm"), _T(".ogv"),
		_T(".flv"), _T(".f4v"), _T(".3gp"), _T(".3g2"),
		_T(".divx"), _T(".rm"), _T(".rmvb")
	};

	for (int i = 0; i < _countof(videoExts); ++i) {
		if (fileExt == videoExts[i])
			return true;
	}
	return false;
}

int CPlayList::Add(CString name,int sub,int loop1,int loop2,CString art,CString alb,CString fol,int ret,int time,BOOL f,BOOL ff)
{
	// 旧プレイリストに KPI 再生として保存された動画も CDouga 再生へ移行する。
	if (sub == -3 && IsDougaVideoFile(fol))
		sub = -2;

	int cnt1;
	CString s,ss;
	switch(sub){
		case 1:s=LL14(L"空の軌跡SC", L"Trails in the Sky SC", L"Les Sentiers du Ciel SC", L"Trails in the Sky SC", L"Trails in the Sky SC", L"하늘의 궤적 SC", L"空之轨迹SC", L"Trails in the Sky SC", L"Тропы в Небе SC", L"Himmelsleitern SC", L"Trails in the Sky SC", L"Trails in the Sky SC", L"Trails in the Sky SC", L"Trails in the Sky SC");break;
		case 2:s=LL14(L"空の軌跡FC", L"Trails in the Sky FC", L"Les Sentiers du Ciel FC", L"Trails in the Sky FC", L"Trails in the Sky FC", L"하늘의 궤적 FC", L"空之轨迹FC", L"Trails in the Sky FC", L"Тропы в Небе FC", L"Himmelsleitern FC", L"Trails in the Sky FC", L"Trails in the Sky FC", L"Trails in the Sky FC", L"Trails in the Sky FC");break;
		case 3:s=LL14(L"YS フェルガナの誓い", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys 菲尔盖纳之誓", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai");break;
		case 4:s=LL14(L"YS6 ナピシュテムの匣", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 纳比斯汀的方舟", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako");break;
		case 5:s=LL14(L"イース・オリジン", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"이스 오리진", L"伊苏起源", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin");break;
		case 6:s=LL14(L"空の軌跡 The3rd", L"Trails in the Sky The 3rd", L"Les Sentiers du Ciel The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"하늘의 궤적 The 3rd", L"空之轨迹 The3rd", L"Trails in the Sky The 3rd", L"Тропы в Небе The 3rd", L"Himmelsleitern The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd");break;
		case 7:s="ZWEI II";break;
		case 8:s="Ys I&II Chronicles 1";break;
		case 9:s="Ys I&II Chronicles 2";break;
		case 10:s="XANADU NEXT";break;
		case 11:s=LL14(L"Ys I&II 完全版 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II 完全版 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1");break;
		case 12:s=LL14(L"Ys I&II 完全版 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II 完全版 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2");break;
		case 13:s="Sorcerian Original";break;
		case 14:s="Zwei!!";break;
		case 15:s=LL14(L"ぐるみん -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"구루민 -GURUMIN-", L"咕噜小天使 -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-");break;
		case 16:s=LL14(L"ダイナソア リザレクション", L"Dinosaur Resurrection", L"Dinosaur Résurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"恐龙复活", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Auferstehung", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection");break;
		case 17:s=LL14(L"Brandish4 眠れる神の塔", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tour de Dieu endormi", L"Brandish4 Torre del Dio addormentato", L"Brandish4 Torre del Dios dormido", L"Brandish4 잠든 신의 탑", L"Brandish4 沉睡之神塔", L"Brandish4 برج الإله النائم", L"Brandish4 Башня спящего бога", L"Brandish4 Turm des schlafenden Gottes", L"Brandish4 Torre do Deus adormecido", L"Brandish4 Toren van de slapende god", L"Brandish4 Wieża śpiącego boga", L"Brandish4 Uyuyan Tanrı'nın Kulesi");break;
		case 18:s=LL14(L"白き魔女", L"White Witch", L"Sorcière blanche", L"Strega bianca", L"Bruja blanca", L"하얀 마녀", L"白之魔女", L"الساحرة البيضاء", L"Белая ведьма", L"Weiße Hexe", L"Bruxa branca", L"Witte heks", L"Biała czarownica", L"Beyaz cadı");break;
		case 19:s=LL14(L"朱紅い雫", L"Crimson Tears", L"Larmes cramoisies", L"Lacrime cremisi", L"Lágrimas carmesí", L"주홍빛 눈물", L"朱红之泪", L"دموع قرمزية", L"Багровые слёзы", L"Karmesinrote Tränen", L"Lágrimas carmesim", L"Karmozijnrode tranen", L"Karmazynowe łzy", L"Kızıl Gözyaşları");break;
		case 20:s=LL14(L"海の檻歌", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"바다의 감옥 노래", L"海之槛歌", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean");break;
		case 21:s = LL14(L"閃の軌跡Ⅰ,Ⅱ,Ys8", L"Trails of Cold Steel I,II,Ys8", L"Trails of Cold Steel I,II,Ys8", L"Trails of Cold Steel I,II,Ys8", L"Trails of Cold Steel I,II,Ys8", L"섬의 궤적 I,II,Ys8", L"闪之轨迹I,II,Ys8", L"Trails of Cold Steel I,II,Ys8", L"Trails of Cold Steel I,II,Ys8", L"Trails of Cold Steel I,II,Ys8", L"Trails of Cold Steel I,II,Ys8", L"Trails of Cold Steel I,II,Ys8", L"Trails of Cold Steel I,II,Ys8", L"Trails of Cold Steel I,II,Ys8"); break;
		case 30:s = LL14(L"空の軌跡 The 1st", L"Trails in the Sky The 1st", L"Les Sentiers du Ciel The 1st", L"Trails in the Sky The 1st", L"Trails in the Sky The 1st", L"하늘의 궤적 The 1st", L"空之轨迹 The 1st", L"Trails in the Sky The 1st", L"Тропы в Небе The 1st", L"Himmelsleitern The 1st", L"Trails in the Sky The 1st", L"Trails in the Sky The 1st", L"Trails in the Sky The 1st", L"Trails in the Sky The 1st"); break;
		// mode 31-32: 予約 / 33-35: Daybreak / Daybreak II / beyond the Horizon (opus loops=)
		case 31:s = LL14(L"空の軌跡 The 2nd", L"Trails in the Sky The 2nd", L"Les Sentiers du Ciel The 2nd", L"Trails in the Sky The 2nd", L"Trails in the Sky The 2nd", L"하늘의 궤적 The 2nd", L"空之轨迹 The 2nd", L"Trails in the Sky The 2nd", L"Тропы в Небе The 2nd", L"Himmelsleitern The 2nd", L"Trails in the Sky The 2nd", L"Trails in the Sky The 2nd", L"Trails in the Sky The 2nd", L"Trails in the Sky The 2nd"); break;
		case 32:s = LL14(L"空の軌跡 The 3rd", L"Trails in the Sky The 3rd", L"Les Sentiers du Ciel The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"하늘의 궤적 The 3rd", L"空之轨迹 The 3rd", L"Trails in the Sky The 3rd", L"Тропы в Небе The 3rd", L"Himmelsleitern The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd"); break;
		case 33:s = LL14(L"英雄伝説 黎の軌跡", L"The Legend of Heroes: Trails through Daybreak", L"The Legend of Heroes: Trails through Daybreak", L"The Legend of Heroes: Trails through Daybreak", L"The Legend of Heroes: Trails through Daybreak", L"영웅전설 여의 궤적", L"英雄传说 黎之轨迹", L"The Legend of Heroes: Trails through Daybreak", L"The Legend of Heroes: Trails through Daybreak", L"The Legend of Heroes: Trails through Daybreak", L"The Legend of Heroes: Trails through Daybreak", L"The Legend of Heroes: Trails through Daybreak", L"The Legend of Heroes: Trails through Daybreak", L"The Legend of Heroes: Trails through Daybreak"); break;
		case 34:s = LL14(L"英雄伝説 黎の軌跡Ⅱ", L"The Legend of Heroes: Trails through Daybreak II", L"The Legend of Heroes: Trails through Daybreak II", L"The Legend of Heroes: Trails through Daybreak II", L"The Legend of Heroes: Trails through Daybreak II", L"영웅전설 여의 궤적 II", L"英雄传说 黎之轨迹Ⅱ", L"The Legend of Heroes: Trails through Daybreak II", L"The Legend of Heroes: Trails through Daybreak II", L"The Legend of Heroes: Trails through Daybreak II", L"The Legend of Heroes: Trails through Daybreak II", L"The Legend of Heroes: Trails through Daybreak II", L"The Legend of Heroes: Trails through Daybreak II", L"The Legend of Heroes: Trails through Daybreak II"); break;
		case 35:s = LL14(L"英雄伝説 界の軌跡", L"The Legend of Heroes: Trails beyond the Horizon", L"The Legend of Heroes: Trails beyond the Horizon", L"The Legend of Heroes: Trails beyond the Horizon", L"The Legend of Heroes: Trails beyond the Horizon", L"영웅전설 계의 궤적", L"英雄传说 界之轨迹", L"The Legend of Heroes: Trails beyond the Horizon", L"The Legend of Heroes: Trails beyond the Horizon", L"The Legend of Heroes: Trails beyond the Horizon", L"The Legend of Heroes: Trails beyond the Horizon", L"The Legend of Heroes: Trails beyond the Horizon", L"The Legend of Heroes: Trails beyond the Horizon", L"The Legend of Heroes: Trails beyond the Horizon"); break;
		case -6:s = LL14(L"閃Ⅲ,Ⅳ,創,零改,Ys9,YsX", L"CS III,IV,Reverie,Zero Kai,Ys9,YsX", L"CS III,IV,Reverie,Zero Kai,Ys9,YsX", L"CS III,IV,Reverie,Zero Kai,Ys9,YsX", L"CS III,IV,Reverie,Zero Kai,Ys9,YsX", L"섬 III,IV,창,영改,Ys9,YsX", L"闪III,IV,创,零改,Ys9,YsX", L"CS III,IV,Reverie,Zero Kai,Ys9,YsX", L"CS III,IV,Reverie,Zero Kai,Ys9,YsX", L"CS III,IV,Reverie,Zero Kai,Ys9,YsX", L"CS III,IV,Reverie,Zero Kai,Ys9,YsX", L"CS III,IV,Reverie,Zero Kai,Ys9,YsX", L"CS III,IV,Reverie,Zero Kai,Ys9,YsX", L"CS III,IV,Reverie,Zero Kai,Ys9,YsX"); break;
		case -11:s=LL14(L"月影のラプソディー", L"Lunacy of the Moon", L"Rhapsodie de l'ombre lunaire", L"Rapsodia dell'ombra lunare", L"Rapsodia de la sombra lunar", L"월영의 랩소디", L"月影狂想曲", L"رابسودي ظل القمر", L"Рапсодия лунной тени", L"Rhapsodie des Mondschatten", L"Rapsódia da sombra lunar", L"Rapsodie van de maanschaduw", L"Rapsodia księżycowego cienia", L"Ay gölgesinin rapsodisi");break;
		case -12:s=LL14(L"西風の狂詩曲", L"Rhapsody of the West Wind", L"Rhapsodie du vent d'ouest", L"Rapsodia del vento d'ovest", L"Rapsodia del viento del oeste", L"서풍의 랩소디", L"西风狂想曲", L"نشيد الرياح الغربية", L"Рапсодия западного ветра", L"Rhapsodie des Westwinds", L"Rapsódia do vento oeste", L"Rapsodie van de westenwind", L"Rapsodia zachodniego wiatru", L"Batı rüzgarının rapsodisi");break;
		case -13:s=LL14(L"アークトゥルス", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"아크투루스", L"阿尔克图斯", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus");break;
		case -14:s=LL14(L"幻想三国志1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"환상삼국지 1", L"幻想三国志1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1");break;
		case -15:s=LL14(L"幻想三国志2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"환상삼국지 2", L"幻想三国志2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2");break;
		case -3:
			ss = fol.Right(fol.GetLength() - fol.ReverseFind('.') - 1);
			s.Format(LL14(L"%sファイル", L"%s File", L"%s fichier", L"%s file", L"%s archivo", L"%s 파일", L"%s文件", L"ملف %s", L"файл %s", L"%s-Datei", L"arquivo %s", L"%s bestand", L"plik %s", L"%s dosyası"), ss);
			break;

		case -2:
			ss = fol.Right(fol.GetLength() - fol.ReverseFind('.') - 1);
			s.Format(LL14(L"%sファイル", L"%s File", L"%s fichier", L"%s file", L"%s archivo", L"%s 파일", L"%s文件", L"ملف %s", L"файл %s", L"%s-Datei", L"arquivo %s", L"%s bestand", L"plik %s", L"%s dosyası"), ss);
			break;

		case -1:
			s = LL14(L"oggファイル", L"ogg File", L"ogg fichier", L"ogg file", L"ogg archivo", L"ogg 파일", L"ogg文件", L"ملف ogg", L"ogg файл", L"ogg Datei", L"ogg arquivo", L"ogg bestand", L"ogg plik", L"ogg dosyası");
			break;

		case -7:
			s = fol; s.MakeLower();
			if (s.Right(3) == "dsf") { s = LL14(L"dsfファイル(DSD)", L"dsf File(DSD)", L"dsf fichier(DSD)", L"dsf file(DSD)", L"dsf archivo(DSD)", L"dsf 파일(DSD)", L"dsf文件(DSD)", L"ملف dsf(DSD)", L"dsf файл(DSD)", L"dsf Datei(DSD)", L"dsf arquivo(DSD)", L"dsf bestand(DSD)", L"dsf plik(DSD)", L"dsf dosyası(DSD)"); break; }
			if (s.Right(3) == "wsd") { s = LL14(L"wsdファイル(DSD)", L"wsd File(DSD)", L"wsd fichier(DSD)", L"wsd file(DSD)", L"wsd archivo(DSD)", L"wsd 파일(DSD)", L"wsd文件(DSD)", L"ملف wsd(DSD)", L"wsd файл(DSD)", L"wsd Datei(DSD)", L"wsd arquivo(DSD)", L"wsd bestand(DSD)", L"wsd plik(DSD)", L"wsd dosyası(DSD)"); break; }
			if (s.Right(3) == "dff") { s = LL14(L"dffファイル(DSD)", L"dff File(DSD)", L"dff fichier(DSD)", L"dff file(DSD)", L"dff archivo(DSD)", L"dff 파일(DSD)", L"dff文件(DSD)", L"ملف dff(DSD)", L"dff файл(DSD)", L"dff Datei(DSD)", L"dff arquivo(DSD)", L"dff bestand(DSD)", L"dff plik(DSD)", L"dff dosyası(DSD)"); break; }
			break;

		case -8:
			s = fol; s.MakeLower();
			if (s.Right(4) == "flac") { s = LL14(L"flacファイル", L"flac File", L"flac fichier", L"flac file", L"flac archivo", L"flac 파일", L"flac文件", L"ملف flac", L"flac файл", L"flac Datei", L"flac arquivo", L"flac bestand", L"flac plik", L"flac dosyası"); break; }
			if (s.Right(6) == "qull3h") { s = LL14(L"Qull3Hファイル", L"Qull3H File", L"Qull3H fichier", L"Qull3H file", L"Qull3H archivo", L"Qull3H 파일", L"Qull3H文件", L"ملف Qull3H", L"Qull3H файл", L"Qull3H Datei", L"Qull3H arquivo", L"Qull3H bestand", L"Qull3H plik", L"Qull3H dosyası"); break; }
			break;

		case -9:
			s = fol; s.MakeLower();
			if (s.Right(3) == "m4a") { s = LL14(L"m4aファイル", L"m4a File", L"m4a fichier", L"m4a file", L"m4a archivo", L"m4a 파일", L"m4a文件", L"ملف m4a", L"m4a файл", L"m4a Datei", L"m4a arquivo", L"m4a bestand", L"m4a plik", L"m4a dosyası"); break; }
			if (s.Right(3) == "aac") { s = LL14(L"aacファイル", L"aac File", L"aac fichier", L"aac file", L"aac archivo", L"aac 파일", L"aac文件", L"ملف aac", L"aac файл", L"aac Datei", L"aac arquivo", L"aac bestand", L"aac plik", L"aac dosyası"); break; }
			break;

		case 999:
			s = LL14(L"wavファイル", L"wav File", L"wav fichier", L"wav file", L"wav archivo", L"wav 파일", L"wav文件", L"ملف wav", L"wav файл", L"wav Datei", L"wav arquivo", L"wav bestand", L"wav plik", L"wav dosyası");
			break;

		case -10:
			s = fol; s.MakeLower();
			if (s.Right(3) == "mp3") { s = LL14(L"mp3ファイル", L"mp3 File", L"mp3 fichier", L"mp3 file", L"mp3 archivo", L"mp3 파일", L"mp3文件", L"ملف mp3", L"mp3 файл", L"mp3 Datei", L"mp3 arquivo", L"mp3 bestand", L"mp3 plik", L"mp3 dosyası"); break; }
			if (s.Right(3) == "mp2") { s = LL14(L"mp2ファイル", L"mp2 File", L"mp2 fichier", L"mp2 file", L"mp2 archivo", L"mp2 파일", L"mp2文件", L"ملف mp2", L"mp2 файл", L"mp2 Datei", L"mp2 arquivo", L"mp2 bestand", L"mp2 plik", L"mp2 dosyası"); break; }
			if (s.Right(3) == "mp1") { s = LL14(L"mp1ファイル", L"mp1 File", L"mp1 fichier", L"mp1 file", L"mp1 archivo", L"mp1 파일", L"mp1文件", L"ملف mp1", L"mp1 файл", L"mp1 Datei", L"mp1 arquivo", L"mp1 bestand", L"mp1 plik", L"mp1 dosyası"); break; }
			if (s.Right(3) == "rmp") { s = LL14(L"rmpファイル", L"rmp File", L"rmp fichier", L"rmp file", L"rmp archivo", L"rmp 파일", L"rmp文件", L"ملف rmp", L"rmp файл", L"rmp Datei", L"rmp arquivo", L"rmp bestand", L"rmp plik", L"rmp dosyası"); break; }
			break;
	}

	const CString folNorm = PlStorePlaylistFol(fol, sub);

	if(f) {
		if (!PlIsFalcomGameBgmMode(sub)) {
			int byPath = FindByPath(folNorm);
			if (byPath >= 0) {
				pc[byPath].loop1 = loop1;
				pc[byPath].loop2 = loop2;
				pc[byPath].ret2 = ret;
				if (time != 0)
					pc[byPath].time = time;
				return byPath;
			}
		}
		if((cnt1=chk(name,sub,art,folNorm,ret))!=-1){
			pc[cnt1].loop1=loop1;
			pc[cnt1].loop2=loop2;
			pc[cnt1].ret2=ret;
			if (time != 0)
				pc[cnt1].time=time;
			RECT r;
			m_lc.GetItemRect(cnt1,&r,LVIR_BOUNDS);
			m_lc.RedrawWindow(&r);	
			return cnt1;
		}
	}
//	if(playcnt<60000){
		if(ff){
			if(pc==NULL){
				pc=(playlistdata0*)malloc(sizeof(playlistdata0));
				if(!pc) return -1;
			}
			playlistdata0 *tmp;	tmp=pc;
		size_t size=_msize(pc);
		playlistdata0 *newPc = (playlistdata0*)realloc(tmp, size + sizeof(playlistdata0));
		if (newPc == NULL) {
			pc = tmp;
			return -1;
		}
		pc = newPc;
			m_lc.SetItemCount(playcnt+1);
		}
		_tcscpy(pc[playcnt].name,name);
		_tcscpy(pc[playcnt].art,art);
		_tcscpy(pc[playcnt].alb,alb);
		_tcscpy(pc[playcnt].fol, folNorm);
		_tcscpy(pc[playcnt].game,s);
		pc[playcnt].loop1=loop1;
		pc[playcnt].loop2=loop2;
		pc[playcnt].sub=sub;
		pc[playcnt].ret2=ret;
		pc[playcnt].icon=1;
		pc[playcnt].time=time;
//		RECT r;
//		m_lc.GetItemRect(playcnt,&r,LVIR_BOUNDS);
//		m_lc.RedrawWindow(&r);	
		playcnt++;
//	}		
		
	return -1;
}

void CPlayList::Del()
{
	if (!pc || playcnt <= 0) return;
	std::vector<int> sel;
	int Lindex = -1;
	while ((Lindex = m_lc.GetNextItem(Lindex, LVNI_ALL | LVNI_SELECTED)) >= 0) {
		if (Lindex < playcnt) sel.push_back(Lindex);
	}
	if (sel.empty()) return;
	DelByIndices(sel);
}

void CPlayList::DelByIndices(const std::vector<int>& indices)
{
	if (!pc || playcnt <= 0 || indices.empty()) return;
	std::vector<int> sel = indices;
	std::sort(sel.begin(), sel.end());
	sel.erase(std::unique(sel.begin(), sel.end()), sel.end());
	std::sort(sel.begin(), sel.end(), std::greater<int>());
	for (int i : sel) {
		if (i < 0 || i >= playcnt) continue;
		for (int j = i + 1; j < playcnt; ++j)
			memcpy(&pc[j - 1], &pc[j], sizeof(playlistdata0));
		playcnt--;
	}
	playlistdata0* newPc = (playlistdata0*)realloc(pc, (size_t)sizeof(playlistdata0) * (playcnt + 2));
	if (newPc) pc = newPc;
	extern int plcnt;
	std::vector<int> selAsc = indices;
	std::sort(selAsc.begin(), selAsc.end());
	selAsc.erase(std::unique(selAsc.begin(), selAsc.end()), selAsc.end());
	plcnt = PlAdjustIndexAfterRemovals(plcnt, selAsc);
	pnt = PlAdjustIndexAfterRemovals(pnt, selAsc);
	pnt1 = PlAdjustIndexAfterRemovals(pnt1, selAsc);
	extern CMediaPlayerDlg* mp;
	if (mp) {
		int w = 0;
		for (int i = 0; i < mp->m_queueN; ++i) {
			const int adj = PlAdjustIndexAfterRemovals(mp->m_queue[i], selAsc);
			if (adj >= 0)
				mp->m_queue[w++] = adj;
		}
		mp->m_queueN = w;
	}
	if (::IsWindow(m_lc.GetSafeHwnd())) {
		m_lc.SetItemCount(playcnt);
		for (int j = 0; j < playcnt; j++) pc[j].icon = 1;
		m_lc.RedrawWindow();
	}
	Save();
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->RefreshList(TRUE);
}

void CPlayList::OnSUP()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount();
	CString s,s1;
	for(int j=0;j<i-1;j++){
		if((m_lc.GetItemState(j+1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j+1].icon=1;
			memcpy(&ppp,&pc[j+1],sizeof(playlistdata0));
			memcpy(&pc[j+1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j,j+1);
			m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
			m_lc.SetItemState(j+1,m_lc.GetItemState(j+1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
	}
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnUP()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount(),i2=0;
	CString s,s1;
	for(;;){i2=0;
		for(int j=0;j<i-1;j++){
			if((m_lc.GetItemState(j+1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j+1].icon=1;
			memcpy(&ppp,&pc[j+1],sizeof(playlistdata0));
			memcpy(&pc[j+1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j,j+1);
				m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
				m_lc.SetItemState(j+1,m_lc.GetItemState(j+1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
				i2=1;
			}
		}
		if(i2==0) break;
	}	
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnSDOWN()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount(),i2=0;
	CString s,s1;
	for(;;){i2=0;
		for(int j=i-1;j>0;j--){
			if((m_lc.GetItemState(j-1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j-1].icon=1;   // 旧 pc[j+1] は j=playcnt-1 のとき pc[playcnt] へのヒープ範囲外書込みで破壊の原因
			memcpy(&ppp,&pc[j-1],sizeof(playlistdata0));
			memcpy(&pc[j-1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j-1,j);
				m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
				m_lc.SetItemState(j-1,m_lc.GetItemState(j-1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
				i2=1;
			}
		}
		if(i2==0) break;
	}	
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnDOWN()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount();
	CString s,s1;
	for(int j=i-1;j>0;j--){
		if((m_lc.GetItemState(j-1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j-1].icon=1;
			memcpy(&ppp,&pc[j-1],sizeof(playlistdata0));
			memcpy(&pc[j-1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j-1,j);

			m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
			m_lc.SetItemState(j-1,m_lc.GetItemState(j-1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
	}
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnXCHG(int i,int j)
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
			playlistdata0 ppp;
			pc[j].icon=pc[j-1].icon=1;
			memcpy(&ppp,&pc[i],sizeof(playlistdata0));
			memcpy(&pc[i],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
}

extern COggDlg *og;
extern CString filen,fnn;

extern int modesub,ret2;
extern int loop1, loop2;

void CPlayList::Get(int i)
{
		pnt1 = -1;  // 再生曲選択時はあいまい検索アンカーを解除
		fnn=pc[i].name; filen=pc[i].fol; modesub=pc[i].sub; mode=modesub;
		loop1=pc[i].loop1; loop2=pc[i].loop2; ret2=pc[i].ret2;
		ApplyPlaylistRowDisplay(pc[i]);
		for(int k=0;k<playcnt;k++){
			m_lc.SetItemState(k,m_lc.GetItemState(k,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
		m_lc.SetItemState(i,LVIS_SELECTED,LVIS_SELECTED);
		SIcon(i);
}

void CPlayList::RestoreSavedPlaybackRow()
{
	extern int plcnt;
	if (pnt < 0 || pnt >= playcnt || !pc)
		return;
	plcnt = pnt;
	fnn = pc[pnt].name;
	filen = pc[pnt].fol;
	modesub = pc[pnt].sub;
	mode = modesub;
	loop1 = pc[pnt].loop1;
	loop2 = pc[pnt].loop2;
	ret2 = pc[pnt].ret2;
	ApplyPlaylistRowDisplay(pc[pnt]);
	for (int k = 0; k < playcnt; k++)
		m_lc.SetItemState(k, m_lc.GetItemState(k, LVIS_SELECTED) & ~LVIS_SELECTED, LVIS_SELECTED);
	m_lc.SetItemState(pnt, LVIS_SELECTED, LVIS_SELECTED);
	SIcon(pnt);
}

extern int gameon;
static void RequestPlaylistRestartAsync()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());
}

void CPlayList::OnLvnKeydownList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLVKEYDOWN pLVKeyDow = reinterpret_cast<LPNMLVKEYDOWN>(pNMHDR);
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	if(pLVKeyDow->wVKey == VK_DELETE){
		Del();
	}
	*pResult = 0;
}


void CPlayList::OnDropFiles(HDROP hDropInfo)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	TCHAR filen_c[1024];
	syo = 0; syos = ""; syomode = 0;
	int ii=m_lc.GetItemCount();
	UINT cnt = DragQueryFile(hDropInfo,(UINT)-1,filen_c,sizeof(filen_c));
	TCHAR tmp[1024];
	_tgetcwd(tmp,1000);
	m_lc.SetRedraw(FALSE);
		for(UINT i=0;i<cnt;i++){
			DragQueryFile(hDropInfo,(UINT)i,filen_c,sizeof(filen_c));
			Fol(filen_c);
		}
	m_lc.SetRedraw(TRUE);
	m_lc.Invalidate();
	m_lc.UpdateWindow();
	_tchdir(tmp);
	if (syo == 1) {
		BOOL requestPlay = FALSE;
		if (m_renzoku.GetCheck() == FALSE) {
			requestPlay = TRUE;
		}
		if (pMediaPosition && (mode == -2 || videoonly == TRUE)) {
			REFTIME aa, bb;
			pMediaPosition->get_CurrentPosition(&aa);
			pMediaPosition->get_Duration(&bb);
			if (aa >= bb) {
				requestPlay = TRUE;
			}
		}
		if ((fade1 == 1 || playf == 0) && !pMediaPosition) {
			requestPlay = TRUE;
		}
		if (requestPlay && ii >= 0 && ii < playcnt) {
			plcnt = ii;
			Get(plcnt);
			gameon = 0;
			RequestPlaylistRestartAsync();
		}
		else if ((fade1 == 1 || playf == 0) && !pMediaPosition && ii >= 0 && ii < playcnt) {
			plcnt = ii;
			SIcon(ii);
		}
	}
	Save();
	CCustomBlurDialogBase::OnDropFiles(hDropInfo);
}

#include "Id3tagv1.h"
#include "Id3tagv2.h"

#include "vorbis/codec.h"
#include "vorbis/vorbisfile.h"
OggVorbis_File vf1;
extern BYTE bufimage[0x30000f];

// OggVorbisコールバック関数
extern size_t Callback_Read(
	void* ptr,
	size_t size,
	size_t nmemb,
	void* datasource
);

extern int Callback_Seek(
	void *datasource,
	ogg_int64_t offset,
	int whence
);

extern int Callback_Close(void *datasource);

extern long Callback_Tell(void *datasource);

extern ov_callbacks callbacks;

void CPlayList::AddFilePath(LPCTSTR path)
{
	if (!path || !*path) return;
	const CString norm = NormalizePlaylistPath(path);
	if (norm.IsEmpty()) return;
	if (FindByPath(norm) >= 0) return;
	Fol(norm);
}

void CPlayList::Fol(CString fname)
{
	CString fname_full = fname;
	CString fname1 = fname;
	CString ft; 
	ft = "*.*";
	CString ft2;
	if (PathIsDirectory(fname) == FALSE) {
		CString ft1;
		ft1 = fname;
		ft = ft1.Right(ft1.GetLength()-ft1.ReverseFind(L'\\')-1);
	}
	CString s, ss;
	playlistdata p; ZeroMemory(&p, sizeof(p));
	CFileFind f;
	if (PathIsDirectory(fname) == FALSE) {
		CString ff = fname.Left(fname.ReverseFind('\\'));
		_tchdir(ff);
	}
	else {
		_tchdir(fname);
	}
	if (f.FindFile(ft)) {
		int b = 1;
		for (; b;) {
			b = f.FindNextFile();
			s = f.GetFileName();
			if (f.IsDirectory() == 0) {
				fname = fname1;
				BOOL a1 = PathIsDirectory(fname);
				if (a1) {
					fname = fname1 + L"\\" + s;
				}
				else {
					
				}
				//CString ff = fname.Left(fname.ReverseFind('\\'));
				//_tchdir(ff);
				ft = s;
				ft2 = s;
				ft.MakeLower();
				BOOL has_aac_syncword = FALSE;
				if (ft.Right(4) == ".aac") {
					CFile ff2;
					if (ff2.Open(s, CFile::modeRead | CFile::shareDenyWrite, NULL)) {
						ff2.Read(bufimage, 2);
						ff2.Close();
						if (bufimage[0] == 0xff && (bufimage[1] & 0xf0) == 0xf0) {
							has_aac_syncword = TRUE;
						}
					}
				}
				if (ft.Right(4) == ".ogg" || ft.Right(6) == ".qull3") {
					p.sub = -1;
					mode = -1;
					int oggL1 = 0, oggL2 = 0;
					FILE *fp;
					fp = _tfopen(fname, _T("rb"));
					if (fp == NULL) {
						return;
					}

					if (ov_open_callbacks(fp, &vf1, NULL, 0, callbacks) < 0) {
						fclose(fp);
						return;
					}
					CString cc;
					_tcscpy(p.name, ft2);
					p.alb[0] = p.art[0] = NULL;
					for (int iii = 0; iii < vf1.vc->comments; iii++) {
#if _UNICODE
						WCHAR f[1024];
						MultiByteToWideChar(CP_UTF8, 0, vf1.vc->user_comments[iii], -1, f, 1024);
						cc = f;
#else
						cc = vf1.vc->user_comments[iii];
#endif
						if (cc.Left(6).MakeUpper() == "TITLE=")
						{
#if _UNICODE
							ss = UTF8toUNI(cc.Mid(6));
#else
							ss = UTF8toSJIS(cc.Mid(6));
#endif
							_tcscpy(p.name, ss);
						}
						if (cc.Left(7).MakeUpper() == "ARTIST=")
						{
#if _UNICODE
							ss = UTF8toUNI(cc.Mid(7));
#else
							ss = UTF8toSJIS(cc.Mid(7));
#endif
							_tcscpy(p.art, ss);
						}
						if (cc.Left(6).MakeUpper() == "ALBUM=")
						{
#if _UNICODE
							ss = UTF8toUNI(cc.Mid(6));
#else
							ss = UTF8toSJIS(cc.Mid(6));
#endif
							_tcscpy(p.alb, ss);
						}
						if (cc.Left(10) == "LOOPSTART=")
							oggL1 = _tstoi(cc.Mid(10));
						if (cc.Left(11) == "LOOPLENGTH=")
							oggL2 = _tstoi(cc.Mid(11));
					}
					ov_clear(&vf1);
					fclose(fp);

					//YS8 steam版用　bgmテーブル変換
					CString sss = fname.Left(fname.ReverseFind('\\')); ss = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					_tchdir(sss);
					//ys8用
					CStdioFile f;
					char *buff;
					int looping = 0;
					int igg;

					ss = ss.Left(ss.ReverseFind('.'));
					char file[256];
					WCHAR outcm[1024];
					WideCharToMultiByte(CP_ACP, 0, ss, 1024, file, 256, NULL, NULL);
					FILE *fp2;
					fp2 = _wfopen(L"..\\text\\bgmtbl.tbl", L"r");
					if (fp2) {
						buff = (char*)calloc(256, 1);
						for (;;) {
							if (fgets(buff, 256, fp2) == NULL) {
								free(buff); break;
							}
							char *p1 = strstr(buff, file);
							if (p1 == NULL) continue;
							if (buff[0] == '/') continue;
							p1 += strlen(file) + 1;
							for (; *p1 == 0x09; p1++);
							if (*p1 == '1') looping = 1;
							p1++;
							for (; *p1 == 0x09; p1++);
							typedef struct {
								char st[8];
								char a[1];
								char ed[8];
							} aa;
							aa *aa1;
							aa1 = (aa*)p1;
							int i, j;
							j = 0;
							for (i = 0; i < 8; i++) {
								switch (aa1->st[i])
								{
								case '0':
									j *= 10; j += 0;
									break;
								case '1':
									j *= 10; j += 1;
									break;
								case '2':
									j *= 10; j += 2;
									break;
								case '3':
									j *= 10; j += 3;
									break;
								case '4':
									j *= 10; j += 4;
									break;
								case '5':
									j *= 10; j += 5;
									break;
								case '6':
									j *= 10; j += 6;
									break;
								case '7':
									j *= 10; j += 7;
									break;
								case '8':
									j *= 10; j += 8;
									break;
								case '9':
									j *= 10; j += 9;
									break;
								}
							}
							oggL1 = j;
							j = 0;
							for (i = 0; i < 8; i++) {
								switch (aa1->ed[i])
								{
								case '0':
									j *= 10; j += 0;
									break;
								case '1':
									j *= 10; j += 1;
									break;
								case '2':
									j *= 10; j += 2;
									break;
								case '3':
									j *= 10; j += 3;
									break;
								case '4':
									j *= 10; j += 4;
									break;
								case '5':
									j *= 10; j += 5;
									break;
								case '6':
									j *= 10; j += 6;
									break;
								case '7':
									j *= 10; j += 7;
									break;
								case '8':
									j *= 10; j += 8;
									break;
								case '9':
									j *= 10; j += 9;
									break;
								}
							}
							oggL2 = j - oggL1;
							p1 += sizeof(aa) + 1;
							for (; *p1 == 0x09; p1++);
							p1 += 3;
							char* pp = p1;
							for (; *p1 != 0xd; p1++) {
								if (*p1 == 0x9) {
									*p1 = 0x20;
								}
							}
							p1 = pp;
							MultiByteToWideChar(CP_ACP, 0, p1, -1, outcm, 1024);
							ss = outcm;
							_tcscpy(p.name, ss.Trim());
							if (looping == 0) {
								oggL1 = oggL2 = 0;
							}
							free(buff); break;
						}
						fclose(fp2);
					}

					//YSC
					sss = fname.Left(fname.ReverseFind('\\'));
					_tchdir(sss);
					ss = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					if (ss == "yc_b001.ogg") {
						ss = LL14(L"バトル#58", L"Battle #58", L"Combat #58", L"Battaglia #58", L"Batalla #58", L"배틀 #58", L"战斗 #58", L"معركة #58", L"Сражение #58", L"Kampf #58", L"Batalha #58", L"Gevecht #58", L"Bitwa #58", L"Savaş #58");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b002.ogg") {
						ss = LL14(L"灼熱の炎の中で", L"Within the Blazing Flames", L"Dans les flammes ardentes", L"Tra le fiamme ardenti", L"Entre las llamas ardientes", L"작열하는 불꽃 속에서", L"在灼热的火焰中", L"في لهيب النار", L"В раскаленном пламени", L"In den lodernden Flammen", L"Nas chamas ardentes", L"In de brandende vlammen", L"W płonących płomieniach", L"Yanan Alevlerin İçinde");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b003.ogg") {
						ss = LL14(L"最終決戦", L"Final Battle", L"Bataille finale", L"Battaglia finale", L"Batalla final", L"최종 결전", L"最终决战", L"المعركة النهائية", L"Финальная битва", L"Letzter Kampf", L"Batalha final", L"Laatste gevecht", L"Ostateczna bitwa", L"Son Savaş");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b004.ogg") {
						ss = LL14(L"黒き翼", L"Black Wings", L"Ailes noires", L"Ali nere", L"Alas negras", L"검은 날개", L"黑色翅膀", L"أجنحة سوداء", L"Черные крылья", L"Schwarze Flügel", L"Asas negras", L"Zwarte vleugels", L"Czarne skrzydła", L"Siyah Kanatlar");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b005.ogg") {
						ss = L"The False God of Causality";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d101.ogg") {
						ss = LL14(L"ダンジョン", L"Dungeon", L"Donjon", L"Sotterraneo", L"Mazmorra", L"던전", L"迷宫", L"زنزانة", L"Подземелье", L"Kerker", L"Masmorra", L"Kerker", L"Loch", L"Zindan");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d201.ogg") {
						ss = LL14(L"道化師の誘い", L"Clown's Invitation", L"Invitation du bouffon", L"Invito del clown", L"Invitación del payaso", L"광대의 유혹", L"小丑的引诱", L"دعوة المهرج", L"Приглашение клоуна", L"Einladung des Clowns", L"Convite do palhaço", L"Uitnodiging van de clown", L"Zaproszenie błazna", L"Palyaçonun Daveti");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d301.ogg") {
						ss = LL14(L"地下遺跡", L"Underground Ruins", L"Ruines souterraines", L"Rovine sotterranee", L"Ruinas subterráneas", L"지하 유적", L"地下遗迹", L"الأطلال تحت الأرض", L"Подземные руины", L"Unterirdische Ruinen", L"Ruínas subterrâneas", L"Ondergrondse ruïnes", L"Podziemne ruiny", L"Yeraltı Harabeleri");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d401.ogg") {
						ss = LL14(L"導きの塔〜エルディールにくちづけを", L"Tower of Guidance -Kiss for Eldeel-", L"Tour de guidance -Un baiser pour Eldeel-", L"Torre della guida -Un bacio per Eldeel-", L"Torre de guía -Un beso para Eldeel-", L"인도의 탑 ~ Eldeel에게 입맞춤을", L"引导之塔〜给 Eldeel 的吻", L"برج الإرشاد - قبلة لـ Eldeel", L"Башня наставления -Поцелуй для Eldeel-", L"Turm der Führung -Kuss für Eldeel-", L"Torre de Orientação -Beijo para Eldeel-", L"Toren van begeleiding -Kus voor Eldeel-", L"Wieża przewodnictwa -Pocałunek dla Eldeel-", L"Rehberlik Kulesi -Eldeel için Bir Öpücük-");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d501.ogg") {
						ss = LL14(L"失われし仮面を求めて", L"Seeking the Lost Mask", L"À la recherche du masque perdu", L"Alla ricerca della maschera perduta", L"Buscando la máscara perdida", L"잃어버린 가면을 찾아서", L"寻找失落的面具", L"البحث عن القناع المفقود", L"В поисках утраченной маски", L"Auf der Suche nach der verlorenen Maske", L"Em busca da máscara perdida", L"Op zoek naar het verloren masker", L"W poszukiwaniu zagubionej maski", L"Kayıp Maskenin Peşinde");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d701.ogg") {
						ss = LL14(L"イリス", L"Iris", L"Iris", L"Iris", L"Iris", L"이리스", L"伊莉丝", L"إيريس", L"Ирис", L"Iris", L"Íris", L"Iris", L"Irys", L"Iris");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d702.ogg") {
						ss = L"yc_d702";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d703.ogg") {
						ss = LL14(L"聖域", L"Sanctuary", L"Sanctuaire", L"Santuario", L"Santuario", L"성역", L"圣域", L"ملاذ", L"Святилище", L"Heiligtum", L"Santuário", L"Heiligdom", L"Sanktuarium", L"Kutsal Alan");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e001.ogg") {
						ss = LL14(L"賢者", L"Sage", L"Sage", L"Saggio", L"Sabio", L"현자", L"賢者", L"حكيم", L"Мудрец", L"Weiser", L"Sábio", L"Wijze", L"Mędrzec", L"Bilge");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e002.ogg") {
						ss = LL14(L"復活の儀式", L"Resurrection Ceremony", L"Cérémonie de résurrection", L"Cerimonia di resurrezione", L"Ceremonia de resurrección", L"부활의 의식", L"复活的仪式", L"طريقة الإحياء", L"Церемония воскрешения", L"Auferstehungszeremonie", L"Cerimônia de ressurreição", L"Opstandingsceremonie", L"Ceremonia wskrzeszenia", L"Diriliş Töreni");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e003.ogg") {
						ss = LL14(L"レファンス", L"Refance", L"Refance", L"Refance", L"Refance", L"레판스", L"雷凡斯", L"ريفانس", L"Рефанс", L"Refance", L"Refance", L"Refance", L"Refance", L"Refance");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e004.ogg") {
						ss = LL14(L"涙の少年剣士", L"Young Swordsman in Tears", L"Jeune épéiste en larmes", L"Giovane spadaccino in lacrime", L"Joven espadachín en lágrimas", L"눈물의 소년 검사", L"流泪的少年剑士", L"المبارز الفتى الباكي", L"Юный мечник в слезах", L"Junger Schwertkämpfer in Tränen", L"Jovem espadachim em lágrimas", L"Jonge zwaardvechter in tranen", L"Młody szermierz we łzach", L"Gözü Yaşlı Genç Kılıç Ustası");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e005.ogg") {
						ss = LL14(L"エルディール", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"엘딜", L"艾尔迪尔", L"إلديل", L"Эльдил", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e006.ogg") {
						ss = LL14(L"ロムン帝国 -嗚呼レオ団長-", L"Romun Empire -Alas Captain Leo-", L"Empire de Romun -Hélas Capitaine Leo-", L"Impero di Romun -Ahimè Capitano Leo-", L"Imperio de Romun -Ay, Capitán Leo-", L"로문 제국 ~아아 레오 단장~", L"Romun 帝国 -呜呼里欧团长-", L"الإمبراطورية الرومانية - وا أسفاه كابتن ليو", L"Империя Ромун -Увы, капитан Лео-", L"Romun Reich -Ach, Kapitän Leo-", L"Império de Romun -Ai, Capitão Leo-", L"Romun-rijk -Helaas Kapitein Leo-", L"Imperium Romun -Ach, Kapitanie Leo-", L"Romun İmparatorluğu -Vah Yüzbaşı Leo-");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e008.ogg") {
						ss = L"yc_e008";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e010.ogg") {
						ss = LL14(L"冒険家、誕生", L"Birth of an Adventurer", L"Naissance d'un aventurier", L"Nascita di un avventuriero", L"Nacimiento de un aventurero", L"모험가, 탄생", L"冒险家诞生", L"ولادة مغامر", L"Рождение искателя приключений", L"Geburt eines Abenteurers", L"Nascimento de um aventureiro", L"Geboorte van een avonturier", L"Narodziny poszukiwacza przygód", L"Bir Maceracının Doğuşu");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f101.ogg") {
						ss = LL14(L"燃ゆる剣", L"Burning Sword", L"Épée brûlante", L"Spada ardente", L"Espada ardiente", L"불타는 검", L"燃烧之剑", L"السيف المشتعل", L"Пылающий меч", L"Brennendes Schwert", L"Espada flamejante", L"Brandend zwaard", L"Płonący miecz", L"Yanan Kılıç");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f201.ogg") {
						ss = LL14(L"セルセタの樹海", L"Forest of Celceta", L"Forêt de Celceta", L"Foresta di Celceta", L"Bosque de Celceta", L"셀세타의 수해", L"Celceta 的树海", L"غابة سيلسيتا", L"Лес Celceta", L"Wald von Celceta", L"Floresta de Celceta", L"Woud van Celceta", L"Las Celceta", L"Celceta Ormanı");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f301.ogg") {
						ss = LL14(L"クレーター", L"Crater", L"Cratère", L"Cratere", L"Cráter", L"크레이터", L"火山口", L"فوهة البركان", L"Кратер", L"Krater", L"Cratera", L"Krater", L"Krater", L"Krater");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f401.ogg") {
						ss = L"THE DAWN OF YS";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f501.ogg") {
						ss = LL14(L"暁の森", L"Forest of Dawn", L"Forêt de l'aube", L"Foresta dell'alba", L"Bosque del alba", L"새벽의 숲", L"晓之森", L"غابة الفجر", L"Лес рассвета", L"Wald der Dämmerung", L"Floresta da aurora", L"Woud van de dageraad", L"Las świtu", L"Şafak Ormanı");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f601.ogg") {
						ss = LL14(L"一陣の風", L"Gust of Wind", L"Une rafale de vent", L"Raffica di vento", L"Ráfaga de viento", L"한 줄기 바람", L"一阵风", L"هبة ريح", L"Порыв ветра", L"Windstoß", L"Rajada de vento", L"Windvlaag", L"Podmuch wiatru", L"Bir Rüzgar Esintisi");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f701.ogg") {
						ss = LL14(L"神代の地", L"Land of the Gods", L"Terre des dieux", L"Terra degli dei", L"Tierra de los dioses", L"신대의 땅", L"神代之地", L"أرض الآلهة", L"Земля богов", L"Land der Götter", L"Terra dos deuses", L"Land van de goden", L"Kraina bogów", L"Tanrıların Diyarı");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f801.ogg") {
						ss = LL14(L"真実への序曲", L"Overture to Truth", L"Ouverture vers la vérité", L"Ouverture alla verità", L"Obertura a la verdad", L"진실로의 서곡", L"通往真实的序曲", L"مقدمة الحقيقة", L"Увертюра к истине", L"Ouverture zur Wahrheit", L"Prelúdio para a verdade", L"Ouverture naar de waarheid", L"Uwertura do prawdy", L"Gerçeğe Üvertür");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f901.ogg") {
						ss = LL14(L"雨上がりの朝に", L"Morning After the Rain", L"Le matin après la pluie", L"Mattina dopo la pioggia", L"Mañana después de la lluvia", L"비 갠 아침에", L"雨过天晴的早晨", L"صباح ما بعد المطر", L"Утро после дождя", L"Morgen nach dem Regen", L"Manhã após a chuva", L"Ochtend na de regen", L"Poranek po deszczu", L"Yağmur Sonrası Sabah");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_over.ogg") {
						ss = LL14(L"ゲームオーバー", L"Game Over", L"Fin de partie", L"Fine del gioco", L"Juego terminado", L"게임 오버", L"游戏结束", L"انتهت اللعبة", L"Конец игры", L"Spiel vorbei", L"Fim de jogo", L"Game over", L"Koniec gry", L"Oyun Bitti");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t101.ogg") {
						ss = LL14(L"辺境都市《キャスナン》", L"Frontier City Casnan", L"Ville frontalière Casnan", L"Città di confine Casnan", L"Ciudad fronteriza Casnan", L"변경도시 Casnan", L"边境都市 Casnan", L"مدينة كاسنان الحدودية", L"Пограничный город Casnan", L"Grenzstadt Casnan", L"Cidade fronteiriça Casnan", L"Grensstad Casnan", L"Graniczne miasto Casnan", L"Sınır Şehri Casnan");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t201.ogg") {
						ss = LL14(L"優しくなりたい", L"I Want to Be Kind", L"Je veux être gentil", L"Voglio essere gentile", L"Quiero ser amable", L"상냥해지고 싶어", L"想要变得温柔", L"أريد أن أكون لطيفاً", L"Я хочу быть добрым", L"Ich möchte gütig sein", L"Eu quero ser gentil", L"Ik wil vriendelijk zijn", L"Chcę być miły", L"Nazik Olmak İstiyorum");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t301.ogg") {
						ss = LL14(L"古代の伝承", L"Ancient Legend", L"Légende ancienne", L"Antica leggenda", L"Leyenda antigua", L"고대의 전승", L"古代的传承", L"أسطورة قديمة", L"Древняя легенда", L"Alte Legende", L"Lenda antiga", L"Oude legende", L"Starożytna legenda", L"Kadim Efsane");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t501.ogg") {
						ss = L"RODA";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_title.ogg") {
						ss = L"THEME OF ADOL 2012";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_op.ogg") {
						ss = L"The Foliage Ocean in CELCETA -Opening size-";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_end.ogg") {
						ss = LL14(L"新たな時代のステージへ", L"To the Stage of a New Era", L"Vers l'étape d'une nouvelle ère", L"Verso il palcoscenico di una nuova era", L"Hacia el escenario de una nueva era", L"새로운 시대의 스테이지로", L"迈向新时代的舞台", L"إلى مرحلة عصر جديد", L"На сцену новой эры", L"Auf die Bühne einer neuen Ära", L"Para o palco de uma nova era", L"Naar het podium van een nieuw tijdperk", L"Do etapu nowej ery", L"Yeni Bir Çağın Sahnesine");
						_tcscpy(p.name, ss);
					}

					//zero 
					CString ss;
					ss = fname.Right(fname.GetLength() - fname.ReverseFind(L'\\') - 1);
					sss = fname.Left(fname.ReverseFind('\\'));
					int fg = 0;
					CFile ffff;
					if (ffff.Open(sss + L"\\..\\text\\t_bgm._dt", CFile::modeRead | CFile::shareDenyWrite)) { fg = 1; ffff.Close(); }
					CString zero = savedata.zero;
					if(zero != L"") if (ffff.Open(savedata.zero, CFile::modeRead | CFile::shareDenyWrite)) { fg = 1; ffff.Close(); }
					CString a;
					if (ss.Mid(0, 3) == L"ed7" && fg == 1) {
						switch (_ttoi(ss.Mid(2, 4))) {
						case 7001:
							a = LL14(L"零の軌跡", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"영의 궤적", L"零之轨迹", L"مسارات من الصفر", L"Тропы от Нуля", L"Trails from Zero", L"Trilhas do Zero", L"Trails from Zero", L"Trails from Zero", L"Sıfırdan İzler");
							break;
						case 7002:
							a = L"way of live -Opening Version-";
							break;
						case 7003:
							a = LL14(L"新しき日々〜予兆", L"New Days -Omen-", L"Nouveaux Jours -Présage-", L"Nuovi Giorni -Presagio-", L"Nuevos Días -Presagio-", L"새로운 날들 ~예조", L"更新之日~预兆", L"أيام جديدة -بشرى-", L"Новые Дни -Предзнаменование-", L"Neue Tage -Omen-", L"Novos Dias -Presságio-", L"Nieuwe Dagen -Voorteken-", L"Nowe Dni -Omen-", L"Yeni Günler -İşaret-");
							break;
						case 7005:
							a = LL14(L"想い破れて・・・", L"Broken Heart...", L"Cœur Brisé...", L"Cuore Spezzato...", L"Corazón Roto...", L"부서진 마음...", L"心意破碎...", L"قلب مكسور...", L"Разбитое Сердце...", L"Gebrochenes Herz...", L"Coração Partido...", L"Gebroken Hart...", L"Złamane Serce...", L"Kırık Kalp...");
							break;
						case 7052:
							a = LL14(L"碧い軌跡 -Opening size-", L"Azure Arbitrator -Opening size-", L"Arbitre Azur -taille ouverture-", L"Arbitro Azzurro -dimensione apertura-", L"Árbitro Azur -tamaño apertura-", L"벽의 궤적 -Opening size-", L"碧之轨迹 -片头版-", L"المحكم الأزرق -حجم الافتتاحية-", L"Лазурный Арбитр -размер открытия-", L"Azur-Schiedsrichter -Eröffnungsgröße-", L"Árbitro Azul -tamanho abertura-", L"Azure Scheidsrechter -openingsgrootte-", L"Lazurowy Arbitr -rozmiar otwierający-", L"Gök Mavisi Hakem -açılış boyutu-");
							break;
						case 7053:
							a = LL14(L"それでも僕らは。", L"Yet We're Still Here.", L"Pourtant Nous Sommes Là.", L"Eppure Siamo Ancora Qui.", L"Pero Seguimos Aquí.", L"그래도 우리는.", L"即便如此我们仍在。", L"ومع ذلك نحن هنا.", L"Но Мы Всё Ещё Здесь.", L"Dennoch Sind Wir Noch Hier.", L"Mas Ainda Estamos Aqui.", L"Toch Zijn We Er Nog.", L"A Jednak Nadal Tu Jesteśmy.", L"Yine de Buradayız.");
							break;
						case 7100:
							a = LL14(L"街角の風景", L"Street Corner Scenery", L"Scène de Rue", L"Scena Angolo Strada", L"Paisaje de Esquina", L"길모퉁이 풍경", L"街角风景", L"مشهد زاوية الشارع", L"Вид Угола Улицы", L"Straßenecken-Szenerie", L"Cenário da Esquina", L"Straathoekscene", L"Scena Ulicznego Rogu", L"Sokak Köşesi Manzarası");
							break;
						case 7101:
							a = LL14(L"明日は明日の風が吹く", L"Tomorrow the Wind Will Blow", L"Demain le Vent Soufflera", L"Domani Soffierà il Vento", L"Mañana Soplara el Viento", L"내일은 내일의 바람이 분다", L"明日自有明日风", L"غداً تهب الرياح", L"Завтра Подует Ветер", L"Morgen Wird der Wind Wehen", L"Amanhã o Vento Soprará", L"Morgen Zal de Wind Waaien", L"Jutro Zawieje Wiatr", L"Yarın Rüzgar Esecek");
							break;
						case 7102:
							a = LL14(L"クロスベルの午後", L"Afternoon in Crossbell", L"Après-midi à Crossbell", L"Pomeriggio a Crossbell", L"Tarde en Crossbell", L"크로스벨의 오후", L"克洛斯贝尔的午后", L"وقت الظهيرة في كروسبيل", L"Послеполуденное время в Кроссбелле", L"Nachmittag in Crossbell", L"Tarde em Crossbell", L"Middag in Crossbell", L"Popołudnie w Crossbell", L"Crossbell'de Öğleden Sonra");
							break;
						case 7103:
							a = L"During Mission Accomplishment";
							break;
						case 7104:
							a = LL14(L"創立記念祭", L"Founding Festival", L"Fête de Fondation", L"Festival della Fondazione", L"Festival Fundacional", L"창립 기념제", L"创立纪念祭", L"مهرجان التأسيس", L"Праздник Основания", L"Gründungsfest", L"Festival da Fundação", L"Stichtingsfeest", L"Święto Założenia", L"Kuruluş Festivali");
							break;
						case 7105:
							a = LL14(L"降水確率10%", L"10% Chance of Rain", L"10% de chances de pluie", L"10% di probabilità di pioggia", L"10% de probabilidad de lluvia", L"강수확률 10%", L"降水概率10%", L"10% فرصة هطول", L"10% Вероятность Дождя", L"10% Regenwahrscheinlichkeit", L"10% de chance de chuva", L"10% kans op regen", L"10% szans na deszcz", L"%10 yağmur ihtimali");
							break;
						case 7106:
							a = LL14(L"風船と紙吹雪", L"Balloons and Confetti", L"Ballons et Confettis", L"Palloncini e Coriandoli", L"Globos y Confeti", L"풍선과 종이 꽃가루", L"气球与纸屑", L"بالونات وقصاصات ملونة", L"Воздушные Шары и Конфетти", L"Luftballons und Konfetti", L"Balões e Confete", L"Ballonnen en Confetti", L"Balony i Konfetti", L"Balonlar ve Konfeti");
							break;
						case 7110:
							a = LL14(L"特務支援課", L"Special Support Section", L"Section Soutien Spécial", L"Sezione Supporto Speciale", L"Sección de Apoyo Especial", L"특무지원과", L"特务支援科", L"قسم الدعم الخاص", L"Специальная Поддержка", L"Sondereinsatztruppe", L"Seção de Suporte Especial", L"Speciale Ondersteuningssectie", L"Specjalna Sekcja Wsparcia", L"Özel Destek Bölümü");
							break;
						case 7111:
							a = LL14(L"C.S.P.D. -クロスベル警察", L"C.S.P.D. -Crossbell Police", L"C.S.P.D. -Police de Crossbell", L"C.S.P.D. -Polizia Crossbell", L"C.S.P.D. -Policía Crossbell", L"C.S.P.D. -크로스벨 경찰", L"C.S.P.D. -克洛斯贝尔警察", L"C.S.P.D. -شرطة كروسبيل", L"C.S.P.D. -Полиция Кроссбелла", L"C.S.P.D. -Crossbell Polizei", L"C.S.P.D. -Polícia Crossbell", L"C.S.P.D. -Crossbell Politie", L"C.S.P.D. -Policja Crossbell", L"C.S.P.D. -Crossbell Polisi");
							break;
						case 7113:
							a = L"Arc-en-ciel";
							break;
						case 7114:
							a = LL14(L"黒月貿易公司", L"Heiyue Trading Company", L"Compagnie Heiyue", L"Heiyue Trading Company", L"Heiyue Trading Company", L"헤이위에 무역공사", L"黑月贸易公司", L"شركة هييو التجارية", L"Торговая Компания Хэйюэ", L"Heiyue Handelsgesellschaft", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company");
							break;
						case 7116:
							a = L"IGNIS";
							break;
						case 7117:
							a = L"TRINITY";
							break;
						case 7120:
							a = LL14(L"アルモリカ村", L"Armorica Village", L"Village d'Armorica", L"Villaggio Armorica", L"Aldea Armorica", L"아르모리카 마을", L"阿莫利卡村", L"قرية أرموريكا", L"Деревня Арморика", L"Armorica-Dorf", L"Vila Armorica", L"Armorica-dorp", L"Wieś Armorica", L"Armorica Köyü");
							break;
						case 7121:
							a = LL14(L"鉱山町マインツ", L"Mines Town Mainz", L"Ville minière Mainz", L"Città mineraria Mainz", L"Ciudad minera Mainz", L"광산마을 마인츠", L"矿山镇美因茨", L"بلدة المناجم ماينز", L"Город Шахт Майнц", L"Bergarbeiterstadt Mainz", L"Cidade das Minas Mainz", L"Mijnstad Mainz", L"Miasto Kopalni Mainz", L"Mainz Maden Kasabası");
							break;
						case 7122:
							a = L"Killing Bear";
							break;
						case 7123:
							a = LL14(L"聖ウルスラ医科大学", L"St. Ursula Medical College", L"Faculté St-Ursule", L"Collegio medico St. Ursula", L"Universidad Médica St. Ursula", L"성 우르술라 의과대학", L"圣乌尔苏拉医科大学", L"كلية سانت أورسولا الطبية", L"Медколледж св. Урсулы", L"St. Ursula Medizinhochschule", L"Faculdade St. Ursula", L"St. Ursula Medische Hogeschool", L"Szpital św. Urszuli", L"Aziz Ursula Tıp Koleji");
							break;
						case 7124:
							a = LL14(L"クロスベル大聖堂", L"Crossbell Cathedral", L"Cathédrale de Crossbell", L"Cattedrale di Crossbell", L"Catedral de Crossbell", L"크로스벨 대성당", L"克洛斯贝尔大教堂", L"كاتدرائية كروسبيل", L"Собор Кроссбелла", L"Crossbell-Kathedrale", L"Catedral de Crossbell", L"Crossbell Kathedraal", L"Katedra Crossbell", L"Crossbell Katedrali");
							break;
						case 7125:
							a = LL14(L"黒の競売会", L"Black Auction", L"Vente aux enchères noire", L"Asta nera", L"Subasta negra", L"검은 경매회", L"黑色拍卖会", L"المزاد الأسود", L"Чёрный Аукцион", L"Schwarze Auktion", L"Leilão negro", L"Zwarte Veiling", L"Czarna Aukcja", L"Kara Müzayede");
							break;
						case 7126:
							a = LL14(L"大国にはさまれて", L"Caught Between Nations", L"Pris entre les Nations", L"Intrappolati tra le Nazioni", L"Atrapados entre Naciones", L"대국 사이에 끼어", L"夹在大国之间", L"محاصرون بين الأمم", L"Зажатые Между Державами", L"Zwischen den Nationen gefangen", L"Preso entre Nações", L"Gevangen tussen Naties", L"Uwięziony między Mocarstwami", L"Uluslar Arasında Sıkışmış");
							break;
						case 7150:
							a = LL14(L"新たなる日常", L"New Daily Life", L"Nouvelle Vie Quotidienne", L"Nuova Vita Quotidiana", L"Nueva Vida Cotidiana", L"새로운 일상", L"崭新的日常", L"حياة يومية جديدة", L"Новые Будни", L"Neuer Alltag", L"Nova Vida Cotidiana", L"Nieuw Dagelijks Leven", L"Nowe Codzienne Życie", L"Yeni Günlük Yaşam");
							break;
						case 7151:
							a = LL14(L"動き始めた事態", L"Events in Motion", L"Événements en Mouvement", L"Eventi in Movimento", L"Eventos en Movimiento", L"움직이기 시작한 사태", L"开始运作的局面", L"الأحداث في حركة", L"События Приходят в Движение", L"Ereignisse in Bewegung", L"Eventos em Movimento", L"Gebeurtenissen in Beweging", L"Zdarzenia w Ruchu", L"Harekete Geçen Olaylar");
							break;
						case 7160:
							a = LL14(L"ミシュラムワンダーランド", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"미슈람 원더랜드", L"米修拉姆乐园", L"أرض عجائب ميشرام", L"Мишрам Уандерленд", L"Mishyram Wunderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Harikalar Diyarı");
							break;
						case 7161:
							a = LL14(L"束の間の休息", L"Brief Respite", L"Bref Répit", L"Breve Respiro", L"Breve Respiro", L"잠시 동안의 휴식", L"短瞬的休息", L"استراحة قصيرة", L"Краткая Передышка", L"Kurze Verschnaufpause", L"Breve Descanso", L"Kort Respijt", L"Krótki Odpoczynek", L"Kısa Mola");
							break;
						case 7162:
							a = LL14(L"ささやかな晩餐", L"Simple Dinner", L"Dîner Simple", L"Cena Semplice", L"Cena Sencilla", L"소박한 만찬", L"简单的晚餐", L"عشاء بسيط", L"Скромный Ужин", L"Einfaches Abendessen", L"Jantar Simples", L"Eenvoudig Diner", L"Prosty Obiad", L"Sade Akşam Yemeği");
							break;
						case 7200:
							a = LL14(L"水と草木と青い空", L"Water, Trees and Blue Sky", L"Eau, Arbres et Ciel Bleu", L"Acqua, Alberi e Cielo Azzurro", L"Agua, Árboles y Cielo Azul", L"물과 풀과 파란 하늘", L"水与草木和蓝天", L"المياه والأشجار والسماء الزرقاء", L"Вода, Деревья и Голубое Небо", L"Wasser, Bäume und blauer Himmel", L"Água, Árvores e Céu Azul", L"Water, Bomen en Blauwe Lucht", L"Woda, Drzewa i Błękitne Niebo", L"Su, Ağaçlar ve Mavi Gökyüzü");
							break;
						case 7201:
							a = LL14(L"片手にはレモネード", L"Lemonade in One Hand", L"Limonade dans une Main", L"Limonata in una Mano", L"Limonada en una Mano", L"한 손에는 레모네이드", L"一手拿着柠檬水", L"ليمونادة في يد واحدة", L"Лимонад в Одной Руке", L"Limonade in einer Hand", L"Limonada em uma Mão", L"Limonade in een Hand", L"Lemoniada w Jednej Ręce", L"Bir Elde Limonata");
							break;
						case 7202:
							a = LL14(L"木霊の道", L"Path of Echoes", L"Chemin des Échos", L"Sentiero degli Echi", L"Senda de los Ecos", L"메아리의 길", L"回声之道", L"طريق الأصداء", L"Тропа Эхо", L"Pfad der Echos", L"Caminho dos Ecos", L"Pad van Echo's", L"Ścieżka Ech", L"Yankılar Yolu");
							break;
						case 7203:
							a = LL14(L"古の鼓動", L"Ancient Pulse", L"Pulsation Ancienne", L"Pulsazione Antica", L"Pulso Antiguo", L"고대의 고동", L"古老的脉动", L"النبض القديم", L"Древний Пульс", L"Alter Puls", L"Pulso Antigo", L"Oude Puls", L"Starożytne Tętno", L"Kadim Nabız");
							break;
						case 7204:
							a = L"On The Green Road";
							break;
						case 7205:
							a = LL14(L"鉄橋を越えて", L"Crossing the Iron Bridge", L"Traverser le Pont de Fer", L"Attraversare il Ponte di Ferro", L"Cruzando el Puente de Hierro", L"철교를 건너", L"越过铁桥", L"عبر الجسر الحديدي", L"Пересекая Железный Мост", L"Die Eisenbrücke überqueren", L"Cruzando a Ponte de Ferro", L"De IJzeren Brug Oversteken", L"Przekraczając Żelazny Most", L"Demir Köprüyü Geçerken");
							break;
						case 7250:
							a = LL14(L"木洩れ日の中の静寂", L"Tranquility in the Dappled Light", L"Tranquillité dans la Lumière Tachetée", L"Tranquillità nella Luce Screziata", L"Tranquilidad en la Luz Moteada", L"햇살 속의 정적", L"斑驳光影中的静谧", L"الهدوء في الضوء المرقط", L"Тишина в Пятнистом Свете", L"Stille im gefilterten Licht", L"Tranquilidade na Luz Filtrada", L"Rust in het Gefiltreerde Licht", L"Spokój w Migotliwym Świetle", L"Işık Süzülürken Huzur");
							break;
						case 7251:
							a = LL14(L"偽りの楽土を越えて", L"Beyond the False Paradise", L"Au-Delà du Faux Paradis", L"Oltre il Falso Paradiso", L"Más Allá del Falso Paraíso", L"거짓 낙원을 넘어", L"超越虚假的乐土", L"ما وراء الجنة المزيفة", L"За пределами Ложного Рая", L"Jenseits des falschen Paradieses", L"Além do Falso Paraíso", L"Voorbij het Valse Paradijs", L"Poza Fałszywym Rajem", L"Sahte Cennetin Ötesinde");
							break;
						case 7300:
							a = LL14(L"ジオフロント", L"Geofront", L"Géofront", L"Geofront", L"Geofront", L"지오프런트", L"地底都市", L"جيوفرونت", L"Геофронт", L"Geofront", L"Geofront", L"Geofront", L"Geofront", L"Geofront");
							break;
						case 7301:
							a = LL14(L"七耀の煌き", L"Septium Radiance", L"Éclat du Septium", L"Splendore del Septium", L"Resplandor del Septium", L"칠요의 광채", L"七曜之光辉", L"تألق السفيتيوم", L"Сияние Септиума", L"Septium-Glanz", L"Resplendor do Septium", L"Septium Glinstering", L"Blask Septium", L"Septium Işıltısı");
							break;
						case 7302:
							a = LL14(L"ルバーチェ商会", L"Revache Trading Company", L"Compagnie Revache", L"Revache Trading Company", L"Revache Trading Company", L"르바체 상회", L"鲁巴切商会", L"شركة ريفاش التجارية", L"Торговая Компания Реваш", L"Revache Handelsgesellschaft", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Ticaret Şirketi");
							break;
						case 7303:
							a = LL14(L"鳴るはずのない鐘", L"The Bell That Shouldn't Ring", L"La Cloche Qui Ne Devrait Pas Sonner", L"La Campana Che Non Dovrebbe Suonare", L"La Campana Que No Debería Sonar", L"울릴 리 없는 종", L"不该鸣起的钟声", L"الجرس الذي لا يجب أن يرن", L"Колокол, Который Не Должен Звонить", L"Die Glocke, die nicht läuten sollte", L"O Sino Que Não Deveria Tocar", L"De Klok Die Niet Zou Moeten Luiden", L"Dzwon, Który Nie Powinien Bić", L"Çalmaması Gereken Çan");
							break;
						case 7304:
							a = LL14(L"忘れられし幻夢の狭間", L"Forgotten Phantasmal Gap", L"Interstice Fantomatique Oublié", L"Varco Fantasmatico Dimenticato", L"Brecha Fantasmal Olvidada", L"잊힌 환몽의 틈새", L"被遗忘的幻梦之间", L"فجوة خيالية منسية", L"Забытый Призрачный Разрыв", L"Vergessene Phantomale Lücke", L"Lacuna Fantasmal Esquecida", L"Vergeten Spookachtige Kloof", L"Zapomniana Fantomalna Szczelina", L"Unutulmuş Hayali Boşluk");
							break;
						case 7305:
							a = L"A Light Illuminating The Depths";
							break;
						case 7350:
							a = LL14(L"Dの残影", L"D's Shadow", L"L'Ombre de D", L"L'Ombra di D", L"La Sombra de D", L"D의 잔영", L"D的残影", L"ظل D", L"Тень D", L"Ds Schatten", L"A Sombra de D", L"D's Schaduw", L"Cień D", L"D'nin Gölgesi");
							break;
						case 7351:
							a = LL14(L"異変の兆し", L"Omen of Change", L"Présage de Changement", L"Presagio di Cambiamento", L"Presagio de Cambio", L"이변의 징조", L"异变的征兆", L"نذير التغيير", L"Предзнаменование Перемен", L"Vorbote des Wandels", L"Presságio de Mudança", L"Voorteken van Verandering", L"Zwiastun Zmiany", L"Değişimin İşareti");
							break;
						case 7352:
							a = L"Mystic Core";
							break;
						case 7353:
							a = LL14(L"最果ての樹", L"Tree at World's End", L"L'Arbre au Bout du Monde", L"L'Albero alla Fine del Mondo", L"El Árbol al Fin del Mundo", L"땅 끝의 나무", L"天涯之树", L"شجرة عند نهاية العالم", L"Дерево на Краю Света", L"Baum am Ende der Welt", L"A Árvore no Fim do Mundo", L"De Boom aan het Einde van de Wereld", L"Drzewo na Końcu Świata", L"Dünyanın Sonundaki Ağaç");
							break;
						case 7354:
							a = LL14(L"暴魔の呼び声", L"Call of the Beast", L"L'Appel de la Bête", L"Il Richiamo della Bestia", L"El Llamado de la Bestia", L"폭마의 부름", L"暴魔的呼唤", L"دعوة الوحش", L"Зов Зверя", L"Ruf des Ungeheuers", L"O Chamado da Besta", L"De Roep van het Beest", L"Wołanie Bestii", L"Canavarın Çağrısı");
							break;
						case 7356:
							a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
							break;
						case 7400:
							a = L"Get Over The Barrier!";
							break;
						case 7401:
							a = L"Arrest The Criminal";
							break;
						case 7402:
							a = L"Formidable Enemy";
							break;
						case 7403:
							a = L"Stand Up Battle Formation Again!";
							break;
						case 7404:
							a = L"Inevitable Struggle";
							break;
						case 7405:
							a = L"Demonic Drive";
							break;
						case 7406:
							a = L"Arrival Existence";
							break;
						case 7408:
							a = LL14(L"これが俺たちの力だ!", L"This Is Our Power!", L"C'est Notre Pouvoir !", L"Questo È il Nostro Potere!", L"¡Este Es Nuestro Poder!", L"이것이 우리들의 힘이다!", L"这就是我们的力量!", L"هذه هي قوتنا!", L"Это Наша Сила!", L"Das Ist Unsere Kraft!", L"Este É o Nosso Poder!", L"Dit Is Onze Kracht!", L"To Jest Nasza Siła!", L"Bu Bizim Gücümüz!");
							break;
						case 7450:
							a = L"Seize The Truth!";
							break;
						case 7451:
							a = L"Concentrate All Firepower!!";
							break;
						case 7452:
							a = L"Conflicting Passions";
							break;
						case 7453:
							a = L"Unexpected Emergency";
							break;
						case 7454:
							a = L"Mythtic Roar";
							break;
						case 7455:
							a = L"Destruction Impulse";
							break;
						case 7458:
							a = L"Unfathomed Force";
							break;
						case 7459:
							a = L"The Azure Arbitrator";
							break;
						case 7460:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
							break;
						case 7500:
							a = LL14(L"金の太陽、銀の月　-陽の熱情", L"Golden Sun, Silver Moon -Solar Passion-", L"Soleil d'Or, Lune d'Argent -Passion Solaire-", L"Sole d'Oro, Luna d'Argento -Passione Solare-", L"Sol Dorado, Luna de Plata -Pasión Solar-", L"황금의 태양, 은의 달 -태양의 열정-", L"黄金之阳，白银之月 -太阳的热情-", L"الشمس الذهبية، القمر الفضي -شغف شمسي-", L"Золотое Солнце, Серебряная Луна -Солнечная Страсть-", L"Goldene Sonne, Silberner Mond -Sonnenleidenschaft-", L"Sol Dourado, Lua de Prata -Paixão Solar-", L"Gouden Zon, Zilveren Maan -Zonnige Passie-", L"Złote Słońce, Srebrny Księżyc -Słoneczna Namiętność-", L"Altın Güneş, Gümüş Ay -Güneş Tutkusu-");
							break;
						case 7501:
							a = LL14(L"金の太陽、銀の月　-月の慕情", L"Golden Sun, Silver Moon -Lunar Affection-", L"Soleil d'Or, Lune d'Argent -Affection Lunaire-", L"Sole d'Oro, Luna d'Argento -Affetto Lunare-", L"Sol Dorado, Luna de Plata -Afecto Lunar-", L"황금의 태양, 은의 달 -달의 모정-", L"黄金之阳，白银之月 -月亮的思慕-", L"الشمس الذهبية، القمر الفضي -مودة قمرية-", L"Золотое Солнце, Серебряная Луна -Лунная Нежность-", L"Goldene Sonne, Silberner Mond -Mondneigung-", L"Sol Dourado, Lua de Prata -Afeição Lunar-", L"Gouden Zon, Zilveren Maan -Maanachtige Genegenheid-", L"Złote Słońce, Srebrny Księżyc -Księżycowe Uczucie-", L"Altın Güneş, Gümüş Ay -Ay Sevgisi-");
							break;
						case 7502:
							a = LL14(L"金の太陽、銀の月　-童心", L"Golden Sun, Silver Moon -Innocence-", L"Soleil d'Or, Lune d'Argent -Innocence-", L"Sole d'Oro, Luna d'Argento -Innocenza-", L"Sol Dorado, Luna de Plata -Inocencia-", L"황금의 태양, 은의 달 -동심-", L"黄金之阳，白银之月 -童心-", L"الشمس الذهبية، القمر الفضي -براءة-", L"Золотое Солнце, Серебряная Луна -Невинность-", L"Goldene Sonne, Silberner Mond -Unschuld-", L"Sol Dourado, Lua de Prata -Inocência-", L"Gouden Zon, Zilveren Maan -Onschuld-", L"Złote Słońce, Srebrny Księżyc -Niewinność-", L"Altın Güneş, Gümüş Ay -Masumiyet-");
							break;
						case 7503:
							a = LL14(L"金の太陽、銀の月　-運命の刻", L"Golden Sun, Silver Moon -Hour of Fate-", L"Soleil d'Or, Lune d'Argent -L'Heure du Destin-", L"Sole d'Oro, Luna d'Argento -L'Ora del Destino-", L"Sol Dorado, Luna de Plata -La Hora del Destino-", L"황금의 태양, 은의 달 -운명의 각-", L"黄金之阳，白银之月 -命运的时刻-", L"الشمس الذهبية، القمر الفضي -ساعة القدر-", L"Золотое Солнце, Серебряная Луна -Час Судьбы-", L"Goldene Sonne, Silberner Mond -Stunde des Schicksals-", L"Sol Dourado, Lua de Prata -A Hora do Destino-", L"Gouden Zon, Zilveren Maan -Het Uur van het Lot-", L"Złote Słońce, Srebrny Księżyc -Godzina Przeznaczenia-", L"Altın Güneş, Gümüş Ay -Kaderin Saati-");
							break;
						case 7504:
							a = LL14(L"金の太陽、銀の月　-譲れぬ想い", L"Golden Sun, Silver Moon -Unyielding Feelings-", L"Soleil d'Or, Lune d'Argent -Sentiments Inébranlables-", L"Sole d'Oro, Luna d'Argento -Sentimenti Irremovibili-", L"Sol Dorado, Luna de Plata -Sentimientos Inquebrantables-", L"황금의 태양, 은의 달 -양보할 수 없는 마음-", L"黄金之阳，白银之月 -不可退让的心意-", L"الشمس الذهبية، القمر الفضي -مشاعر لا تلين-", L"Золотое Солнце, Серебряная Луна -Непреклонные Чувства-", L"Goldene Sonne, Silberner Mond -Unnachgiebige Gefühle-", L"Sol Dourado, Lua de Prata -Sentimentos Inabaláveis-", L"Gouden Zon, Zilveren Maan -Onwrikbare Gevoelens-", L"Złote Słońce, Srebrny Księżyc -Nieustępliwe Uczucia-", L"Altın Güneş, Gümüş Ay -Vazgeçilmez Duygular-");
							break;
						case 7505:
							a = LL14(L"金の太陽、銀の月　-幾千の夜を越えて", L"Golden Sun, Silver Moon -Beyond Countless Nights-", L"Soleil d'Or, Lune d'Argent -Au-Delà de Nuits Sans Nombre-", L"Sole d'Oro, Luna d'Argento -Oltre Innumerevoli Notti-", L"Sol Dorado, Luna de Plata -Más Allá de Incontables Noches-", L"황금의 태양, 은의 달 -수천의 밤을 넘어-", L"黄金之阳，白银之月 -跨越无数夜晚-", L"الشمس الذهبية، القمر الفضي -عبر لا يحصى من الليالي-", L"Золотое Солнце, Серебряная Луна -Сквозь Бесчисленные Ночи-", L"Goldene Sonne, Silberner Mond -Jenseits Unzähliger Nächte-", L"Sol Dourado, Lua de Prata -Além de Incontáveis Noites-", L"Gouden Zon, Zilveren Maan -Voorbij Ontelbare Nachten-", L"Złote Słońce, Srebrny Księżyc -Poza Niezliczonymi Nocami-", L"Altın Güneş, Gümüş Ay -Sayısız Gecelerin Ötesinde-");
							break;
						case 7506:
							a = LL14(L"金の太陽、銀の月　-夜明け〜大団円", L"Golden Sun, Silver Moon -Dawn to Grand Finale-", L"Soleil d'Or, Lune d'Argent -Aube vers le Grand Finale-", L"Sole d'Oro, Luna d'Argento -Alba verso il Gran Finale-", L"Sol Dorado, Luna de Plata -Amanecer hasta el Gran Final-", L"황금의 태양, 은의 달 -새벽~대단원-", L"黄金之阳，白银之月 -黎明~大团圆-", L"الشمس الذهبية، القمر الفضي -الفجر حتى الخاتمة الكبرى-", L"Золотое Солнце, Серебряная Луна -Рассвет до Грандиозного Финала-", L"Goldene Sonne, Silberner Mond -Morgengrauen bis zum großen Finale-", L"Sol Dourado, Lua de Prata -Amanhecer até o Grande Final-", L"Gouden Zon, Zilveren Maan -Dageraad tot het Grote Finale-", L"Złote Słońce, Srebrny Księżyc -Świt do Wielkiego Finału-", L"Altın Güneş, Gümüş Ay -Şafaktan Büyük Finale-");
							break;
						case 7507:
							a = L"Intense Chase";
							break;
						case 7509:
							a = LL14(L"守りぬく意志", L"Unyielding Will", L"Volonté Inébranlable", L"Volontà Irremovibile", L"Voluntad Inquebrantable", L"지켜내는 의지", L"守护的意志", L"إرادة لا تلين", L"Непреклонная Воля", L"Unnachgiebiger Wille", L"Vontade Inabalável", L"Onwrikbare Wil", L"Nieustępliwa Wola", L"Vazgeçmeyen İrade");
							break;
						case 7510:
							a = LL14(L"叡智への誘い", L"Invitation to Wisdom", L"Invitation à la Sagesse", L"Invito alla Saggezza", L"Invitación a la Sabiduría", L"예지로의 유혹", L"通往智慧的邀请", L"دعوة إلى الحكمة", L"Приглашение к Мудрости", L"Einladung zur Weisheit", L"Convite à Sabedoria", L"Uitnodiging tot Wijsheid", L"Zaproszenie do Mądrości", L"Bilgeliğe Davet");
							break;
						case 7511:
							a = LL14(L"危地", L"Perilous Ground", L"Terrain Périlleux", L"Terreno Pericoloso", L"Terreno Peligroso", L"위지", L"危险之地", L"أرض خطرة", L"Опасная Территория", L"Gefährliches Terrain", L"Terreno Perigoso", L"Gevaarlijk Terrein", L"Niebezpieczny Teren", L"Tehlikeli Bölge");
							break;
						case 7512:
							a = LL14(L"揺るぎない強さ", L"Unshakable Strength", L"Force Inébranlable", L"Forza Incrollabile", L"Fuerza Inquebrantable", L"흔들림 없는 강함", L"不可动摇的力量", L"قوة لا تتزعزع", L"Непоколебимая Сила", L"Unerschütterliche Stärke", L"Força Inabalável", L"Onwankelbare Kracht", L"Niezachwiana Siła", L"Sarsılmaz Güç");
							break;
						case 7513:
							a = LL14(L"夜景に霞む星空", L"Starry Sky in the Night", L"Ciel Étoilé dans la Nuit", L"Cielo Stellato nella Notte", L"Cielo Estrellado en la Noche", L"야경에 가려진 별하늘", L"夜色中朦胧的星空", L"سماء مرصعة بالنجوم في الليل", L"Звёздное Небо Ночью", L"Sternenhimmel in der Nacht", L"Céu Estrelado na Noite", L"Sterrenhemel in de Nacht", L"Rozgwieżdżone Niebo w Nocy", L"Gece Yıldızlı Gökyüzü");
							break;
						case 7514:
							a = LL14(L"いつかきっと", L"Someday", L"Un Jour, Sûrement", L"Un Giorno, Di Certo", L"Algún Día, Seguro", L"언젠가 반드시", L"总有一天", L"يوماً ما بالتأكيد", L"Когда-нибудь Обязательно", L"Irgendwann Bestimmt", L"Um Dia, Com Certeza", L"Ooit Zeker", L"Kiedyś Na Pewno", L"Bir Gün Mutlaka");
							break;
						case 7515:
							a = LL14(L"柔らかな心", L"Tender Heart", L"Cœur Tendre", L"Cuore Tenero", L"Corazón Tierno", L"부드러운 마음", L"温柔的心", L"قلب رقيق", L"Нежное Сердце", L"Zartes Herz", L"Coração Terno", L"Teder Hart", L"Czułe Serce", L"Nazik Kalp");
							break;
						case 7516:
							a = LL14(L"点と線", L"Dots and Lines", L"Points et Lignes", L"Punti e Linee", L"Puntos y Líneas", L"점과 선", L"点与线", L"نقاط وخطوط", L"Точки и Линии", L"Punkte und Linien", L"Pontos e Linhas", L"Punten en Lijnen", L"Punkty i Linie", L"Noktalar ve Çizgiler");
							break;
						case 7517:
							a = LL14(L"一触即発", L"Imminent Crisis", L"Crise Imminente", L"Crisi Imminente", L"Crisis Inminente", L"일촉즉발", L"一触即发", L"أزمة وشيكة", L"Надвигающийся Кризис", L"Unmittelbar Bevorstehende Krise", L"Crise Iminente", L"Dreigende Crisis", L"Bezpośredni Kryzys", L"Yaklaşan Kriz");
							break;
						case 7518:
							a = L"Foolish Gig";
							break;
						case 7519:
							a = LL14(L"リベールからの風", L"Wind from Liberl", L"Vent de Liberl", L"Vento da Liberl", L"Viento de Liberl", L"리베르로부터의 바람", L"来自利贝尔的风", L"ريح من ليبرل", L"Ветер из Либерла", L"Wind aus Liberl", L"Vento de Liberl", L"Wind uit Liberl", L"Wiatr z Liberl", L"Liberl'den Rüzgar");
							break;
						case 7520:
							a = LL14(L"とどいた想い", L"Feelings Delivered", L"Sentiments Transmis", L"Sentimenti Consegnati", L"Sentimientos Entregados", L"닿은 마음", L"传达到的心意", L"مشاعر واصلة", L"Переданные Чувства", L"Übermittelte Gefühle", L"Sentimentos Entregues", L"Bezorgde Gevoelens", L"Dostarczone Uczucia", L"İletilen Duygular");
							break;
						case 7521:
							a = L"Underground Kids";
							break;
						case 7522:
							a = L"Terminal Room";
							break;
						case 7523:
							a = LL14(L"響きあう心", L"Resonating Hearts", L"Cœurs en Résonance", L"Cuori in Risonanza", L"Corazones en Resonancia", L"서로 울리는 마음", L"共鸣的心", L"قلوب رنانة متناغمة", L"Резонирующие Сердца", L"Resonierender Herzen", L"Corações em Ressonância", L"Resonerende Harten", L"Rezonujące Serca", L"Rezonans Eden Kalpler");
							break;
						case 7524:
							a = L"Limit Break";
							break;
						case 7525:
							a = LL14(L"パラダイスミ☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"패러다임☆", L"帕拉迪gm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆");
							break;
						case 7526:
							a = L"Gnosis";
							break;
						case 7527:
							a = L"Get Over The Barrier! -Roaring Version-";
							break;
						case 7528:
							a = LL14(L"それぞれの明日", L"Our Tomorrows", L"Nos Lendemains", L"I Nostri Domani", L"Nuestros Mañanas", L"저마다의 내일", L"各自的明天", L"غد كل منا", L"Наши Завтрашние Дни", L"Unsere Morgigen Tage", L"Nossos Amanhãs", L"Onze Morgens", L"Nasze Jutrzejsze Dni", L"Hepimizin Yarınları");
							break;
						case 7529:
							a = LL14(L"効果音楽1", L"Sound Effect Music 1", L"Musique d'Effet Sonore 1", L"Musica Effetto Sonoro 1", L"Música de Efecto de Sonido 1", L"효과 음악 1", L"音效音乐1", L"موسيقى مؤثرات صوتية 1", L"Звуковая Музыка 1", L"Soundeffekt-Musik 1", L"Música de Efeito Sonoro 1", L"Geluidseffect Muziek 1", L"Muzyka Efektów Dźwiękowych 1", L"Ses Efekti Müziği 1");
							break;
						case 7530:
							a = LL14(L"効果音楽2", L"Sound Effect Music 2", L"Musique d'Effet Sonore 2", L"Musica Effetto Sonoro 2", L"Música de Efecto de Sonido 2", L"효과 음악 2", L"音效音乐2", L"موسيقى مؤثرات صوتية 2", L"Звуковая Музыка 2", L"Soundeffekt-Musik 2", L"Música de Efeito Sonoro 2", L"Geluidseffect Muziek 2", L"Muzyka Efektów Dźwiękowych 2", L"Ses Efekti Müziği 2");
							break;
						case 7531:
							a = LL14(L"効果音楽3", L"Sound Effect Music 3", L"Musique d'Effet Sonore 3", L"Musica Effetto Sonoro 3", L"Música de Efecto de Sonido 3", L"효과 음악 3", L"音效音乐3", L"موسيقى مؤثرات صوتية 3", L"Звуковая Музыка 3", L"Soundeffekt-Musik 3", L"Música de Efeito Sonoro 3", L"Geluidseffect Muziek 3", L"Muzyka Efektów Dźwiękowych 3", L"Ses Efekti Müziği 3");
							break;
						case 7532:
							a = LL14(L"効果音楽4", L"Sound Effect Music 4", L"Musique d'Effet Sonore 4", L"Musica Effetto Sonoro 4", L"Música de Efecto de Sonido 4", L"효과 음악 4", L"音效音乐4", L"موسيقى مؤثرات صوتية 4", L"Звуковая Музыка 4", L"Soundeffekt-Musik 4", L"Música de Efeito Sonoro 4", L"Geluidseffect Muziek 4", L"Muzyka Efektów Dźwiękowych 4", L"Ses Efekti Müziği 4");
							break;
						case 7533:
							a = LL14(L"踏み出す勇気", L"Courage to Step Forward", L"Courage d'Avancer", L"Coraggio di Andare Avanti", L"Valentía para Avanzar", L"내딛는 용기", L"踏出的勇气", L"الشجاعة للتقدم للأمام", L"Смелость Шагнуть Вперёд", L"Mut Voranzugehen", L"Coragem de Dar um Passo", L"Moed om Vooruit te Stappen", L"Odwaga by Ruszyć Naprzód", L"İlerleme Cesareti");
							break;
						case 7534:
							a = LL14(L"その背中を見つめて", L"Watching Your Back", L"Regarder ton Dos", L"Guardare le Tue Spalle", L"Mirando tu Espalda", L"그 뒷모습을 바라보며", L"凝视着那背影", L"مراقبة ظهرك", L"Глядя в Твою Спину", L"Deinen Rücken Beobachten", L"Olhando suas Costas", L"Naar Je Rug Kijken", L"Patrząc na Twoje Plecy", L"Sırtına Bakarak");
							break;
						case 7540:
						case 7541:
						case 7542:
						case 7543:
						case 7544:
							a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
							break;
						case 7550:
							a = LL14(L"オルキスタワー", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"오르키스 타워", L"兰花塔", L"برج أوركيس", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower");
							break;
						case 7551:
							a = L"Catastrophe";
							break;
						case 7552:
							a = LL14(L"碧き雫", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"푸른 물방울", L"碧之雫", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator");
							break;
						case 7553:
							a = LL14(L"神機降臨", L"Divine Mechanoid Descent", L"Descente du Mécanisme Divin", L"Discesa del Meccanismo Divino", L"Descenso del Mecanismo Divino", L"신기강림", L"神机降临", L"نزول الآلية الإلهية", L"Нисхождение Божественного Механизма", L"Abstieg des Göttlichen Mechanoids", L"Descida do Mecanismo Divino", L"Afdaling van het Goddelijke Mechanisme", L"Zstąpienie Boskiego Mechanizmu", L"İlahi Mekanizmanın İnişi");
							break;
						case 7554:
							a = LL14(L"ふるわれる奇蹟", L"Shaking Miracle", L"Miracle Tremblant", L"Miracolo Tremante", L"Milagro Tembloroso", L"뒤흔들리는 기적", L"震撼的奇迹", L"معجزة مهتزة", L"Дрожащее Чудо", L"Erschütterndes Wunder", L"Milagre Tremendo", L"Trillend Wonder", L"Drżący Cud", L"Sarsılan Mucize");
							break;
						case 7555:
							a = LL14(L"予定外の奇蹟", L"Unexpected Miracle", L"Miracle Inattendu", L"Miracolo Inaspettato", L"Milagro Inesperado", L"예정 밖의 기적", L"意料之外的奇迹", L"معجزة غير متوقعة", L"Неожиданное Чудо", L"Unerwartetes Wunder", L"Milagre Inesperado", L"Onverwacht Wonder", L"Nieoczekiwany Cud", L"Beklenmedik Mucize");
							break;
						case 7556:
							a = LL14(L"鋼鉄の咆哮 -脅威-", L"Roar of Steel -Threat-", L"Rugissement d'Acier -Menace-", L"Ruggito d'Acciaio -Minaccia-", L"Rugido de Acero -Amenaza-", L"강철의 포효 -위협-", L"钢铁的咆哮 -威胁-", L"زئير الحديد -تهديد-", L"Рёв Стали -Угроза-", L"Stahlgebrüll -Bedrohung-", L"Rugido de Aço -Ameaça-", L"Staalgebulder -Bedreiging-", L"Ryk Stali -Zagrożenie-", L"Çeliğin Kükremesi -Tehdit-");
							break;
						case 7560:
							a = LL14(L"雨の日の真実", L"Truth on a Rainy Day", L"Vérité un Jour de Pluie", L"Verità in un Giorno di Pioggia", L"Verdad en un Día Lluvioso", L"비 오는 날의 진실", L"雨天的真相", L"الحقيقة في يوم ممطر", L"Правда в Дождливый День", L"Wahrheit an einem Regentag", L"Verdade em um Dia Chuvoso", L"Waarheid op een Regenachtige Dag", L"Prawda w Deszczowy Dzień", L"Yağmurlu Bir Günde Gerçek");
							break;
						case 7561:
							a = LL14(L"不穏", L"Troubled", L"Trouble", L"Turbato", L"Perturbado", L"불온", L"不稳", L"قلق", L"Тревожный", L"Unruhig", L"Perturbado", L"Onrustig", L"Niepokój", L"Huzursuz");
							break;
						case 7562:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
							break;
						case 7563:
							a = LL14(
								L"犠牲の先の希望",                   /* 0: ja */
								L"Hope Beyond Sacrifice",          /* 1: en */
								L"Espoir au-Delà du Sacrifice",    /* 2: fr */
								L"Speranza Oltre il Sacrificio",   /* 3: it */
								L"Esperanza Más Allá del Sacrificio", /* 4: es */
								L"희생 뒤의 희망",                   /* 5: ko */
								L"牺牲之后的希望",                   /* 6: zh */
								L"الأمل بعد التضحية",              /* 7: ar */
								L"Надежда за жертвой",             /* 8: ru (修正点) */
								L"Hoffnung Jenseits des Opfers",   /* 9: de */
								L"Esperança Além do Sacrifício",  /* 10: pt */
								L"Hoop Voorbij Opoffering",        /* 11: nl */
								L"Nadzieja Poza Poświęceniem",     /* 12: pl */
								L"Fedakarlığın Ötesinde Umut"      /* 13: tr */
							);					break;
						case 7564:
							a = L"Strange Feel";
							break;
						case 7565:
							a = L"Exhilarating Ride";
							break;
						case 7566:
							a = LL14(L"それぞれの正義", L"Each One's Justice", L"La Justice de Chacun", L"La Giustizia di Ognuno", L"La Justicia de Cada Uno", L"저마다의 정의", L"各自的正义", L"عدالة كل فرد", L"Справедливость Каждого", L"Gerechtigkeit Jedes Einzelnen", L"A Justiça de Cada Um", L"Ieders Gerechtigheid", L"Sprawiedliwość Każdego", L"Herkesin Adaleti");
							break;
						case 7567:
							a = LL14(L"乗り越えるべき壁", L"Wall to Overcome", L"Mur à Surmonter", L"Muro da Superare", L"Muro a Surperar", L"극복해야 할 벽", L"需要翻越的墙", L"جدار يجب عبوره", L"Стена, Которую Нужно Преодолеть", L"Zu Überwindende Wand", L"Muro a Surperar", L"Muur om te Overwinnen", L"Mur do Pokonania", L"Aşılması Gereken Duvar");
							break;
						case 7568:
							a = LL14(L"月下の想い", L"Feelings Under the Moon", L"Sentiments sous la Lune", L"Sentimenti sotto la Luna", L"Sentimientos bajo la Luna", L"월하의 진심", L"月下的心意", L"مشاعر تحت القمر", L"Чувства под Луной", L"Gefühle unter dem Mond", L"Sentimentos sob a Lua", L"Gevoelens onder de Maan", L"Uczucia pod Księżycem", L"Ay Işığında Duygular");
							break;
						case 7569:
							a = L"Miss You";
							break;
						case 7570:
							a = LL14(L"天の車", L"Chariot of Heaven", L"Char Céleste", L"Carro del Cielo", L"Carro Celestial", L"하늘의 수레", L"天之车轮", L"عربة السماء", L"Небесная Колесница", L"Himmelswagen", L"Carruagem do Céu", L"Hemelse Strijdwagen", L"Niebieski Rydwan", L"Gök Arabası");
							break;
						case 7571:
							a = LL14(L"突きつけられた現実", L"Reality Thrust Upon Us", L"Réalité Imposée", L"Realtà Imposta", L"Realidad Impuesta", L"들이닥친 현실", L"被加诸的现实", L"الحقيقة المفروضة علينا", L"Реальность, Навязанная Нам", L"Uns Aufgezwungene Realität", L"Realidade Imposta", L"Opgelegde Realiteit", L"Narzucona Rzeczywistość", L"Üstümüze Dayatılan Gerçek");
							break;
						case 7572:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
							break;
						case 7573:
							a = LL14(L"全てを識るもの", L"The Omniscient", L"L'Omniscient", L"L'Onnisciente", L"El Omnisciente", L"모든 것을 아는 자", L"无所不知者", L"العليم بكل شيء", L"Всезнающий", L"Der Allwissende", L"O Onisciente", L"De Alwetende", L"Wszechwiedzący", L"Her Şeyi Bilen");
							break;
						case 7574:
							a = LL14(L"想い、辿り着く場所", L"Where Feelings Lead", L"Là où Mènent les Sentiments", L"Dove Portano i Sentimenti", L"Adonde Llevan los Sentimientos", L"마음이 가닿는 곳", L"心意所至之处", L"حيث تقود المشاعر", L"Куда Ведут Чувства", L"Wohin Gefühle Führen", L"Para Onde os Sentimentos Levam", L"Waar Gevoelens Naartoe Leiden", L"Dokąd Prowadzą Uczucia", L"Duyguların Götürdüğü Yer");
							break;
						case 7575:
							a = LL14(L"揺れ動く心", L"Wavering Heart", L"Cœur Vacillant", L"Cuore Vacillante", L"Corazón Vacilante", L"동요하는 마음", L"摇曳的心", L"قلب متردد", L"Колеблющееся Сердце", L"Schwankendes Herz", L"Coração Vacilante", L"Weifelend Hart", L"Chwiejące się Serce", L"Kararsız Kalp");
							break;
						case 7576:
							a = LL14(L"星降る夜に", L"On a Starry Night", L"Par une Nuit Étoilée", L"In una Notte Stellata", L"En una Noche Estrellada", L"별 내리는 밤에", L"星降之夜", L"في ليلة مرصعة بالنجوم", L"В Звёздную Ночь", L"In einer Sternennacht", L"Em uma Noite Estrelada", L"Op een Sterrenachtige Nacht", L"W Gwiaździstą Noc", L"Yıldızlı Bir Gecede");
							break;
						case 7577:
						case 7578:
						case 7579:
						case 7580:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
							break;
						case 7581:
							a = LL14(L"本当の絆", L"True Bonds", L"Vrais Liens", L"Veri Legami", L"Lazos Verdaderos", L"진정한 인연", L"真正的羁绊", L"الروابط الحقيقية", L"Настоящие Узы", L"Wahre Bande", L"Laços Verdadeiros", L"Ware Banden", L"Prawdziwe Więzi", L"Gerçek Bağlar");
							break;
						case 7582:
							a = LL14(L"猛き獣たち", L"Fierce Beasts", L"Bêtes Féroces", L"Bestie Feroci", L"Bestias Feroces", L"사나운 짐승들", L"凶猛的野兽们", L"وحوش ضارية", L"Свирепые Звери", L"Wilde Bestien", L"Bestas Ferozes", L"Woeste Beesten", L"Dzikie Bestie", L"Vahşi Canavarlar");
							break;
						case 7583:
							a = LL14(L"西ゼムリア通商会議", L"West Zemuria Trade Conference", L"Conférence Commerciale de Zemuria Occidentale", L"Conferenza Commerciale della Zemuria Occidentale", L"Conferencia Comercial de Zemuria Occidental", L"서제무리아 통상회의", L"西塞姆利亚通商会议", L"مؤتمر تجارة غرب زيموريا", L"Западно-Земурийская Торговая Конференция", L"Westzemuranische Handelskonferenz", L"Conferência Comercial da Zemuria Ocidental", L"West-Zemuria Handelsconferentie", L"Zachodnia Konferencja Handlowa Zemurii", L"Batı Zemuria Ticaret Konferansı");
							break;
						case 7584:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
							break;
						case 7585:
							a = LL14(L"千年の妄執", L"Obsession of Millennia", L"Obsession des Millénaires", L"Ossessione dei Millenni", L"Obsesión de los Milenios", L"천년의 망집", L"千年的妄执", L"هوس الألفية", L"Одержимость Тысячелетий", L"Obsession der Jahrtausende", L"Obsessão dos Milênios", L"Obsessie van Millennia", L"Obsesja Tysiącleci", L"Bin Yılın Takıntısı");
							break;
						case 7586:
							a = LL14(L"鋼鉄の咆哮 -死線-", L"Roar of Steel -Death Line-", L"Rugissement d'Acier -Ligne de Mort-", L"Ruggito d'Acciaio -Linea della Morte-", L"Rugido de Acero -Línea de Muerte-", L"강철의 포효 -사선-", L"钢铁的咆哮 -死线-", L"زئير الحديد -خط الموت-", L"Рёв Стали -Линия Смерти-", L"Stahlgebrüll -Todeslinie-", L"Rugido de Aço -Linha da Morte-", L"Staalgebulder -Doodslijn-", L"Ryk Stali -Linia Śmierci-", L"Çeliğin Kükremesi -Ölüm Hattı-");
							break;
						case 7587:
							a = LL14(L"ポムっと! -お花見団子の逆襲-", L"Pom! -Cherry Blossom Dango Counterattack-", L"Pom ! -Contre-attaque des Dango de Fleurs de Cerisier-", L"Pom! -Contrattacco dei Dango di Fiori di Ciliegio-", L"¡Pom! -Contraataque de los Dango de Flores de Cerezo-", L"폼앗! -꽃구경 경단의 역습-", L"Pom! -赏花团子的反攻-", L"بوم! -هجوم مضاد لدانغو براعم الكرز-", L"Пом! -Контратака Данго из Цветков Сакуры-", L"Pom! -Gegenangriff der Kirschblüten-Dango-", L"Pom! -Contra-ataque dos Dango de Flor de Cerejeira-", L"Pom! -Tegenaanval van Kersenbloesem Dango-", L"Pom! -Kontratak Dango z Kwiatami Wiśni-", L"Pom! -Kiraz Çiçeği Dango'nun Karşı Saldırısı-");
							break;
						case 7588:
							a = L"Fateful Confrontation -Pom! Ver.-";
							break;
						case 7589:
							a = LL14(L"ポムりますか", L"Shall We Pom?", L"On Pomme ?", L"Facciamo Pom ?", L"¿Hacemos Pom?", L"폼 할까요?", L"来一局Pom吗？", L"هل نلعب بوم؟", L"Сыграем в Пом?", L"Sollen Wir Pom Spielen?", L"Vamos Pom?", L"Zullen We Pomme?", L"Czy Zagramy w Pom?", L"Pom Oynayalım mı?");
							break;
						case 7590:
							a = LL14(L"エリィ絶叫コースター", L"Elie Scream Coaster", L"Montagnes Russes des Cris d'Elie", L"Montagne Russe delle Urla di Elie", L"Montaña Rusa de los Gritos de Elie", L"에리 절규 코스터", L"艾莉尖叫云霄飞车", L"أفعوانية صرخة إيلي", L"Американские Горки Воплей Эли", L"Elie-Schrei-Achterbahn", L"Montanha-russa dos Gritos de Elie", L"Elie Schreeuw Achtbaan", L"Kolejka Krzyków Elie", L"Elie Çığlık Roller Coaster");
							break;
						case 7591:
							a = LL14(L"小さな英雄 -オルゴール-", L"Little Hero -Music Box-", L"Petit Héros -Boîte à Musique-", L"Piccolo Eroe -Carillon-", L"Pequeño Héroe -Caja de Música-", L"작은 영웅 -오르골-", L"小小英雄 -音乐盒-", L"البطل الصغير -صندوق الموسيقى-", L"Маленький Герой -Музыкальная Шкатулка-", L"Kleiner Held -Spieluhr-", L"Pequeno Herói -Caixa de Música-", L"Kleine Held -Muziekdoos-", L"Mały Bohater -Pozytywka-", L"Küçük Kahraman -Müzik Kutusu-");
							break;
						case 7592:
							a = L"TOWER OF THE SHADOW OF DEATH -Jukebox-";
							break;
						}
						_tcscpy(p.name, a);
					}
					_tcscpy(p.fol, fname1);
					p.loop1 = oggL1;
					p.loop2 = oggL2;
						}
				else if (fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1).Mid(0,3) == L"ed8" && (ft.Right(4) == ".wav")) {
					p.sub = 21; p.loop1 = p.loop2 = 0;
					CString a = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					switch (_ttoi(a.Mid(2, 4))) {
					case 8001:
						a = LL14(L"特科クラス《VII組》", L"Class VII", L"Classe VII", L"Classe VII", L"Clase VII", L"특과 클래스 《VII반》", L"特科班《VII组》", L"الفئة السابعة", L"Класс VII", L"Klasse VII", L"Classe VII", L"Klasse VII", L"Klasa VII", L"Sınıf VII");
						break;
					case 8002:
						a = LL14(L"ただひたすらに、前へ", L"Ever Forward", L"Toujours de l'Avant", L"Sempre Avanti", L"Siempre Adelante", L"오직 한결같이, 앞으로", L"唯有向前", L"دائما إلى الأمام", L"Всегда Вперёд", L"Immer Vorwärts", L"Sempre em Frente", L"Altijd Vooruit", L"Zawsze Naprzód", L"Hep İleri");
						break;
					case 8100:
						a = LL14(L"近郊都市トリスタ", L"Suburban City Trista", L"Ville de Banlieue Trista", L"Città Suburbana Trista", L"Ciudad Suburbana Trista", L"근교 도시 트리스타", L"近郊城市特里斯塔", L"مدينة تريستا الضاحية", L"Пригородный Город Триста", L"Vorortstadt Trista", L"Cidade Suburbana Trista", L"Buitenstad Trista", L"Miasto Podmiejskie Trista", L"Banliyö Şehri Trista");
						break;
					case 8101:
						a = LL14(L"交易町ケルディック", L"Trading Town Celdic", L"Ville Marchande Celdic", L"Città Commerciale Celdic", L"Ciudad Comercial Celdic", L"교역 마을 켈딕", L"交易小镇塞尔迪克", L"بلدة سيلديك التجارية", L"Торговый Город Селдик", L"Handelsstadt Celdic", L"Cidade Comercial Celdic", L"Handelsstad Celdic", L"Miasto Handlowe Celdic", L"Ticaret Kasabası Celdic");
						break;
					case 8102:
						a = LL14(L"翡翠の公都バリアハート", L"Jade Capital Bareahard", L"Capitale de Jade Bareahard", L"Capitale di Giada Bareahard", L"Capital de Jade Bareahard", L"비취의 공도 바리아하트", L"翡翠公都巴里亚哈特", L"عاصمة اليشم باري هارد", L"Нефритовая Столица Бэрихард", L"Jade-Hauptstadt Bareahard", L"Capital de Jade Bareahard", L"Jade-Hoofdstad Bareahard", L"Jadeitowa Stolica Bareahard", L"Yeşim Başkenti Bareahard");
						break;
					case 8103:
						a = LL14(L"湖畔の街レグラム", L"Lakeside Town Legram", L"Ville au Bord du Lac Legram", L"Città Lacustre Legram", L"Ciudad Junto al Lago Legram", L"호반의 거리 레그람", L"湖畔小镇勒格拉姆", L"بلدة ليغرام بجانب البحيرة", L"Город у Озера Леграм", L"Seestadt Legram", L"Cidade à Beira do Lago Legram", L"Meerstad Legram", L"Miasto nad Jeziorem Legram", L"Göl Kenarı Kasabası Legram");
						break;
					case 8104:
						a = LL14(L"黒銀の鋼都ルーレ", L"Iron City Roer", L"Cité d'Acier Roer", L"Città d'Acciaio Roer", L"Ciudad de Acero Roer", L"흑은의 강도 루레", L"黑银钢都卢雷", L"مدينة روير الحديدية", L"Стальной Город Рур", L"Stahlstadt Roer", L"Cidade de Aço Roer", L"Staalstad Roer", L"Stalowe Miasto Roer", L"Çelik Şehri Roer");
						break;
					case 8106:
						a = LL14(L"遊牧民の集落", L"Nomad Settlement", L"Campement Nomade", L"Accampamento Nomade", L"Asentamiento Nómada", L"유목민의 집락", L"游牧民聚落", L"مستوطنة البدو", L"Поселение Кочевников", L"Nomadensiedlung", L"Assentamento Nômade", L"Nomadennederzetting", L"Osada Koczowników", L"Göçebe Yerleşimi");
						break;
					case 8107:
						a = LL14(L"緋の帝都ヘイムダル", L"Crimson Capital Heimdallr", L"Capitale Cramoisie Heimdallr", L"Capitale Cremisi Heimdallr", L"Capital Carmesí Heimdallr", L"비의 제도 헤임달", L"绯之帝都海姆达尔", L"عاصمة القرمزي هايمدال", L"Малиновая Столица Хеймдалл", L"Purpurrote Hauptstadt Heimdallr", L"Capital Carmesim Heimdallr", L"Karmozijnrode Hoofdstad Heimdallr", L"Karmazynowa Stolica Heimdallr", L"Kırmızı Başkent Heimdallr");
						break;
					case 8108:
						a = LL14(L"癒しの我が家", L"Healing Home", L"Foyer Apaisant", L"Casa Guaritrice", L"Hogar Sanador", L"치유의 우리 집", L"治愈的家", L"المنزل الشافي", L"Исцеляющий Дом", L"Heilendes Zuhause", L"Lar Curador", L"Helend Thuis", L"Uzdrawiający Dom", L"İyileştirici Ev");
						break;
					case 8109:
						a = LL14(L"ダイニングバー《F》", L"Dining Bar F", L"Bar-Restaurant F", L"Dining Bar F", L"Bar Comedor F", L"다이닝 바 《F》", L"餐厅酒吧《F》", L"بار الطعام F", L"Обеденный Бар F", L"Dining Bar F", L"Bar Restaurante F", L"Dining Bar F", L"Bar Restauracyjny F", L"Yemek Barı F");
						break;
					case 8110:
						a = LL14(L"常在戦場の気概", L"Ever-Present War Spirit", L"Esprit de Guerre Omniprésent", L"Spirito di Guerra Onnipresente", L"Espíritu de Guerra Omnipresente", L"상재전장의 기개", L"常在战场的气概", L"روح الحرب الدائمة", L"Вечный Боевой Дух", L"Allgegenwärtiger Kriegsgeist", L"Espírito de Guerra Onipresente", L"Altijd Aanwezige Oorlogsgeest", L"Wszechobecny Duch Wojenny", L"Her Zaman Savaş Ruhu");
						break;
					case 8111:
						a = LL14(L"ガレリアの巨壁", L"Garelia Fortress", L"Forteresse de Garelia", L"Fortezza di Garelia", L"Fortaleza de Garelia", L"가렐리아의 거벽", L"加勒利亚巨壁", L"قلعة غاريليا", L"Крепость Гарелия", L"Festung Garelia", L"Fortaleza de Garelia", L"Vesting Garelia", L"Twierdza Garelia", L"Garelia Kalesi");
						break;
					case 8120:
						a = LL14(L"足湯の温もり", L"Foot Bath Warmth", L"Chaleur du Bain de Pieds", L"Calore del Pediluvio", L"Calidez del Baño de Pies", L"족탕의 온기", L"足浴的温暖", L"دفء حمام القدمين", L"Тепло Ножной Ванны", L"Wärme des Fußbades", L"Calor do Banho de Pés", L"Warmte van het Voetbad", L"Ciepło Kąpieli Stóp", L"Ayak Banyosunun Sıcaklığı");
						break;
					case 8121:
						a = LL14(L"静寂の郷", L"Silent Village", L"Village Silencieux", L"Villaggio Silenzioso", L"Pueblo Silencioso", L"정적의 고을", L"静寂之乡", L"القرية الصامتة", L"Тихая Деревня", L"Stilles Dorf", L"Vila Silenciosa", L"Stil Dorp", L"Ciche Miasteczko", L"Sessiz Köy");
						break;
					case 8122:
						a = LL14(L"明日への休息", L"Rest for Tomorrow", L"Repos pour Demain", L"Riposo per Domani", L"Descanso para Mañana", L"내일로의 휴식", L"为明日而休息", L"راحة ليوم غد", L"Отдых ради Завтра", L"Ruhe für Morgen", L"Descanso para Amanhã", L"Rust voor Morgen", L"Odpoczynek na Jutro", L"Yarın İçin Dinlenme");
						break;
					case 8123:
						a = LL14(L"春の陽射し", L"Spring Sunshine", L"Soleil de Printemps", L"Sole Primaverile", L"Sol de Primavera", L"봄의 햇살", L"春日阳光", L"أشعة شمس الربيع", L"Весеннее Солнце", L"Frühlingssonne", L"Sol de Primavera", L"Lentezonnestralen", L"Wiosenne Słońce", L"İlkbahar Güneşi");
						break;
					case 8125:
						a = LL14(L"カレイジャス発進！", L"Courageous Launch!", L"Décollage du Courageux !", L"Lancio del Courageous!", L"¡Lanzamiento del Courageous!", L"카레이저스 발진!", L"无畏号出发！", L"انطلاق الشجاعة!", L"Старт Отважного!", L"Courageous startet!", L"Lançamento do Courageous!", L"Courageous lanceert!", L"Start Courageous!", L"Courageous Fırlatıldı!");
						break;
					case 8126:
						a = LL14(L"目覚める意志", L"Awakening Will", L"Volonté qui s'Éveille", L"Volontà che si Risveglia", L"Voluntad que Despierta", L"깨어나는 의지", L"觉醒的意志", L"الإرادة المستيقظة", L"Пробуждающаяся Воля", L"Erwachender Wille", L"Vontade que Desperta", L"Ontwakende Wil", L"Przebudzająca się Wola", L"Uyanış İradesi");
						break;
					case 8127:
						a = LL14(L"白銀の巨船", L"Silver Ship", L"Vaisseau d'Argent", L"Nave d'Argento", L"Nave de Plata", L"백은의 거선", L"白银巨船", L"السفينة الفضية", L"Серебряный Корабль", L"Silbernes Schiff", L"Navio de Prata", L"Zilveren Schip", L"Srebrny Okręt", L"Gümüş Gemi");
						break;
					case 8150:
						a = LL14(L"放課後の時間", L"After School", L"Après l'École", L"Dopo Scuola", L"Después de la Escuela", L"방과 후의 시간", L"放学后的时光", L"وقت ما بعد المدرسة", L"После Уроков", L"Nach der Schule", L"Depois da Escola", L"Na School", L"Po Szkole", L"Okul Sonrası");
						break;
					case 8152:
						a = LL14(L"さわやかな朝", L"Refreshing Morning", L"Matin Rafraîchissant", L"Mattino Rinfrescante", L"Mañana Refrescante", L"상쾌한 아침", L"清爽的早晨", L"صباح منعش", L"Бодрящее Утро", L"Erfrischender Morgen", L"Manhã Refrescante", L"Verfrissende Ochtend", L"Orzeźwiający Poranek", L"Ferah Sabah");
						break;
					case 8153:
						a = LL14(L"雨音の学院", L"Rain-sound Academy", L"Académie sous la Pluie", L"Accademia della Pioggia", L"Academia Bajo la Lluvia", L"빗소리의 학원", L"雨声学院", L"الأكاديمية تحت المطر", L"Академия Дождя", L"Regen-Akademie", L"Academia da Chuva", L"Regen-Academie", L"Akademia Deszczu", L"Yağmur Sesi Akademisi");
						break;
					case 8154:
						a = LL14(L"爽やかな陽射し", L"Clear Sunshine", L"Soleil Clair", L"Sole Limpido", L"Sol Despejado", L"상쾌한 햇살", L"清爽的阳光", L"أشعة الشمس الصافية", L"Ясное Солнце", L"Klarer Sonnenschein", L"Sol Claro", L"Helder Zonlicht", L"Jasne Słońce", L"Berrak Güneş Işığı");
						break;
					case 8156:
						a = LL14(L"トールズ士官学院祭", L"Thors Academy Festival", L"Festival de l'Académie Thors", L"Festival dell'Accademia Thors", L"Festival de la Academia Thors", L"토르즈 사관학원제", L"托尔斯士官学院祭", L"مهرجان أكاديمية ثورز", L"Праздник Академии Торс", L"Thors-Akademie-Festival", L"Festival da Academia Thors", L"Thors Academie Festival", L"Festiwal Akademii Thors", L"Thors Akademisi Festivali");
						break;
					case 8158:
						a = LL14(L"青空の開放感", L"Open Sky", L"Ciel Ouvert", L"Cielo Aperto", L"Cielo Abierto", L"푸른 하늘의 개방감", L"蓝天的开放感", L"السماء المفتوحة", L"Открытое Небо", L"Offener Himmel", L"Céu Aberto", L"Open Lucht", L"Otwarte Niebo", L"Açık Gökyüzü");
						break;
					case 8159:
						a = LL14(L"自由行動日", L"Free Day", L"Journée Libre", L"Giorno Libero", L"Día Libre", L"자유 행동일", L"自由行动日", L"يوم حر", L"Свободный День", L"Freier Tag", L"Dia Livre", L"Vrije Dag", L"Wolny Dzień", L"Serbest Gün");
						break;
					case 8200:
						a = LL14(L"異郷の空", L"Foreign Sky", L"Ciel Étranger", L"Cielo Straniero", L"Cielo Extranjero", L"이향의 하늘", L"异乡的天空", L"سماء غريبة بعيدة", L"Чужое Небо", L"Fremder Himmel", L"Céu Estrangeiro", L"Vreemde Hemel", L"Obce Niebo", L"Yabancı Gökyüzü");
						break;
					case 8201:
						a = LL14(L"峡谷道を往く", L"Through the Canyon", L"À Travers le Canyon", L"Attraverso il Canyon", L"A Través del Cañón", L"협곡길을 가다", L"穿越峡谷之道", L"عبر الوادي", L"Сквозь Каньон", L"Durch die Schlucht", L"Através do Canyon", L"Door de Kloof", L"Przez Kanion", L"Kanyondan Geçerek");
						break;
					case 8202:
						a = LL14(L"精霊の小道", L"Spirit Path", L"Chemin des Esprits", L"Sentiero degli Spiriti", L"Senda de los Espíritus", L"정령의 오솔길", L"精灵小道", L"طريق الأرواح", L"Тропа Духов", L"Geisterpfad", L"Caminho dos Espíritos", L"Geestenpad", L"Ścieżka Duchów", L"Ruh Yolu");
						break;
					case 8203:
						a = LL14(L"蒼穹の大地", L"Azure Skies Land", L"Terre du Ciel Azuré", L"Terra del Cielo Azzurro", L"Tierra del Cielo Azul", L"창궁의 대지", L"苍穹大地", L"أرض السماء الزرقاء", L"Земля Лазурного Неба", L"Land des Azurhimmels", L"Terra do Céu Azul", L"Land van de Azuurblauwe Lucht", L"Kraina Lazurowego Nieba", L"Gök Mavisi Topraklar");
						break;
					case 8210:
						a = LL14(L"戦火を越えて", L"Beyond the Flames of War", L"Au-Delà des Flammes de la Guerre", L"Oltre le Fiamme della Guerra", L"Más Allá de las Llamas de la Guerra", L"전화를 넘어", L"超越战火", L"ما وراء لهيب الحرب", L"За Пламенем Войны", L"Jenseits der Kriegsflammen", L"Além das Chamas da Guerra", L"Voorbij de Vlammen van de Oorlog", L"Poza Płomieniami Wojny", L"Savaşın Alevlerinin Ötesinde");
						break;
					case 8212:
						a = L"Trudge Along";
						break;
					case 8213:
						a = LL14(L"冬の訪れ", L"Arrival of Winter", L"Arrivée de l'Hiver", L"Arrivo dell'Inverno", L"Llegada del Invierno", L"겨울의 방문", L"冬日将来", L"وصول الشتاء", L"Приход Зимы", L"Ankunft des Winters", L"Chegada do Inverno", L"Komst van de Winter", L"Nadejście Zimy", L"Kışın Gelişi");
						break;
					case 8300:
						a = LL14(L"旧校舎の謎", L"Old Schoolhouse Mystery", L"Mystère de l'Ancienne École", L"Mistero della Vecchia Scuola", L"Misterio del Antiguo Edificio Escolar", L"구교사의 수수께끼", L"旧校舍之谜", L"سر مبنى المدرسة القديم", L"Загадка Старого Корпуса", L"Geheimnis des alten Schulgebäudes", L"Mistério do Antigo Prédio Escolar", L"Mysterie van het Oude Schoolgebouw", L"Tajemnica Starego Budynku Szkolnego", L"Eski Okul Binasının Gizemi");
						break;
					case 8301:
						a = LL14(L"探索", L"Exploration", L"Exploration", L"Esplorazione", L"Exploración", L"탐색", L"探索", L"استكشاف", L"Исследование", L"Erkundung", L"Exploração", L"Verkenning", L"Eksploracja", L"Keşif");
						break;
					case 8302:
						a = LL14(L"深淵へ向かう", L"Toward the Abyss", L"Vers l'Abîme", L"Verso l'Abisso", L"Hacia el Abismo", L"심연을 향해", L"走向深渊", L"نحو الهاوية", L"К Бездне", L"In den Abgrund", L"Rumo ao Abismo", L"Naar de Afgrond", L"Ku Otchłani", L"Uçuruma Doğru");
						break;
					case 8303:
						a = LL14(L"聖女の城", L"Saint's Castle", L"Château de la Sainte", L"Castello della Santa", L"Castillo de la Santa", L"성녀의 성", L"圣女之城", L"قلعة القديسة", L"Замок Святой", L"Schloss der Heiligen", L"Castelo da Santa", L"Kasteel van de Heilige", L"Zamek Świętej", L"Aziz Kale");
						break;
					case 8304:
						a = LL14(L"明日を掴むために", L"To Seize Tomorrow", L"Pour Saisir Demain", L"Per Afferrare il Domani", L"Para Aferrar el Mañana", L"내일을 잡기 위해", L"为了抓住明日", L"لإمساك الغد", L"Чтобы Схватить Завтра", L"Um Morgen zu Greifen", L"Para Agarrar o Amanhã", L"Om Morgen te Grijpen", L"By Pochwycić Jutro", L"Yarını Yakalamak İçin");
						break;
					case 8305:
						a = LL14(L"地下に眠る遺構", L"Ruins Beneath", L"Ruines Souterraines", L"Rovine Sotterranee", L"Ruinas Subterráneas", L"지하에 잠든 유구", L"眠地下的遗构", L"الأطلال تحت الأرض", L"Подземные Руины", L"Unterirdische Ruinen", L"Ruínas Subterrâneas", L"Ondergrondse Ruines", L"Podziemne Ruiny", L"Yeraltındaki Harabeler");
						break;
					case 8308:
						a = LL14(L"世の礎たるために", L"To Be the World's Foundation", L"Pour Être le Fondement du Monde", L"Per Essere il Fondamento del Mondo", L"Para Ser el Fundamento del Mundo", L"세상의 초석이 되기 위해", L"成为世界的基石", L"لنكون أساس العالم", L"Чтобы Стать Основой Мира", L"Um das Fundament der Welt zu Sein", L"Para Ser o Fundamento do Mundo", L"Om het Fundament van de Wereld te Zijn", L"By Być Fundamentem Świata", L"Dünyanın Temeli Olmak İçin");
						break;
					case 8310:
						a = LL14(L"精霊窟", L"Spirit Cave", L"Grotte des Esprits", L"Grotta degli Spiriti", L"Cueva de los Espíritus", L"정령굴", L"精灵窟", L"كهف الأرواح", L"Пещера Духов", L"Geisterhöhle", L"Caverna dos Espíritos", L"Geestesgrot", L"Jaskinia Duchów", L"Ruh Mağarası");
						break;
					case 8311:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
						break;
					case 8312:
						a = L"Phantasmal Blaze";
						break;
					case 8313:
						a = LL14(L"夢幻回廊", L"Phantasmagoria Corridor", L"Couloir Fantasmagorique", L"Corridoio Fantasmagorico", L"Corredor Fantasmagórico", L"몽환 회랑", L"梦幻回廊", L"رواق الفانتازيا", L"Фантасмагорический Коридор", L"Phantasmagorischer Korridor", L"Corredor Fantasmagórico", L"Fantasmagorische Gang", L"Fantasmagoryczny Korytarz", L"Fantazmagori Koridoru");
						break;
					case 8315:
						a = LL14(L"幻煌", L"Phantom Radiance", L"Éclat Fantôme", L"Splendore Fantasma", L"Resplandor Fantasma", L"환황", L"幻煌", L"التألق الخيالي", L"Призрачное Сияние", L"Phantomglanz", L"Resplendor Fantasma", L"Fantoomglinstering", L"Blask Widma", L"Hayalet Işıltı");
						break;
					case 8400:
						a = L"The Glint of Cold Steel";
						break;
					case 8401:
						a = L"Tie a Link of ARCUS!";
						break;
					case 8402:
						a = L"Belief";
						break;
					case 8403:
						a = L"Even if Driven to the Wall";
						break;
					case 8404:
						a = L"Eliminate Crisis!";
						break;
					case 8405:
						a = L"Exceed!";
						break;
					case 8406:
						a = L"Don't be Defeated by a Friend!";
						break;
					case 8407:
						a = L"Machinery Attack";
						break;
					case 8408:
						a = LL14(L"巨イナルチカラ", L"Colossal Power", L"Puissance Colossale", L"Potere Colossale", L"Poder Colosal", L"거대한 힘", L"巨大的力量", L"قوة هائلة", L"Колоссальная Сила", L"Kolossale Kraft", L"Poder Colossal", L"Kolossale Kracht", L"Kolosalna Siła", L"Devasa Güç");
						break;
					case 8409:
						a = L"The Decisive Collision";
						break;
					case 8410:
						a = LL14(L"この手で道を切り拓く!", L"Carve Our Path with These Hands!", L"Traçons Notre Chemin de Ces Mains !", L"Tracciamo il Nostro Cammino con Queste Mani!", L"¡Abramos Nuestro Camino con Estas Manos!", L"이 손으로 길을 개척한다!", L"用这双手开拓道路!", L"سنشق طريقنا بأيدينا!", L"Проложим Путь Этими Руками!", L"Mit diesen Händen unseren Weg bahnen!", L"Abrir Nosso Caminho com Estas Mãos!", L"Ons Pad Banen met Deze Handen!", L"Torujemy Drogę Tymi Rękami!", L"Bu Ellerle Yolumuzu Açalım!");
						break;
					case 8411:
						a = LL14(L"赤点です...", L"Failed...", L"Échec...", L"Fallito...", L"Reprobado...", L"낙제입니다...", L"挂科了...", L"فشل...", L"Провалено...", L"Durchgefallen...", L"Reprovado...", L"Gezakt...", L"Oblany...", L"Başarısız...");
						break;
					case 8412:
						a = L"Unknown Threat";
						break;
					case 8413:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
						break;
					case 8420:
						a = L"Heated Mind";
						break;
					case 8421:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
						break;
					case 8423:
						a = L"Impatient";
						break;
					case 8424:
						a = L"Severe Blow";
						break;
					case 8426:
						a = L"Transcend Beat";
						break;
					case 8429:
						a = L"Blue Destination";
						break;
					case 8430:
						a = L"Heteromorphy";
						break;
					case 8431:
						a = LL14(L"輝ける明日へ", L"Toward a Shining Tomorrow", L"Vers un Lendemain Radieux", L"Verso un Domani Splendente", L"Hacia un Mañana Brillante", L"빛나는 내일을 향해", L"走向光辉的明天", L"نحو غد مشرق", L"К Сияющему Завтра", L"Einem Strahlenden Morgen Entgegen", L"Rumo a um Amanhã Brilhante", L"Naar een Stralende Toekomst", L"Ku Jaśniejszemu Jutru", L"Parlayan Bir Yarın Doğu");
						break;
					case 8435:
						a = LL14(L"迫る巨影", L"Approaching Giant Shadow", L"Ombre Géante qui Approche", L"Ombra Gigante che si Avvicina", L"Sombra Gigante que se Acerca", L"다가오는 거영", L"逼近的巨影", L"الظل العملاق يقترب", L"Приближающаяся Гигантская Тень", L"Nahende Riesenschatten", L"Sombra Gigante se Aproximando", L"Naderende Reusachtige Schaduw", L"Zbliżający się Ogromny Cień", L"Yaklaşan Dev Gölge");
						break;
					case 8441:
						a = L"E.O.V";
						break;
					case 8442:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
						break;
					case 8500:
						a = L"Strain";
						break;
					case 8501:
						a = LL14(L"夜のひととき", L"Nighttime", L"Un Moment Nocturne", L"Momento Notturno", L"Un Momento Nocturno", L"밤의 한때", L"夜晚的片刻", L"وقت الليل", L"Ночное Время", L"Nachtzeit", L"Um Momento Noturno", L"Nachtelijk Moment", L"Nocna Chwila", L"Gece Vakti");
						break;
					case 8502:
						a = LL14(L"トラブル発生", L"Trouble", L"Problème Survenu", L"Problema Sorto", L"Problema Surgido", L"트러블 발생", L"麻烦发生", L"حدثت مشكلة", L"Возникли Неприятности", L"Ärger", L"Problema Surgido", L"Probleem Opgetreden", L"Kłopoty", L"Sorun Çıktı");
						break;
					case 8503:
						a = LL14(L"鉄路遥々", L"Distant Iron Road", L"Longue Route de Fer", L"Lunga Strada di Ferro", L"Larga Ruta de Hierro", L"철로 아득히", L"遥远的铁路", L"طريق حديدي بعيد", L"Далёкий Железный Путь", L"Weite Eisenstraße", L"Longa Estrada de Ferro", L"Verre IJzeren Weg", L"Daleka Żelazna Droga", L"Uzak Demir Yolu");
						break;
					case 8504:
						a = LL14(L"旅愁", L"Travel Melancholy", L"Mélancolie du Voyage", L"Malinconia del Viaggio", L"Melancolía del Viaje", L"여수", L"旅途旅愁", L"حزن السفر", L"Дорожная Меланхолия", L"Reisemelancholie", L"Melancolia da Viagem", L"Reismelancholie", L"Melancholia Podróży", L"Yolculuk Hüznü");
						break;
					case 8505:
						a = LL14(L"皇城にて", L"At the Imperial Castle", L"Au Château Impérial", L"Al Castello Imperiale", L"En el Castillo Imperial", L"황성에서", L"在皇城", L"في القصر الإمبراطوري", L"В Императорском Замке", L"Im Kaiserlichen Schloss", L"No Castelo Imperial", L"In het Keizerlijke Kasteel", L"W Cesarskim Zamku", L"İmparatorluk Kalesinde");
						break;
					case 8506:
						a = L"Let's Study";
						break;
					case 8507:
						a = LL14(L"知恵を絞って", L"Rack Your Brains", L"Se Creuser la Tête", L"Sforzarsi di Pensare", L"Exprimirse el Cerebro", L"지혜를 짜내어", L"尽心竭力", L"عصر الأفكار", L"Напрячь Мозги", L"Den Kopf Zerbrechen", L"Quebrar a Cabeça", L"Hersens Pijnigen", L"Wytężając Umysł", L"Beyin Fırtınası");
						break;
					case 8508:
						a = LL14(L"実技教練", L"Combat Training", L"Entraînement au Combat", L"Addestramento al Combattimento", L"Entrenamiento de Combate", L"실기교련", L"实技教练", L"تدريب قتالي", L"Боевая Тренировка", L"Kampftraining", L"Treinamento de Combate", L"Gevechtstraining", L"Trening Bojowy", L"Muharebe Eğitimi");
						break;
					case 8509:
						a = LL14(L"寮に帰ろう", L"Back to the Dorm", L"Retour au Dortoir", L"Ritorno al Dormitorio", L"De Vuelta al Dormitorio", L"기숙사로 돌아가자", L"回宿舍去吧", L"العودة إلى السكن", L"Обратно в Общежитие", L"Zurück zum Wohnheim", L"De Volta ao Dormitório", L"Terug naar het Dorm", L"Z Powrotem do Akademika", L"Yurda Dönelim");
						break;
					case 8510:
						a = LL14(L"アーベントタイム", L"Evening Time", L"Soirée", L"Ora della Sera", L"Hora de la Tarde", L"아벤트 타임", L"黄昏时光", L"وقت المساء", L"Вечернее Время", L"Abendzeit", L"Hora da Tarde", L"Avondtijd", L"Czas Wieczoru", L"Akşam Vakti");
						break;
					case 8512:
						a = LL14(L"鉄の統率", L"Iron Command", L"Commandement de Fer", L"Comando di Ferro", L"Mando de Hierro", L"철의 통솔", L"铁的统率", L"القيادة الحديدية", L"Железное Командование", L"Eiserner Befehl", L"Comando de Ferro", L"IJzeren Bevel", L"Żelazne Dowodzenie", L"Demir Komuta");
						break;
					case 8513:
						a = LL14(L"暗躍", L"Moving in the Shadows", L"Agissant dans l'Ombre", L"Agendo nell'Ombra", L"Actuando en las Sombras", L"암약", L"暗中活跃", L"التحرك في الظلال", L"Действия в Тени", L"Im Verborgenen Agieren", L"Movendo-se nas Sombras", L"In het Donker Bewegen", L"Działanie w Cieniu", L"Gölgede Hareket");
						break;
					case 8514:
						a = LL14(L"想いの行き先", L"Where Feelings Lead", L"Là où Mènent les Sentiments", L"Dove Portano i Sentimenti", L"Adonde Llevan los Sentimientos", L"진심이 가는 곳", L"心意所去之处", L"حيث تقود المشاعر", L"Куда Ведут Чувства", L"Wohin Gefühle Führen", L"Para Onde os Sentimentos Levam", L"Waar Gevoelens Naartoe Leiden", L"Dokąd Prowadzą Uczucia", L"Duyguların Götürdüğü Yer");
						break;
					case 8515:
						a = LL14(L"傷心", L"Heartbreak", L"Cœur Brisé", L"Cuore Spezzato", L"Corazón Roto", L"상심", L"伤心", L"كسر القلب", L"Разбитое Сердце", L"Herzschmerz", L"Coração Partido", L"Hartenpijn", L"Złamane Serce", L"Kırık Kalp");
						break;
					case 8516:
						a = LL14(L"揺らめく炎を見つめて", L"Watching the Flickering Flames", L"Regarder les Flammes Vacillantes", L"Guardare le Fiamme Tremolanti", L"Mirando las Llamas Parpadeantes", L"어른거리는 불꽃을 바라보며", L"凝视着摇曳的火焰", L"مراقبة اللهب المتذبذب", L"Глядя на Мерцающее Пламя", L"Die Flackernden Flammen Beobachten", L"Observando as Chamas Oscilantes", L"De Flakkerende Vlammen Bekijken", L"Wpatrując się w Migoczące Płomienie", L"Titreyen Alevlere Bakarken");
						break;
					case 8517:
						a = LL14(L"一途な気持ち", L"Single-minded Feelings", L"Sentiments Sincères", L"Sentimenti Sinceri", L"Sentimientos Sinceros", L"한결같은 마음", L"一心一意的心情", L"مشاعر مخلصة", L"Искренние Чувства", L"Aufrichtige Gefühle", L"Sentimentos Sinceros", L"Oprechte Gevoelens", L"Szczere Uczucia", L"Tek Yönlü Duygular");
						break;
					case 8520:
						a = LL14(L"臨戦態勢", L"Combat Ready", L"Prêt au Combat", L"Pronto al Combattimento", L"Listo para el Combate", L"임전태세", L"战备状态", L"الاستعداد للقتال", L"Боевая Готовность", L"Kampfbereit", L"Pronto para o Combate", L"Gevechtsklaar", L"Gotowość Bojowa", L"Muharebe Hazırlığı");
						break;
					case 8521:
						a = L"Seriousness";
						break;
					case 8522:
						a = LL14(L"静かなる昂揚", L"Quiet Exhilaration", L"Exaltation Silencieuse", L"Esaltazione Silenziosa", L"Exaltación Silenciosa", L"조용한 고양", L"静静的昂扬", L"النشوة الهادئة", L"Тихое Воодушевление", L"Stille Begeisterung", L"Exaltação Silenciosa", L"Stille Opwinding", L"Cicha Ekscytacja", L"Sessiz Coşku");
						break;
					case 8523:
						a = LL14(L"暖かな夕餉", L"Warm Dinner", L"Dîner Chaleureux", L"Cena Calda", L"Cena Cálida", L"따뜻한 저녁 식사", L"温暖的晚餐", L"عشاء دافئ", L"Тёплый Ужин", L"Warmes Abendessen", L"Jantar Caloroso", L"Warm Avondeten", L"Ciepła Kolacja", L"Sıcak Akşam Yemeği");
						break;
					case 8524:
						a = L"Atrocious Raid";
						break;
					case 8525:
						a = LL14(L"全てを賭して今、ここに立つ", L"Standing Here, Betting Everything", L"Debout Ici, Tout Misant", L"In Piedi Qui, Scommettendo Tutto", L"De Pie Aquí, Apostándolo Todo", L"모든 것을 걸고 지금, 여기에 선다", L"押上一切，此刻站在这里", L"أقف هنا مراهناً على كل شيء", L"Стоя Здесь, Ставя Всё на Кон", L"Hier Stehend, Alles Einsetzend", L"Aqui de Pé, Apostando Tudo", L"Hier Staand, Alles Inzettend", L"Stojąc tu, Stawiając Wszystko na Szali", L"Burada Durarak Her Şeyi Bahse Girerek");
						break;
					case 8527:
						a = LL14(L"新しい仲間たち", L"New Comrades", L"Nouveaux Camarades", L"Nuovi Compagni", L"Nuevos Compañeros", L"새로운 동료들", L"新的伙伴们", L"رفاق جدد", L"Новые Товарищи", L"Neue Kameraden", L"Novos Camaradas", L"Nieuwe Kameraden", L"Nowi Towarzysze", L"Yeni Yoldaşlar");
						break;
					case 8528:
						a = LL14(L"不透明な事態", L"Opaque Situation", L"Situation Opaque", L"Situazione Opaca", L"Situación Opaca", L"불투명한 사태", L"不透明的局面", L"وضع غامض", L"Непрозрачная Ситуация", L"Undurchsichtige Lage", L"Situação Opaca", L"Ondoorzichtige Situatie", L"Niejasna Sytuacja", L"Belirsiz Durum");
						break;
					case 8529:
						a = LL14(L"鉄血へのレクイエム", L"Requiem for Iron and Blood", L"Requiem pour le Fer et le Sang", L"Requiem per il Ferro e il Sangue", L"Réquiem por el Hierro y la Sangre", L"철혈로의 레퀴엠", L"献给铁与血的安魂曲", L"قداس من أجل الحديد والدم", L"Реквием по Железу и Крови", L"Requiem für Eisen und Blut", L"Requiem pelo Ferro e pelo Sangue", L"Requiem voor IJzer en Bloed", L"Requiem dla Żelaza i Krwi", L"Demir ve Kan İçin Requiem");
						break;
					case 8530:
						a = LL14(L"幻想の唄 -PHANTASMAGORIA-", L"Phantom Song -PHANTASMAGORIA-", L"Chant Fantôme -PHANTASMAGORIA-", L"Canto Fantasma -PHANTASMAGORIA-", L"Canción Fantasma -PHANTASMAGORIA-", L"환상의 노래 -PHANTASMAGORIA-", L"幻想之歌 -PHANTASMAGORIA-", L"أغنية خيالية -PHANTASMAGORIA-", L"Призрачная Песня -PHANTASMAGORIA-", L"Phantomgesang -PHANTASMAGORIA-", L"Canção Fantasma -PHANTASMAGORIA-", L"Spooklied -PHANTASMAGORIA-", L"Pieśń Widmo -PHANTASMAGORIA-", L"Hayalet Şarkı -PHANTASMAGORIA-");
						break;
					case 8531:
						a = LL14(L"刻ハ至レリ", L"The Hour Has Come", L"L'Heure est Venue", L"L'Ora è Giunta", L"La Hora Ha Llegado", L"때는 이르렀다", L"时刻已至", L"لقد حانت الساعة", L"Час Настал", L"Die Stunde ist Gekommen", L"A Hora Chegou", L"Het Uur is Gekomen", L"Godzina Nadeszła", L"Saat Geldi");
						break;
					case 8532:
						a = LL14(L"目覚めし伝承", L"Awakening Legend", L"Légende Éveillée", L"Leggenda Risvegliata", L"Leyenda Despertada", L"깨어난 전승", L"觉醒的传承", L"الأسطورة المستيقظة", L"Пробуждённая Легенда", L"Erwachende Legende", L"Lenda Despertada", L"Ontwakende Legende", L"Przebudzona Legenda", L"Uyanan Efsane");
						break;
					case 8533:
						a = LL14(L"唯一の希望", L"Only Hope", L"Seul Espoir", L"Unica Speranza", L"Única Esperanza", L"유일한 희망", L"唯一的希望", L"الأمل الوحيد", L"Единственная Надежда", L"Einzige Hoffnung", L"Única Esperança", L"Enige Hoop", L"Jedyna Nadzieja", L"Tek Umut");
						break;
					case 8535:
					case 8537:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
						break;
					case 8538:
						a = LL14(L"今はまだ...", L"Not Yet...", L"Pas Encore...", L"Non Ancora...", L"Todavía No...", L"지금은 아직...", L"现在还不行...", L"ليس بعد...", L"Ещё Нет...", L"Noch Nicht...", L"Ainda Não...", L"Nog Niet...", L"Jeszcze Nie...", L"Henüz Değil...");
						break;
					case 8539:
						a = LL14(L"あの日に見た夜空", L"The Night Sky I Saw That Day", L"Le Ciel Nocturne que j'ai Vu Ce Jour-là", L"Il Cielo Notturno che Vidi Quel Giorno", L"El Cielo Nocturno que Vi Ese Día", L"그날에 본 밤하늘", L"那天看到的夜空", L"سماء الليل التي رأيتها ذلك اليوم", L"Ночное Небо, Которое Я Видел В Тот День", L"Der Nachthimmel, den Ich Damals Sah", L"O Céu Noturno que Vi Naquele Dia", L"De Nachtelijke Hemel die ik Die Dag Zag", L"Nocne Niebo, które Widziałem Tamtego Dnia", L"O Gün Gördüğüm Gece Gökyüzü");
						break;
					case 8540:
						a = LL14(L"偽りの時間", L"False Time", L"Temps Fictif", L"Tempo Falso", L"Tiempo Falso", L"거짓된 시간", L"虚假的时间", L"الوقت المزيف", L"Ложное Время", L"Falsche Zeit", L"Tempo Falso", L"Valse Tijd", L"Fałszywy Czas", L"Sahte Zaman");
						break;
					case 8541:
						a = LL14(L"紅き翼 -新たなる風-", L"Crimson Wings -New Wind-", L"Ailes Cramoisies -Nouveau Vent-", L"Ali Cremisi -Nuovo Vento-", L"Alas Carmesí -Nuevo Viento-", L"붉은 날개 -새로운 바람-", L"红之翼 -新风-", L"الأجنحة القرمزية -رياح جديدة-", L"Багровые Крылья -Новый Ветер-", L"Karmesinrote Flügel -Neuer Wind-", L"Asas Carmesim -Novo Vento-", L"Karmozijnrode Vleugels -Nieuwe Wind-", L"Karmazynowe Skrzydła -Nowy Wiatr-", L"Kırmızı Kanatlar -Yeni Rüzgar-");
						break;
					case 8550:
						a = LL14(L"再会", L"Reunion", L"Retrouvailles", L"Riunione", L"Reencuentro", L"재회", L"重逢", L"لقاء ثان", L"Воссоединение", L"Wiedersehen", L"Reencontro", L"Hereniging", L"Ponowne Spotkanie", L"Yeniden Buluşma");
						break;
					case 8551:
						a = LL14(L"かけがえのない人へ", L"To Someone Irreplaceable", L"À Quelqu'un d'Irremplaçable", L"A Qualcuno di Insostituibile", L"A Alguien Insustituible", L"둘도 없는 소중한 사람에게", L"致无可替代之人", L"إلى شخص لا يعوض", L"Незаменимому Человеку", L"An Jemanden Unersetzlichen", L"A Alguém Insubstituível", L"Aan Iemand Onvervangbaar", L"Do Kogoś Niezastąpionego", L"Vazgeçilmez Birine");
						break;
					case 8552:
						a = LL14(L"惜しむように、愛おしむように", L"Cherishing, Treasuring", L"Chérissant, Précieux", L"Custodendo, Tesaurizzando", L"Atesorando, Valorando", L"아쉬워하듯, 아끼고 사랑하듯", L"如同珍惜，如同爱怜", L"بالاعتزاز والمحبة", L"Дорожа, Храня", L"Kosten, Schätzen", L"Prezando, Valorizando", L"Koesterend, Waarderend", L"Ceniąc, Pielęgnując", L"Değer Vererek, Sevgiyle");
						break;
					case 8553:
						a = LL14(L"ライノの花が咲く頃", L"When the Rhino Flower Blooms", L"Quand la Fleur de Rhino s'Épanouit", L"Quando il Fiore di Rhino Sboccia", L"Cuando Florece la Flor de Rhino", L"라이노 꽃이 필 무렵", L"莱诺花盛开之时", L"عندما تفتح زهرة راينو", L"Когда Цветёт Цветок Райно", L"Wenn die Rhino-Blume Blüht", L"Quando a Flor de Rhino Desabrocha", L"Als de Rhino Bloem Bloeit", L"Gdy Kwitnie Kwiat Rhino", L"Rhino Çiçeği Açtığında");
						break;
					case 8555:
						a = LL14(L"戦場の掟", L"Rules of Battlefield", L"Règles du Champ de Bataille", L"Regole del Campo di Battaglia", L"Reglas del Campo de Batalla", L"전장의 규칙", L"战场的规则", L"قوانين ساحة المعركة", L"Правила Поля Боя", L"Regeln des Schlachtfeldes", L"Regras do Campo de Batalha", L"Regels van het Slagveld", L"Zasady Pola Bitwy", L"Savaş Alanının Kuralları");
						break;
					case 8556:
						a = L"Remaining Glow";
						break;
					case 8557:
						a = LL14(L"深淵の魔女", L"Witch of the Abyss", L"Sorcière de l'Abîme", L"Strega dell'Abisso", L"Bruja del Abismo", L"심연의 마녀", L"深渊的魔女", L"ساحرة الهاوية", L"Ведьма Бездны", L"Hexe des Abgrunds", L"Bruxa do Abismo", L"Heks van de Afgrond", L"Wiedźma Otchłani", L"Uçurumun Cadısı");
						break;
					case 8558:
						a = L"ALTINA";
						break;
					case 8559:
						a = LL14(L"威風", L"Dignity", L"Dignité", L"Dignità", L"Dignidad", L"위풍", L"威风", L"المهابة", L"Достоинство", L"Würde", L"Dignidade", L"Waardigheid", L"Godność", L"Haysiyet");
						break;
					case 8560:
						a = LL14(L"一撃に賭ける", L"Bet on One Strike", L"Miser sur un Seul Coup", L"Scommettere su un Solo Colpo", L"Apostar por un Solo Golpe", L"일격에 건다", L"赌在一击", L"الرهان على ضربة واحدة", L"Ставить на Один Удар", L"Auf einen Schlag Setzen", L"Apostar em um Único Golpe", L"Alles op Een Slag Zetten", L"Postawić na Jeden Cios", L"Tek Darbeye Bahse Girmek");
						break;
					case 8561:
						a = LL14(L"ユミル渓谷道", L"Ymir Valley Road", L"Route de la Vallée de Ymir", L"Strada della Valle di Ymir", L"Camino del Valle de Ymir", L"유미르 계곡길", L"尤弥尔谷道", L"طريق وادي إيمير", L"Дорога Долины Имир", L"Ymir-Talstraße", L"Estrada do Vale de Ymir", L"Ymir Valleiroute", L"Droga Doliny Ymir", L"Ymir Vadi Yolu");
						break;
					case 8562:
						a = L"Awakening";
						break;
					case 8563:
						a = L"Blitzkrieg";
						break;
					case 8564:
						a = LL14(L"魔王の凱歌", L"Demon Lord's Triumph", L"Triomphe du Seigneur Démon", L"Trionfo del Signore dei Demoni", L"Triunfo del Señor Demonio", L"마왕의 개가", L"魔王的凯歌", L"نشيد نصر سيد الشياطين", L"Триумф Повелителя Демонов", L"Triumph des Dämonenkönigs", L"Triunfo do Senhor dos Demônios", L"Triomf van de Demonenkoning", L"Triumf Władcy Demonów", L"Şeytan Lordu'nun Zaferi");
						break;
					case 8566:
						a = LL14(L"内なる黄昏", L"Inner Twilight", L"Crépuscule Intérieur", L"Crepuscolo Interiore", L"Crepúsculo Interior", L"내면의 황혼", L"内心的黄昏", L"الغسق الداخلي", L"Внутренние Сумерки", L"Innere Dämmerung", L"Crepúsculo Interior", L"Innerlijke Schemering", L"Wewnętrzny Zmierzch", L"İç Alacakaranlık");
						break;
					case 8567:
						a = LL14(L"蘇る記憶", L"Awakened Memories", L"Souvenirs Ressuscités", L"Ricordi Risvegliati", L"Recuerdos Resucitados", L"살아나는 기억", L"觉醒的记忆", L"الذكريات المستعادة", L"Пробуждённые Воспоминания", L"Erwachte Erinnerungen", L"Memórias Despertadas", L"Ontwakende Herinneringen", L"Przebudzone Wspomnienia", L"Uyanan Anılar");
						break;
					case 8570:
						a = LL14(L"静かな決意", L"Quiet Resolution", L"Résolution Silencieuse", L"Risoluzione Silenziosa", L"Resolución Silenciosa", L"조용한 결의", L"静静的决意", L"عزيمة هادئة", L"Тихая Решимость", L"Stille Entschlossenheit", L"Resolução Silenciosa", L"Stille Vastberadenheid", L"Cicha Determinacja", L"Sessiz Kararlılık");
						break;
					case 8571:
						a = LL14(L"乾坤一擲", L"All or Nothing", L"Tout ou Rien", L"Tutto o Niente", L"Todo o Nada", L"건곤일척", L"孤注一掷", L"الكل أو لا شيء", L"Всё или Ничего", L"Alles oder Nichts", L"Tudo ou Nada", L"Alles of Niets", L"Wszystko albo Nic", L"Ya Hep Ya Hiç");
						break;
					case 8572:
						a = LL14(L"交戦", L"Combat", L"Combat", L"Combattimento", L"Combate", L"교전", L"交战", L"اشتباك", L"Бой", L"Kampf", L"Combate", L"Gevecht", L"Walka", L"Muharebe");
						break;
					case 8573:
					case 8584:
					case 8605:
					case 8606:
					case 8608:
					case 8610:
					case 8622:
					case 8623:
					case 8624:
					case 8625:
					case 8627:
					case 8629:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
						break;
					case 8600:
						a = LL14(L"大市の賑わい", L"Bustling Market", L"Marché Animé", L"Mercato Vivace", L"Mercado Animado", L"장터의 북적임", L"大市场的热闹", L"سوق مزدحم", L"Оживлённый Рынок", L"Belebter Markt", L"Mercado Movimentado", L"Drukke Markt", L"Tętniący życiem Rynek", L"Kalabalık Pazar");
						break;
					case 8601:
						a = LL14(L"剣の遊戯", L"Sword Play", L"Jeu d'Épée", L"Gioco di Spada", L"Juego de Espada", L"검의 유희", L"剑的游玩", L"لعب بالسيف", L"Игра на Мечах", L"Schwertkampfspiel", L"Jogo de Espada", L"Zwaardspel", L"Gra na Miecze", L"Kılıç Oyunu");
						break;
					case 8602:
						a = LL14(L"紙一重の攻防", L"Close Fight", L"Combat Serré", L"Combattimento Serrato", L"Combate Reñido", L"종이 한 장 차이의 공방", L"纸之一线的攻防", L"دفاع وهجوم متقارب", L"Напряжённый Бой", L"Knappes Gefecht", L"Luta Apertada", L"Nipt Gevecht", L"Zacięta Walka", L"Çekişmeli Dövüş");
						break;
					case 8603:
						a = LL14(L"走れマッハ号!", L"Run Mach Train!", L"En Avant Mach Train !", L"Corri Treno Mach!", L"¡Corre Tren Mach!", L"달려라 마하 호!", L"快跑马赫号！", L"اركض يا قطار ماخ!", L"Беги, Поезд Мах!", L"Lauf, Mach-Zug!", L"Corra Trem Mach!", L"Ren Mach Trein!", L"Biegnij Pociągu Mach!", L"Koş Mach Treni!");
						break;
					case 8607:
						a = LL14(L"星屑のカンタータ", L"Cantata of Stardust", L"Cantate de Poussière d'Étoiles", L"Cantata di Polvere di Stelle", L"Cantata de Polvo de Estrellas", L"별가루의 칸타타", L"星屑康塔塔", L"كنتاتا غبار النجوم", L"Кантата Звёздной Пыли", L"Kantate des Sternenstaubs", L"Cantata de Poeira Estelar", L"Cantate van Sterrenstof", L"Kantata Gwiazdowego Pyłu", L"Yıldız Tozu Kantası");
						break;
					case 8609:
						a = L"Sonata No.45";
						break;
					case 8620:
						a = LL14(L"雪ウサギを追いかけて", L"Chasing the Snow Rabbit", L"Chasser le Lapin des Neiges", L"Inseguire il Coniglio della Neve", L"Persiguiendo al Conejo de Nieve", L"눈토끼를 쫓아서", L"追逐雪兔", L"ملاحقة أرنب الثلج", L"Погоня за Снежным Кроликом", L"Das Schneekaninchen Jagen", L"Perseguindo o Coelho da Neve", L"Het Sneeuwkonijn Najagen", L"Goniąc śnieżnego Królika", L"Kar Tavşanını Kovalayarak");
						break;
					case 8621:
						a = L"Take The Windward!";
						break;
					case 8628:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
						break;
					case 8700:
					case 8703:
					case 8704:
					case 8710:
					case 8711:
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Música", L"음악", L"音乐", L"موسيقى", L"Музыка", L"Musik", L"Música", L"Muziek", L"Muzyka", L"Müzik");
						break;
					}
					_tcscpy(p.name, a);
					_tcscpy(p.fol, fname1);
				}
				else if ((ft == L"bgm1.pac" || ft == L"bgm2.pac" || ft == L"bgm3.pac") && fname.Find(L"Trails in the Sky 1st Chapter") > 0) {
					p.sub = 30; p.loop1 = p.loop2 = 0;
					TCHAR ti1[][100] = {
						L"001 風を共に舞う気持ち",
						L"100 地方都市ロレント",
						L"101 商業都市ボース",
						L"102 海港都市ルーアン",
						L"103 工房都市ツァイス",
						L"104 王都グランセル",
						L"105 陽だまりにて和む猫",
						L"106 国境警備も楽じゃない",
						L"107 王城",
						L"108 グランアリーナ",
						L"108bグランアリーナ(ノーイントロ)",
						L"200 リベールの歩き方",
						L"201 Secret Green Passage",
						L"202 Rock on the Road",
						L"300 闇を彷徨う",
						L"301 行く手をはばむ鋼の床",
						L"302 暗がりがくれた安らぎ",
						L"303 四輪の塔",
						L"304 レイストン要塞",
						L"305 虚ろなる光の封土",
						L"400 Sophisticated Fight -Quick Battle-",
						L"401 Sophisticated Fight -Command Battle-",
						L"402 To be Suggestive",
						L"403 銀の意志",
						L"404 Challenger Invited",
						L"405 Ancient Makes",
						L"406 至宝を守護せしモノ",
						L"407 撃破！！",
						L"408 消え行く星",
						L"410 ピンチ！！",
						L"500 星の在り処 Harmonica short Ver.",
						L"501 琥珀の愛 Hum Ver.(日本語)",
						L"501e琥珀の愛 Hum Ver.",
						L"502 琥珀の愛 Piano Ver.",
						L"502b琥珀の愛 Piano Ver.1.5",
						L"503 琥珀の愛 Lute Ver.",
						L"504 星の在り処 Harmonica long Ver.",
						L"505 賑やかに行こう",
						L"510 去り行く決意",
						L"511 暗躍する者たち",
						L"512 奴を逃がすな！",
						L"513 胸の中に",
						L"514 月明りの下で",
						L"516 忍び寄る危機",
						L"517 俺達カプア一家！",
						L"518 旅立ちの小径",
						L"519 奪還",
						L"520 呪縛からの解放、そして・・・",
						L"521 告白",
						L"522 黒のオーブメント",
						L"523 リベールの誇り",
						L"530 組曲 白き花のマドリガル - 姫の悩み",
						L"531 組曲 白き花のマドリガル - 騎士達の嘆き",
						L"532 組曲 白き花のマドリガル - それぞれの思惑",
						L"533 組曲 白き花のマドリガル - 城",
						L"534 組曲 白き花のマドリガル - コロシアム",
						L"535 組曲 白き花のマドリガル - 決闘",
						L"536 組曲 白き花のマドリガル - 姫の死",
						L"537 組曲 白き花のマドリガル - 大団円",
						L""
					};
					TCHAR ti1_en[][100] = {
						L"001 Dancing with the Wind",
						L"100 Provincial City Rolent",
						L"101 Commercial City Bose",
						L"102 Port City Ruan",
						L"103 Workshop City Zeiss",
						L"104 Royal Capital Grancel",
						L"105 Cat Relaxing in the Sun",
						L"106 Border Patrol Isn't Easy",
						L"107 Royal Castle",
						L"108 Grand Arena",
						L"108b Grand Arena (No Intro)",
						L"200 Walking in Liberl",
						L"201 Secret Green Passage",
						L"202 Rock on the Road",
						L"300 Wandering in the Darkness",
						L"301 Steel Floor Blocking the Path",
						L"302 Peace in the Darkness",
						L"303 Tetracyclic Towers",
						L"304 Leiston Fortress",
						L"305 Hollow Land of Light",
						L"400 Sophisticated Fight -Quick Battle-",
						L"401 Sophisticated Fight -Command Battle-",
						L"402 To be Suggestive",
						L"403 Silver Will",
						L"404 Challenger Invited",
						L"405 Ancient Makes",
						L"406 Guardian of the Treasure",
						L"407 Crush!!",
						L"408 Disappearing Star",
						L"410 Pinch!!",
						L"500 Where the Stars Are Harmonica short Ver.",
						L"501 Amber Love Hum Ver.(Japanese)",
						L"501e Amber Love Hum Ver.",
						L"502 Amber Love Piano Ver.",
						L"502b Amber Love Piano Ver.1.5",
						L"503 Amber Love Lute Ver.",
						L"504 Where the Stars Are Harmonica long Ver.",
						L"505 Let's Go Lively",
						L"510 Determination to Leave",
						L"511 Those Who Move in the Shadows",
						L"512 Don't Let Him Escape!",
						L"513 In My Heart",
						L"514 Under the Moonlight",
						L"516 Creeping Crisis",
						L"517 We're the Capua Family!",
						L"518 Path of Departure",
						L"519 Recapture",
						L"520 Liberation from the Curse, and...",
						L"521 Confession",
						L"522 Black Ouroboros",
						L"523 Pride of Liberl",
						L"530 Suite Madrigal of the White Flower - Princess's Worry",
						L"531 Suite Madrigal of the White Flower - Knights' Lament",
						L"532 Suite Madrigal of the White Flower - Each One's Scheme",
						L"533 Suite Madrigal of the White Flower - Castle",
						L"534 Suite Madrigal of the White Flower - Colosseum",
						L"535 Suite Madrigal of the White Flower - Duel",
						L"536 Suite Madrigal of the White Flower - Princess's Death",
						L"537 Suite Madrigal of the White Flower - Grand Finale",
						L""
					};
					TCHAR ti1_fr[][100] = {
	L"001 Sentiments dansant avec le vent", L"100 Rolent - Ville provinciale", L"101 Bose - Ville commerciale", L"102 Ruan - Ville portuaire", L"103 Zeiss - Ville atelier", L"104 Grancel - Capitale royale", L"105 Chat au soleil", L"106 La patrouille frontière n'est pas facile", L"107 Château royal", L"108 Grand Arena", L"108b Grand Arena (Sans intro)", L"200 Comment se déplacer à Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Errance dans les ténèbres", L"301 Plancher d'acier bloquant le chemin", L"302 Paix des ténèbres", L"303 Tours tétracycliques", L"304 Forteresse de Leiston", L"305 Terre vacante de lumière", L"400 Sophisticated Fight -Combat rapide-", L"401 Sophisticated Fight -Combat commandé-", L"402 To be Suggestive", L"403 Volonté d'argent", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Gardien du trésor", L"407 Écrasement !!", L"408 Étoile défaillante", L"410 Pinch !!", L"500 Où sont les étoiles Harmonica court", L"501 Amour d'ambre Hum Ver.(Japonais)", L"501e Amour d'ambre Hum Ver.", L"502 Amour d'ambre Piano Ver.", L"502b Amour d'ambre Piano Ver.1.5", L"503 Amour d'ambre Luth Ver.", L"504 Où sont les étoiles Harmonica long", L"505 Allons gaiement", L"510 Décision de partir", L"511 Ceux qui agissent dans l'ombre", L"512 Ne le laissez pas s'échapper !", L"513 Dans mon cœur", L"514 Sous le clair de lune", L"516 Crise rampante", L"517 Nous sommes la famille Capua !", L"518 Sentier du départ", L"519 Reprise", L"520 Libération de la malédiction, et...", L"521 Aveu", L"522 Orbement noir", L"523 Fierté de Liberl", L"530 Suite Madrigal de la Fleur Blanche - Souci de la princesse", L"531 Suite Madrigal - Lamentation des chevaliers", L"532 Suite Madrigal - Intentions de chacun", L"533 Suite Madrigal - Château", L"534 Suite Madrigal - Colisée", L"535 Suite Madrigal - Duel", L"536 Suite Madrigal - Mort de la princesse", L"537 Suite Madrigal - Grand final", L""
					};
					TCHAR ti1_de[][100] = {
						L"001 Gefühle tanzend mit dem Wind", L"100 Rolent - Provinzstadt", L"101 Bose - Handelsstadt", L"102 Ruan - Hafenstadt", L"103 Zeiss - Werkstadt", L"104 Grancel - Königshauptstadt", L"105 Katze in der Sonne", L"106 Grenzpatrouille ist nicht leicht", L"107 Königsschloss", L"108 Grand Arena", L"108b Grand Arena (Ohne Intro)", L"200 Zu Fuß durch Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Wandern in der Dunkelheit", L"301 Stahlboden versperrt den Weg", L"302 Frieden in der Dunkelheit", L"303 Tetrazyklische Türme", L"304 Leiston-Festung", L"305 Hohles Land des Lichts", L"400 Sophisticated Fight -Schneller Kampf-", L"401 Sophisticated Fight -Kommando-Kampf-", L"402 To be Suggestive", L"403 Silberner Wille", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Hüter des Schatzes", L"407 Zerschmettern !!", L"408 Verblassender Stern", L"410 Pinch !!", L"500 Wo die Sterne sind Harmonica kurz", L"501 Bernstein-Liebe Hum Ver.(Japanisch)", L"501e Bernstein-Liebe Hum Ver.", L"502 Bernstein-Liebe Klavier Ver.", L"502b Bernstein-Liebe Klavier Ver.1.5", L"503 Bernstein-Liebe Laute Ver.", L"504 Wo die Sterne sind Harmonica lang", L"505 Gehen wir fröhlich", L"510 Entschlossenheit zu gehen", L"511 Die im Schatten handeln", L"512 Lasst ihn nicht entkommen !", L"513 In meinem Herzen", L"514 Im Mondschein", L"516 Schleichende Krise", L"517 Wir sind die Capua-Familie !", L"518 Pfad des Aufbruchs", L"519 Rückeroberung", L"520 Befreiung vom Fluch, und...", L"521 Geständnis", L"522 Schwarzer Ouroboros", L"523 Stolz von Liberl", L"530 Suite Madrigal der Weißen Blume - Sorge der Prinzessin", L"531 Suite Madrigal - Klage der Ritter", L"532 Suite Madrigal - Jeder sein Plan", L"533 Suite Madrigal - Schloss", L"534 Suite Madrigal - Kolosseum", L"535 Suite Madrigal - Duell", L"536 Suite Madrigal - Tod der Prinzessin", L"537 Suite Madrigal - Großer Schluss", L""
					};
					TCHAR ti1_es[][100] = {
						L"001 Sentimientos bailando con el viento", L"100 Rolent - Ciudad provincial", L"101 Bose - Ciudad comercial", L"102 Ruan - Ciudad portuaria", L"103 Zeiss - Ciudad taller", L"104 Grancel - Capital real", L"105 Gato al sol", L"106 La patrulla fronteriza no es fácil", L"107 Castillo real", L"108 Grand Arena", L"108b Grand Arena (Sin intro)", L"200 Cómo caminar por Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Vagando en la oscuridad", L"301 Piso de acero bloqueando el camino", L"302 Paz en la oscuridad", L"303 Torres tetracyclic", L"304 Fortaleza Leiston", L"305 Tierra vacía de luz", L"400 Sophisticated Fight -Batalla rápida-", L"401 Sophisticated Fight -Batalla comando-", L"402 To be Suggestive", L"403 Voluntad de plata", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Guardián del tesoro", L"407 ¡¡Aplastar!!", L"408 Estrella desvaneciente", L"410 Pinch !!", L"500 Dónde están las estrellas Harmónica corto", L"501 Amor ámbar Hum Ver.(Japonés)", L"501e Amor ámbar Hum Ver.", L"502 Amor ámbar Piano Ver.", L"502b Amor ámbar Piano Ver.1.5", L"503 Amor ámbar Laúd Ver.", L"504 Dónde están las estrellas Harmónica largo", L"505 Vamos alegres", L"510 Determinación de partir", L"511 Los que actúan en la sombra", L"512 ¡No lo dejes escapar!", L"513 En mi corazón", L"514 Bajo la luna", L"516 Crisis creciente", L"517 ¡Somos la familia Capua!", L"518 Camino de partida", L"519 Recaptura", L"520 Liberación de la maldición, y...", L"521 Confesión", L"522 Orbement negro", L"523 Orgullo de Liberl", L"530 Suite Madrigal de la Flor Blanca - Preocupación de la princesa", L"531 Suite Madrigal - Lamento de los caballeros", L"532 Suite Madrigal - Intenciones de cada uno", L"533 Suite Madrigal - Castillo", L"534 Suite Madrigal - Coliseo", L"535 Suite Madrigal - Duelo", L"536 Suite Madrigal - Muerte de la princesa", L"537 Suite Madrigal - Gran final", L""
					};
					TCHAR ti1_it[][100] = {
						L"001 Sentimenti danzanti con il vento", L"100 Rolent - Città provinciale", L"101 Bose - Città commerciale", L"102 Ruan - Città portuale", L"103 Zeiss - Città officina", L"104 Grancel - Capitale reale", L"105 Gatto al sole", L"106 La pattuglia di frontiera non è facile", L"107 Castello reale", L"108 Grand Arena", L"108b Grand Arena (Senza intro)", L"200 Come camminare a Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Vagando nelle tenebre", L"301 Pavimento d'acciaio che blocca il sentiero", L"302 Pace nelle tenebre", L"303 Torri tetracyclic", L"304 Fortezza Leiston", L"305 Terra vuota di luce", L"400 Sophisticated Fight -Battaglia rapida-", L"401 Sophisticated Fight -Battaglia comando-", L"402 To be Suggestive", L"403 Volontà d'argento", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Guardiano del tesoro", L"407 Schiacciare !!", L"408 Stella che svanisce", L"410 Pinch !!", L"500 Dove sono le stelle Fisarmonica corto", L"501 Amore ambra Hum Ver.(Giapponese)", L"501e Amore ambra Hum Ver.", L"502 Amore ambra Piano Ver.", L"502b Amore ambra Piano Ver.1.5", L"503 Amore ambra Liuto Ver.", L"504 Dove sono le stelle Fisarmonica lungo", L"505 Andiamo allegri", L"510 Determinazione a partire", L"511 Coloro che agiscono nell'ombra", L"512 Non lasciarlo scappare !", L"513 Nel mio cuore", L"514 Sotto la luna", L"516 Crisi strisciante", L"517 Siamo la famiglia Capua !", L"518 Sentiero di partenza", L"519 Riconquista", L"520 Liberazione dalla maledizione, e...", L"521 Confessione", L"522 Orbement nero", L"523 Orgoglio di Liberl", L"530 Suite Madrigal del Fiore Bianco - Preoccupazione della principessa", L"531 Suite Madrigal - Lamento dei cavalieri", L"532 Suite Madrigal - Intenzioni di ciascuno", L"533 Suite Madrigal - Castello", L"534 Suite Madrigal - Colosseo", L"535 Suite Madrigal - Duello", L"536 Suite Madrigal - Morte della principessa", L"537 Suite Madrigal - Gran finale", L""
					};
					TCHAR ti1_ko[][100] = {
						L"001 바람과 함께 춤추는 마음", L"100 지방도시 로렌트", L"101 상업도시 보스", L"102 해항도시 루안", L"103 공방도시 차이스", L"104 왕도 그란셀", L"105 햇볕 아래의 고양이", L"106 국경 경비도 쉽지 않아", L"107 왕성", L"108 그란 아레나", L"108b 그란 아레나 (전주 없음)", L"200 리베르를 걷는 법", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 어둠 속의 방황", L"301 가로막는 강철의 바닥", L"302 어둠이 주는 평온", L"303 사륜의 탑", L"304 레이스톤 요새", L"305 허무한 빛의 봉토", L"400 Sophisticated Fight -빠른 전투-", L"401 Sophisticated Fight -커맨드 전투-", L"402 To be Suggestive", L"403 은의 의지", L"404 Challenger Invited", L"405 Ancient Makes", L"406 지보를 지키는 자", L"407 격파 !!", L"408 사라져가는 별", L"410 Pinch !!", L"500 별이 머무는 곳 하모니카 short Ver.", L"501 琥珀의 사랑 Hum Ver.(일본어)", L"501e 琥珀의 사랑 Hum Ver.", L"502 琥珀의 사랑 Piano Ver.", L"502b 琥珀의 사랑 Piano Ver.1.5", L"503 琥珀의 사랑 류트 Ver.", L"504 별이 머무는 곳 하모니카 long Ver.", L"505 씩씩하게 가자", L"510 떠나가려는 결의", L"511 어둠 속에서 행동하는 자들", L"512 그를 놓치지 마 !", L"513 마음 속", L"514 달빛 아래에서", L"516 다가오는 위기", L"517 우리들은 카푸아 일가 !", L"518 출발의 오솔길", L"519 탈환", L"520 저주로부터 해방, 그리고...", L"521 고백", L"522 검은 오브먼트", L"523 리베르의 긍지", L"530 백화의 소야곡 - 왕녀의 걱정", L"531 백화의 소야곡 - 기사들의 비탄", L"532 백화의 소야곡 - 각자의 생각", L"533 백화의 소야곡 - 성", L"534 백화의 소야곡 - 투기장", L"535 백화의 소야곡 - 결투", L"536 백화의 소야곡 - 왕녀의 죽음", L"537 백화의 소야곡 - 대단원", L""
					};
					TCHAR ti1_zh[][100] = {
						L"001 与风共舞的心", L"100 地方都市洛伦特", L"101 商业都市柏斯", L"102 海港都市卢安", L"103 工房都市蔡斯", L"104 王都格兰赛尔", L"105 阳光下的猫", L"106 国境警备也不轻松", L"107 王城", L"108 格兰竞技场", L"108b 格兰竞技场(无前奏)", L"200 利贝尔的步道", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 彷徨于黑暗中", L"301 阻挡去路的钢铁之床", L"302 黑暗带来的安宁", L"303 四轮之塔", L"304 雷斯顿要塞", L"305 虚渺之光封土", L"400 Sophisticated Fight -快速战斗-", L"401 Sophisticated Fight -指令战斗-", L"402 To be Suggestive", L"403 银之意志", L"404 Challenger Invited", L"405 Ancient Makes", L"406 至宝守护者", L"407 击破 !!", L"408 消逝之星", L"410 Pinch !!", L"500 星之所在 口琴short Ver.", L"501 琥珀之爱 Hum Ver.(日语)", L"501e 琥珀之爱 Hum Ver.", L"502 琥珀之爱 钢琴 Ver.", L"502b 琥珀之爱 钢琴 Ver.1.5", L"503 琥珀之爱 鲁特琴 Ver.", L"504 星之所在 口琴long Ver.", L"505 精神地出发", L"510 离去的决意", L"511 暗中行动者们", L"512 别让他逃了 !", L"513 心中", L"514 月光下", L"516 悄悄逼近的危机", L"517 我们是卡普亚一家 !", L"518 旅程小路", L"519 夺还", L"520 从诅咒中解放，然后...", L"521 告白", L"522 黑色导力器", L"523 利贝尔的骄傲", L"530 组曲 白花之恋曲 - 公主的忧虑", L"531 组曲 白花之恋曲 - 骑士们的悲叹", L"532 组曲 白花之恋曲 - 各自的思绪", L"533 组曲 白花之恋曲 - 城堡", L"534 组曲 白花之恋曲 - 竞技场", L"535 组曲 白花之恋曲 - 决斗", L"536 组曲 白花之恋曲 - 公主之死", L"537 组曲 白花之恋曲 - 大团圆", L""
					};
					TCHAR ti1_ar[][100] = {
						L"001 مشاعر ترقص مع الرياح", L"100 رولينت - مدينة إقليمية", L"101 بوس - مدينة تجارية", L"102 روان - مدينة ميناء", L"103 زايس - مدينة ورشة", L"104 غرانسيل - العاصمة الملكية", L"105 قطة تحت الشمس", L"106 دورية الحدود ليست سهلة", L"107 القصر الملكي", L"108 Grand Arena", L"108b Grand Arena (بدون مقدمة)", L"200 كيفية التجول في ليبيرل", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 التجوال في الظلام", L"301 الأرضية الفولاذية تعيق الطريق", L"302 السكينة في الظلام", L"303 الأبراج الرباعية", L"304 حصن ليستون", L"305 أرض الضوء الخاوية", L"400 Sophisticated Fight -قتال سريع-", L"401 Sophisticated Fight -قتال الأوامر-", L"402 To be Suggestive", L"403 الإرادة الفضية", L"404 Challenger Invited", L"405 Ancient Makes", L"406 حارس الكنز", L"407 سحق !!", L"408 النجم المتلاشي", L"410 Pinch !!", L"500 أين النجوم الهارمونيكا قصيرة", L"501 الحب الكهرماني Hum Ver.(ياباني)", L"501e الحب الكهرماني Hum Ver.", L"502 الحب الكهرماني بيانو Ver.", L"502b الحب الكهرماني بيانو Ver.1.5", L"503 الحب الكهرماني عود Ver.", L"504 أين النجوم الهارمونيكا طويلة", L"505 لننطلق بمرح", L"510 القرار بالرحيل", L"511 الذين يعملون في الظلال", L"512 لا تدعه يهرب !", L"513 في قلبي", L"514 تحت ضوء القمر", L"516 أزمة زاحفة", L"517 نحن عائلة كابوا !", L"518 طريق الرحيل", L"519 الاسترداد", L"520 التحرر من اللعنة، و...", L"521 اعتراف", L"522 Orbement أسود", L"523 فخر ليبيرل", L"530 Suite Madrigal الزهرة البيضاء - قلق الأميرة", L"531 Suite Madrigal - رثاء الفرسان", L"532 Suite Madrigal - نوايا كل واحد", L"533 Suite Madrigal - القصر", L"534 Suite Madrigal - الكولوسيوم", L"535 Suite Madrigal - المبارزة", L"536 Suite Madrigal - موت الأميرة", L"537 Suite Madrigal - النهاية الكبرى", L""
					};
					TCHAR ti1_ru[][100] = {
						L"001 Чувства, танцующие с ветром", L"100 Ролент - Провинциальный город", L"101 Бос - Торговый город", L"102 Руан - Портовый город", L"103 Цейсс - Город мастерских", L"104 Грансель - Королевская столица", L"105 Кот на солнце", L"106 Пограничный патруль нелёгок", L"107 Королевский замок", L"108 Grand Arena", L"108b Grand Arena (Без вступления)", L"200 Как ходить по Либерлу", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Блуждание во тьме", L"301 Стальной пол преграждает путь", L"302 Покой во тьме", L"303 Тетрациклические башни", L"304 Крепость Лейстон", L"305 Пустая земля света", L"400 Sophisticated Fight -Быстрый бой-", L"401 Sophisticated Fight -Командный бой-", L"402 To be Suggestive", L"403 Серебряная воля", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Страж сокровища", L"407 Разгром !!", L"408 Исчезающая звезда", L"410 Pinch !!", L"500 Где звёзды Гармоника короткая", L"501 Янтарная любовь Hum Ver.(Японский)", L"501e Янтарная любовь Hum Ver.", L"502 Янтарная любовь Фортепиано Ver.", L"502b Янтарная любовь Фортепиано Ver.1.5", L"503 Янтарная любовь Лютня Ver.", L"504 Где звёзды Гармоника длинная", L"505 Пойдём весело", L"510 Решимость уйти", L"511 Действующие в тени", L"512 Не дай ему сбежать !", L"513 В моём сердце", L"514 Под лунным светом", L"516 Надвигающийся кризис", L"517 Мы семья Капуа !", L"518 Тропа отбытия", L"519 Захват", L"520 Освобождение от проклятия, и...", L"521 Признание", L"522 Чёрный Orbment", L"523 Гордость Либерла", L"530 Сюита Мадригал Белого Цветка - Забота принцессы", L"531 Сюита Мадригал - Плач рыцарей", L"532 Сюита Мадригал - Замыслы каждого", L"533 Сюита Мадригал - Замок", L"534 Сюита Мадригал - Колизей", L"535 Сюита Мадригал - Поединок", L"536 Сюита Мадригал - Смерть принцессы", L"537 Сюита Мадригал - Большой финал", L""
					};
					TCHAR ti1_pt[][100] = {
						L"001 Sentimentos dançando com o vento", L"100 Rolent - Cidade provincial", L"101 Bose - Cidade comercial", L"102 Ruan - Cidade portuária", L"103 Zeiss - Cidade oficina", L"104 Grancel - Capital real", L"105 Gato ao sol", L"106 A patrulha de fronteira não é fácil", L"107 Castelo real", L"108 Grand Arena", L"108b Grand Arena (Sem intro)", L"200 Como andar por Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Vagando na escuridão", L"301 Piso de aço bloqueando o caminho", L"302 Paz na escuridão", L"303 Torres tetracíclicas", L"304 Fortaleza Leiston", L"305 Terra vazia de luz", L"400 Sophisticated Fight -Batalha rápida-", L"401 Sophisticated Fight -Batalha comando-", L"402 To be Suggestive", L"403 Vontade de prata", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Guardião do tesouro", L"407 Esmagar !!", L"408 Estrela desvanecente", L"410 Pinch !!", L"500 Onde estão as estrelas Harmónica curto", L"501 Amor âmbar Hum Ver.(Japonês)", L"501e Amor âmbar Hum Ver.", L"502 Amor âmbar Piano Ver.", L"502b Amor âmbar Piano Ver.1.5", L"503 Amor âmbar Alaúde Ver.", L"504 Onde estão as estrelas Harmónica longo", L"505 Vamos animados", L"510 Determinação de partir", L"511 Os que agem nas sombras", L"512 Não o deixe escapar !", L"513 No meu coração", L"514 Sob o luar", L"516 Crise rastejante", L"517 Somos a família Capua !", L"518 Caminho da partida", L"519 Recaptura", L"520 Libertação da maldição, e...", L"521 Confissão", L"522 Orbement negro", L"523 Orgulho de Liberl", L"530 Suite Madrigal da Flor Branca - Preocupação da princesa", L"531 Suite Madrigal - Lamento dos cavaleiros", L"532 Suite Madrigal - Intenções de cada um", L"533 Suite Madrigal - Castelo", L"534 Suite Madrigal - Coliseu", L"535 Suite Madrigal - Duelo", L"536 Suite Madrigal - Morte da princesa", L"537 Suite Madrigal - Grande final", L""
					};
					TCHAR ti1_nl[][100] = {
						L"001 Gevoelens dansend met de wind", L"100 Rolent - Provinciestad", L"101 Bose - Handelsstad", L"102 Ruan - Havenstad", L"103 Zeiss - Werkplaatsstad", L"104 Grancel - Koninklijke hoofdstad", L"105 Kat in de zon", L"106 Grenspatrouille is niet gemakkelijk", L"107 Koninklijk kasteel", L"108 Grand Arena", L"108b Grand Arena (Zonder intro)", L"200 Hoe te lopen in Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Dwalen in de duisternis", L"301 Stalen vloer blokkeert het pad", L"302 Vrede in de duisternis", L"303 Tetracyclische torens", L"304 Leiston vesting", L"305 Leeg land van licht", L"400 Sophisticated Fight -Snelle gevecht-", L"401 Sophisticated Fight -Commando gevecht-", L"402 To be Suggestive", L"403 Zilveren wil", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Bewaker van de schat", L"407 Verpletteren !!", L"408 Vervagende ster", L"410 Pinch !!", L"500 Waar de sterren zijn Harmonica kort", L"501 Amberliefde Hum Ver.(Japans)", L"501e Amberliefde Hum Ver.", L"502 Amberliefde Piano Ver.", L"502b Amberliefde Piano Ver.1.5", L"503 Amberliefde Luit Ver.", L"504 Waar de sterren zijn Harmonica lang", L"505 Laten we vrolijk gaan", L"510 Vastberadenheid om te vertrekken", L"511 Degenen in de schaduw", L"512 Laat hem niet ontsnappen !", L"513 In mijn hart", L"514 Onder het maanlicht", L"516 Sluipende crisis", L"517 Wij zijn de Capua-familie !", L"518 Pad van vertrek", L"519 Herovering", L"520 Bevrijding van de vloek, en...", L"521 Biecht", L"522 Zwarte Orbment", L"523 Trots van Liberl", L"530 Suite Madrigal van de Witte Bloem - Zorg van de prinses", L"531 Suite Madrigal - Klaagzang van ridders", L"532 Suite Madrigal - Intenties van iedereen", L"533 Suite Madrigal - Kasteel", L"534 Suite Madrigal - Colosseum", L"535 Suite Madrigal - Duel", L"536 Suite Madrigal - Dood van prinses", L"537 Suite Madrigal - Grote finale", L""
					};
					TCHAR ti1_pl[][100] = {
						L"001 Uczucia tańczące z wiatrem", L"100 Rolent - Miasto prowincjonalne", L"101 Bose - Miasto handlowe", L"102 Ruan - Miasto portowe", L"103 Zeiss - Miasto warsztatów", L"104 Grancel - Stolica królewska", L"105 Kot w słońcu", L"106 Patrol graniczny nie jest łatwy", L"107 Zamek królewski", L"108 Grand Arena", L"108b Grand Arena (Bez wstępu)", L"200 Jak chodzić po Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Błądzenie w ciemności", L"301 Stalowa podłoga blokująca drogę", L"302 Pokój w ciemności", L"303 Wieże tetracyclic", L"304 Twierdza Leiston", L"305 Pusta ziemia światła", L"400 Sophisticated Fight -Szybka bitwa-", L"401 Sophisticated Fight -Bitwa komendy-", L"402 To be Suggestive", L"403 Srebrna wola", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Strażnik skarbu", L"407 Zniszczyć !!", L"408 Gasnąca gwiazda", L"410 Pinch !!", L"500 Gdzie są gwiazdy Harmonica krótka", L"501 Bursztynowa miłość Hum Ver.(Japoński)", L"501e Bursztynowa miłość Hum Ver.", L"502 Bursztynowa miłość Fortepian Ver.", L"502b Bursztynowa miłość Fortepian Ver.1.5", L"503 Bursztynowa miłość Lutnia Ver.", L"504 Gdzie są gwiazdy Harmonica długa", L"505 Idźmy wesoło", L"510 Determinacja do odejścia", L"511 Ci działający w cieniu", L"512 Nie daj mu uciec !", L"513 W moim sercu", L"514 W świetle księżyca", L"516 Pełzający kryzys", L"517 Jesteśmy rodziną Capua !", L"518 Ścieżka odejścia", L"519 Odzyskanie", L"520 Wyzwolenie od klątwy, i...", L"521 Wyznanie", L"522 Czarny Orbment", L"523 Duma Liberl", L"530 Suita Madrygał Białego Kwiatu - Troska księżniczki", L"531 Suita Madrygał - Lament rycerzy", L"532 Suita Madrygał - Zamiary każdego", L"533 Suita Madrygał - Zamek", L"534 Suita Madrygał - Koloseum", L"535 Suita Madrygał - Pojedynek", L"536 Suita Madrygał - Śmierć księżniczki", L"537 Suita Madrygał - Wielki finał", L""
					};
					TCHAR ti1_tr[][100] = {
						L"001 Rüzgarla dans eden duygular", L"100 Rolent - İl şehri", L"101 Bose - Ticaret şehri", L"102 Ruan - Liman şehri", L"103 Zeiss - Atölye şehri", L"104 Grancel - Kraliyet başkenti", L"105 Güneşte kedi", L"106 Sınır devriyesi kolay değil", L"107 Kraliyet kalesi", L"108 Grand Arena", L"108b Grand Arena (Intro yok)", L"200 Liberl'de nasıl yürünür", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Karanlıkta dolaşma", L"301 Yolu kapatan çelik zemin", L"302 Karanlıktaki huzur", L"303 Dörtlü kuleler", L"304 Leiston kalesi", L"305 Işık boş arazisi", L"400 Sophisticated Fight -Hızlı savaş-", L"401 Sophisticated Fight -Komut savaşı-", L"402 To be Suggestive", L"403 Gümüş irade", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Hazine bekçisi", L"407 Ez !!", L"408 Solan yıldız", L"410 Pinch !!", L"500 Yıldızların olduğu yer Mızıka kısa", L"501 Kehribar aşkı Hum Ver.(Japonca)", L"501e Kehribar aşkı Hum Ver.", L"502 Kehribar aşkı Piyano Ver.", L"502b Kehribar aşkı Piyano Ver.1.5", L"503 Kehribar aşkı Lut Ver.", L"504 Yıldızların olduğu yer Mızıka uzun", L"505 Neşeyle gidelim", L"510 Ayrılma kararlılığı", L"511 Gölgelerde hareket edenler", L"512 Kaçmasına izin verme !", L"513 Kalbimde", L"514 Ay ışığı altında", L"516 Sinsice yaklaşan kriz", L"517 Biz Capua ailesiyiz !", L"518 Ayrılış yolu", L"519 Geri alma", L"520 Lanetten kurtulma, ve...", L"521 İtiraf", L"522 Siyah Orbment", L"523 Liberl gururu", L"530 Beyaz Çiçek Madrigal Süiti - Prenses endişesi", L"531 Madrigal Süiti - Şövalyelerin ağıtı", L"532 Madrigal Süiti - Herkesin niyeti", L"533 Madrigal Süiti - Kale", L"534 Madrigal Süiti - Kolezyum", L"535 Madrigal Süiti - Düello", L"536 Madrigal Süiti - Prensesin ölümü", L"537 Madrigal Süiti - Büyük final", L""
					};
					auto PL_FC_Track = [&](int j) -> CString {
						switch (savedata.lang) {
						case 0: return CString(ti1[j]);
						case 1: return CString(ti1_en[j]);
						case 2: return CString(ti1_fr[j]);
						case 3: return CString(ti1_it[j]);
						case 4: return CString(ti1_es[j]);
						case 5: return CString(ti1_ko[j]);
						case 6: return CString(ti1_zh[j]);
						case 7: return CString(ti1_ar[j]);
						case 8: return CString(ti1_ru[j]);
						case 9: return CString(ti1_de[j]);
						case 10: return CString(ti1_pt[j]);
						case 11: return CString(ti1_nl[j]);
						case 12: return CString(ti1_pl[j]);
						case 13: return CString(ti1_tr[j]);
						default: return CString(ti1_en[j]);
						}
					};
					// TOC 先頭の 32byte レコードはループ表ではなく索引。誤解釈した
					// loop1/2 を入れると初回再生で変な数字になり、再演奏(smpl再読込)で直る。
					// ループは再生時に WAV smpl から取るので、ここでは 0 のままにする。

					char data[33] = { 0 };
					char data0;

					CFile f;
					if (f.Open(fname, CFile::modeRead | CFile::shareDenyWrite)) {
						//空の軌跡 The 1st
						f.SeekToBegin();
						f.Seek(0x770, CFile::begin);
						ZeroMemory(data, 21);
						//bgmのファイル名は20文字
						//bgmファイル名取得
						CString a = L"";
						for (int i = 0; i < 59; i++) {
							f.Read(data, 20);
							CString s = CString(data);
							if (s.Find(L"fmt") > 0) break;
							if (s.Find(L"F") > 0) break;
							if (s.Find(L"b.") > 0 || s.Find(L"e.") > 0) {
								f.Read(&data0, 1); }

							//10文字目から、ed6001.wav と入っているので、001だけ抜き出す
							CString s1 = s.Mid(12, 4); s1.Replace(L".", L""); 
							a = CString(s1) + L" ";
							CString aa1a = L"";
							p.loop1 = 0;
							p.loop2 = 0;
							for (int j = 0;; j++) {
								CString s2 = ti1[j];
								if (s2 == "") {
									a += LL14(
										L"不明",            /* 0: ja */
										L"Unknown",         /* 1: en */
										L"Inconnu",         /* 2: fr */
										L"Sconosciuto",     /* 3: it */
										L"Desconocido",     /* 4: es */
										L"미상",            /* 5: ko */
										L"不明",            /* 6: zh */
										L"غير معروف",      /* 7: ar */
										L"Неизвестно",      /* 8: ru */
										L"Unbekannt",       /* 9: de */
										L"Desconhecido",    /* 10: pt */
										L"Onbekend",        /* 11: nl */
										L"Nieznany",        /* 12: pl */
										L"Bilinmiyor"       /* 13: tr */
									);
									break;
								}

								if (s2.Left(4).Trim() == s1) {
									a = PL_FC_Track(j).Mid(4);
									aa1a = CString(ti1[j]).Left(4).Trim();
									if (aa1a == L"501e") {
										if (ft == L"bgm1.pac") a += L"(English)";
										if (ft == L"bgm2.pac") a += L"(English)";
										if (ft == L"bgm3.pac") a += LL14(
											L"(日本語)",        /* 0: ja */
											L"(Japanese)",     /* 1: en */
											L"(Japonais)",     /* 2: fr */
											L"(Giapponese)",   /* 3: it */
											L"(Japonés)",      /* 4: es */
											L"(일본어)",        /* 5: ko */
											L"(日本語)",        /* 6: zh */
											L"(اليابانية)",    /* 7: ar */
											L"(Японский)",     /* 8: ru */
											L"(Japanisch)",    /* 9: de */
											L"(Japonês)",      /* 10: pt */
											L"(Japans)",       /* 11: nl */
											L"(Japoński)",     /* 12: pl */
											L"(Japonca)"       /* 13: tr */
										);
									}
									break;
								}
							}

							_tcscpy(p.name, a);
							_tcscpy(p.fol, fname + L"::" + aa1a + a);
							p.alb[0] = 0;
							p.art[0] = 0;

							if (ft == L"bgm1.pac") {
								wcscpy(p.art, LL14(
									L"steam版 空の軌跡 1st bgm1.pac",      /* 0: ja */
									L"Steam Trails in the Sky 1st bgm1.pac", /* 1: en */
									L"Steam Les Sentiers du Ciel 1st bgm1.pac", /* 2: fr */
									L"Steam Trails in the Sky 1st bgm1.pac", /* 3: it */
									L"Steam Trails in the Sky 1st bgm1.pac", /* 4: es */
									L"Steam 하늘의 궤적 1st bgm1.pac",       /* 5: ko */
									L"Steam 空之轨迹 1st bgm1.pac",         /* 6: zh */
									L"Steam Trails in the Sky 1st bgm1.pac", /* 7: ar */
									L"Steam Тропы в Небе 1st bgm1.pac",      /* 8: ru */
									L"Steam Himmelsleitern 1st bgm1.pac",    /* 9: de */
									L"Steam Trails in the Sky 1st bgm1.pac", /* 10: pt */
									L"Steam Trails in the Sky 1st bgm1.pac", /* 11: nl */
									L"Steam Trails in the Sky 1st bgm1.pac", /* 12: pl */
									L"Steam Trails in the Sky 1st bgm1.pac"  /* 13: tr */
								));
								wcscpy(p.alb, LL14(
									L"BGM:標準",            /* 0: ja */
									L"BGM:Standard",        /* 1: en */
									L"BGM:Standard",        /* 2: fr */
									L"BGM:Standard",        /* 3: it */
									L"BGM:Estándar",        /* 4: es */
									L"BGM:표준",            /* 5: ko */
									L"BGM:标准",            /* 6: zh */
									L"BGM:قياسي",          /* 7: ar */
									L"BGM:Стандарт",        /* 8: ru */
									L"BGM:Standard",        /* 9: de */
									L"BGM:Padrão",          /* 10: pt */
									L"BGM:Standaard",       /* 11: nl */
									L"BGM:Standard",        /* 12: pl */
									L"BGM:Standart"         /* 13: tr */
								));
							}

							if (ft == L"bgm2.pac") {
								wcscpy(p.art, LL14(
									L"steam版 空の軌跡 1st bgm2.pac",
									L"Steam Trails in the Sky 1st bgm2.pac",
									L"Steam Les Sentiers du Ciel 1st bgm2.pac",
									L"Steam Trails in the Sky 1st bgm2.pac",
									L"Steam Trails in the Sky 1st bgm2.pac",
									L"Steam 하늘의 궤적 1st bgm2.pac",
									L"Steam 空之轨迹 1st bgm2.pac",
									L"Steam Trails in the Sky 1st bgm2.pac",
									L"Steam Тропы в Небе 1st bgm2.pac",
									L"Steam Himmelsleitern 1st bgm2.pac",
									L"Steam Trails in the Sky 1st bgm2.pac",
									L"Steam Trails in the Sky 1st bgm2.pac",
									L"Steam Trails in the Sky 1st bgm2.pac",
									L"Steam Trails in the Sky 1st bgm2.pac"
								));
								wcscpy(p.alb, LL14(
									L"BGM:アレンジ",          /* 0: ja */
									L"BGM:Arrange",         /* 1: en */
									L"BGM:Arrangement",     /* 2: fr */
									L"BGM:Arrangiamento",   /* 3: it */
									L"BGM:Arreglo",         /* 4: es */
									L"BGM:어레인지",         /* 5: ko */
									L"BGM:改編",            /* 6: zh */
									L"BGM:توزيع",          /* 7: ar */
									L"BGM:Аранжировка",     /* 8: ru */
									L"BGM:Arrangement",     /* 9: de */
									L"BGM:Arranjo",         /* 10: pt */
									L"BGM:Arrangement",     /* 11: nl */
									L"BGM:Aranżacja",       /* 12: pl */
									L"BGM:Aranjman"         /* 13: tr */
								));
							}

							if (ft == L"bgm3.pac") {
								wcscpy(p.art, LL14(
									L"steam版 空の軌跡 1st bgm3.pac",
									L"Steam Trails in the Sky 1st bgm3.pac",
									L"Steam Les Sentiers du Ciel 1st bgm3.pac",
									L"Steam Trails in the Sky 1st bgm3.pac",
									L"Steam Trails in the Sky 1st bgm3.pac",
									L"Steam 하늘의 궤적 1st bgm3.pac",
									L"Steam 空之轨迹 1st bgm3.pac",
									L"Steam Trails in the Sky 1st bgm3.pac",
									L"Steam Тропы в Небе 1st bgm3.pac",
									L"Steam Himmelsleitern 1st bgm3.pac",
									L"Steam Trails in the Sky 1st bgm3.pac",
									L"Steam Trails in the Sky 1st bgm3.pac",
									L"Steam Trails in the Sky 1st bgm3.pac",
									L"Steam Trails in the Sky 1st bgm3.pac"
								));
								wcscpy(p.alb, LL14(
									L"BGM:オリジナル",        /* 0: ja */
									L"BGM:Original",        /* 1: en */
									L"BGM:Original",        /* 2: fr */
									L"BGM:Originale",       /* 3: it */
									L"BGM:Original",        /* 4: es */
									L"BGM:오리지널",         /* 5: ko */
									L"BGM:原创",            /* 6: zh */
									L"BGM:أصلي",           /* 7: ar */
									L"BGM:Оригинал",        /* 8: ru */
									L"BGM:Original",        /* 9: de */
									L"BGM:Original",        /* 10: pt */
									L"BGM:Origineel",       /* 11: nl */
									L"BGM:Oryginał",        /* 12: pl */
									L"BGM:Orijinal"         /* 13: tr */
								));
							}
							if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; syomode = 30; }
							Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
						}

						f.Close();
					}
				}
				else if (ft.Right(5) == ".opus") {
					p.sub = -6; p.loop1 = p.loop2 = 0;
					CString a = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					CString b = a.Mid(6, 1);
					int err;
					int fff = 0;
					// ed9 + フォルダ名で mode 33/34/35（インストール先はユーザー依存）
					{
						CString pathL = fname;
						pathL.MakeLower();
						if (a.GetLength() >= 3 && a.Left(3).CompareNoCase(L"ed9") == 0) {
							// Ⅱ を先に判定（Daybreak / 黎の軌跡 単体はⅠ）
							if (pathL.Find(L"daybreak ii") >= 0 || pathL.Find(L"crimson sin") >= 0
								|| pathL.Find(L"黎の軌跡ⅱ") >= 0 || pathL.Find(L"黎の軌跡Ⅱ") >= 0
								|| pathL.Find(L"黎の軌跡ii") >= 0 || pathL.Find(L"黎の軌跡 ii") >= 0 || pathL.Find(L"黎の軌跡2") >= 0
								|| pathL.Find(L"黎之轨迹ⅱ") >= 0 || pathL.Find(L"黎之轨迹Ⅱ") >= 0 || pathL.Find(L"黎之轨迹ii") >= 0)
								p.sub = 34;
							else if (pathL.Find(L"beyond the horizon") >= 0 || pathL.Find(L"界の軌跡") >= 0
								|| pathL.Find(L"kai no kiseki") >= 0 || pathL.Find(L"界之轨迹") >= 0)
								p.sub = 35;
							else if (pathL.Find(L"daybreak") >= 0 || pathL.Find(L"黎の軌跡") >= 0 || pathL.Find(L"黎之轨迹") >= 0)
								p.sub = 33;
						}
					}
					// Ys X - 楽曲情報の一括修正
// 前半：ファイル名判定による「音楽」カテゴリ設定
					if (a.Left(2) == L"y_" && a.Right(5) == L".opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Música", L"음악", L"音乐", L"موسيقى", L"Музыка", L"Musik", L"Música", L"Muziek", L"Muzyka", L"Müzik");
						fff = 1;
					}

					// 後半：詳細タイトル設定
					// Add 中の filen は再生中パス(グローバル)なので、スキャン中の basename は ft2 を使う
					CString ft = ft2;

					if (ft == L"y_act_e002.opus") {
						a = L"Operation SANDRAS"; fff = 1;
					}
					else if (ft == L"y_act_e002_s1.opus") {
						a = LL14(L"Operation SANDRAS(重低音)", L"Operation SANDRAS (Bass Boost)", L"Operation SANDRAS (Renfort graves)", L"Operation SANDRAS (Rinforzo bassi)", L"Operation SANDRAS (Refuerzo graves)", L"Operation SANDRAS (저음 강조)", L"Operation SANDRAS (重低音)", L"Operation SANDRAS (تعزيز الجهير)", L"Operation SANDRAS (Усиление низких)", L"Operation SANDRAS (Bassverstärkung)", L"Operation SANDRAS (Reforço graves)", L"Operation SANDRAS (Basversterking)", L"Operation SANDRAS (Wzmocnienie basów)", L"Operation SANDRAS (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b100.opus") {
						a = L"Overblaze"; fff = 1;
					}
					else if (ft == L"y_b100_s1.opus") {
						a = LL14(L"Overblaze(重低音)", L"Overblaze (Bass Boost)", L"Overblaze (Renfort graves)", L"Overblaze (Rinforzo bassi)", L"Overblaze (Refuerzo graves)", L"Overblaze (저음 강조)", L"Overblaze (重低音)", L"Overblaze (تعزيز الجهير)", L"Overblaze (Усиление низких)", L"Overblaze (Bassverstärkung)", L"Overblaze (Reforço graves)", L"Overblaze (Basversterking)", L"Overblaze (Wzmocnienie basów)", L"Overblaze (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b200.opus") {
						a = L"Through the North Wind"; fff = 1;
					}
					else if (ft == L"y_b200_s1.opus") {
						a = LL14(L"Through the North Wind(重低音)", L"Through the North Wind (Bass Boost)", L"Through the North Wind (Renfort graves)", L"Through the North Wind (Rinforzo bassi)", L"Through the North Wind (Refuerzo graves)", L"Through the North Wind (저음 강조)", L"Through the North Wind (重低音)", L"Through the North Wind (تعزيز الجهير)", L"Through the North Wind (Усиление низких)", L"Through the North Wind (Bassverstärkung)", L"Through the North Wind (Reforço graves)", L"Through the North Wind (Basversterking)", L"Through the North Wind (Wzmocnienie basów)", L"Through the North Wind (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b210.opus") {
						a = LL14(L"高鳴る鼓動", L"Pounding Heartbeat", L"Battement de cœur saccadé", L"Battito accelerato", L"Latido palpitante", L"두근거리는 고동", L"剧烈的心跳", L"نبضات القلب المتسارعة", L"Учащенное сердцебиение", L"Pochendes Herzklopfen", L"Batida forte do coração", L"Bonzend hart", L"Łomoczące serce", L"Küt Küt Atan Kalp");
						fff = 1;
					}
					else if (ft == L"y_b210_s1.opus") {
						a = LL14(L"高鳴る鼓動(重低音)", L"Pounding Heartbeat (Bass Boost)", L"Pounding Heartbeat (Renfort graves)", L"Pounding Heartbeat (Rinforzo bassi)", L"Pounding Heartbeat (Refuerzo graves)", L"Pounding Heartbeat (저음 강조)", L"剧烈的心跳 (重低音)", L"Pounding Heartbeat (تعزيز الجهير)", L"Pounding Heartbeat (Усиление низких)", L"Pounding Heartbeat (Bassverstärkung)", L"Pounding Heartbeat (Reforço graves)", L"Pounding Heartbeat (Basversterking)", L"Pounding Heartbeat (Wzmocnienie basów)", L"Pounding Heartbeat (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b300.opus") {
						a = LL14(L"石火の如く", L"Like Flint", L"Comme le silex", L"Come la selce", L"Como el sílex", L"전광석화처럼", L"如同火石", L"مثل الصوان", L"Словно кремень", L"Wie Feuerstein", L"Como pederneira", L"Als vuursteen", L"Jak krzemień", L"Çakmak Taşı Gibi");
						fff = 1;
					}
					else if (ft == L"y_b300_s1.opus") {
						a = LL14(L"石火の如く(重低音)", L"Like Flint (Bass Boost)", L"Like Flint (Renfort graves)", L"Like Flint (Rinforzo bassi)", L"Like Flint (Refuerzo graves)", L"Like Flint (저음 강조)", L"如同火石 (重低音)", L"Like Flint (تعزيز الجهير)", L"Like Flint (Усиление низких)", L"Like Flint (Bassverstärkung)", L"Like Flint (Reforço graves)", L"Like Flint (Basversterking)", L"Like Flint (Wzmocnienie basów)", L"Like Flint (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b400.opus") {
						a = L"Can You Do It"; fff = 1;
					}
					else if (ft == L"y_b400_s1.opus") {
						a = LL14(L"Can You Do It(重低音)", L"Can You Do It (Bass Boost)", L"Can You Do It (Renfort graves)", L"Can You Do It (Rinforzo bassi)", L"Can You Do It (Refuerzo graves)", L"Can You Do It (저음 강조)", L"Can You Do It (重低音)", L"Can You Do It (تعزيز الجهير)", L"Can You Do It (Усиление низких)", L"Can You Do It (Bassverstärkung)", L"Can You Do It (Reforço graves)", L"Can You Do It (Basversterking)", L"Can You Do It (Wzmocnienie basów)", L"Can You Do It (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b500.opus") {
						a = LL14(L"BERSERK -戦斧の咆哮-", L"BERSERK -Roar of the Battle Axe-", L"BERSERK -Rugissement de la hache de guerre-", L"BERSERK -Ruggito dell'ascia da battaglia-", L"BERSERK -Rugido del hacha de batalla-", L"BERSERK -전부의 포효-", L"BERSERK -战斧的咆哮-", L"BERSERK - زئير فأس الحرب", L"BERSERK -Рев боевого топора-", L"BERSERK -Brüllen der Streitaxt-", L"BERSERK -Rugido do machado de batalha-", L"BERSERK -Geknal van de strijdbijl-", L"BERSERK -Ryk topora wojennego-", L"BERSERK -Savaş Baltasının Kükreyişi-");
						fff = 1;
					}
					else if (ft == L"y_b500_s1.opus") {
						a = LL14(L"BERSERK -戦斧の咆哮-(重低音)", L"BERSERK -Roar of the Battle Axe- (Bass Boost)", L"BERSERK -Roar of the Battle Axe- (Renfort graves)", L"BERSERK -Roar of the Battle Axe- (Rinforzo bassi)", L"BERSERK -Roar of the Battle Axe- (Refuerzo graves)", L"BERSERK -Roar of the Battle Axe- (저음 강조)", L"BERSERK -战斧的咆哮- (重低音)", L"BERSERK -Roar of the Battle Axe- (تعزيز الجهير)", L"BERSERK -Roar of the Battle Axe- (Усиление низких)", L"BERSERK -Roar of the Battle Axe- (Bassverstärkung)", L"BERSERK -Roar of the Battle Axe- (Reforço graves)", L"BERSERK -Roar of the Battle Axe- (Basversterking)", L"BERSERK -Roar of the Battle Axe- (Wzmocnienie basów)", L"BERSERK -Roar of the Battle Axe- (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b510.opus") {
						a = LL14(L"悪意の洗礼", L"Baptism of Malice", L"Baptême de malice", L"Battesimo di malizia", L"Bautismo de malicia", L"악의의 세례", L"恶意的洗礼", L"معمودية الخبث", L"Крещение злобой", L"Taufe der Bosheit", L"Batismo de malícia", L"Doop van kwaadaardigheid", L"Chrzest złośliwości", L"Garez Vaftizi");
						fff = 1;
					}
					else if (ft == L"y_b510_s1.opus") {
						a = LL14(L"悪意の洗礼(重低音)", L"Baptism of Malice (Bass Boost)", L"Baptism of Malice (Renfort graves)", L"Baptism of Malice (Rinforzo bassi)", L"Baptism of Malice (Refuerzo graves)", L"Baptism of Malice (저음 강조)", L"恶意的洗礼 (重低音)", L"Baptism of Malice (تعزيز الجهير)", L"Baptism of Malice (Усиление низких)", L"Baptism of Malice (Bassverstärkung)", L"Baptism of Malice (Reforço graves)", L"Baptism of Malice (Basversterking)", L"Baptism of Malice (Wzmocnienie basów)", L"Baptism of Malice (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b520.opus") {
						a = L"The Ultimate Pleasure in My Hands"; fff = 1;
					}
					else if (ft == L"y_b520_s1.opus") {
						a = LL14(L"The Ultimate Pleasure in My Hands(重低音)", L"The Ultimate Pleasure in My Hands (Bass Boost)", L"The Ultimate Pleasure in My Hands (Renfort graves)", L"The Ultimate Pleasure in My Hands (Rinforzo bassi)", L"The Ultimate Pleasure in My Hands (Refuerzo graves)", L"The Ultimate Pleasure in My Hands (저음 강조)", L"The Ultimate Pleasure in My Hands (重低音)", L"The Ultimate Pleasure in My Hands (تعزيز الجهير)", L"The Ultimate Pleasure in My Hands (Усиление низких)", L"The Ultimate Pleasure in My Hands (Bassverstärkung)", L"The Ultimate Pleasure in My Hands (Reforço graves)", L"The Ultimate Pleasure in My Hands (Basversterking)", L"The Ultimate Pleasure in My Hands (Wzmocnienie basów)", L"The Ultimate Pleasure in My Hands (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b610.opus") {
						a = LL14(L"辿り着いた極光の下で", L"Under the Northern Lights", L"Sous les aurores boréales", L"Sotto l'aurora boreale", L"Bajo la aurora boreal", L"다다른 극광 아래에서", L"抵达极光之下", L"تحت أضواء الشفق القطبي", L"Под северным сиянием", L"Unter dem Nordlicht", L"Sob a aurora boreal", L"Onder het noorderlicht", L"Pod zorzą polarną", L"Kuzey Işıkları Altında");
						fff = 1;
					}
					else if (ft == L"y_b610_s1.opus") {
						a = LL14(L"辿り着いた極光の下で(重低音)", L"Under the Northern Lights (Bass Boost)", L"Under the Northern Lights (Renfort graves)", L"Under the Northern Lights (Rinforzo bassi)", L"Under the Northern Lights (Refuerzo graves)", L"Under the Northern Lights (저음 강조)", L"抵达极光之下 (重低音)", L"Under the Northern Lights (تعزيز الجهير)", L"Under the Northern Lights (Усиление низких)", L"Under the Northern Lights (Bassverstärkung)", L"Under the Northern Lights (Reforço graves)", L"Under the Northern Lights (Basversterking)", L"Under the Northern Lights (Wzmocnienie basów)", L"Under the Northern Lights (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b620.opus") {
						a = L"Nordics Saga -The Endless Bloody Sea-"; fff = 1;
					}
					else if (ft == L"y_b620_s1.opus") {
						a = LL14(L"Nordics Saga -The Endless Bloody Sea-(重低音)", L"Nordics Saga (Bass Boost)", L"Nordics Saga (Renfort graves)", L"Nordics Saga (Rinforzo bassi)", L"Nordics Saga (Refuerzo graves)", L"Nordics Saga (저음 강조)", L"Nordics Saga (重低音)", L"Nordics Saga (تعزيز الجهير)", L"Nordics Saga (Усиление низких)", L"Nordics Saga (Bassverstärkung)", L"Nordics Saga (Reforço graves)", L"Nordics Saga (Basversterking)", L"Nordics Saga (Wzmocnienie basów)", L"Nordics Saga (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b700.opus") {
						a = L"Ready to Fire!"; fff = 1;
					}
					else if (ft == L"y_b700_s1.opus") {
						a = LL14(L"Ready to Fire!(重低音)", L"Ready to Fire! (Bass Boost)", L"Ready to Fire! (Renfort graves)", L"Ready to Fire! (Rinforzo bassi)", L"Ready to Fire! (Refuerzo graves)", L"Ready to Fire! (저음 강조)", L"Ready to Fire! (重低音)", L"Ready to Fire! (تعزيز الجهير)", L"Ready to Fire! (Усиление низких)", L"Ready to Fire! (Bassverstärkung)", L"Ready to Fire! (Reforço graves)", L"Ready to Fire! (Basversterking)", L"Ready to Fire! (Wzmocnienie basów)", L"Ready to Fire! (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b710.opus") {
						a = L"Hello, Those Who Can't Die"; fff = 1;
					}
					else if (ft == L"y_b710_s1.opus") {
						a = LL14(L"Hello, Those Who Can't Die(重低音)", L"Hello (Bass Boost)", L"Hello (Renfort graves)", L"Hello (Rinforzo bassi)", L"Hello (Refuerzo graves)", L"Hello (저음 강조)", L"Hello (重低音)", L"Hello (تعزيز الجهير)", L"Hello (Усиление низких)", L"Hello (Bassverstärkung)", L"Hello (Reforço graves)", L"Hello (Basversterking)", L"Hello (Wzmocnienie basów)", L"Hello (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_b720.opus") {
						a = L"Landing Warfare"; fff = 1;
					}
					else if (ft == L"y_b720_s1.opus") {
						a = LL14(L"Landing Warfare(重低音)", L"Landing Warfare (Bass Boost)", L"Landing Warfare (Renfort graves)", L"Landing Warfare (Rinforzo bassi)", L"Landing Warfare (Refuerzo graves)", L"Landing Warfare (저음 강조)", L"Landing Warfare (重低音)", L"Landing Warfare (تعزيز الجهير)", L"Landing Warfare (Усиление низких)", L"Landing Warfare (Bassverstärkung)", L"Landing Warfare (Reforço graves)", L"Landing Warfare (Basversterking)", L"Landing Warfare (Wzmocnienie basów)", L"Landing Warfare (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_bgm_none.opus") {
						a = LL14(L"無音", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"무음", L"无音", L"صمت", L"Тишина", L"Stille", L"Silencio", L"Stilte", L"Cisza", L"Sessizlik");
						fff = 1;
					}
					else if (ft == L"y_d100.opus") {
						a = LL14(L"光届かぬその奥に", L"In the Depths Where Light Doesn't Reach", L"Dans les profondeurs hors de portée de la lumière", L"Nelle profondità dove non arriva la luce", L"En las profundidades donde no llega la luz", L"빛이 닿지 않는 그 깊은 곳에", L"光线无法到达的深处", L"في الأعماق حيث لا يصل الضوء", L"В глубинах, куда не доходит свет", L"In den Tiefen, die kein Licht erreicht", L"Nas profundezas onde a luz não chega", L"In de diepten waar geen licht komt", L"W głębinach, gdzie nie sięga światło", L"Işığın Ulaşamadığı Derinliklerde");
						fff = 1;
					}
					else if (ft == L"y_d100_s1.opus") {
						a = LL14(L"光届かぬその奥に(重低音)", L"In the Depths (Bass Boost)", L"In the Depths (Renfort graves)", L"In the Depths (Rinforzo bassi)", L"In the Depths (Refuerzo graves)", L"In the Depths (저음 강조)", L"光线无法到达的深处 (重低音)", L"In the Depths (تعزيز الجهير)", L"In the Depths (Усиление низких)", L"In the Depths (Bassverstärkung)", L"In the Depths (Reforço graves)", L"In the Depths (Basversterking)", L"In the Depths (Wzmocnienie basów)", L"In the Depths (Bas güçlendirme)");
						fff = 1;
					}
					else if (ft == L"y_d200.opus") {
						a = L"Eerie Stillness"; fff = 1;
					}
					else if (ft == L"y_d400.opus") {
						a = LL14(L"飽くなき渇望", L"Insatiable Thirst", L"Soif insatiable", L"Sete insaziabile", L"Sed insaciable", L"끝없는 갈망", L"永无止境的渴望", L"عطش لا ينتهي", L"Ненасытная жажда", L"Unstillbares Verlangen", L"Sede insaciável", L"Onverzadigbare dorst", L"Nienasycone pragnienie", L"Doymak Bilmez Susuzluk");
						fff = 1;
					}
					else if (ft == L"y_d410.opus") {
						a = L"The Inner Darkness"; fff = 1;
					}
					else if (ft == L"y_d500.opus") {
						a = L"Hardhearted Rock Line"; fff = 1;
					}
					else if (ft == L"y_d600.opus") {
						a = LL14(L"夢の痕跡", L"Dream Traces", L"Traces de rêves", L"Tracce di sogni", L"Rastros de sueños", L"꿈의 흔적", L"梦的痕迹", L"آثار الأحلام", L"Следы снов", L"Traumspuren", L"Rastros de sonhos", L"Droomsporen", L"Ślady snów", L"Rüya İzleri");
						fff = 1;
					}
					else if (ft == L"y_d710.opus") {
						a = LL14(L"甲鉄戦艦ナグルファ", L"Ironclad Battleship Naglfar", L"Cuirassé Naglfar", L"Corazzata Naglfar", L"Acorazado Naglfar", L"갑철전함 나글파르", L"甲铁战舰 Naglfar", L"البارجة الحديدية ناجلفار", L"Броненосец Нагльфар", L"Panzerschiff Naglfar", L"Encouraçado Naglfar", L"Slagschip Naglfar", L"Pancernik Naglfar", L"Zırhlı Savaş Gemisi Naglfar");
						fff = 1;
					}
					else if (ft == L"y_d800.opus") {
						a = L"LILA -Innocent Wish-"; fff = 1;
					}
					else if (ft == L"y_d900.opus") {
						a = LL14(L"エギル海底神殿", L"Egil Undersea Temple", L"Temple sous-marin d'Egil", L"Tempio sottomarino di Egil", L"Templo submarino de Egil", L"에길 해저신전", L"Egil 海底神殿", L"معبد إيغيل تحت البحر", L"Подводный храм Эгиля", L"Egil-Unterseetempel", L"Templo submarino de Egil", L"Egil onderzeese tempel", L"Podmorska świątynia Egila", L"Egil Denizaltı Tapınağı");
						fff = 1;
					}
					else if (ft == L"y_d1010.opus") {
						a = L"The Paradise Lost of Norman"; fff = 1;
					}
					else if (ft == L"y_e004.opus") {
						a = LL14(L"あの時からずっと…", L"Ever Since That Day...", L"Depuis ce jour-là...", L"Da quel giorno...", L"Desde aquel día...", L"그때부터 줄곧...", L"从那时起一直...", L"منذ ذلك اليوم...", L"С того самого дня...", L"Seit jenem Tag...", L"Desde aquele dia...", L"Sinds die dag...", L"Od tamtego dnia...", L"O Günden Beri...");
						fff = 1;
					}
					else if (ft == L"y_e006.opus") {
						a = LL14(L"切っても切れない絆", L"Unbreakable Bonds", L"Liens indéfectibles", L"Legami indissolubili", L"Vínculos inquebrantables", L"뗄래야 뗄 수 없는 인연", L"无法割舍的羁绊", L"روابط لا تنفصم", L"Неразрывные узы", L"Unzerbrechliche Bande", L"Laços inquebráveis", L"Onbreekbare banden", L"Nierozerwalne więzi", L"Yıkılmaz Bağlar");
						fff = 1;
					}
					else if (ft == L"y_e007.opus") {
						a = LL14(L"灰色の深層", L"Gray Depths", L"Profondeurs grises", L"Profondità grigie", L"Profundidades grises", L"회색의 심층", L"灰色的深层", L"الأعماق الرمادية", L"Серые глубины", L"Graue Tiefen", L"Profundezas cinzentas", L"Grijze diepten", L"Szare głębiny", L"Gri Derinlikler");
						fff = 1;
					}
					else if (ft == L"y_e009.opus") {
						a = LL14(L"歪な願望", L"Twisted Desire", L"Désir tordu", L"Desiderio distorto", L"Deseo retorcido", L"일그러진 염원", L"歪曲的愿望", L"رغبة ملتوية", L"Искаженное желание", L"Verdrehtes Verlangen", L"Desejo distorcido", L"Verdraaid verlangen", L"Skręcone pragnienie", L"Çarpık Arzu");
						fff = 1;
					}
					else if (ft == L"y_e012.opus") {
						a = LL14(L"手筈通りに", L"As Planned", L"Comme prévu", L"Come pianificato", L"Como se planeó", L"절차대로", L"按照计划", L"كما هو مخطط له", L"Как и планировалось", L"Wie geplant", L"Como planejado", L"Zoals gepland", L"Zgodnie z planem", L"Planlandığı Gibi");
						fff = 1;
					}
					else if (ft == L"y_f160.opus") {
						a = LL14(L"瞳の中の少年剣士", L"Young Swordsman in My Eyes", L"Le jeune épéiste dans mes yeux", L"Il giovane spadaccino nei miei occhi", L"El joven espadachín en mis ojos", L"눈 속의 소년 검사", L"瞳孔中的少年剑士", L"المبارز الفتى في عيني", L"Юный мечник в моих глазах", L"Junger Schwertkämpfer in meinen Augen", L"Jovem espadachim nos meus olhos", L"Jonge zwaardvechter in mijn ogen", L"Młody szermierz w moich oczach", L"Gözlerimdeki Genç Kılıç Ustası");
						fff = 1;
					}
					else if (ft == L"y_f200.opus") {
						a = LL14(L"錨を揚げろ！", L"Weigh Anchor!", L"Levez l'ancre !", L"Leva l'ancora!", L"¡Leven anclas!", L"닻을 올려라!", L"起锚！", L"ارفعوا المرساة!", L"Поднять якорь!", L"Anker lichten!", L"Levantar âncora!", L"Licht het anker!", L"Podnieść kotwicę!", L"Demir Al!");
						fff = 1;
					}
					else if (ft == L"y_f210.opus") {
						a = LL14(L"悠き海に生きる者", L"Those Who Live in the Vast Sea", L"Ceux qui vivent dans la mer vaste", L"Coloro che vivono nel vasto mare", L"Aquellos que viven en el mar vasto", L"유구한 바다에 사는 자", L"生活在悠久大海的人", L"الذين يعيشون في البحر الشاسع", L"Те, кто живет в бескрайнем море", L"Die im weiten Meer leben", L"Aqueles que vivem no mar vasto", L"Zij die in de onmetelijke zee leven", L"Ci, którzy żyją w rozległym morzu", L"Engin Denizlerde Yaşayanlar");
						fff = 1;
					}
					else if (ft == L"y_f220.opus") {
						a = LL14(L"コンパスは踊る", L"The Compass Dances", L"La boussole danse", L"La bussola danza", L"La brújula danza", L"나침반은 춤춘다", L"罗盤在跳舞", L"البوصلة ترقص", L"Компас танцует", L"Der Kompass tanzt", L"A bússola dança", L"Het kompas danst", L"Kompas tańczy", L"Pusula Dans Ediyor");
						fff = 1;
					}
					else if (ft == L"y_f230.opus") {
						a = LL14(L"開闢の海", L"Sea of Genesis", L"Mer de la genèse", L"Mare della genesi", L"Mar de la génesis", L"개벽의 바다", L"开辟之海", L"بحر التكوين", L"Море сотворения", L"Meer der Schöpfung", L"Mar da génese", L"Zee van de genesis", L"Morze genezy", L"Yaratılış Denizi");
						fff = 1;
					}
					else if (ft == L"y_t200.opus") {
						a = LL14(L"根ざすべき場所", L"Where We Belong", L"Là où nous appartenons", L"Il posto a cui apparteniamo", L"El lugar al que pertenecemos", L"뿌리 내려야 할 곳", L"落地生根之处", L"حيث ننتمي", L"Там, где наш дом", L"Wo wir hingehören", L"Onde pertencemos", L"Waar we thuishoren", L"Miejsce, do którego należymy", L"Ait Olduğumuz Yer");
						fff = 1;
					}
					else if (ft == L"y_t500.opus") {
						a = LL14(L"情景に揺蕩う", L"Drifting in the Scene", L"Dérivant dans la scène", L"Oscillando nella scena", L"Derivando en la escena", L"정경 속에 흔들리며", L"浸于情景中", L"تائه في المشهد", L"Дрейфуя в пейзаже", L"In der Szenerie treiben", L"Derivando na cena", L"Drijvend in de scène", L"Dryfując w scenerii", L"Manzarada Süzülmek");
						fff = 1;
					}
					else if (ft == L"y_t600.opus") {
						a = LL14(L"盾の兄弟", L"Shield Brothers", L"Frères de bouclier", L"Fratelli di scudo", L"Hermanos de escudo", L"방패의 형제", L"盾之兄弟", L"إخوة الدروع", L"Братья по щиту", L"Schildbrüder", L"Irmãos de escudo", L"Schildbroeders", L"Bracia tarczy", L"Kalkan Kardeşliği");
						fff = 1;
					}
					else if (ft == L"y_title.opus") {
						a = LL14(L"その優しさは誰のため", L"For Whom Is That Kindness", L"Pour qui est cette gentillesse", L"Per chi è quella gentilezza", L"Para quién es esa amabilidad", L"그 친절은 누구를 위한 것인가", L"那份温柔是为了谁", L"لمن هذا اللطف", L"Для кого эта доброта", L"Wem gilt diese Güte", L"Para quem é essa bondade", L"Voor wie is die vriendelijkheid", L"Dla kogo ta dobroć", L"Bu Nezaket Kimin İçin");
						fff = 1;
					}

					// --- ed9 mode 33 titles (黎I: 当面ファイル名。購入後に曲名対応) ---
					if (fff == 0 && (p.sub == 33)) {
						if (a.Left(3).CompareNoCase(L"ed9") == 0) {
							a = ft;
							if (a.GetLength() > 5 && a.Right(5).CompareNoCase(L".opus") == 0)
								a = a.Left(a.GetLength() - 5);
							fff = 1;
						}
					}

					// --- auto ed9 mode 35 titles ---
					if (fff == 0 && (p.sub == 35)) {
						if (fff == 0 && ft == L"ed91000.opus") {
							a = L"Dark Stalker"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91002.opus") {
							a = LL14(L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91003.opus") {
							a = LL14(L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91004.opus") {
							a = LL14(L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91006.opus") {
							a = L"Captive Soul"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91010.opus") {
							a = LL14(L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91016.opus") {
							a = LL14(L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91020.opus") {
							a = LL14(L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91021.opus") {
							a = LL14(L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91022.opus") {
							a = LL14(L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91024.opus") {
							a = LL14(L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91027.opus") {
							a = LL14(L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91030.opus") {
							a = LL14(L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91031.opus") {
							a = LL14(L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91032.opus") {
							a = LL14(L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91033.opus") {
							a = LL14(L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91034.opus") {
							a = LL14(L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91037.opus") {
							a = LL14(L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91200.opus") {
							a = L"Let me know, my SPRIGGAN..."; fff = 1;
						}
						if (fff == 0 && ft == L"ed91202.opus") {
							a = LL14(L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91203.opus") {
							a = LL14(L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91204.opus") {
							a = L"The Days of Forest Flowers"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91206.opus") {
							a = LL14(L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91207.opus") {
							a = LL14(L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91208.opus") {
							a = L"Cloudy Cloudy"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91210.opus") {
							a = LL14(L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91211.opus") {
							a = LL14(L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91216.opus") {
							a = L"Step in the EDITH"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91218.opus") {
							a = LL14(L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91219.opus") {
							a = L"Hazard Coming"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91220.opus") {
							a = LL14(L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91223.opus") {
							a = L"Wailing Siren"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91224.opus") {
							a = LL14(L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91225.opus") {
							a = LL14(L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91228.opus") {
							a = LL14(L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91229b.opus") {
							a = LL14(L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91300.opus") {
							a = L"First Skirmish"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91301.opus") {
							a = L"OPEN THE SHARD!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92123.opus") {
							a = LL14(L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92153.opus") {
							a = L"Tharbad Night"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92169.opus") {
							a = LL14(L"共に、新たな季節を -Diverse World-", L"Together, Toward a New Season -Diverse World-", L"Ensemble, vers une nouvelle saison -Diverse World-", L"Insieme, verso una nuova stagione -Diverse World-", L"Juntos, hacia una nueva estación -Diverse World-", L"함께, 새로운 계절을 -Diverse World-", L"一起,迎向新的季节 -Diverse World-", L"معاً نحو موسم جديد -Diverse World-", L"Вместе к новому сезону -Diverse World-", L"Gemeinsam in eine neue Jahreszeit -Diverse World-", L"Juntos, rumo a uma nova estação -Diverse World-", L"Samen naar een nieuw seizoen -Diverse World-", L"Razem ku nowej porze roku -Diverse World-", L"Birlikte Yeni Bir Mevsim -Diverse World-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92187.opus") {
							a = LL14(L"空高く風に乗り", L"Riding the Wind High in the Sky", L"Chevaucher le vent haut dans le ciel", L"Cavalcando il vento alto nel cielo", L"Montando el viento alto en el cielo", L"하늘 높이 바람을 타고", L"乘风直上高空", L"ركوب الريح عالياً في السماء", L"Оседлав ветер высоко в небе", L"Hoch am Himmel auf dem Wind reiten", L"Montando o vento alto no céu", L"Hoog in de lucht op de wind rijden", L"Pędząc z wiatrem wysoko na niebie", L"Göklerde Rüzgara Binerek"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92195.opus") {
							a = L"Diabolic Howl"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92197.opus") {
							a = LL14(L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92199.opus") {
							a = L"Too Much Vitality"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92202.opus") {
							a = L"Hard Desperation"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92203.opus") {
							a = L"Make a Breakthrough!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92205.opus") {
							a = LL14(L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92206.opus") {
							a = LL14(L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92211.opus") {
							a = LL14(L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92212.opus") {
							a = L"Scorching Impact"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92213.opus") {
							a = LL14(L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92215.opus") {
							a = LL14(L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92218.opus") {
							a = L"Satisfied Madness"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92219.opus") {
							a = L"Get Rid of the Urgent Menace"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92220.opus") {
							a = LL14(L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92222.opus") {
							a = LL14(L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92226.opus") {
							a = L"Unreasonable Beast"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92227.opus") {
							a = LL14(L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92230.opus") {
							a = L"Unmitigated Evil"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92232.opus") {
							a = L"I'm Grimcats!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92233.opus") {
							a = LL14(L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92234.opus") {
							a = L"Flowing Illusion"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92235.opus") {
							a = L"Brilliant Grow"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92236.opus") {
							a = LL14(L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92239.opus") {
							a = L"Mechanical Syndrome"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92240.opus") {
							a = LL14(L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92242.opus") {
							a = L"Be Prepared"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92245.opus") {
							a = L"Bang of Metatron"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92246.opus") {
							a = LL14(L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92247.opus") {
							a = L"WILD BEAT"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92253.opus") {
							a = LL14(L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ..."); fff = 1;
						}
						if (fff == 0 && ft == L"ed92254.opus") {
							a = LL14(L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92255.opus") {
							a = LL14(L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92257.opus") {
							a = L"Have a Good Time"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92258.opus") {
							a = LL14(L"クセ者たちの挽歌", L"Elegy of the Eccentrics", L"Élégie des originaux", L"Elegia degli eccentrici", L"Elegía de los excéntricos", L"괴짜들의 만가", L"怪人们的挽歌", L"مرثية الغريبي الأطوار", L"Элегия чудаков", L"Elegie der Sonderlinge", L"Elegia dos excêntricos", L"Elegie van de zonderlingen", L"Elegia ekscentryków", L"Tuhaf Tiplerin Ağıtı"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92260b.opus") {
							a = L"Deep Affection"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92262.opus") {
							a = L"Do or Die!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92266.opus") {
							a = LL14(L"冷厳なる現実", L"Harsh Reality", L"Réalité implacable", L"Crudele realtà", L"Cruda realidad", L"냉엄한 현실", L"冷酷的现实", L"واقع قاسٍ", L"Суровая реальность", L"Harte Realität", L"Realidade severa", L"Harde realiteit", L"Surowa rzeczywistość", L"Acımasız Gerçek"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92278.opus") {
							a = L"Everlasting Dream"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92281.opus") {
							a = L"Invitation of Cube!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92282.opus") {
							a = L"Exert All One's Strength"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92283.opus") {
							a = LL14(L"喧噪と吐息のあいだ", L"Between Clamor and Breath", L"Entre clameur et souffle", L"Tra clamore e respiro", L"Entre clamor y aliento", L"훤화와 한숨 사이", L"喧嚣与吐息之间", L"بين الضجيج والتنهد", L"Между шумом и вздохом", L"Zwischen Lärm und Atemzug", L"Entre clamor e suspiro", L"Tussen rumoer en ademtocht", L"Między zgiełkiem a oddechem", L"Gürültü ile Nefes Arasında"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92284.opus") {
							a = L"Infinite Ferocity"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92292.opus") {
							a = L"Believing Heart"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92298.opus") {
							a = L"Hide and Seek"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92301.opus") {
							a = LL14(L"御伽ヲ歪メシモノ", L"That Which Distorts Fairyland", L"Ce qui déforme le pays des contes", L"Ciò che distorce il paese delle fiabe", L"Lo que distorsiona el país de las hadas", L"동화를 왜곡하는 것", L"扭曲御伽之物", L"ما يشوه بلاد الجنيات", L"То, что искажает сказочную страну", L"Was das Märchenland verzerrt", L"O que distorce o país das fadas", L"Wat sprookjesland vervormt", L"To, co wypacza krainę baśni", L"Masal Diyarını Çarpıtan"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92304.opus") {
							a = LL14(L"煌天の頂を目指して", L"Aiming for the Summit of the Radiant Sky", L"Visant le sommet du ciel éclatant", L"Verso la vetta del cielo radioso", L"Hacia la cima del cielo radiante", L"황천의 정상을 향해", L"以煌天之顶为目标", L"نحو قمة السماء المتلألئة", L"К вершине сияющего неба", L"Zum Gipfel des strahlenden Himmels", L"Rumo ao cume do céu radiante", L"Op naar de top van de stralende hemel", L"Ku szczytowi lśniącego nieba", L"Parlak Göğün Zirvesine"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92306.opus") {
							a = LL14(L"在りし日を越えて", L"Beyond Days Gone By", L"Au-delà des jours d'antan", L"Oltre i giorni passati", L"Más allá de los días idos", L"지나간 날을 넘어", L"跨越往昔之日", L"ما وراء الأيام الخالية", L"За минувшими днями", L"Jenseits vergangener Tage", L"Além dos dias que se foram", L"Voorbij vervlogen dagen", L"Poza minionymi dniami", L"Geçmiş Günlerin Ötesinde"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92308.opus") {
							a = LL14(L"幽世への還り路", L"Road Back to the Netherworld", L"Chemin du retour vers l'au-delà", L"Via di ritorno all'oltretomba", L"Camino de regreso al inframundo", L"유세로의 귀환로", L"通往幽世的归路", L"طريق العودة إلى العالم الآخر", L"Путь возвращения в загробный мир", L"Rückweg in die Unterwelt", L"Caminho de volta ao além", L"Terugweg naar de onderwereld", L"Droga powrotu do zaświatów", L"Öteki Dünyaya Dönüş Yolu"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92310.opus") {
							a = L"Days Gone By"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92312.opus") {
							a = LL14(L"暖かな昼下がり", L"Warm Afternoon", L"Chaude après-midi", L"Caldo pomeriggio", L"Tarde cálida", L"따뜻한 오후", L"温暖的午后", L"ظهيرة دافئة", L"Тёплый полдень", L"Warmer Nachmittag", L"Tarde quente", L"Warme namiddag", L"Ciepłe popołudnie", L"Sıcak Öğleden Sonra"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92313.opus") {
							a = LL14(L"それでもあなたの側で", L"Still by Your Side", L"Pourtant à tes côtés", L"Eppure al tuo fianco", L"Aun así a tu lado", L"그래도 당신 곁에서", L"即便如此仍在你身边", L"ومع ذلك إلى جانبك", L"И всё же рядом с тобой", L"Dennoch an deiner Seite", L"Ainda assim ao seu lado", L"Toch aan jouw zijde", L"Mimo wszystko u twojego boku", L"Yine de Senin Yanında"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92315.opus") {
							a = LL14(L"譲れない一線", L"A Line I Won't Cross", L"Une ligne que je ne franchirai pas", L"Una linea che non varcherò", L"Una línea que no cruzaré", L"양보할 수 없는 선", L"不能退让的一线", L"خط لن أتجاوزه", L"Черта, которую не переступлю", L"Eine Linie, die ich nicht überschreite", L"Uma linha que não cruzarei", L"Een lijn die ik niet oversteek", L"Linia, której nie przekroczę", L"Vazgeçemeyeceğim Çizgi"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92318.opus") {
							a = LL14(L"暗き澱みの底で", L"At the Bottom of Dark Stagnation", L"Au fond de la stagnation sombre", L"In fondo alla stagnazione oscura", L"En el fondo del estancamiento oscuro", L"어두운 침전의 밑바닥에서", L"在暗沉淤积之底", L"في قاع الركود المظلم", L"На дне тёмного застоя", L"Am Grund dunkler Stagnation", L"No fundo da estagnação sombria", L"Op de bodem van donkere stagnatie", L"Na dnie mrocznego zastoju", L"Karanlık Durgunluğun Dibinde"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92319.opus") {
							a = LL14(L"一瞬の駆け引き", L"A Moment's Maneuver", L"Une manœuvre d'un instant", L"Una manovra d'un istante", L"Una maniobra de un instante", L"순간의 기싸움", L"一瞬的交锋", L"مناورة للحظة", L"Манёвр на мгновение", L"Manöver eines Augenblicks", L"Manobra de um instante", L"Manoeuvre van een ogenblik", L"Manewr chwili", L"Bir Anlık Hamle"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92320.opus") {
							a = L"Playing Dirty?"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92322.opus") {
							a = L"Cut Off the Retreat"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92323.opus") {
							a = LL14(L"赫焉のディグニティ", L"Dignity of the Crimson Sunset", L"Dignité du crépuscule écarlate", L"Dignità del tramonto scarlatto", L"Dignidad del ocaso escarlata", L"혁연의 디그니티", L"赫焉的尊严", L"كرامة الغروب القرمزي", L"Достоинство алого заката", L"Würde des scharlachroten Abendrots", L"Dignidade do crepúsculo escarlate", L"Waardigheid van de scharlaken zonsondergang", L"Godność szkarłatnego zmierzchu", L"Kıpkırmızı Gün Batımının İtibarı"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92326.opus") {
							a = LL14(L"遠き日の記憶", L"Memories of Distant Days", L"Souvenirs de jours lointains", L"Memorie di giorni lontani", L"Recuerdos de días lejanos", L"머나먼 날의 기억", L"遥远日子的记忆", L"ذكريات الأيام البعيدة", L"Воспоминания далёких дней", L"Erinnerungen an ferne Tage", L"Memórias de dias distantes", L"Herinneringen aan verre dagen", L"Wspomnienia odległych dni", L"Uzak Günlerin Anıları"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92330.opus") {
							a = LL14(L"共に、新たな季節を -Prelude-", L"Together, Toward a New Season -Prelude-", L"Ensemble, vers une nouvelle saison -Prelude-", L"Insieme, verso una nuova stagione -Prelude-", L"Juntos, hacia una nueva estación -Prelude-", L"함께, 새로운 계절을 -Prelude-", L"一起,迎向新的季节 -Prelude-", L"معاً نحو موسم جديد -Prelude-", L"Вместе к новому сезону -Prelude-", L"Gemeinsam in eine neue Jahreszeit -Prelude-", L"Juntos, rumo a uma nova estação -Prelude-", L"Samen naar een nieuw seizoen -Prelude-", L"Razem ku nowej porze roku -Prelude-", L"Birlikte Yeni Bir Mevsim -Prelude-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92332.opus") {
							a = L"Where to Join the Chorus"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92335.opus") {
							a = L"Just Arrived"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92337.opus") {
							a = LL14(L"匣の中に在るのは...", L"What Lies Within the Box...", L"Ce qui se trouve dans la boîte...", L"Ciò che giace nella scatola...", L"Lo que hay dentro de la caja...", L"상자 속에 있는 것은...", L"匣中之物是...", L"ما يوجد داخل الصندوق...", L"То, что лежит в шкатулке...", L"Was in der Schatulle liegt...", L"O que há dentro da caixa...", L"Wat er in het kistje zit...", L"Co kryje się w szkatułce...", L"Kutunun İçinde Olan..."); fff = 1;
						}
						if (fff == 0 && ft == L"ed92342.opus") {
							a = LL14(L"古趣礼賛", L"Praise of Antique Charm", L"Éloge du charme antique", L"Elogio del fascino antico", L"Elogio del encanto antiguo", L"고취 예찬", L"古趣礼赞", L"مدح السحر العتيق", L"Хвала старинному очарованию", L"Lob des antiken Charmes", L"Elogio do charme antigo", L"Lofzang op antieke charme", L"Pochwała dawnego uroku", L"Eski Tarzın Övgüsü"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92343.opus") {
							a = LL14(L"共に、新たな季節を -Grand Finale-", L"Together, Toward a New Season -Grand Finale-", L"Ensemble, vers une nouvelle saison -Grand Finale-", L"Insieme, verso una nuova stagione -Grand Finale-", L"Juntos, hacia una nueva estación -Grand Finale-", L"함께, 새로운 계절을 -Grand Finale-", L"一起,迎向新的季节 -Grand Finale-", L"معاً نحو موسم جديد -Grand Finale-", L"Вместе к новому сезону -Grand Finale-", L"Gemeinsam in eine neue Jahreszeit -Grand Finale-", L"Juntos, rumo a uma nova estação -Grand Finale-", L"Samen naar een nieuw seizoen -Grand Finale-", L"Razem ku nowej porze roku -Grand Finale-", L"Birlikte Yeni Bir Mevsim -Grand Finale-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92343b.opus") {
							a = LL14(L"共に、新たな季節を -Grand Finale-", L"Together, Toward a New Season -Grand Finale-", L"Ensemble, vers une nouvelle saison -Grand Finale-", L"Insieme, verso una nuova stagione -Grand Finale-", L"Juntos, hacia una nueva estación -Grand Finale-", L"함께, 새로운 계절을 -Grand Finale-", L"一起,迎向新的季节 -Grand Finale-", L"معاً نحو موسم جديد -Grand Finale-", L"Вместе к новому сезону -Grand Finale-", L"Gemeinsam in eine neue Jahreszeit -Grand Finale-", L"Juntos, rumo a uma nova estação -Grand Finale-", L"Samen naar een nieuw seizoen -Grand Finale-", L"Razem ku nowej porze roku -Grand Finale-", L"Birlikte Yeni Bir Mevsim -Grand Finale-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92343c.opus") {
							a = LL14(L"共に、新たな季節を -Grand Finale-", L"Together, Toward a New Season -Grand Finale-", L"Ensemble, vers une nouvelle saison -Grand Finale-", L"Insieme, verso una nuova stagione -Grand Finale-", L"Juntos, hacia una nueva estación -Grand Finale-", L"함께, 새로운 계절을 -Grand Finale-", L"一起,迎向新的季节 -Grand Finale-", L"معاً نحو موسم جديد -Grand Finale-", L"Вместе к новому сезону -Grand Finale-", L"Gemeinsam in eine neue Jahreszeit -Grand Finale-", L"Juntos, rumo a uma nova estação -Grand Finale-", L"Samen naar een nieuw seizoen -Grand Finale-", L"Razem ku nowej porze roku -Grand Finale-", L"Birlikte Yeni Bir Mevsim -Grand Finale-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92344.opus") {
							a = LL14(L"紅の衝撃", L"Crimson Impact", L"Impact cramoisi", L"Impatto cremisi", L"Impacto carmesí", L"홍의 충격", L"红之冲击", L"صدمة قرمزية", L"Алый удар", L"Purpurner Einschlag", L"Impacto carmesim", L"Karmozijnrode inslag", L"Szkarłatne uderzenie", L"Kızıl Darbe"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92_op_full.opus") {
							a = L"CRIMSON SiN"; fff = 1;
						}
						if (fff == 0 && ft == L"ed93104.opus") {
							a = LL14(L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ"); fff = 1;
						}
						if (fff == 0 && ft == L"ed93105.opus") {
							a = L"The Decisive Point"; fff = 1;
						}
						if (fff == 0 && ft == L"ed93108.opus") {
							a = LL14(L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ"); fff = 1;
						}
						if (fff == 0 && ft == L"ed93112.opus") {
							a = LL14(L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好"); fff = 1;
						}
						if (fff == 0 && ft == L"ed93_ed_inst.opus") {
							a = LL14(L"女神の唄 -Instrumental Ver.-", L"Song of the Goddess -Instrumental Ver.-", L"Chant de la déesse -Instrumental Ver.-", L"Canto della dea -Instrumental Ver.-", L"Canto de la diosa -Instrumental Ver.-", L"여신의 노래 -Instrumental Ver.-", L"女神之歌 -Instrumental Ver.-", L"أغنية الإلهة -Instrumental Ver.-", L"Песнь богини -Instrumental Ver.-", L"Lied der Göttin -Instrumental Ver.-", L"Cântico da deusa -Instrumental Ver.-", L"Lied van de godin -Instrumental Ver.-", L"Pieśń bogini -Instrumental Ver.-", L"Tanrıça'nın Şarkısı -Instrumental Ver.-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed93_ed_maya.opus") {
							a = LL14(L"女神の唄", L"Song of the Goddess", L"Chant de la déesse", L"Canto della dea", L"Canto de la diosa", L"여신의 노래", L"女神之歌", L"أغنية الإلهة", L"Песнь богини", L"Lied der Göttin", L"Cântico da deusa", L"Lied van de godin", L"Pieśń bogini", L"Tanrıça'nın Şarkısı"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94003.opus") {
							a = LL14(L"学び舎に憩う", L"Resting at the Academy", L"Repos à l'académie", L"Riposo all'accademia", L"Descanso en la academia", L"배움터에서 쉬다", L"憩于学舍", L"الراحة في الأكاديمية", L"Отдых в учебном заведении", L"Ruhend in der Lehranstalt", L"Descansando na academia", L"Rustend op school", L"Odpoczynek w szkole", L"Okulda Dinlenmek"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94004.opus") {
							a = LL14(L"リベレーションフロント", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"리버레이션 프론트", L"启示前线", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"Revelation Front"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94005.opus") {
							a = LL14(L"刻の螺旋", L"Spiral of Time", L"Spirale du temps", L"Spirale del tempo", L"Espiral del tiempo", L"각의 나선", L"刻之螺旋", L"لولب الزمن", L"Спираль времени", L"Spirale der Zeit", L"Espiral do tempo", L"Spiraal van de tijd", L"Spirala czasu", L"Zamanın Spiralı"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94008.opus") {
							a = L"Windy Corridor"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94010.opus") {
							a = LL14(L"暗がりの先へ", L"Beyond the Darkness", L"Au-delà de l'obscurité", L"Oltre l'oscurità", L"Más allá de la oscuridad", L"어둠 너머로", L"向着黑暗的前方", L"إلى ما وراء الظلام", L"За темнотой", L"Jenseits der Dunkelheit", L"Além da escuridão", L"Voorbij de duisternis", L"Poza ciemnością", L"Karanlığın Ötesine"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94015.opus") {
							a = L"The Battle by a Hair's Breath"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94016.opus") {
							a = LL14(L"崩れる日常", L"Collapsing Everyday", L"Quotidien qui s'effondre", L"Quotidiano che crolla", L"Cotidiano que se derrumba", L"무너지는 일상", L"崩坏的日常", L"اليوميات المنهارة", L"Разрушающаяся повседневность", L"Zusammenbrechender Alltag", L"Cotidiano que desaba", L"Ineenstortend dagelijks leven", L"Waląca się codzienność", L"Yıkılan Günlük Hayat"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94017.opus") {
							a = LL14(L"湧き上がる疑念", L"Rising Doubts", L"Doutes qui montent", L"Dubbi che sorgono", L"Dudas que emergen", L"솟아오르는 의념", L"涌起的疑念", L"شكوك متصاعدة", L"Нарастающие сомнения", L"Aufkommende Zweifel", L"Dúvidas que emergem", L"Opkomende twijfels", L"Narastające wątpliwości", L"Yükselen Şüpheler"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94018.opus") {
							a = L"Smoky Smoky"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94020.opus") {
							a = L"Advent Day"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94022.opus") {
							a = LL14(L"想い愁う", L"Sorrowful Thoughts", L"Pensées mélancoliques", L"Pensieri melanconici", L"Pensamientos melancólicos", L"시름에 잠긴 마음", L"忧思", L"أفكار حزينة", L"Печальные думы", L"Trauernde Gedanken", L"Pensamentos melancólicos", L"Droevige gedachten", L"Smutne myśli", L"Hüzünlü Düşünceler"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94023.opus") {
							a = LL14(L"焔舞 -友のために-", L"Flame Dance -For a Friend-", L"Danse des flammes -Pour un ami-", L"Danza delle fiamme -Per un amico-", L"Danza de llamas -Por un amigo-", L"염무 -친구를 위해-", L"焰舞 -为了友人-", L"رقصة اللهب -من أجل صديق-", L"Танец пламени -Ради друга-", L"Flammentanz -Für einen Freund-", L"Dança das chamas -Por um amigo-", L"VlammenDans -Voor een vriend-", L"Taniec płomieni -Dla przyjaciela-", L"Alev Dansı -Bir Dost İçin-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94024.opus") {
							a = L"Blaze Soul"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94025.opus") {
							a = LL14(L"さあ冒険の世界へ", L"Now, to the World of Adventure", L"Allons vers le monde de l'aventure", L"Ora, verso il mondo dell'avventura", L"Ahora, al mundo de la aventura", L"자, 모험의 세계로", L"出发吧,向冒险的世界", L"هيا إلى عالم المغامرة", L"Вперёд, в мир приключений", L"Auf in die Welt des Abenteuers", L"Agora, ao mundo da aventura", L"Nu, naar de wereld van avontuur", L"Naprzód, do świata przygód", L"Haydi Macera Dünyasına"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94026.opus") {
							a = LL14(L"幻想領域", L"Realm of Fantasy", L"Royaume de fantaisie", L"Reame della fantasia", L"Reino de la fantasía", L"환상 영역", L"幻想领域", L"عالم الخيال", L"Царство фантазии", L"Reich der Fantasie", L"Reino da fantasia", L"Rijk der fantasie", L"Kraina fantazji", L"Fantazi Âlemi"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94027.opus") {
							a = LL14(L"夢はゴロゴロライフ", L"Dreams Are a Laid-Back Life", L"Les rêves, une vie tranquille", L"I sogni sono una vita rilassata", L"Los sueños son una vida relajada", L"꿈은 뒹굴뒹굴 라이프", L"梦想是慵懒生活", L"الأحلام حياة مسترخية", L"Мечты — это беззаботная жизнь", L"Träume sind ein gemütliches Leben", L"Sonhos são uma vida descontraída", L"Dromen zijn een relaxed leven", L"Marzenia to leniwe życie", L"Rüyalar Tembel Bir Hayat"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94029.opus") {
							a = LL14(L"最悪の思惑", L"The Worst Designs", L"Les pires intentions", L"I peggiori disegni", L"Las peores intenciones", L"최악의 속셈", L"最坏的企图", L"أسوأ النوايا", L"Худшие замыслы", L"Die schlimmsten Absichten", L"As piores intenções", L"De ergste bedoelingen", L"Najgorsze zamiary", L"En Kötü Hesaplar"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94201.opus") {
							a = LL14(L"迫りくる災厄", L"Approaching Calamity", L"Calamité qui approche", L"Calamità in avvicinamento", L"Calamidad que se acerca", L"다가오는 재앙", L"迫近的灾厄", L"كارثة تقترب", L"Приближающееся бедствие", L"Nahendes Unheil", L"Calamidade que se aproxima", L"Naderende ramp", L"Zbliżająca się klęska", L"Yaklaşan Felaket"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94202.opus") {
							a = LL14(L"君が残してくれたもの", L"What You Left Behind", L"Ce que tu as laissé", L"Ciò che hai lasciato", L"Lo que dejaste atrás", L"네가 남겨준 것", L"你留下的东西", L"ما تركته لي", L"То, что ты оставил", L"Was du hinterlassen hast", L"O que você deixou", L"Wat jij achterliet", L"To, co zostawiłeś", L"Bıraktığın Şeyler"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94400.opus") {
							a = L"Regain the Administration! -Field Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94401.opus") {
							a = L"Regain the Administration! -Field Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94402.opus") {
							a = L"The Expanding World"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94404.opus") {
							a = L"Pushed Over"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94406.opus") {
							a = LL14(L"モノクローム・デイズ", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"모노크롬 데이즈", L"单色的日子", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94407.opus") {
							a = LL14(L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇", L"陰影の照らす闇"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94408.opus") {
							a = LL14(L"愛しき刻限の果てに", L"At the End of the Beloved Hour", L"Au bout de l'heure chérie", L"Alla fine dell'ora amata", L"Al final de la hora querida", L"사랑스러운 각한의 끝에서", L"在心爱时限的尽头", L"في نهاية الساعة العزيزة", L"В конце любимого срока", L"Am Ende der geliebten Frist", L"No fim da hora amada", L"Aan het eind van het geliefde uur", L"U kresu ukochanej godziny", L"Sevgili Vaktin Sonunda"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94410.opus") {
							a = L"Work Out a Solution! -Field Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94411.opus") {
							a = L"Work Out a Solution! -Command Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94412.opus") {
							a = L"Into the Chill"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94414.opus") {
							a = L"Buster the Ghost of Garden"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94415.opus") {
							a = L"Skip to the Edith"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94416.opus") {
							a = L"Hard-Boiled Short Cake"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94417.opus") {
							a = L"Blurred Scene"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94418.opus") {
							a = L"After the Spring Rain"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94419.opus") {
							a = L"With the Dilemma"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94420.opus") {
							a = LL14(L"雪解けの知らせ", L"News of the Thaw", L"Nouvelles du dégel", L"Notizie del disgelo", L"Noticias del deshielo", L"눈이 녹는 소식", L"融雪的消息", L"أخبار ذوبان الثلج", L"Весть о таянии снега", L"Kunde vom Tauwetter", L"Notícias do degelo", L"Nieuws van de dooi", L"Wieść o odwilży", L"Karların Erime Haberi"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95000.opus") {
							a = LL14(L"GLORIA ~la revue -五月革命の奇跡-", L"GLORIA ~la revue -Miracle of the May Revolution-", L"GLORIA ~la revue -Miracle de la révolution de mai-", L"GLORIA ~la revue -Miracolo della rivoluzione di maggio-", L"GLORIA ~la revue -Milagro de la revolución de mayo-", L"GLORIA ~la revue -5월 혁명의 기적-", L"GLORIA ~la revue -五月革命的奇迹-", L"GLORIA ~la revue -معجزة ثورة مايو-", L"GLORIA ~la revue -Чудо майской революции-", L"GLORIA ~la revue -Wunder der Mairevolution-", L"GLORIA ~la revue -Milagre da revolução de maio-", L"GLORIA ~la revue -Wonder van de meirevolutie-", L"GLORIA ~la revue -Cud majowej rewolucji-", L"GLORIA ~la revue -Mayıs Devrimi Mucizesi-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95002.opus") {
							a = LL14(L"崩れゆく日常", L"Crumbling Everyday", L"Quotidien qui s'effondre", L"Quotidiano che crolla", L"Cotidiano que se derrumba", L"무너져가는 일상", L"崩坏的日常", L"اليوميات المتداعية", L"Разрушающаяся повседневность", L"Zerbrechender Alltag", L"Cotidiano que desmorona", L"Brokkelend dagelijks leven", L"Krusząca się codzienność", L"Yıkılan Günlük Hayat"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95004.opus") {
							a = LL14(L"妖魔の森", L"Forest of Demons", L"Forêt des démons", L"Foresta dei demoni", L"Bosque de demonios", L"요마의 숲", L"妖魔之森", L"غابة الشياطين", L"Лес демонов", L"Dämonenwald", L"Floresta dos demônios", L"Demonenbos", L"Las demonów", L"Şeytan Ormanı"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95005.opus") {
							a = LL14(L"スターテイカー計画", L"Startaker Project", L"Projet Startaker", L"Progetto Startaker", L"Proyecto Startaker", L"스타테이커 계획", L"星取者计划", L"مشروع ستارتيكر", L"Проект Стартэйкер", L"Startaker-Projekt", L"Projeto Startaker", L"Startaker-project", L"Projekt Startaker", L"Startaker Projesi"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95006.opus") {
							a = LL14(L"密かなる水底の記憶", L"Secret Memories of the Waterbed", L"Souvenirs secrets du fond de l'eau", L"Memorie segrete del fondo acqueo", L"Recuerdos secretos del lecho acuático", L"은밀한 수저의 기억", L"隐秘的水底记忆", L"ذكريات سرية لقاع الماء", L"Тайные воспоминания водной глубины", L"Geheime Erinnerungen des Wassergrunds", L"Memórias secretas do fundo d'água", L"Geheime herinneringen van de waterbodem", L"Skryte wspomnienia dna wodnego", L"Gizli Su Dibi Anıları"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95008.opus") {
							a = LL14(L"罪の痕", L"Mark of Sin", L"Marque du péché", L"Segno del peccato", L"Marca del pecado", L"죄의 흔적", L"罪之痕", L"أثر الخطيئة", L"След греха", L"Mal der Sünde", L"Marca do pecado", L"Spoor van zonde", L"Ślad grzechu", L"Günahın İzi"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95009.opus") {
							a = LL14(L"黒の庭城", L"Black Garden Castle", L"Château-jardin noir", L"Castello-giardino nero", L"Castillo-jardín negro", L"흑의 정성", L"黑之庭城", L"قلعة الحديقة السوداء", L"Чёрный садовый замок", L"Schwarzes Gartenschloss", L"Castelo-jardim negro", L"Zwart tuinkasteel", L"Czarny zamek-ogród", L"Siyah Bahçe Kalesi"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95013.opus") {
							a = L"Gloomy Moon"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95014.opus") {
							a = L"Starry Night"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95016.opus") {
							a = L"Invited to a Foreign Town"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95020.opus") {
							a = LL14(L"執行者 -Kai No Kiseki Ver.-", L"The Enforcer -Kai No Kiseki Ver.-", L"L'exécuteur -Kai No Kiseki Ver.-", L"L'esecutore -Kai No Kiseki Ver.-", L"El ejecutor -Kai No Kiseki Ver.-", L"집행자 -Kai No Kiseki Ver.-", L"执行者 -Kai No Kiseki Ver.-", L"المنفذ -Kai No Kiseki Ver.-", L"Исполнитель -Kai No Kiseki Ver.-", L"Vollstrecker -Kai No Kiseki Ver.-", L"O executor -Kai No Kiseki Ver.-", L"De handhaver -Kai No Kiseki Ver.-", L"Egzekutor -Kai No Kiseki Ver.-", L"İcraatçı -Kai No Kiseki Ver.-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95023.opus") {
							a = LL14(L"静寂の余韻", L"Afterglow of Silence", L"Résonance du silence", L"Eco del silenzio", L"Resonancia del silencio", L"정적의 여운", L"静寂的余韵", L"صدى الصمت", L"Отзвук тишины", L"Nachhall der Stille", L"Ressonância do silêncio", L"Nagalm van stilte", L"Echo ciszy", L"Sessizliğin Yankısı"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95024.opus") {
							a = LL14(L"プラネット・レイン", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"플래닛 레인", L"行星之雨", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"Planet Rain"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95024b.opus") {
							a = LL14(L"プラネット・レイン", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"플래닛 레인", L"行星之雨", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"Planet Rain", L"Planet Rain"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95026.opus") {
							a = LL14(L"安息の時間", L"Time of Rest", L"Temps de repos", L"Tempo di riposo", L"Tiempo de descanso", L"안식의 시간", L"安息之时", L"وقت الراحة", L"Время покоя", L"Zeit der Ruhe", L"Tempo de descanso", L"Tijd van rust", L"Czas odpoczynku", L"Dinlenme Zamanı"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95027.opus") {
							a = LL14(L"掴もう、明るい未来!", L"Seize the Bright Future!", L"Saisissons l'avenir brillant !", L"Afferriamo il futuro luminoso!", L"¡Aferrémonos al futuro brillante!", L"밝은 미래를 잡자!", L"抓住光明的未来!", L"لنمسك بالمستقبل المشرق!", L"Схватим светлое будущее!", L"Ergreifen wir die helle Zukunft!", L"Vamos agarrar o futuro brilhante!", L"Grijp de heldere toekomst!", L"Chwyćmy jasną przyszłość!", L"Parlak Geleceği Yakalayalım!"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95029.opus") {
							a = LL14(L"天空への散歩道", L"Promenade to the Sky", L"Promenade vers le ciel", L"Passeggiata verso il cielo", L"Paseo hacia el cielo", L"하늘로의 산책로", L"通向天空的散步道", L"ممشى نحو السماء", L"Прогулочная тропа к небу", L"Spazierweg zum Himmel", L"Passeio ao céu", L"Wandelpad naar de hemel", L"Promenada ku niebu", L"Gökyüzüne Yürüyüş Yolu"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95030.opus") {
							a = L"Divine Reef"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95031.opus") {
							a = LL14(L"冒険の準備はこちらで", L"Prepare for Adventure Here", L"Préparez l'aventure ici", L"Preparatevi all'avventura qui", L"Prepárate para la aventura aquí", L"모험 준비는 여기에서", L"冒险的准备在这边", L"جهّز للمغامرة هنا", L"Готовьтесь к приключению здесь", L"Hier bereitet man sich aufs Abenteuer vor", L"Prepare-se para a aventura aqui", L"Bereid hier het avontuur voor", L"Tu przygotuj się do przygody", L"Macera Hazırlığı Burada"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95032.opus") {
							a = LL14(L"虚ろなる時の狭間で", L"In the Hollow Between Times", L"Dans le creux entre les temps", L"Nel vuoto tra i tempi", L"En el hueco entre los tiempos", L"공허한 시간의 틈에서", L"在虚空的时隙中", L"في الفراغ بين الأزمنة", L"В полом промежутке времён", L"Im hohlen Zwischenraum der Zeit", L"No vão oco entre os tempos", L"In de holle spleet tussen tijden", L"W pustej szczelinie między czasami", L"Boş Zaman Aralığında"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95033.opus") {
							a = L"Absolute Being"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95034.opus") {
							a = LL14(L"リミット迫る", L"Limit Closing In", L"Limite qui approche", L"Limite in avvicinamento", L"Límite acercándose", L"한계가 다가온다", L"极限迫近", L"الحد يقترب", L"Предел приближается", L"Limit rückt näher", L"Limite se aproximando", L"Limiet nadert", L"Limit się zbliża", L"Limit Yaklaşıyor"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95035.opus") {
							a = LL14(L"真実の眼", L"Eye of Truth", L"Œil de la vérité", L"Occhio della verità", L"Ojo de la verdad", L"진실의 눈", L"真实之眼", L"عين الحقيقة", L"Око истины", L"Auge der Wahrheit", L"Olho da verdade", L"Oog der waarheid", L"Oko prawdy", L"Gerçeğin Gözü"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95036.opus") {
							a = LL14(L"あの日の面影", L"Remnants of That Day", L"Souvenirs de ce jour-là", L"Ricordi di quel giorno", L"Reminiscencias de aquel día", L"그날의 면영", L"那日的面影", L"ملامح ذلك اليوم", L"Отголоски того дня", L"Schatten jenes Tages", L"Reminescências daquele dia", L"Schimmen van die dag", L"Widma tamtego dnia", L"O Günün Silueti"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95038.opus") {
							a = LL14(L"目の前にいる貴方へ", L"To You Standing Before Me", L"À toi devant moi", L"A te che stai davanti a me", L"A ti que estás frente a mí", L"눈앞의 당신에게", L"致眼前的你", L"إليك الواقف أمامي", L"Тебе, стоящему передо мной", L"An dich, der vor mir steht", L"A você diante de mim", L"Aan jou die voor mij staat", L"Do ciebie stojącego przede mną", L"Karşımdaki Sana"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95100.opus") {
							a = LL14(L"交差する意志", L"Crossing Wills", L"Volontés qui se croisent", L"Volontà che si incrociano", L"Voluntades que se cruzan", L"교차하는 의지", L"交错的意志", L"إرادات متقاطعة", L"Пересекающиеся воли", L"Kreuzende Willen", L"Vontades que se cruzam", L"Kruisende wilskrachten", L"Krzyżujące się wole", L"Kesişen İradeler"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95101.opus") {
							a = L"Dies irae -juncture of the dawn-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95202.opus") {
							a = LL14(L"異郷で見た花", L"Flower Seen in a Foreign Land", L"Fleur vue en terre étrangère", L"Fiore visto in terra straniera", L"Flor vista en tierra extranjera", L"이향에서 본 꽃", L"在异乡见到的花", L"زهرة رُئيت في أرض غريبة", L"Цветок, увиденный на чужбине", L"In der Fremde gesehene Blume", L"Flor vista em terra estrangeira", L"Bloem gezien in den vreemde", L"Kwiat widziany na obcej ziemi", L"Gurbette Görülen Çiçek"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95203.opus") {
							a = L"Autumn Knows -Piano Solo Ver.-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95203b.opus") {
							a = L"Autumn Knows -Piano Solo Ver.-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95204.opus") {
							a = L"Battle Junction -1209-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95205.opus") {
							a = LL14(L"いくつもの想い、積み重ねて", L"Piling Up So Many Feelings", L"Accumulant tant de sentiments", L"Accumulando tanti sentimenti", L"Acumulando tantos sentimientos", L"수많은 마음을 쌓아올려", L"层层堆积的心意", L"تراكم الكثير من المشاعر", L"Накапливая множество чувств", L"So viele Gefühle aufschichtend", L"Acumulando tantos sentimentos", L"Zoveel gevoelens opgestapeld", L"Piętrząc tyle uczuć", L"Birçok Duyguyu Biriktirerek"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95206.opus") {
							a = LL14(L"仄白き光の先へ", L"Beyond the Pale Light", L"Au-delà de la pâle lumière", L"Oltre la luce pallida", L"Más allá de la luz pálida", L"어슴푸레한 빛 너머로", L"向着微白之光的前方", L"إلى ما وراء الضوء الشاحب", L"За бледным светом", L"Jenseits des fahlen Lichts", L"Além da luz pálida", L"Voorbij het bleke licht", L"Poza bladym światłem", L"Soluk Işığın Ötesine"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95207.opus") {
							a = L"Find the 4spg Out! -Field Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95208.opus") {
							a = L"Find the 4spg Out! -Command Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95209.opus") {
							a = L"Autumn Knows"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95210.opus") {
							a = L"Stroll Around the Edith"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95211.opus") {
							a = L"Detoxify the Snake Venom! -Field Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95212.opus") {
							a = L"Detoxify the Snake Venom! -Command Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95215.opus") {
							a = L"Smells Like Mystery and Sweets"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95216.opus") {
							a = L"Close to the Eruption"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95217.opus") {
							a = L"KEVIN GRAHAM"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95217b.opus") {
							a = L"KEVIN GRAHAM"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95218.opus") {
							a = LL14(L"偽猫ト千年ノ呪イノ迷宮", L"Fake Cat and the Labyrinth of a Millennium Curse", L"Faux chat et le labyrinthe de la malédiction millénaire", L"Falso gatto e il labirinto della maledizione millenaria", L"Gato falso y el laberinto de la maldición milenaria", L"가짜 고양이와 천년의 저주의 미궁", L"伪猫与千年诅咒的迷宫", L"القط المزيف ومتاهة لعنة الألف عام", L"Фальшивый кот и лабиринт тысячелетнего проклятия", L"Falsche Katze und das Labyrinth des Jahrtausendfluchs", L"Gato falso e o labirinto da maldição milenar", L"Nepkat en het doolhof van de millennialangvloek", L"Fałszywy kot i labirynt tysiącletniej klątwy", L"Sahte Kedi ve Bin Yıllık Lanetin Labirenti"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95219.opus") {
							a = L"Farewell, My Dear..."; fff = 1;
						}
						if (fff == 0 && ft == L"ed95222.opus") {
							a = LL14(L"もう一つの夜明け", L"Another Dawn", L"Une autre aube", L"Un'altra alba", L"Otro amanecer", L"또 하나의 여명", L"另一个黎明", L"فجر آخر", L"Другой рассвет", L"Eine weitere Dämmerung", L"Outra aurora", L"Een andere dageraad", L"Kolejny świt", L"Başka Bir Şafak"); fff = 1;
						}
						if (fff == 0 && ft == L"ed95224.opus") {
							a = L"Abyss Looking into You"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95225.opus") {
							a = L"Mementos"; fff = 1;
						}
						if (fff == 0 && ft == L"ed95227.opus") {
							a = LL14(L"街角シエスタ", L"Street Corner Siesta", L"Sieste au coin de la rue", L"Siesta all'angolo della strada", L"Siesta en la esquina", L"거리 모퉁이 시에스타", L"街角午睡", L"قيلولة على ناصية الشارع", L"Сиеста на углу улицы", L"Siesta an der Straßenecke", L"Sesta na esquina", L"Siësta op de straathoek", L"Sjesta na rogu ulicy", L"Sokak Köşesi Siesta"); fff = 1;
						}
						if (fff == 0 && ft == L"ed97246.opus") {
							a = L"The Legend of VII"; fff = 1;
						}
						if (fff == 0 && ft == L"ed97305.opus") {
							a = LL14(L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9_op_full.opus") {
							a = LL14(L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z144.opus") {
							a = L"Pain of Memories"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z192.opus") {
							a = L"Give It Your All!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z200.opus") {
							a = LL14(L"小さな決意", L"小さな決意", L"小さな決意", L"小さな決意", L"小さな決意", L"小さな決意", L"小さな決意", L"小さな決意", L"小さな決意", L"小さな決意", L"小さな決意", L"小さな決意", L"小さな決意", L"小さな決意"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z204.opus") {
							a = LL14(L"そして、宇宙へ──", L"And Then, to Space──", L"Et puis, vers l'espace──", L"E poi, verso lo spazio──", L"Y entonces, hacia el espacio──", L"그리고, 우주로──", L"然后,向着宇宙──", L"ثم إلى الفضاء──", L"И затем — в космос──", L"Und dann, ins All──", L"E então, para o espaço──", L"En dan, de ruimte in──", L"A potem w kosmos──", L"Ve Sonra, Uzaya──"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z261.opus") {
							a = LL14(L"陽炎に浮かぶ都", L"City Floating in Heat Haze", L"Cité flottant dans la brume de chaleur", L"Città che fluttua nel miraggio", L"Ciudad flotando en el espejismo", L"아지랑이에 떠오르는 도시", L"浮现于阳炎中的都城", L"مدينة تطفو في السراب", L"Город в мареве", L"Stadt im Hitzeflimmern", L"Cidade flutuando na miragem", L"Stad in de hittewaas", L"Miasto w upalnym mirażu", L"Serapta Yüzen Kent"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z333.opus") {
							a = LL14(L"遠い夜明けを求めて", L"Seeking a Distant Dawn", L"En quête d'une aube lointaine", L"Cercando un'alba lontana", L"Buscando un amanecer lejano", L"머나먼 여명을 찾아서", L"追寻遥远的黎明", L"بحثاً عن فجر بعيد", L"В поисках далёкого рассвета", L"Auf der Suche nach einer fernen Dämmerung", L"Buscando uma aurora distante", L"Op zoek naar een verre dageraad", L"W poszukiwaniu odległego świtu", L"Uzak Bir Şafak Arayarak"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z368.opus") {
							a = L"Chaotic Beat"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z378.opus") {
							a = L"Waltz of the Chardin"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z380.opus") {
							a = L"Get to the Crux"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z385.opus") {
							a = L"Raging Bloom"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z398.opus") {
							a = L"Outrageous Bout"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z399.opus") {
							a = L"Punish with All Your Might!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z411.opus") {
							a = LL14(L"千古の夢", L"Age-Old Dream", L"Rêve immémorial", L"Sogno millenario", L"Sueño milenario", L"천고의 꿈", L"千古之梦", L"حلم قديم", L"Древний сон", L"Uralter Traum", L"Sonho milenar", L"Eeuwenoude droom", L"Wieczny sen", L"Kadim Rüya"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z415.opus") {
							a = L"Breakneck Havoc"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z417.opus") {
							a = L"The Fortress of Destiny"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z422.opus") {
							a = L"Anchor in the Dark"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z426.opus") {
							a = L"More Bets!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z427.opus") {
							a = LL14(L"暗中行路", L"Path in the Dark", L"Chemin dans l'obscurité", L"Cammino nell'oscurità", L"Camino en la oscuridad", L"암중의 행로", L"暗中行路", L"طريق في الظلام", L"Путь во тьме", L"Pfad im Dunkeln", L"Caminho na escuridão", L"Pad in het donker", L"Ścieżka w ciemności", L"Karanlıktaki Yol"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z439.opus") {
							a = L"Lunatic Showdown"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z452.opus") {
							a = L"Let's be Maximum Energy!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z453.opus") {
							a = LL14(L"光への矢印", L"Arrow Toward the Light", L"Flèche vers la lumière", L"Freccia verso la luce", L"Flecha hacia la luz", L"빛을 향한 화살표", L"指向光的箭矢", L"سهم نحو النور", L"Стрела к свету", L"Pfeil zum Licht", L"Seta em direção à luz", L"Pijl naar het licht", L"Strzała ku światłu", L"Işığa Doğru Ok"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z456.opus") {
							a = LL14(L"燐光に憩う", L"Resting in Phosphorescence", L"Repos dans la phosphorescence", L"Riposare nella fosforescenza", L"Descansar en la fosforescencia", L"인광에 쉬다", L"憩于燐光之中", L"الراحة في التوهج الفسفوري", L"Отдых в фосфоресценции", L"Ruhend im Phosphorlicht", L"Descansando na fosforescência", L"Rustend in fosforescentie", L"Odpoczynek w fosforescencji", L"Fosforışıltıda Dinlenmek"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z460.opus") {
							a = LL14(L"深き緑の小天地", L"Little World of Deep Green", L"Petit monde de vert profond", L"Piccolo mondo di verde profondo", L"Pequeño mundo de verde profundo", L"깊은 초록의 소천지", L"深绿的小天地", L"عالم صغير من الأخضر العميق", L"Малый мир глубокой зелени", L"Kleine Welt aus tiefem Grün", L"Pequeno mundo de verde profundo", L"Kleine wereld van diep groen", L"Mały świat głębokiej zieleni", L"Derin Yeşilin Küçük Dünyası"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z461.opus") {
							a = LL14(L"墜ちたる希望", L"Fallen Hope", L"Espoir déchu", L"Speranza caduta", L"Esperanza caída", L"추락한 희망", L"坠落的希望", L"أمل ساقط", L"Падшая надежда", L"Gefallene Hoffnung", L"Esperança caída", L"Gevallen hoop", L"Upadła nadzieja", L"Düşmüş Umut"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z464.opus") {
							a = L"The Height of Majesty"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z465.opus") {
							a = LL14(L"丘のあなたの空遠く", L"Beyond the Sky Over Your Hill", L"Au-delà du ciel de votre colline", L"Oltre il cielo della tua collina", L"Más allá del cielo de tu colina", L"언덕 너머 그대의 먼 하늘", L"丘上彼方的遥远天空", L"وراء سماء تلّك", L"За небом над твоим холмом", L"Jenseits des Himmels über deinem Hügel", L"Além do céu da sua colina", L"Voorbij de hemel boven jouw heuvel", L"Poza niebem nad twoim wzgórzem", L"Tependeki Gökyüzünün Ötesinde"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z466.opus") {
							a = L"So Heart-pounding"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z468.opus") {
							a = LL14(L"貴方に伝えたいこと", L"What I Want to Tell You", L"Ce que je veux te dire", L"Ciò che voglio dirti", L"Lo que quiero decirte", L"당신에게 전하고 싶은 것", L"想告诉你的事", L"ما أريد أن أخبرك به", L"То, что хочу тебе сказать", L"Was ich dir sagen möchte", L"O que quero te dizer", L"Wat ik je wil zeggen", L"Co chcę ci powiedzieć", L"Sana Söylemek İstediklerim"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z471.opus") {
							a = L"Wonderful Road"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z476.opus") {
							a = LL14(L"裏路地の犬たち", L"Dogs of the Back Alleys", L"Chiens des ruelles", L"Cani dei vicoli", L"Perros de los callejones", L"뒷골목의 개들", L"后巷的狗们", L"كلاب الأزقة الخلفية", L"Псы задних переулков", L"Hunde der Hintergassen", L"Cães dos becos", L"Honden van de steegjes", L"Psy z zaułków", L"Arka Sokakların Köpekleri"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z479.opus") {
							a = LL14(L"千切れ雲", L"Scattered Clouds", L"Nuages déchirés", L"Nuvole lacere", L"Nubes desgarradas", L"갈기갈기 찢긴 구름", L"千裂之云", L"غيوم ممزقة", L"Разорванные облака", L"Zerschnittene Wolken", L"Nuvens esfarrapadas", L"Verscheurde wolken", L"Postrzępione chmury", L"Parçalanmış Bulutlar"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z482.opus") {
							a = L"Let's Meet in a Dream"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z483.opus") {
							a = LL14(L"美味しいひととき", L"A Delicious Moment", L"Un délicieux moment", L"Un momento delizioso", L"Un momento delicioso", L"맛있는 한때", L"美味的片刻", L"لحظة لذيذة", L"Вкусный миг", L"Ein köstlicher Augenblick", L"Um momento delicioso", L"Een heerlijk moment", L"Smaczny moment", L"Lezzetli Bir An"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z484.opus") {
							a = L"Mad Killer"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z484b.opus") {
							a = L"Mad Killer"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z486_full.opus") {
							a = LL14(L"とある優しき夜想曲", L"A Certain Gentle Nocturne", L"Un certain nocturne tendre", L"Un certo notturno gentile", L"Cierto nocturno gentil", L"어느 다정한 야상곡", L"某首温柔的夜想曲", L"نوكتورن لطيف معين", L"Некий нежный ноктюрн", L"Ein gewisses sanftes Nocturne", L"Certo noturno gentil", L"Een zeker zacht nocturne", L"Pewien łagodny nokturn", L"Belirli Bir Nazik Noktürn"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z487.opus") {
							a = L"Ruthless Ruler"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z492.opus") {
							a = LL14(L"ヴァルハラの戦士たち", L"Warriors of Valhalla", L"Guerriers du Valhalla", L"Guerrieri del Valhalla", L"Guerreros del Valhalla", L"발할라의 전사들", L"瓦尔哈拉的战士们", L"محاربو فالهالا", L"Воины Вальхаллы", L"Krieger von Walhalla", L"Guerreiros do Valhalla", L"Krijgers van Walhalla", L"Wojownicy Walhalli", L"Valhalla Savaşçıları"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z493.opus") {
							a = LL14(L"流転洸刃", L"Transmigrating Brilliant Blade", L"Lame brillante en transmigration", L"Lama brillante in trasmutazione", L"Hoja brillante en transmigracion", L"유전광인", L"流转洸刃", L"نصل ساطع متقلب", L"Перерождающийся сияющий клинок", L"Wandernde gleißende Klinge", L"Lâmina brilhante em transmigração", L"Zwervend schitterend lemmet", L"Wędrujące świetliste ostrze", L"Akıp Giden Parlak Bıçak"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z494.opus") {
							a = L"To Confirm My Existence"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z496.opus") {
							a = L"The Final Phase"; fff = 1;
						}
						if (fff == 0 && ft == L"ed9z500.opus") {
							a = LL14(L"世界の明日を懸けて", L"Staking the World's Tomorrow", L"Enjeu : le lendemain du monde", L"In gioco il domani del mondo", L"Apostando el mañana del mundo", L"세계의 내일을 걸고", L"赌上世界的明天", L"على محك غد العالم", L"Ставя на завтрашний день мира", L"Die Zukunft der Welt aufs Spiel setzen", L"Colocando o amanhã do mundo em jogo", L"De toekomst van de wereld inzetten", L"Stawiając jutro świata", L"Dünyanın Yarını İçin"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9z503.opus") {
							a = L"Deviant Brain"; fff = 1;
						}
						if (fff == 0 && a.Left(3).CompareNoCase(L"ed9") == 0) {
							a = ft;
							if (a.GetLength() > 5 && a.Right(5).CompareNoCase(L".opus") == 0)
								a = a.Left(a.GetLength() - 5);
							fff = 1;
						}
					}

					// --- auto ed9 mode 34 titles ---
					if (fff == 0 && (p.sub == 34)) {
						if (fff == 0 && ft == L"ed91000.opus") {
							a = L"Dark Stalker"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91002.opus") {
							a = LL14(L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街", L"イーディス旧市街"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91003.opus") {
							a = LL14(L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器", L"世界を観測せし器"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91004.opus") {
							a = LL14(L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐", L"暗がりの静謐"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91006.opus") {
							a = L"Captive Soul"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91010.opus") {
							a = LL14(L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々", L"風に揺れる樹々"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91016.opus") {
							a = LL14(L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく", L"夜は更けゆく"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91020.opus") {
							a = LL14(L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律", L"薄ら寒き旋律"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91021.opus") {
							a = LL14(L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち", L"蠢く者たち"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91022.opus") {
							a = LL14(L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割", L"果たすべき役割"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91024.opus") {
							a = LL14(L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン", L"古都オラシオン"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91027.opus") {
							a = LL14(L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-", L"深山幽谷の郷 -龍來-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91030.opus") {
							a = LL14(L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を", L"魂の安寧を"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91031.opus") {
							a = LL14(L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91032.opus") {
							a = LL14(L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91033.opus") {
							a = LL14(L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート", L"煌都ラングポート"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91034.opus") {
							a = LL14(L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき", L"至福のひととき"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91037.opus") {
							a = LL14(L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で", L"動かざる刻の中で"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91200.opus") {
							a = L"Let me know, my SPRIGGAN..."; fff = 1;
						}
						if (fff == 0 && ft == L"ed91202.opus") {
							a = LL14(L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル", L"工学都市バーゼル"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91203.opus") {
							a = LL14(L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状", L"“A”からの招待状"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91204.opus") {
							a = L"The Days of Forest Flowers"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91206.opus") {
							a = LL14(L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所", L"アークライド解決事務所"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91207.opus") {
							a = LL14(L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側", L"曇り空の向こう側"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91208.opus") {
							a = L"Cloudy Cloudy"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91210.opus") {
							a = LL14(L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標", L"路地裏の標"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91211.opus") {
							a = LL14(L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ", L"ようこそモンマルトへ"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91216.opus") {
							a = L"Step in the EDITH"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91218.opus") {
							a = LL14(L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景", L"憩いの原風景"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91219.opus") {
							a = L"Hazard Coming"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91220.opus") {
							a = LL14(L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて", L"宵闇に溶かせて"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91223.opus") {
							a = L"Wailing Siren"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91224.opus") {
							a = LL14(L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海", L"果てなき砂の海"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91225.opus") {
							a = LL14(L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で", L"黎き狭間の中で"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91227.opus") {
							a = LL14(L"昔歳の情景", L"昔歳の情景", L"昔歳の情景", L"昔歳の情景", L"昔歳の情景", L"昔歳の情景", L"昔歳の情景", L"昔歳の情景", L"昔歳の情景", L"昔歳の情景", L"昔歳の情景", L"昔歳の情景", L"昔歳の情景", L"昔歳の情景"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91228.opus") {
							a = LL14(L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道", L"朽廃と獣道"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91229.opus") {
							a = LL14(L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91229b.opus") {
							a = LL14(L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女", L"オアシスに舞う少女"); fff = 1;
						}
						if (fff == 0 && ft == L"ed91300.opus") {
							a = L"First Skirmish"; fff = 1;
						}
						if (fff == 0 && ft == L"ed91301.opus") {
							a = L"OPEN THE SHARD!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92123.opus") {
							a = LL14(L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの", L"目指す先にあるもの"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92153.opus") {
							a = L"Tharbad Night"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92169.opus") {
							a = LL14(L"共に、新たな季節を -Diverse World-", L"Together, Toward a New Season -Diverse World-", L"Ensemble, vers une nouvelle saison -Diverse World-", L"Insieme, verso una nuova stagione -Diverse World-", L"Juntos, hacia una nueva estación -Diverse World-", L"함께, 새로운 계절을 -Diverse World-", L"一起,迎向新的季节 -Diverse World-", L"معاً نحو موسم جديد -Diverse World-", L"Вместе к новому сезону -Diverse World-", L"Gemeinsam in eine neue Jahreszeit -Diverse World-", L"Juntos, rumo a uma nova estação -Diverse World-", L"Samen naar een nieuw seizoen -Diverse World-", L"Razem ku nowej porze roku -Diverse World-", L"Birlikte Yeni Bir Mevsim -Diverse World-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92187.opus") {
							a = LL14(L"空高く風に乗り", L"Riding the Wind High in the Sky", L"Chevaucher le vent haut dans le ciel", L"Cavalcando il vento alto nel cielo", L"Montando el viento alto en el cielo", L"하늘 높이 바람을 타고", L"乘风直上高空", L"ركوب الريح عالياً في السماء", L"Оседлав ветер высоко в небе", L"Hoch am Himmel auf dem Wind reiten", L"Montando o vento alto no céu", L"Hoog in de lucht op de wind rijden", L"Pędząc z wiatrem wysoko na niebie", L"Göklerde Rüzgara Binerek"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92194.opus") {
							a = L"Rise of the GRENDEL"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92195.opus") {
							a = L"Diabolic Howl"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92197.opus") {
							a = LL14(L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し", L"快調な滑り出し"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92199.opus") {
							a = L"Too Much Vitality"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92202.opus") {
							a = L"Hard Desperation"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92203.opus") {
							a = L"Make a Breakthrough!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92205.opus") {
							a = LL14(L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参", L"麒麟児、見参"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92206.opus") {
							a = LL14(L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い", L"混沌への誘い"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92209.opus") {
							a = LL14(L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路", L"瀑布への通い路"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92210.opus") {
							a = L"Extreme Visitor"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92211.opus") {
							a = LL14(L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖", L"白銀の剣聖"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92212.opus") {
							a = L"Scorching Impact"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92213.opus") {
							a = LL14(L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭", L"革命記念祭"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92215.opus") {
							a = LL14(L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92218.opus") {
							a = L"Satisfied Madness"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92219.opus") {
							a = L"Get Rid of the Urgent Menace"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92220.opus") {
							a = LL14(L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域", L"死と戯れの領域"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92222.opus") {
							a = LL14(L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて", L"カクテルを傾けて"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92223.opus") {
							a = L"Popping Smile"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92224.opus") {
							a = L"Marginal Crisis"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92226.opus") {
							a = L"Unreasonable Beast"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92227.opus") {
							a = LL14(L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-", L"逢魔時の伝承 -黒龍城砦-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92230.opus") {
							a = L"Unmitigated Evil"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92232.opus") {
							a = L"I'm Grimcats!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92233.opus") {
							a = LL14(L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス", L"光芒のレゾナンス"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92234.opus") {
							a = L"Flowing Illusion"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92235.opus") {
							a = L"Brilliant Grow"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92236.opus") {
							a = LL14(L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台", L"晴れやかな舞台"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92239.opus") {
							a = L"Mechanical Syndrome"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92240.opus") {
							a = LL14(L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-", L"朱き賑わい -東方人街-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92242.opus") {
							a = L"Be Prepared"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92243.opus") {
							a = L"The Decisive Point"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92245.opus") {
							a = L"Bang of Metatron"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92246.opus") {
							a = LL14(L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの", L"愛おしき色、貴方だけの"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92247.opus") {
							a = L"WILD BEAT"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92248.opus") {
							a = LL14(L"想いは伝わる", L"想いは伝わる", L"想いは伝わる", L"想いは伝わる", L"想いは伝わる", L"想いは伝わる", L"想いは伝わる", L"想いは伝わる", L"想いは伝わる", L"想いは伝わる", L"想いは伝わる", L"想いは伝わる", L"想いは伝わる", L"想いは伝わる"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92253.opus") {
							a = LL14(L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ...", L"ただ、噛み締めるだけ..."); fff = 1;
						}
						if (fff == 0 && ft == L"ed92254.opus") {
							a = LL14(L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂", L"時代が求めた熱狂"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92255.opus") {
							a = LL14(L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》", L"十三区《黒芒街》"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92257.opus") {
							a = L"Have a Good Time"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92258.opus") {
							a = LL14(L"クセ者たちの挽歌", L"Elegy of the Eccentrics", L"Élégie des originaux", L"Elegia degli eccentrici", L"Elegía de los excéntricos", L"괴짜들의 만가", L"怪人们的挽歌", L"مرثية الغريبي الأطوار", L"Элегия чудаков", L"Elegie der Sonderlinge", L"Elegia dos excêntricos", L"Elegie van de zonderlingen", L"Elegia ekscentryków", L"Tuhaf Tiplerin Ağıtı"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92260b.opus") {
							a = L"Deep Affection"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92262.opus") {
							a = L"Do or Die!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92266.opus") {
							a = LL14(L"冷厳なる現実", L"Harsh Reality", L"Réalité implacable", L"Crudele realtà", L"Cruda realidad", L"냉엄한 현실", L"冷酷的现实", L"واقع قاسٍ", L"Суровая реальность", L"Harte Realität", L"Realidade severa", L"Harde realiteit", L"Surowa rzeczywistość", L"Acımasız Gerçek"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92272.opus") {
							a = LL14(L"傲岸なる欲望", L"Arrogant Desire", L"Désir arrogant", L"Desiderio arrogante", L"Deseo arrogante", L"오만한 욕망", L"傲慢的欲望", L"رغبة متغطرسة", L"Горделивое желание", L"Hochmütiges Verlangen", L"Desejo arrogante", L"Arrogant verlangen", L"Aroganckie pożądanie", L"Kibirli Arzu"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92277.opus") {
							a = LL14(L"明日への反抗 -With All Our Powers-", L"Rebellion Toward Tomorrow -With All Our Powers-", L"Rébellion vers demain -With All Our Powers-", L"Ribellione verso il domani -With All Our Powers-", L"Rebelión hacia el mañana -With All Our Powers-", L"내일로의 반항 -With All Our Powers-", L"通向明天的反抗 -With All Our Powers-", L"تمرد نحو الغد -With All Our Powers-", L"Бунт ради завтра -With All Our Powers-", L"Aufbegehren für morgen -With All Our Powers-", L"Rebelião rumo ao amanhã -With All Our Powers-", L"Opstand naar morgen -With All Our Powers-", L"Bunt ku jutru -With All Our Powers-", L"Yarına İsyan -With All Our Powers-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92278.opus") {
							a = L"Everlasting Dream"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92281.opus") {
							a = L"Invitation of Cube!"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92282.opus") {
							a = L"Exert All One's Strength"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92283.opus") {
							a = LL14(L"喧噪と吐息のあいだ", L"Between Clamor and Breath", L"Entre clameur et souffle", L"Tra clamore e respiro", L"Entre clamor y aliento", L"훤화와 한숨 사이", L"喧嚣与吐息之间", L"بين الضجيج والتنهد", L"Между шумом и вздохом", L"Zwischen Lärm und Atemzug", L"Entre clamor e suspiro", L"Tussen rumoer en ademtocht", L"Między zgiełkiem a oddechem", L"Gürültü ile Nefes Arasında"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92284.opus") {
							a = L"Infinite Ferocity"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92289.opus") {
							a = LL14(L"身を切るほどに切なくて", L"Heartache Sharp Enough to Cut", L"Douleur assez vive pour trancher", L"Dolore abbastanza tagliente da ferire", L"Dolor tan agudo que corta", L"몸이 베일 만큼 가슴 아픈", L"痛彻心肺的切念", L"ألم حاد بما يكفي ليقطع", L"Боль, острая до рези", L"Schmerz, der wie ein Schnitt sitzt", L"Dor tão cortante que fere", L"Pijn scherp genoeg om te snijden", L"Ból ostry jak cięcie", L"Kesilecek Kadar Acı"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92292.opus") {
							a = L"Believing Heart"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92295.opus") {
							a = L"Solid-State Shard Blow"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92296.opus") {
							a = LL14(L"流儀、貫くとき", L"When Style Is Carried Through", L"Quand le style s'affirme", L"Quando lo stile si fa strada", L"Cuando el estilo se impone", L"유의를 관철할 때", L"贯彻流仪之时", L"حين يُثبَّت الأسلوب", L"Когда стиль проводится до конца", L"Wenn der Stil durchgesetzt wird", L"Quando o estilo se impõe", L"Wanneer de stijl wordt doorgezet", L"Gdy styl zostaje przeprowadzony", L"Tarzın Uygulandığı An"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92298.opus") {
							a = L"Hide and Seek"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92301.opus") {
							a = LL14(L"御伽ヲ歪メシモノ", L"That Which Distorts Fairyland", L"Ce qui déforme le pays des contes", L"Ciò che distorce il paese delle fiabe", L"Lo que distorsiona el país de las hadas", L"동화를 왜곡하는 것", L"扭曲御伽之物", L"ما يشوه بلاد الجنيات", L"То, что искажает сказочную страну", L"Was das Märchenland verzerrt", L"O que distorce o país das fadas", L"Wat sprookjesland vervormt", L"To, co wypacza krainę baśni", L"Masal Diyarını Çarpıtan"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92304.opus") {
							a = LL14(L"煌天の頂を目指して", L"Aiming for the Summit of the Radiant Sky", L"Visant le sommet du ciel éclatant", L"Verso la vetta del cielo radioso", L"Hacia la cima del cielo radiante", L"황천의 정상을 향해", L"以煌天之顶为目标", L"نحو قمة السماء المتلألئة", L"К вершине сияющего неба", L"Zum Gipfel des strahlenden Himmels", L"Rumo ao cume do céu radiante", L"Op naar de top van de stralende hemel", L"Ku szczytowi lśniącego nieba", L"Parlak Göğün Zirvesine"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92306.opus") {
							a = LL14(L"在りし日を越えて", L"Beyond Days Gone By", L"Au-delà des jours d'antan", L"Oltre i giorni passati", L"Más allá de los días idos", L"지나간 날을 넘어", L"跨越往昔之日", L"ما وراء الأيام الخالية", L"За минувшими днями", L"Jenseits vergangener Tage", L"Além dos dias que se foram", L"Voorbij vervlogen dagen", L"Poza minionymi dniami", L"Geçmiş Günlerin Ötesinde"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92308.opus") {
							a = LL14(L"幽世への還り路", L"Road Back to the Netherworld", L"Chemin du retour vers l'au-delà", L"Via di ritorno all'oltretomba", L"Camino de regreso al inframundo", L"유세로의 귀환로", L"通往幽世的归路", L"طريق العودة إلى العالم الآخر", L"Путь возвращения в загробный мир", L"Rückweg in die Unterwelt", L"Caminho de volta ao além", L"Terugweg naar de onderwereld", L"Droga powrotu do zaświatów", L"Öteki Dünyaya Dönüş Yolu"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92310.opus") {
							a = L"Days Gone By"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92312.opus") {
							a = LL14(L"暖かな昼下がり", L"Warm Afternoon", L"Chaude après-midi", L"Caldo pomeriggio", L"Tarde cálida", L"따뜻한 오후", L"温暖的午后", L"ظهيرة دافئة", L"Тёплый полдень", L"Warmer Nachmittag", L"Tarde quente", L"Warme namiddag", L"Ciepłe popołudnie", L"Sıcak Öğleden Sonra"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92313.opus") {
							a = LL14(L"それでもあなたの側で", L"Still by Your Side", L"Pourtant à tes côtés", L"Eppure al tuo fianco", L"Aun así a tu lado", L"그래도 당신 곁에서", L"即便如此仍在你身边", L"ومع ذلك إلى جانبك", L"И всё же рядом с тобой", L"Dennoch an deiner Seite", L"Ainda assim ao seu lado", L"Toch aan jouw zijde", L"Mimo wszystko u twojego boku", L"Yine de Senin Yanında"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92315.opus") {
							a = LL14(L"譲れない一線", L"A Line I Won't Cross", L"Une ligne que je ne franchirai pas", L"Una linea che non varcherò", L"Una línea que no cruzaré", L"양보할 수 없는 선", L"不能退让的一线", L"خط لن أتجاوزه", L"Черта, которую не переступлю", L"Eine Linie, die ich nicht überschreite", L"Uma linha que não cruzarei", L"Een lijn die ik niet oversteek", L"Linia, której nie przekroczę", L"Vazgeçemeyeceğim Çizgi"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92318.opus") {
							a = LL14(L"暗き澱みの底で", L"At the Bottom of Dark Stagnation", L"Au fond de la stagnation sombre", L"In fondo alla stagnazione oscura", L"En el fondo del estancamiento oscuro", L"어두운 침전의 밑바닥에서", L"在暗沉淤积之底", L"في قاع الركود المظلم", L"На дне тёмного застоя", L"Am Grund dunkler Stagnation", L"No fundo da estagnação sombria", L"Op de bodem van donkere stagnatie", L"Na dnie mrocznego zastoju", L"Karanlık Durgunluğun Dibinde"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92319.opus") {
							a = LL14(L"一瞬の駆け引き", L"A Moment's Maneuver", L"Une manœuvre d'un instant", L"Una manovra d'un istante", L"Una maniobra de un instante", L"순간의 기싸움", L"一瞬的交锋", L"مناورة للحظة", L"Манёвр на мгновение", L"Manöver eines Augenblicks", L"Manobra de um instante", L"Manoeuvre van een ogenblik", L"Manewr chwili", L"Bir Anlık Hamle"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92320.opus") {
							a = L"Playing Dirty?"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92322.opus") {
							a = L"Cut Off the Retreat"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92323.opus") {
							a = LL14(L"赫焉のディグニティ", L"Dignity of the Crimson Sunset", L"Dignité du crépuscule écarlate", L"Dignità del tramonto scarlatto", L"Dignidad del ocaso escarlata", L"혁연의 디그니티", L"赫焉的尊严", L"كرامة الغروب القرمزي", L"Достоинство алого заката", L"Würde des scharlachroten Abendrots", L"Dignidade do crepúsculo escarlate", L"Waardigheid van de scharlaken zonsondergang", L"Godność szkarłatnego zmierzchu", L"Kıpkırmızı Gün Batımının İtibarı"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92326.opus") {
							a = LL14(L"遠き日の記憶", L"Memories of Distant Days", L"Souvenirs de jours lointains", L"Memorie di giorni lontani", L"Recuerdos de días lejanos", L"머나먼 날의 기억", L"遥远日子的记忆", L"ذكريات الأيام البعيدة", L"Воспоминания далёких дней", L"Erinnerungen an ferne Tage", L"Memórias de dias distantes", L"Herinneringen aan verre dagen", L"Wspomnienia odległych dni", L"Uzak Günlerin Anıları"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92330.opus") {
							a = LL14(L"共に、新たな季節を -Prelude-", L"Together, Toward a New Season -Prelude-", L"Ensemble, vers une nouvelle saison -Prelude-", L"Insieme, verso una nuova stagione -Prelude-", L"Juntos, hacia una nueva estación -Prelude-", L"함께, 새로운 계절을 -Prelude-", L"一起,迎向新的季节 -Prelude-", L"معاً نحو موسم جديد -Prelude-", L"Вместе к новому сезону -Prelude-", L"Gemeinsam in eine neue Jahreszeit -Prelude-", L"Juntos, rumo a uma nova estação -Prelude-", L"Samen naar een nieuw seizoen -Prelude-", L"Razem ku nowej porze roku -Prelude-", L"Birlikte Yeni Bir Mevsim -Prelude-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92332.opus") {
							a = L"Where to Join the Chorus"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92335.opus") {
							a = L"Just Arrived"; fff = 1;
						}
						if (fff == 0 && ft == L"ed92337.opus") {
							a = LL14(L"匣の中に在るのは...", L"What Lies Within the Box...", L"Ce qui se trouve dans la boîte...", L"Ciò che giace nella scatola...", L"Lo que hay dentro de la caja...", L"상자 속에 있는 것은...", L"匣中之物是...", L"ما يوجد داخل الصندوق...", L"То, что лежит в шкатулке...", L"Was in der Schatulle liegt...", L"O que há dentro da caixa...", L"Wat er in het kistje zit...", L"Co kryje się w szkatułce...", L"Kutunun İçinde Olan..."); fff = 1;
						}
						if (fff == 0 && ft == L"ed92342.opus") {
							a = LL14(L"古趣礼賛", L"Praise of Antique Charm", L"Éloge du charme antique", L"Elogio del fascino antico", L"Elogio del encanto antiguo", L"고취 예찬", L"古趣礼赞", L"مدح السحر العتيق", L"Хвала старинному очарованию", L"Lob des antiken Charmes", L"Elogio do charme antigo", L"Lofzang op antieke charme", L"Pochwała dawnego uroku", L"Eski Tarzın Övgüsü"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92343.opus") {
							a = LL14(L"共に、新たな季節を -Grand Finale-", L"Together, Toward a New Season -Grand Finale-", L"Ensemble, vers une nouvelle saison -Grand Finale-", L"Insieme, verso una nuova stagione -Grand Finale-", L"Juntos, hacia una nueva estación -Grand Finale-", L"함께, 새로운 계절을 -Grand Finale-", L"一起,迎向新的季节 -Grand Finale-", L"معاً نحو موسم جديد -Grand Finale-", L"Вместе к новому сезону -Grand Finale-", L"Gemeinsam in eine neue Jahreszeit -Grand Finale-", L"Juntos, rumo a uma nova estação -Grand Finale-", L"Samen naar een nieuw seizoen -Grand Finale-", L"Razem ku nowej porze roku -Grand Finale-", L"Birlikte Yeni Bir Mevsim -Grand Finale-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92343b.opus") {
							a = LL14(L"共に、新たな季節を -Grand Finale-", L"Together, Toward a New Season -Grand Finale-", L"Ensemble, vers une nouvelle saison -Grand Finale-", L"Insieme, verso una nuova stagione -Grand Finale-", L"Juntos, hacia una nueva estación -Grand Finale-", L"함께, 새로운 계절을 -Grand Finale-", L"一起,迎向新的季节 -Grand Finale-", L"معاً نحو موسم جديد -Grand Finale-", L"Вместе к новому сезону -Grand Finale-", L"Gemeinsam in eine neue Jahreszeit -Grand Finale-", L"Juntos, rumo a uma nova estação -Grand Finale-", L"Samen naar een nieuw seizoen -Grand Finale-", L"Razem ku nowej porze roku -Grand Finale-", L"Birlikte Yeni Bir Mevsim -Grand Finale-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92343c.opus") {
							a = LL14(L"共に、新たな季節を -Grand Finale-", L"Together, Toward a New Season -Grand Finale-", L"Ensemble, vers une nouvelle saison -Grand Finale-", L"Insieme, verso una nuova stagione -Grand Finale-", L"Juntos, hacia una nueva estación -Grand Finale-", L"함께, 새로운 계절을 -Grand Finale-", L"一起,迎向新的季节 -Grand Finale-", L"معاً نحو موسم جديد -Grand Finale-", L"Вместе к новому сезону -Grand Finale-", L"Gemeinsam in eine neue Jahreszeit -Grand Finale-", L"Juntos, rumo a uma nova estação -Grand Finale-", L"Samen naar een nieuw seizoen -Grand Finale-", L"Razem ku nowej porze roku -Grand Finale-", L"Birlikte Yeni Bir Mevsim -Grand Finale-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92344.opus") {
							a = LL14(L"紅の衝撃", L"Crimson Impact", L"Impact cramoisi", L"Impatto cremisi", L"Impacto carmesí", L"홍의 충격", L"红之冲击", L"صدمة قرمزية", L"Алый удар", L"Purpurner Einschlag", L"Impacto carmesim", L"Karmozijnrode inslag", L"Szkarłatne uderzenie", L"Kızıl Darbe"); fff = 1;
						}
						if (fff == 0 && ft == L"ed92_op_full.opus") {
							a = L"CRIMSON SiN"; fff = 1;
						}
						if (fff == 0 && ft == L"ed93104.opus") {
							a = LL14(L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ", L"ラ=オラシオネーズ"); fff = 1;
						}
						if (fff == 0 && ft == L"ed93105.opus") {
							a = L"The Decisive Point"; fff = 1;
						}
						if (fff == 0 && ft == L"ed93108.opus") {
							a = LL14(L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ", L"百花咲き乱れ"); fff = 1;
						}
						if (fff == 0 && ft == L"ed93112.opus") {
							a = LL14(L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好", L"陽当たり良好"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94002.opus") {
							a = L"Abysmal Erosion"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94003.opus") {
							a = LL14(L"学び舎に憩う", L"Resting at the Academy", L"Repos à l'académie", L"Riposo all'accademia", L"Descanso en la academia", L"배움터에서 쉬다", L"憩于学舍", L"الراحة في الأكاديمية", L"Отдых в учебном заведении", L"Ruhend in der Lehranstalt", L"Descansando na academia", L"Rustend op school", L"Odpoczynek w szkole", L"Okulda Dinlenmek"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94004.opus") {
							a = LL14(L"リベレーションフロント", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"리버레이션 프론트", L"启示前线", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"Revelation Front", L"Revelation Front"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94005.opus") {
							a = LL14(L"刻の螺旋", L"Spiral of Time", L"Spirale du temps", L"Spirale del tempo", L"Espiral del tiempo", L"각의 나선", L"刻之螺旋", L"لولب الزمن", L"Спираль времени", L"Spirale der Zeit", L"Espiral do tempo", L"Spiraal van de tijd", L"Spirala czasu", L"Zamanın Spiralı"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94008.opus") {
							a = L"Windy Corridor"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94010.opus") {
							a = LL14(L"暗がりの先へ", L"Beyond the Darkness", L"Au-delà de l'obscurité", L"Oltre l'oscurità", L"Más allá de la oscuridad", L"어둠 너머로", L"向着黑暗的前方", L"إلى ما وراء الظلام", L"За темнотой", L"Jenseits der Dunkelheit", L"Além da escuridão", L"Voorbij de duisternis", L"Poza ciemnością", L"Karanlığın Ötesine"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94015.opus") {
							a = L"The Battle by a Hair's Breath"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94016.opus") {
							a = LL14(L"崩れる日常", L"Collapsing Everyday", L"Quotidien qui s'effondre", L"Quotidiano che crolla", L"Cotidiano que se derrumba", L"무너지는 일상", L"崩坏的日常", L"اليوميات المنهارة", L"Разрушающаяся повседневность", L"Zusammenbrechender Alltag", L"Cotidiano que desaba", L"Ineenstortend dagelijks leven", L"Waląca się codzienność", L"Yıkılan Günlük Hayat"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94017.opus") {
							a = LL14(L"湧き上がる疑念", L"Rising Doubts", L"Doutes qui montent", L"Dubbi che sorgono", L"Dudas que emergen", L"솟아오르는 의념", L"涌起的疑念", L"شكوك متصاعدة", L"Нарастающие сомнения", L"Aufkommende Zweifel", L"Dúvidas que emergem", L"Opkomende twijfels", L"Narastające wątpliwości", L"Yükselen Şüpheler"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94018.opus") {
							a = L"Smoky Smoky"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94020.opus") {
							a = L"Advent Day"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94021.opus") {
							a = L"Irreverent Laughter"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94022.opus") {
							a = LL14(L"想い愁う", L"Sorrowful Thoughts", L"Pensées mélancoliques", L"Pensieri melanconici", L"Pensamientos melancólicos", L"시름에 잠긴 마음", L"忧思", L"أفكار حزينة", L"Печальные думы", L"Trauernde Gedanken", L"Pensamentos melancólicos", L"Droevige gedachten", L"Smutne myśli", L"Hüzünlü Düşünceler"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94023.opus") {
							a = LL14(L"焔舞 -友のために-", L"Flame Dance -For a Friend-", L"Danse des flammes -Pour un ami-", L"Danza delle fiamme -Per un amico-", L"Danza de llamas -Por un amigo-", L"염무 -친구를 위해-", L"焰舞 -为了友人-", L"رقصة اللهب -من أجل صديق-", L"Танец пламени -Ради друга-", L"Flammentanz -Für einen Freund-", L"Dança das chamas -Por um amigo-", L"VlammenDans -Voor een vriend-", L"Taniec płomieni -Dla przyjaciela-", L"Alev Dansı -Bir Dost İçin-"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94024.opus") {
							a = L"Blaze Soul"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94025.opus") {
							a = LL14(L"さあ冒険の世界へ", L"Now, to the World of Adventure", L"Allons vers le monde de l'aventure", L"Ora, verso il mondo dell'avventura", L"Ahora, al mundo de la aventura", L"자, 모험의 세계로", L"出发吧,向冒险的世界", L"هيا إلى عالم المغامرة", L"Вперёд, в мир приключений", L"Auf in die Welt des Abenteuers", L"Agora, ao mundo da aventura", L"Nu, naar de wereld van avontuur", L"Naprzód, do świata przygód", L"Haydi Macera Dünyasına"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94026.opus") {
							a = LL14(L"幻想領域", L"Realm of Fantasy", L"Royaume de fantaisie", L"Reame della fantasia", L"Reino de la fantasía", L"환상 영역", L"幻想领域", L"عالم الخيال", L"Царство фантазии", L"Reich der Fantasie", L"Reino da fantasia", L"Rijk der fantasie", L"Kraina fantazji", L"Fantazi Âlemi"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94027.opus") {
							a = LL14(L"夢はゴロゴロライフ", L"Dreams Are a Laid-Back Life", L"Les rêves, une vie tranquille", L"I sogni sono una vita rilassata", L"Los sueños son una vida relajada", L"꿈은 뒹굴뒹굴 라이프", L"梦想是慵懒生活", L"الأحلام حياة مسترخية", L"Мечты — это беззаботная жизнь", L"Träume sind ein gemütliches Leben", L"Sonhos são uma vida descontraída", L"Dromen zijn een relaxed leven", L"Marzenia to leniwe życie", L"Rüyalar Tembel Bir Hayat"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94028.opus") {
							a = LL14(L"マルドゥックサポートへようこそ", L"Welcome to Marduk Support", L"Bienvenue chez le support Marduk", L"Benvenuti al supporto Marduk", L"Bienvenido al soporte Marduk", L"마르두크 서포트에 오신 것을 환영합니다", L"欢迎来到马尔杜克支持", L"مرحباً بكم في دعم ماردوك", L"Добро пожаловать в поддержку Мардука", L"Willkommen beim Marduk-Support", L"Bem-vindo ao suporte Marduk", L"Welkom bij Marduk-ondersteuning", L"Witamy w wsparciu Marduka", L"Marduk Destek'e Hoş Geldiniz"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94029.opus") {
							a = LL14(L"最悪の思惑", L"The Worst Designs", L"Les pires intentions", L"I peggiori disegni", L"Las peores intenciones", L"최악의 속셈", L"最坏的企图", L"أسوأ النوايا", L"Худшие замыслы", L"Die schlimmsten Absichten", L"As piores intenções", L"De ergste bedoelingen", L"Najgorsze zamiary", L"En Kötü Hesaplar"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94200.opus") {
							a = LL14(L"迫りくる災厄", L"Approaching Calamity", L"Calamité qui approche", L"Calamità in avvicinamento", L"Calamidad que se acerca", L"다가오는 재앙", L"迫近的灾厄", L"كارثة تقترب", L"Приближающееся бедствие", L"Nahendes Unheil", L"Calamidade que se aproxima", L"Naderende ramp", L"Zbliżająca się klęska", L"Yaklaşan Felaket"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94201.opus") {
							a = LL14(L"迫りくる災厄", L"Approaching Calamity", L"Calamité qui approche", L"Calamità in avvicinamento", L"Calamidad que se acerca", L"다가오는 재앙", L"迫近的灾厄", L"كارثة تقترب", L"Приближающееся бедствие", L"Nahendes Unheil", L"Calamidade que se aproxima", L"Naderende ramp", L"Zbliżająca się klęska", L"Yaklaşan Felaket"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94202.opus") {
							a = LL14(L"君が残してくれたもの", L"What You Left Behind", L"Ce que tu as laissé", L"Ciò che hai lasciato", L"Lo que dejaste atrás", L"네가 남겨준 것", L"你留下的东西", L"ما تركته لي", L"То, что ты оставил", L"Was du hinterlassen hast", L"O que você deixou", L"Wat jij achterliet", L"To, co zostawiłeś", L"Bıraktığın Şeyler"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94203.opus") {
							a = LL14(L"迫りくる災厄", L"Approaching Calamity", L"Calamité qui approche", L"Calamità in avvicinamento", L"Calamidad que se acerca", L"다가오는 재앙", L"迫近的灾厄", L"كارثة تقترب", L"Приближающееся бедствие", L"Nahendes Unheil", L"Calamidade que se aproxima", L"Naderende ramp", L"Zbliżająca się klęska", L"Yaklaşan Felaket"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94400.opus") {
							a = L"Regain the Administration! -Field Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94401.opus") {
							a = L"Regain the Administration! -Command Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94402.opus") {
							a = L"The Expanding World"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94404.opus") {
							a = L"Pushed Over"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94406.opus") {
							a = LL14(L"モノクローム・デイズ", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"모노크롬 데이즈", L"单色的日子", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days", L"Monochrome Days"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94408.opus") {
							a = LL14(L"愛しき刻限の果てに", L"At the End of the Beloved Hour", L"Au bout de l'heure chérie", L"Alla fine dell'ora amata", L"Al final de la hora querida", L"사랑스러운 각한의 끝에서", L"在心爱时限的尽头", L"في نهاية الساعة العزيزة", L"В конце любимого срока", L"Am Ende der geliebten Frist", L"No fim da hora amada", L"Aan het eind van het geliefde uur", L"U kresu ukochanej godziny", L"Sevgili Vaktin Sonunda"); fff = 1;
						}
						if (fff == 0 && ft == L"ed94410.opus") {
							a = L"Work Out a Solution! -Field Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94411.opus") {
							a = L"Work Out a Solution! -Command Battle-"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94412.opus") {
							a = L"Into the Chill"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94414.opus") {
							a = L"Buster the Ghost of Garden"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94415.opus") {
							a = L"Skip to the Edith"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94416.opus") {
							a = L"Hard-Boiled Short Cake"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94417.opus") {
							a = L"Blurred Scene"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94418.opus") {
							a = L"After the Spring Rain"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94419.opus") {
							a = L"With the Dilemma"; fff = 1;
						}
						if (fff == 0 && ft == L"ed94420.opus") {
							a = LL14(L"雪解けの知らせ", L"News of the Thaw", L"Nouvelles du dégel", L"Notizie del disgelo", L"Noticias del deshielo", L"눈이 녹는 소식", L"融雪的消息", L"أخبار ذوبان الثلج", L"Весть о таянии снега", L"Kunde vom Tauwetter", L"Notícias do degelo", L"Nieuws van de dooi", L"Wieść o odwilży", L"Karların Erime Haberi"); fff = 1;
						}
						if (fff == 0 && ft == L"ed97305.opus") {
							a = LL14(L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て"); fff = 1;
						}
						if (fff == 0 && ft == L"ed9_op_full.opus") {
							a = LL14(L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て", L"名もなき悪夢の果て"); fff = 1;
						}
						if (fff == 0 && a.Left(3).CompareNoCase(L"ed9") == 0) {
							a = ft;
							if (a.GetLength() > 5 && a.Right(5).CompareNoCase(L".opus") == 0)
								a = a.Left(a.GetLength() - 5);
							fff = 1;
						}
					}

if (fff == 0)
						if (a.Left(2) == "y9") {
							if (a.Mid(4, 4) == "b001") { a = "FEEL FORCE"; }
							if (a.Mid(4, 4) == "b002") { a = "TROUBLEMAKER"; }
							if (a.Mid(4, 4) == "b003") { a = "MONSTRUM SPECTRUM"; }
							if (a.Mid(4, 4) == "b004") { a = "LACRIMA CRISIS"; }
							if (a.Mid(4, 4) == "b005") { a = "WELCOME TO CHAOS"; }
							if (a.Mid(4, 4) == "b006") { a = "JUDGEMENT TIME"; }
							if (a.Mid(4, 4) == "b007") { a = "KNOCK ON NOX"; }
							if (a.Mid(4, 4) == "b008") { a = "ANIMA ERGASTULUM"; }
							if (a.Mid(4, 5) == "b010b") { a = "URBAN TERROR"; }
							if (a.Mid(4, 4) == "b010") { a = LL14(L"URBAN TERROR(イントロあり)", L"URBAN TERROR (With Intro)", L"URBAN TERROR (Avec Intro)", L"URBAN TERROR (Con Intro)", L"URBAN TERROR (Con Intro)", L"URBAN TERROR (인트로 있음)", L"URBAN TERROR (含前奏)", L"URBAN TERROR (مع مقدمة)", L"URBAN TERROR (С интро)", L"URBAN TERROR (Mit Intro)", L"URBAN TERROR (Com Intro)", L"URBAN TERROR (Met Intro)", L"URBAN TERROR (Z intro)", L"URBAN TERROR (Girişli)"); }
							if (a.Mid(4, 5) == "b011b") { a = "DREAMING IN THE GRIMWALD"; }
							if (a.Mid(4, 4) == "b011") { a = LL14(L"DREAMING IN THE GRIMWALD(イントロあり)", L"DREAMING IN THE GRIMWALD (With Intro)", L"DREAMING IN THE GRIMWALD (Avec Intro)", L"DREAMING IN THE GRIMWALD (Con Intro)", L"DREAMING IN THE GRIMWALD (Con Intro)", L"DREAMING IN THE GRIMWALD (인트로 있음)", L"DREAMING IN THE GRIMWALD (含前奏)", L"DREAMING IN THE GRIMWALD (مع مقدمة)", L"DREAMING IN THE GRIMWALD (С интро)", L"DREAMING IN THE GRIMWALD (Mit Intro)", L"DREAMING IN THE GRIMWALD (Com Intro)", L"DREAMING IN THE GRIMWALD (Met Intro)", L"DREAMING IN THE GRIMWALD (Z intro)", L"DREAMING IN THE GRIMWALD (Girişli)"); }
							if (a.Mid(4, 4) == "b012") { a = "WILD CARD"; }
							if (a.Mid(4, 5) == "b014b") { a = "FULL MOON CEREMONY"; }
							if (a.Mid(4, 4) == "b014") { a = LL14(L"FULL MOON CEREMONY(イントロあり)", L"FULL MOON CEREMONY (With Intro)", L"FULL MOON CEREMONY (Avec Intro)", L"FULL MOON CEREMONY (Con Intro)", L"FULL MOON CEREMONY (Con Intro)", L"FULL MOON CEREMONY (인트로 있음)", L"FULL MOON CEREMONY (含前奏)", L"FULL MOON CEREMONY (مع مقدمة)", L"FULL MOON CEREMONY (С интро)", L"FULL MOON CEREMONY (Mit Intro)", L"FULL MOON CEREMONY (Com Intro)", L"FULL MOON CEREMONY (Met Intro)", L"FULL MOON CEREMONY (Z intro)", L"FULL MOON CEREMONY (Girişli)"); }
							if (a.Mid(4, 4) == "d101") { a = "HEART BEAT SHAKER"; }
							if (a.Mid(4, 4) == "d201") { a = "CLOACA MAXIMA"; }
							if (a.Mid(4, 4) == "d301") { a = "RUIN OF DRY MOAT"; }
							if (a.Mid(4, 4) == "d401") { a = "MARIONETTE, MARIONETTE"; }
							if (a.Mid(4, 4) == "d501") { a = "THE CAVE OF GROAN"; }
							if (a.Mid(4, 4) == "d601") { a = "EVAN MACHA"; }
							if (a.Mid(4, 4) == "d701") { a = "A QUARRY RUIN"; }
							if (a.Mid(4, 4) == "d702") { a = "CROSSING A/A"; }
							if (a.Mid(4, 4) == "d801") { a = "CATCH ME IF YOU CAN"; }
							if (a.Mid(4, 4) == "d901") { a = "ALCHEMY LAB"; }
							if (a.Mid(4, 4) == "d911") { a = "STRATEGIC ZONE"; }
							if (a.Mid(4, 5) == "d1001") { a = "FORTRESS UNDERGROUND"; }
							if (a.Mid(4, 5) == "d2001") { a = "DANCE WITH TRAPS"; }
							if (a.Mid(4, 4) == "e001") { a = "APRILIS"; }
							if (a.Mid(4, 4) == "e002") { a = "TAKE IT EASY!"; }
							if (a.Mid(4, 4) == "e003") { a = "PETITE FLEUR"; }
							if (a.Mid(4, 4) == "e004") { a = "EYES ON..."; }
							if (a.Mid(4, 4) == "e005") { a = "FORGOTTEN DAYS"; }
							if (a.Mid(4, 4) == "e006") { a = "PRISON OF BALDUQ -LIVE THE FUTURE-"; }
							if (a.Mid(4, 4) == "e007") { a = "PRISON OF BALDUQ -YEARNING-"; }
							if (a.Mid(4, 4) == "e008") { a = L"IL ÉTAIT UNE FOIS"; }
							if (a.Mid(4, 4) == "e009") { a = "WHO KNOWS THE TRUTH?"; }
							if (a.Mid(4, 4) == "e010") { a = "DECISION"; }
							if (a.Mid(4, 4) == "e011") { a = "STAGNANT POOL"; }
							if (a.Mid(4, 4) == "e013") { a = "INQUISITION"; }
							if (a.Mid(4, 4) == "e014") { a = "SILLY MEETING"; }
							if (a.Mid(4, 4) == "e016") { a = "MONSTRUM NOX"; }
							if (a.Mid(4, 4) == "e017") { a = "CHALLENGER'S ROAD"; }
							if (a.Mid(4, 4) == "e018") { a = "RED MULETA"; }
							if (a.Mid(4, 4) == "e019") { a = "NAB THE TAIL"; }
							if (a.Mid(4, 4) == "e020") { a = "THUS SPOKE AN ALCHEMIST"; }
							if (a.Mid(4, 4) == "e023") { a = "DENOUEMENT"; }
							if (a.Mid(4, 4) == "e024") { a = "INVITATION TO THE CRIMSON NIGHT"; }
							if (a.Mid(4, 4) == "f101") { a = "NORSE WIND"; }
							if (a.Mid(4, 4) == "f201") { a = "TRANQUIL SILENCE"; }
							if (a.Mid(4, 4) == "f301") { a = "GLESSING WAY!"; }
							if (a.Mid(4, 4) == "f501") { a = "DESERT AFTER TEARS"; }
							if (a.Mid(4, 4) == "muon") { a = LL14(L"無音", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"무음", L"无音", L"صمت", L"Тишина", L"Stille", L"Silencio", L"Stilte", L"Cisza", L"Sessizlik"); }
							if (a.Mid(4, 4) == "t101") { a = "PRISONCITY"; }
							if (a.Mid(4, 4) == "t102") { a = "IN PROFILE, ON BELFRY"; }
							if (a.Mid(4, 4) == "t103") { a = "NEW LIFE"; }
							if (a.Mid(4, 4) == "t104") { a = "GRIA RECOLLECTION"; }
							if (a.Mid(4, 4) == "t201") { a = "BAR \"DANDELION\""; }
							if (a.Mid(4, 4) == "t301") { a = "AMBIGUOUS TERRITORY"; }
							if (a.Mid(4, 4) == "t402") { a = "WALTZ FOR GRACE"; }
							if (a.Mid(4, 4) == "t501") { a = "HEAT AND SPLENDOR"; }
							if (a.Mid(4, 4) == "t901") { a = "ONLY THE CORPSE GOES OUT"; }
							if (a.Mid(4, 4) == "t902") { a = "A GOLDEN KEY CAN OPEN ANY DOOR"; }
							if (a.Mid(4, 4) == "tbox") { a = "TREASURE BOX -Ys IX-"; }
						}
						else {
							switch (_ttoi(a.Mid(2, 5))) {
							case 81004:
								a = LL14(L"罪と罰と偽りと", L"Sin, Punishment and Falsehood", L"Péché, punition et mensonge", L"Peccato, punizione e falsità", L"Pecado, castigo y falsedad", L"죄와 벌과 거짓과", L"罪、罰與欺偽", L"الخطيئة والعقاب والزور", L"Грех, наказание и ложь", L"Sünde, Strafe und Falschheit", L"Pecado, castigo e falsidade", L"Zonde, straf en valsheid", L"Grzech, kara i fałsz", L"Günah, Ceza ve Sahtelik");
								break;
							case 81005:
								a = LL14(L"昏き鐘の残響", L"Resonance of the Dark Bell", L"Résonance de la cloche sombre", L"Risonanza della campana oscura", L"Resonancia de la campana oscura", L"어두운 종의 잔향", L"昏暗之鐘的殘響", L"رنين الجرس المظلم", L"Резонанс темного колокола", L"Resonanz der dunklen Glocke", L"Ressonância do sino sombrio", L"Resonantie van de duistere klok", L"Rezonans mrocznego dzwonu", L"Karanlık Canın Yankısı");
								break;
							case 81006:
								a = "Right on the Mark";
								break;
							case 81007:
								a = LL14(L"悪夢ふたたび", L"Nightmare Again", L"Le cauchemar recommence", L"Incubo di nuovo", L"Pesadilla de nuevo", L"악몽은 다시", L"噩夢重現", L"الكابوس يعود مجدداً", L"Кошмар снова", L"Albtraum erneut", L"Pesadelo novamente", L"Nachtmerrie opnieuw", L"Koszmar ponownie", L"Kabus Yeniden");
								break;
							case 81008:
								a = "Crossbell Nostalgia";
								break;
							case 81009:
								a = LL14(L"創まりの円庭", L"Garden of Beginnings", L"Jardin des commencements", L"Giardino degli inizi", L"Jardín de los inicios", L"시작의 원정", L"創始之圓庭", L"حديقة البدايات", L"Сад начал", L"Garten der Anfänge", L"Jardim dos começos", L"Tuin van het begin", L"Ogród początków", L"Başlangıç Bahçesi");
								break;
							case 81010:
								a = "Mysterious Element";
								break;
							case 81012:
								a = "Stand Up Again and Again!";
								break;
							case 81014:
								a = "Purgatory Scream";
								break;
							case 81015:
								a = LL14(L"さざめきの途路", L"Path of Tumult", L"Chemin du tumulte", L"Sentiero del tumulto", L"Senda del tumulto", L"웅성거림의 길", L"嘈雜的途徑", L"طريق الاضطراب", L"Путь суматохи", L"Pfad des Tumults", L"Caminho do tumulto", L"Pad van rumoer", L"Ścieżka zgiełku", L"Gürültülü Yol");
								break;
							case 81016:
								a = LL14(L"蒼の大地に生きる者", L"Those Who Live on the Azure Land", L"Ceux qui vivent sur la terre d'azur", L"Coloro che vivono sulla terra azzurra", L"Aquellos que viven en la tierra azul", L"창의 대지에 사는 자", L"生活在蒼之大地的人", L"الذين يعيشون على الأرض الزرقاء", L"Те, кто живет на лазурной земле", L"Die auf dem azurblauen Land leben", L"Aqueles que vivem na terra azul", L"Zij die op het azuurblauwe land leven", L"Ci, którzy żyją na błękitnej ziemi", L"Mavi Topraklarda Yaşayanlar");
								break;
							case 81017:
								a = LL14(L"黎明の鐘", L"Bell of Dawn", L"Cloche de l'aube", L"Campana dell'alba", L"Campana del alba", L"여명의 종", L"黎明之鐘", L"جرس الفجر", L"Колокол рассвета", L"Glocke der Dämmerung", L"Sino da aurora", L"Klok van de dageraad", L"Dzwon świtu", L"Şafak Canı");
								break;
							case 81018:
								a = LL14(L"レメディファンタジア -仲間とともに-", L"Remedi Fantasia -With Comrades-", L"Remedi Fantasia -Avec des camarades-", L"Remedi Fantasia -Con i compagni-", L"Remedi Fantasia -Con camaradas-", L"레메디 판타지아 ~동료와 함께~", L"Remedi Fantasia -與夥伴一起-", L"ريميدي فانتازيا -مع الرفاق-", L"Remedi Fantasia -С товарищами-", L"Remedi Fantasia -Mit Kameraden-", L"Remedi Fantasia -Com camaradas-", L"Remedi Fantasia -Met kameraden-", L"Remedi Fantasia -Z towarzyszami-", L"Remedi Fantasia -Yoldaşlarla-");
								break;
							case 81019:
								a = "Slight Suspicion";
								break;
							case 81020:
								a = "Maliciousness in the Mirror";
								break;
							case 81021:
								a = LL14(L"暗澹たる世界", L"Dark World", L"Monde sombre", L"Mondo oscuro", L"Mundo oscuro", L"암담한 세계", L"暗淡的世界", L"عالم مظلم", L"Мрачный мир", L"Dunkle Welt", L"Mundo sombrio", L"Duistere wereld", L"Mroczny świat", L"Karanlık Dünya");
								break;
							case 81022:
								a = LL14(L"ひとときの温もり", L"Brief Warmth", L"Bref répit de chaleur", L"Breve calore", L"Breve calor", L"한때의 온기", L"片刻的溫暖", L"دفء عابر", L"Краткое тепло", L"Kurze Wärme", L"Breve calor", L"Korte warmte", L"Krótkie ciepło", L"Kısa Süreli Sıcaklık");
								break;
							case 81023:
								a = LL14(L"今、創まりのとき", L"Now, the Moment of Creation", L"Maintenant, le moment de la création", L"Ora, il momento della creazione", L"Ahora, el momento de la creación", L"지금, 시작의 시간", L"現在，創始之時", L"الآن، لحظة التأسيس", L"Теперь момент сотворения", L"Nun, der Moment der Schöpfung", L"Agora, o momento da criação", L"Nu, het moment van creatie", L"Teraz moment stworzenia", L"Şimdi, Yaratılış Anı");
								break;
							case 81024:
								a = "KERAUNOS -Fear and Hatred-";
								break;
							case 81025:
								a = LL14(L"亡失われた魂", L"Lost Souls", L"Âmes perdues", L"Anime perse", L"Almas perdidas", L"상실된 영혼", L"迷失的靈魂", L"الأرواح المفقودة", L"Потерянные души", L"Verlorene Seelen", L"Almas perdidas", L"Verloren zielen", L"Zagubione dusze", L"Kayıp Ruhlar");
								break;
							case 81026:
								a = LL14(L"穏やかな時間", L"Peaceful Time", L"Temps paisible", L"Tempo pacifico", L"Tiempo pacífico", L"평온한 시간", L"平靜的時光", L"وقت هادئ", L"Мирное время", L"Friedliche Zeit", L"Tempo pacífico", L"Vredige tijd", L"Spokojny czas", L"Huzurlu Vakit");
								break;
							case 81028:
								a = LL14(L"運命という名の歯車", L"Gears of Fate", L"Engrenages du destin", L"Ingranaggi del destino", L"Engranajes del destino", L"운명이라는 이름의 톱니바퀴", L"名為命運的齒輪", L"تروس القدر", L"Шестеренки судьбы", L"Zahnräder des Schicksals", L"Engrenagens do destino", L"Raderen van het lot", L"Koła zębate losu", L"Kader Çarkları");
								break;
							case 81200:
								a = "Crossing Causal Lines";
								break;
							case 81201:
								a = "Glittering Mirage";
								break;
							case 81202:
								a = "Like a Whirlwind";
								break;
							case 81203:
								a = "Hide and Seek by Myself";
								break;
							case 81315:
								a = L"Mines Town Mainz -Reverie Ver.-";
								break;
							case 81316:
								a = L"Path of Echoes -Reverie Ver.-";
								break;
							case 81317:
								a = "Raindrops with the Wind";
								break;
							case 81319:
								a = LL14(L"陽溜まりにただいまを", L"Home in the Sunshine", L"Retour au soleil", L"A casa sotto il sole", L"Hogar bajo el sol", L"햇살 아래 다녀왔습니다", L"在陽光下，我回來了", L"العودة للمنزل في ضوء الشمس", L"Домой под лучами солнца", L"Zuhause im Sonnenschein", L"Lar sob o sol", L"Thuis in de zon", L"Dom w słońcu", L"Güneş Işığında Eve Dönüş");
								break;
							case 81320:
								a = "Wind-Up Yesterday!";
								break;
							case 81321:
								a = LL14(L"零の邂逅", L"Zero Encounter", L"Rencontre de zéro", L"Incontro zero", L"Encuentro cero", L"영의 해후", L"零之邂逅", L"لقاء الصفر", L"Встреча Зеро", L"Zero-Begegnung", L"Encontro zero", L"Zero ontmoeting", L"Spotkanie zero", L"Sıfır Karşılaşması");
								break;
							case 81322:
								a = LL14(L"影の見えざる手", L"Invisible Hand in the Shadows", L"Main invisible dans l'ombre", L"Mano invisibile nelle ombre", L"Mano invisible en las sombras", L"그림자의 보이지 않는 손", L"影子那看不見的手", L"اليد الخفية في الظلال", L"Невидимая рука в тени", L"Unsichtbare Hand im Schatten", L"Mão invisível nas sombras", L"Onzichtbare hand in de schaduw", L"Niewidzialna ręka w cieniu", L"Gölgedeki Görünmez El");
								break;
							case 82065:
								a = LL14(L"鋼鉄牙城", L"Iron Fortress", L"Forteresse d'acier", L"Fortezza d'acciaio", L"Fortaleza de acero", L"강철아성", L"鋼鐵牙城", L"القلعة الحديدية", L"Железная крепость", L"Eiserne Festung", L"Fortaleza de aço", L"IJzeren vesting", L"Stalowa twierdza", L"Demir Kale");
								break;
							case 82113:
								a = "Zero Break Battle";
								break;
							case 82114:
								a = "Stake Everything Strategy";
								break;
							case 82124:
								a = "POM's Paradise";
								break;
							case 82125:
								a = LL14(L"波間に弾む心", L"Heart Bouncing on the Waves", L"Cœur bondissant sur les vagues", L"Cuore che rimbalza sulle onde", L"Corazón saltando en las olas", L"물결 속에 설레는 마음", L"在波浪間雀躍的心", L"قلب يقفز فوق الأمواج", L"Сердце, прыгающее на волнах", L"Herz, das auf den Wellen hüpft", L"Coração saltitando nas ondas", L"Hart dat stuitert op de golven", L"Serce skaczące na falach", L"Dalgalarda Hoplayan Kalp");
								break;
							case 82129:
								a = "Reverse Babel";
								break;
							case 82131:
								a = "Aim a Gun at the Bullet";
								break;
							case 82133:
								a = "Section G.F.S. II";
								break;
							case 82135:
								a = "Magical Revolt";
								break;
							case 82136:
								a = LL14(L"流麗闘冴", L"Elegant Battle", L"Combat élégant", L"Battaglia elegante", L"Batalla elegante", L"유려투사", L"流麗鬥冴", L"معركة أنيقة", L"Элегантная битва", L"Eleganter Kampf", L"Batalha elegante", L"Elegant gevecht", L"Elegancka bitwa", L"Zarif Savaş");
								break;
							case 82137:
								a = "The Road to All-Out War";
								break;
							case 82138:
								a = "LAPIS";
								break;
							case 82140:
								a = "Invisible Hilly Country";
								break;
							case 82141:
								a = LL14(L"ひとかけらの光明", L"Sliver of Light", L"Lueur d'espoir", L"Barlume di luce", L"Rayo de luz", L"한 조각의 광명", L"一絲光明", L"بصيص من الأمل", L"Лучик света", L"Ein Schimmer Licht", L"Raio de luz", L"Lichtstraaltje", L"Promyk światła", L"Bir Işık Huzmesi");
								break;
							case 82143:
								a = LL14(L"反攻の烽火", L"Beacon of Counterattack", L"Signal de contre-attaque", L"Segnale di contrattacco", L"Señal de contraataque", L"반격의 봉화", L"反攻的烽火", L"منارة الهجوم المضاد", L"Маяк контратаки", L"Leuchtfeuer des Gegenangriffs", L"Sinal de contra-ataque", L"Baken van de tegenaanval", L"Sygnał kontrataku", L"Karşı Atak İşareti");
								break;
							case 82147:
								a = "Rapid Wind";
								break;
							case 82148:
								a = "NO END NO WORLD -Instrumental Ver.-";
								break;
							case 82150:
								a = "Be Caught Up!";
								break;
							case 82151:
								a = "Breeding Innumerable Arms";
								break;
							case 82152:
								a = "The Destination of FATE";
								break;
							case 82154:
								a = "Twinkle Attack";
								break;
							case 82157:
								a = "Sword of Swords";
								break;
							case 82158:
								a = LL14(L"今宵は宴と参りましょう", L"Tonight We Feast", L"Ce soir, nous festoyons", L"Stasera banchettiamo", L"Esta noche festejamos", L"오늘 밤은 연회를 열지요", L"今夜讓我們舉行宴會吧", L"الليلة سنقيم مأدبة", L"Сегодня мы пируем", L"Heute Abend wird gefeiert", L"Esta noite vamos festejar", L"Vanavond vieren we feest", L"Dziś wieczorem ucztujemy", L"Bu Gece Ziyafet Çekelim");
								break;
							case 82159:
								a = "Flash Your Fighting Spirit";
								break;
							case 82161:
								a = LL14(L"鈍色に這う", L"Crawling in Gray", L"Ramper dans le gris", L"Strisciando nel grigio", L"Gateando en el gris", L"회색빛으로 기어가다", L"在灰色中爬行", L"الزحف في اللون الرمادي", L"Ползти в сером", L"Kriechen im Grau", L"Rastejando no cinza", L"Kruipen in het grijs", L"Pełzanie w szarości", L"Gri İçinde Sürünmek");
								break;
							case 82163:
								a = "Pyro Labyrinth";
								break;
							case 82164:
								a = LL14(L"優しさを未来に託して", L"Entrust Kindness to the Future", L"Confier la gentillesse au futur", L"Affidare la gentilezza al futuro", L"Confiar la amabilidad al futuro", L"상냥함을 미래에 맡기고", L"將溫柔託付給未來", L"إيداع اللطف للمستقبل", L"Вверить доброту будущему", L"Güte der Zukunft anvertrauen", L"Confiar a bondade ao futuro", L"Vriendelijkheid aan de toekomst toevertrouwen", L"Powierzyć dobroć przyszłości", L"Nezaketi Geleceğe Emanet Etmek");
								break;
							case 82166:
								a = LL14(L"高らかに、誇らしく", L"Loud and Proud", L"Fort et fier", L"Forte e fiero", L"Fuerte y orgulloso", L"드높게, 자랑스럽게", L"高聲地，自豪地", L"بصوت عال وبكل فخر", L"Громко и гордо", L"Laut und stolz", L"Alto e orgulhoso", L"Luid en trots", L"Głośno i dumnie", L"Yüksek Sesle ve Gururla");
								break;
							case 82170:
								a = "Infinity Rage";
								break;
							case 82171:
								a = "Heavy Violent Match";
								break;
							case 82173:
								a = "Roar of Evil Spirits";
								break;
							case 82174:
								a = "Bad Dream Invasion";
								break;
							case 82175:
								a = "Golden Fever";
								break;
							case 82177:
								a = "The Perfect Steel of ZERO";
								break;
							case 82178:
								a = "Twilight Hermitage";
								break;
							case 82179:
								a = "Something Luxury...?";
								break;
							case 82183:
								a = "Challenger Invigorated";
								break;
							case 82184:
								a = LL14(L"このあと美味しくいただきました", L"Then We Ate Deliciously", L"Ensuite, nous avons mangé délicieusement", L"Poi abbiamo mangiato deliziosamente", L"Luego comimos deliciosamente", L"이후 맛있게 먹었습니다", L"在那之後我們美味地享用了", L"بعد ذلك استمتعنا بالأكل", L"Затем мы вкусно поели", L"Dann haben wir köstlich gegessen", L"Depois comemos deliciosamente", L"Daarna hebben we heerlijk gegeten", L"Potem zjedliśmy wybornie", L"Sonra Afiyetle Yedik");
								break;
							case 82186:
								a = "Emergency Order";
								break;
							case 82188:
								a = LL14(L"激烈! 撃滅! ミシュナイダー!!", L"Fierce! Crush! Mishnayder!!", L"Féroce ! Écraser ! Mishnayder !!", L"Feroce! Schiaccia! Mishnayder!!", L"¡Feroz! ¡Aplasta! ¡Mishnayder!", L"격렬! 격멸! 미슈나이더!!", L"激烈！擊滅！Mishnayder！！", L"ضارٍ! ساحق! ميشنايدر!!", L"Яростно! Разгромить! Mishnayder!!", L"Heftig! Zerschmettern! Mishnayder!!", L"Feroz! Esmagar! Mishnayder!!", L"Heftig! Verpletter! Mishnayder!!", L"Gwałtownie! Zmiażdży! Mishnayder!!", L"Sert! Ez Geç! Mishnayder!!");
								break;
							case 82189:
								a = "Life Goes On";
								break;
							case 8001:
								a = LL14(L"特科クラス《VII組》", L"Class VII", L"Classe VII", L"Classe VII", L"Clase VII", L"특과 클래스 《VII반》", L"特科班《VII組》", L"الفئة السابعة", L"Класс VII", L"Klasse VII", L"Classe VII", L"Klas VII", L"Klasa VII", L"Sınıf VII");
								break;
							case 8002:
								a = LL14(L"スタートライン", L"Start Line", L"Ligne de départ", L"Linea di partenza", L"Linea de salida", L"스타트 라인", L"起跑線", L"خط البداية", L"Стартовая линия", L"Startlinie", L"Linha de partida", L"Startlijn", L"Linia startu", L"Başlangıç Çizgisi");
								break;
							case 8006:
								a = LL14(L"ただひたすらに、前へ", L"Ever Forward", L"Toujours vers l'avant", L"Sempre avanti", L"Siempre adelante", L"오직 한결같이 앞으로", L"一心一意，向前邁進", L"إلى الأمام دائماً", L"Только вперед", L"Immer vorwärts", L"Sempre em frente", L"Altijd vooruit", L"Zawsze do przodu", L"Daima İleri");
								break;
							case 8007:
								a = LL14(L"縁 -つなぐもの-", L"Fate -Connecting-", L"Destin -Connexion-", L"Destino -Connessione-", L"Destino -Conexión-", L"인연 ~이어주는 것~", L"緣 -連結者-", L"الروابط -ما يجمعنا-", L"Судьба -Связующее звено-", L"Schicksal -Verbindend-", L"Destino -Conectando-", L"Lot -Verbindend-", L"Los -Łączący-", L"Kader -Bağlayıcı-");
								break;
							case 8150:
								a = LL14(L"下校途中にパンケーキ", L"Pancakes on the Way Home", L"Des pancakes sur le chemin du retour", L"Pancake sulla via di casa", L"Tortitas de camino a casa", L"하교 길에 팬케이크", L"下學路上的煎餅", L"بانكيك في طريق العودة", L"Блинчики по дороге домой", L"Pfannkuchen auf dem Heimweg", L"Panquecas no caminho para casa", L"Pannenkoeken op weg naar huis", L"Naleśniki w drodze do domu", L"Eve Giderken Krep");
								break;
							case 8151:
								a = LL14(L"可能性は無限大", L"Infinite Possibilities", L"Possibilités infinies", L"Possibilità infinite", L"Posibilidades infinitas", L"가능성은 무한대", L"可能性是無限的", L"احتمالات لا حصر لها", L"Бесконечные возможности", L"Unbegrenzte Möglichkeiten", L"Possibilidades infinitas", L"Oneindige mogelijkheden", L"Nieskończone możliwości", L"Sonsuz Olasılıklar");
								break;
							case 8152:
								a = LL14(L"夜のしじまに", L"In the Night Silence", L"Dans le silence nocturne", L"Nel silenzio della notte", L"En el silencio de la noche", L"밤의 정적 속에", L"在深夜的靜謐中", L"في صمت الليل", L"В ночной тишине", L"In der nächtlichen Stille", L"No silêncio da noite", L"In de nachtelijke stilte", L"W nocnej ciszy", L"Gece Sessizliğinde");
								break;
							case 8153:
								a = LL14(L"夕景", L"Evening Scene", L"Scène de soirée", L"Scena serale", L"Escena vespertina", L"석양 풍경", L"夕陽美景", L"مشهد المساء", L"Вечерний пейзаж", L"Abendszene", L"Cena noturna", L"Avondtafereel", L"Wieczorna scena", L"Akşam Manzarası");
								break;
							case 8154:
								a = LL14(L"新しい朝", L"New Morning", L"Nouveau matin", L"Nuovo mattino", L"Nueva mañana", L"새로운 아침", L"新的早晨", L"صباح جديد", L"Новое утро", L"Neuer Morgen", L"Nova manhã", L"Nieuwe ochtend", L"Nowy poranek", L"Yeni Sabah");
								break;
							case 8156:
								a = LL14(L"白亜の旧都セントアーク", L"White City St. Ark", L"Vieille capitale blanche St. Ark", L"Antica capitale bianca St. Ark", L"Vieja capital blanca St. Ark", L"백아의 구도 세인트아크", L"白亞舊都 St. Ark", L"مدينة سانت آرك البيضاء", L"Белая старая столица Сент-Арк", L"Weiße alte Hauptstadt St. Ark", L"Antiga capital branca St. Ark", L"Witte oude hoofdstad St. Ark", L"Biała stara stolica St. Ark", L"Beyaz Eski Başkent St. Ark");
								break;
							case 8157:
								a = LL14(L"紡績町パルム", L"Spinning Town Parm", L"Ville textile Parm", L"Città tessile Parm", L"Pueblo textil Parm", L"방직 마을 파름", L"紡織鎮 Parm", L"بلدة بارم للغزل", L"Ткацкий городок Парм", L"Spinnereistadt Parm", L"Vila têxtil Parm", L"Spinnerijstad Parm", L"Tkackie miasto Parm", L"Dokuma Kasabası Parm");
								break;
							case 8158:
								a = LL14(L"籠の中のクロスベル", L"Crossbell in a Cage", L"Crossbell en cage", L"Crossbell in gabbia", L"Crossbell en una jaula", L"장 안의 크로스벨", L"籠中 Crossbell", L"كروسبيل في قفص", L"Кроссбелл в клетке", L"Crossbell im Käfig", L"Crossbell em uma gaiola", L"Crossbell in een kooi", L"Crossbell w klatce", L"Kafesteki Crossbell");
								break;
							case 8159:
								a = LL14(L"今、成すべきこと", L"What Must Be Done Now", L"Ce qui doit être fait maintenant", L"Ciò che deve essere fatto ora", L"Lo que debe hacerse ahora", L"지금, 해야 할 일", L"現在，應做之事", L"ما يجب القيام به الآن", L"Что должно быть сделано сейчас", L"Was jetzt getan werden muss", L"O que deve ser feito agora", L"Wat nu moet worden gedaan", L"Co należy teraz zrobić", L"Şimdi Yapılması Gereken");
								break;
							case 8160:
								a = LL14(L"歓楽都市ラクウェル", L"Pleasure City Raquel", L"Ville de plaisir Raquel", L"Città del piacere Raquel", L"Ciudad del placer Raquel", L"환락 도시 라크웰", L"歡樂都市 Raquel", L"مدينة راكيل للترفيه", L"Город развлечений Ракель", L"Vergnügungsstadt Raquel", L"Cidade do prazer Raquel", L"Plezierstad Raquel", L"Miasto rozrywki Raquel", L"Eğlence Şehri Raquel");
								break;
							case 8161:
								a = LL14(L"静かなる駆け引き", L"Quiet Maneuvering", L"Manœuvres silencieuses", L"Manovre silenziose", L"Maniobras silenciosas", L"조용한 밀고 당기기", L"靜默的周旋", L"مناورة هادئة", L"Тихое маневрирование", L"Stilles Manövrieren", L"Manobras silenciosas", L"Stil manoeuvreren", L"Ciche manewry", L"Sessiz Manevralar");
								break;
							case 8162:
								a = LL14(L"赫奕たるヘイムダル", L"Splendid Heimdallr", L"Heimdallr splendide", L"Splendida Heimdallr", L"Espléndida Heimdallr", L"혁혁한 헤임달", L"赫赫有名的 Heimdallr", L"هايمدال العظيمة", L"Великолепный Хеймдалль", L"Prächtiges Heimdallr", L"Esplêndida Heimdallr", L"Prachtig Heimdallr", L"Wspaniały Heimdallr", L"Görkemli Heimdallr");
								break;
							case 8163:
								a = LL14(L"紺碧の海都オルディス", L"Azure Port City Ordys", L"Ville portuaire d'azur Ordys", L"Città portuale azzurra Ordys", L"Ciudad portuaria azul Ordys", L"금벽의 해도 오르디스", L"紺碧海都 Ordys", L"مدينة أورديس الساحلية الفيروزية", L"Лазурный портовый город Ордис", L"Azurblaue Hafenstadt Ordys", L"Cidade portuaria azul Ordys", L"Azuurblauwe havenstad Ordys", L"Błękitne miasto portowe Ordys", L"Gök Mavisi Liman Şehri Ordys");
								break;
							case 8164:
								a = LL14(L"最前線都市", L"Front-line City", L"Ville de première ligne", L"Città di prima linea", L"Ciudad de primera línea", L"최전선 도시", L"最前線都市", L"مدينة الخطوط الأمامية", L"Прифронтовой город", L"Frontstadt", L"Cidade de linha de frente", L"Frontstad", L"Miasto na linii frontu", L"Cephe Şehri");
								break;
							case 8166:
								a = LL14(L"精強なる兵たち", L"Elite Soldiers", L"Soldats d'élite", L"Soldati d'élite", L"Soldados de elite", L"정예병들", L"精強的士兵們", L"جنود النخبة", L"Элитные солдаты", L"Elitesoldaten", L"Soldados de elite", L"Elitesoldaten", L"Elitarni żołnierze", L"Seçkin Askerler");
								break;
							case 8170:
								a = LL14(L"隠れ里エリン", L"Hidden Village Erin", L"Village caché d'Erin", L"Villaggio nascosto di Erin", L"Aldea oculta de Erin", L"은둔 마을 에린", L"隠之里 Erin", L"قرية إيرين المخفية", L"Скрытая деревня Эрин", L"Verborgenes Dorf Erin", L"Vila oculta de Erin", L"Verborgen dorp Erin", L"Ukryta wioska Erin", L"Gizli Köy Erin");
								break;
							case 8171:
								a = LL14(L"潜入調査", L"Infiltration", L"Infiltration", L"Infiltrazione", L"Infiltración", L"잠입 조사", L"潛入調查", L"استطلاع تسللي", L"Инфильтрация", L"Infiltration", L"Infiltração", L"Infiltratie", L"Infiltracja", L"Sızma Harekatı");
								break;
							case 8173:
								a = LL14(L"紅き閃影 -光まとう翼-", L"Crimson Flash -Wings of Light-", L"Éclat carmin -Ailes de lumière-", L"Lampo cremisi -Ali di luce-", L"Destello carmesí -Alas de luz-", L"붉은 섬영 ~빛을 두른 날개~", L"紅之閃影 -披光之翼-", L"الوميض القرمزي -أجنحة الضوء-", L"Алая вспышка -Крылья света-", L"Purpurroter Blitz -Flügel des Lichts-", L"Lampejo carmesim -Asas de luz-", L"Karmozijnrode flits -Vleugels van licht-", L"Szkarłatny błysk -Skrzydła światła-", L"Kızıl Parıltı -Işık Kanatları-");
								break;
							case 8175:
								a = LL14(L"一抹の不安、一縷の望み", L"Hint of Unease, Ray of Hope", L"Une pointe d'inquiétude, un rayon d'espoir", L"Un briciolo di ansia, un raggio di speranza", L"Un rastro de inquietud, un rayo de esperanza", L"일말의 불안, 한 줄기 희망", L"一抹不安，一縷希望", L"لمسة قلق، شعاع أمل", L"Тень беспокойства, луч надежды", L"Ein Hauch von Unbehagen, ein Hoffnungsschimmer", L"Um toque de inquietação, um raio de esperança", L"Een spoortje van onrust, een straal van hoop", L"Cień niepokoju, promień nadziei", L"Bir Parça Huzursuzluk, Bir Umut Işığı");
								break;
							case 8177:
								a = LL14(L"水面を渡る風", L"Wind Over the Water", L"Vent sur l'eau", L"Vento sull'acqua", L"Viento sobre el agua", L"수면을 건너는 바람", L"拂過水面的風", L"الريح فوق الماء", L"Ветер над водой", L"Wind über dem Wasser", L"Vento sobre a água", L"Wind over het water", L"Wiatr nad wodą", L"Su Üstündeki Rüzgar");
								break;
							case 8452:
								a = LL14(L"剣戟怒涛", L"Sword and Lance Storm", L"Tempête d'épées et de lances", L"Tempesta di spade e lance", L"Tormenta de espadas y lanzas", L"검격노도", L"劍戟怒濤", L"عاصفة السيوف والرماح", L"Шторм мечей и копий", L"Schwert- und Lanzensturm", L"Tempestade de espadas e lanças", L"Zwaard- en lansstorm", L"Burza mieczy i włóczni", L"Kılıç ve Mızrak Fırtınası");
								break;
							case 8475:
								a = LL14(L"古の盟約", L"Ancient Covenant", L"Ancienne alliance", L"Antico patto", L"Antiguo pacto", L"고대의 맹약", L"古代盟約", L"العهد القديم", L"Древний завет", L"Alter Bund", L"Antigo pacto", L"Oud verbond", L"Starożytne przymierze", L"Kadim Sözleşme");
								break;
							case 8714:
								a = LL14(L"巨竜目覚める", L"The Great Dragon Awakens", L"Le grand dragon s'éveille", L"Il grande drago si risveglia", L"El gran dragón despierta", L"거룡 깨어나다", L"巨龍覺醒", L"استيقاظ التنين العظيم", L"Великий дракон пробуждается", L"Der große Drache erwacht", L"O grande dragão desperta", L"De grote draak ontwaakt", L"Wielki smok się budzi", L"Büyük Ejderha Uyanıyor");
								break;
							case 8715:
								a = LL14(L"未来へ。", L"To the Future.", L"Vers le futur.", L"Verso il futuro.", L"Hacia el futuro.", L"미래로.", L"往未來。", L"إلى المستقبل.", L"В будущее.", L"In die Zukunft.", L"Para o futuro.", L"Naar de toekomst.", L"W przyszłość.", L"Geleceğe.");
								break;
							case 8720:
								a = LL14(L"明日への軌跡", L"Trails to Tomorrow", L"Sillage vers demain", L"Tracce verso il domani", L"Estela hacia el mañana", L"내일로의 궤적", L"通向明天的軌跡", L"مسارات الغد", L"Пути в завтрашний день", L"Pfade nach morgen", L"Rastros para o amanhã", L"Sporen naar morgen", L"Ścieżki do jutra", L"Yarına Giden İzler");
								break;
							case 8721:
								a = LL14(L"愛の詩(歌)", L"Poem of Love (vocal)", L"Poème d'amour (vocal)", L"Poema d'amore (vocal)", L"Poema de amor (vocal)", L"사랑의 시 (노래)", L"愛之詩(歌)", L"قصيدة الحب", L"Поэма о любви (вокал)", L"Liebesgedicht (Gesang)", L"Poema de amor (vocal)", L"Liefdesgedicht (vocaal)", L"Poemat miłości (wokal)", L"Aşk Şiiri (vokal)");
								break;
							case 8802:
								a = LL14(L"風よりも駿く", L"Swifter Than the Wind", L"Plus rapide que le vent", L"Più veloce del vento", L"Más rápido que el viento", L"바람보다 빠르게", L"比風更迅捷", L"أسرع من الريح", L"Быстрее ветра", L"Schneller als der Wind", L"Mais rápido que o vento", L"Sneller dan de wind", L"Szybszy niż wiatr", L"Rüzgardan Daha Hızlı");
								break;
							default:
								if (a == L"ed8_inf_ex.opus") {
									a = LL14(L"夢幻の彼方へ", L"To the Realm of Dreams", L"Vers le royaume des rêves", L"Verso il regno dei sogni", L"Hacia el reino de los sueños", L"몽환의 저편으로", L"往夢幻的彼方", L"إلى مملكة الأحلام", L"В царство снов", L"In das Reich der Träume", L"Para o reino dos sonhos", L"Naar het rijk der dromen", L"Do krainy snów", L"Rüyalar Alemine");
								}
								else if (a.Find(L"muon") != -1 || a.Find(L"不明") != -1 || a.Find(L"Unknown") != -1) {
									a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								}
								break;
							}
						}

					//a = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					if (a.Left(3) == L"ed7") {
						int b = _ttoi(a.Mid(3, 3));
						CString fil = fname.Left(fname.ReverseFind(L'\\')) + L"\\..\\..\\data\\bgm\\info.yaml";
						FILE* fp;
						errno_t ferr;
						ferr = _tfopen_s(&fp, fil, _T("r, ccs=UTF-8"));
						if (ferr == 0) {
							CStdioFile fzero(fp);
							fzero.SeekToBegin();
							CString stf, stl, stn;
							BOOL ck = FALSE;
							for (;;) {
								if (fzero.ReadString(stf) == FALSE) break;
								stl.Format(L"'%d'", b);
								if (stf.Find(stl) != -1) {
									ck = TRUE;
								}
								if (stf.Find(L"jp:") != -1 && ck == TRUE) {
									int k = stf.Find(L"jp:") + 4;
									stn = stf.Mid(k);
									break;
								}
							}
							if (stn != L"") {
								a = stn;
							}
							fzero.Close();
							fclose(fp);
						}
					}
					_tcscpy(p.name, a);
					_tcscpy(p.fol, fname1);
				}
				else if (ft.Right(4) == ".mp3" || ft.Right(4) == ".MP3" || ft.Right(4) == ".mp2" || ft.Right(4) == ".MP2" ||
					ft.Right(4) == ".mp1" || ft.Right(4) == ".MP1" || ft.Right(4) == ".rmp" || ft.Right(4) == ".RMP") {
					p.sub = -10; p.loop1 = p.loop2 = 0;
					ft = ft2;
					_tcscpy(p.fol, fname1);
					CId3tagv1 ta1p;
					CId3tagv2 ta2p;
					int b = ta2p.Load(fname);
					ss = ta2p.GetArtist(); if (b == -1) { ta1p.Load(fname); ss = ta1p.GetArtist(); } _tcscpy(p.art, ss);
					ss = ta2p.GetTitle(); if (b == -1) ss = ta1p.GetTitle(); if (ss == "")ss = ft; _tcscpy(p.name, ss);
					ss = ta2p.GetAlbum(); if (b == -1) ss = ta1p.GetAlbum(); _tcscpy(p.alb, ss);
				}
				else if (has_aac_syncword) {
					p.sub = -9;
					ft = ft2;
					_tcscpy(p.name, ft2);
					_tcscpy(p.fol, fname1);
				}
				else if ((ft.Right(4) == ".dsf" || ft.Right(4) == ".DSF" || ft.Right(4) == ".dff" || ft.Right(4) == ".DFF" || ft.Right(4) == ".wsd" || ft.Right(4) == ".WSD")) {
					CString tagfile, tagname, tagalbum;
					ULONGLONG po;
					ft = ft2;
					og->dsdload(fname,tagfile, tagname, tagalbum,po, 1);
					og->dsdclose();
					_tcscpy(p.name, tagfile);
					_tcscpy(p.alb, tagalbum);
					_tcscpy(p.art, tagname);
					_tcscpy(p.fol, fname1);
					p.sub = -7; p.loop1 = p.loop2 = 0;
				}
				else if ((ft.Right(4) == ".m4a" || ft.Right(4) == ".M4A" || ft.Right(4) == ".aac" || ft.Right(4) == ".AAC")) {
					ft = ft2;
					CFile ff;
					char buf[1024];
					TCHAR kpi[512];
					ff.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL);
					int flg, read = ff.Read(bufimage, sizeof(bufimage));
					ff.Close();
					kpi[0] = 0;
					plugs(s, &p, kpi, kvver);
					if (kpi[0] == 0)
						p.sub = -3;
					else
						p.sub = -2;
					if (savedata.m4a == 1)
						p.sub = -9;
					_tcscpy(p.name, ft);
					_tcscpy(p.fol, fname1);
					flg = 0;
					int i;
					for (i = 0; i < read - 4; i++) {
						if (bufimage[i] == 'u' && bufimage[i + 1] == 'd' && bufimage[i + 2] == 't' && bufimage[i + 3] == 'a') {
							int j;
							for (j = i + 4; j < read - 4; j++) {
								if (bufimage[j] == 'a' && bufimage[j + 1] == 'l' && bufimage[j + 2] == 'b' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
									j += 19;
									for (int k = j; k < read - 4; k++) {
										if (bufimage[k] == 0) {
											flg = 1;
											buf[k - j] = 0;
											buf[k - j + 1] = 0;
											buf[k - j + 2] = 0;
											break;
										}
										buf[k - j] = bufimage[k];
									}
								}
								if (flg == 1) {
									const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
									TCHAR* buff = new TCHAR[wlen + 1];
									if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
									{
										buff[wlen] = _T('\0');
									}
									wcscpy(p.alb, buff);
									delete[] buff;
									flg = 0;
									break;
								}
							}
							for (j = i + 4; j < read - 4; j++) {
								if (bufimage[j] == 'A' && bufimage[j + 1] == 'R' && bufimage[j + 2] == 'T' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
									j += 19;
									for (int k = j; k < read - 4; k++) {
										if (bufimage[k] == 0) {
											flg = 1;
											buf[k - j] = 0;
											buf[k - j + 1] = 0;
											buf[k - j + 2] = 0;
											break;
										}
										buf[k - j] = bufimage[k];
									}
								}
								if (flg == 1) {
									const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
									TCHAR* buff = new TCHAR[wlen + 1];
									if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
									{
										buff[wlen] = _T('\0');
									}
									wcscpy(p.art, buff);
									delete[] buff;
									flg = 0;
									break;
								}
							}
							for (j = i + 4; j < read - 4; j++) {
								if (bufimage[j] == 'n' && bufimage[j + 1] == 'a' && bufimage[j + 2] == 'm' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
									j += 19;
									for (int k = j; k < read - 4; k++) {
										if (bufimage[k] == 0) {
											flg = 1;
											buf[k - j] = 0;
											buf[k - j + 1] = 0;
											buf[k - j + 2] = 0;
											break;
									}
										buf[k - j] = bufimage[k];
									}
								}
								if (flg == 1) {
									const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
									TCHAR* buff = new TCHAR[wlen + 1];
									if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
									{
										buff[wlen] = _T('\0');
								}
									wcscpy(p.name, buff);
									delete[] buff;
									flg = 0;
									break;
							}
						}
					}
				}
						}
				else if ((ft.Right(5).MakeLower() == ".flac" || ft.Right(5) == ".FLAC" || ft.Right(7).MakeLower() == L".qull3h")) {
					ft = ft2;
					CFile ff;
					char buf[2024];
					TCHAR kpi[512];
					ff.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL);
					int flg, read = ff.Read(bufimage, sizeof(bufimage));
					ff.Close();
						if (bufimage[0] == 0xBF) {
							BYTE offenc[7] = { 0xd9,0x3F,0x86,0x7B,0xC7,0x61,0xaa };
							int off = 0;
							for (int ll = 0; ll < sizeof(bufimage); ll++) {
								bufimage[ll] ^= offenc[off];
								off++; off %= 7;
							}
						}
					kpi[0] = 0;
					plugs(s, &p, kpi, kvver);
					if (kpi[0] == 0)
						p.sub = -3;
					else
						p.sub = -2;
					//			if (savedata.m4a == 1)
					p.sub = -8;
					_tcscpy(p.name, ft);
					_tcscpy(p.fol, fname1);
					flg = 0;
					int i = 0, j;
					for (j = i; j < read - 6; j++) {
						if (bufimage[j] == 'A' && bufimage[j + 1] == 'L' && bufimage[j + 2] == 'B' && bufimage[j + 3] == 'U' && bufimage[j + 4] == 'M' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = 0;
							}
							wcscpy(p.alb, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 6; j++) {
						if ((bufimage[j] == 'A' || bufimage[j] == 'a') && bufimage[j + 1] == 'l' && bufimage[j + 2] == 'b' && bufimage[j + 3] == 'u' && bufimage[j + 4] == 'm' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = 0;
							}
							wcscpy(p.alb, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 6; j++) {
						if (bufimage[j] == 'A' && bufimage[j + 1] == 'R' && bufimage[j + 2] == 'T' && bufimage[j + 3] == 'I' && bufimage[j + 4] == 'S' && bufimage[j + 5] == 'T' && bufimage[j + 6] == '=') {
							j += 7;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.art, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 6; j++) {
						if ((bufimage[j] == 'A' || bufimage[j] == 'a') && bufimage[j + 1] == 'r' && bufimage[j + 2] == 't' && bufimage[j + 3] == 'i' && bufimage[j + 4] == 's' && bufimage[j + 5] == 't' && bufimage[j + 6] == '=') {
							j += 7;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
							}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.art, buff);
							delete[] buff;
							flg = 0;
							break;
							}
						}
					for (j = i; j < read - 4; j++) {
						if (bufimage[j] == 'T' && bufimage[j + 1] == 'I' && bufimage[j + 2] == 'T' && bufimage[j + 3] == 'L' && bufimage[j + 4] == 'E' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.name, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 4; j++) {
						if ((bufimage[j] == 'T' || bufimage[j] == 't') && bufimage[j + 1] == 'i' && bufimage[j + 2] == 't' && bufimage[j + 3] == 'l' && bufimage[j + 4] == 'e' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.name, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
				}
				else if (ft.Right(4).MakeLower() == ".wav" || ft.Right(4) == ".WAV") {
					CFile ff;
					if (ff.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL)) {
						int read = ff.Read(bufimage, 44);
						ff.Close();
						if (read >= 12 && bufimage[0] == 'R' && bufimage[1] == 'I' && bufimage[2] == 'F' && bufimage[3] == 'F' && bufimage[8] == 'W' && bufimage[9] == 'A' && bufimage[10] == 'V' && bufimage[11] == 'E') {
							p.sub = 999;
							ft = ft2;
							_tcscpy(p.name, ft2);
							_tcscpy(p.fol, fname);
							p.alb[0] = p.art[0] = NULL;
							p.loop1 = p.loop2 = 0;
							WavReadRiffListInfoTags(fname, p.name, p.art, p.alb);
							int totalRead = 0;
							{
								CFile ff2;
								if (ff2.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL)) {
									totalRead = ff2.Read(bufimage, sizeof(bufimage));
									ff2.Close();
								}
							}
							int wavTime = 0;
							if (totalRead >= 44) {
								DWORD sr = *(DWORD*)&bufimage[24];
								WORD ch = *(WORD*)&bufimage[22];
								WORD bps = *(WORD*)&bufimage[34];
								if (sr > 0 && ch > 0 && bps > 0) {
									for (int j = 12; j + 8 < totalRead; ) {
										DWORD ckid = *(DWORD*)&bufimage[j];
										DWORD cksize = *(DWORD*)&bufimage[j + 4];
										if (ckid == 0x61746164) {
											if (cksize > 0 && sr > 0) {
												DWORD bytesPerSample = (bps + 7) / 8 * ch;
												if (bytesPerSample > 0)
													wavTime = (int)(cksize / bytesPerSample / sr);
											}
											break;
										}
										j += 8 + (int)((cksize + 1) & ~1);
										if (j + 8 >= totalRead) break;
									}
								}
							}
							
						}
						else {
							p.sub = -2;
							_tcscpy(p.name, s);
							_tcscpy(p.fol, fname1);
							p.alb[0] = p.art[0] = NULL; p.loop1 = p.loop2 = 0;
							TCHAR kpi[512]; kpi[0] = 0;
							plugs(fname, &p, kpi, kvver);
						}
					}
					else {
						p.sub = -2;
						_tcscpy(p.name, s);
						_tcscpy(p.fol, fname1);
						p.alb[0] = p.art[0] = NULL; p.loop1 = p.loop2 = 0;
						TCHAR kpi[512]; kpi[0] = 0;
						plugs(fname, &p, kpi, kvver);
					}
				}
				else {
					p.sub = -2;
					_tcscpy(p.name, s);
					_tcscpy(p.fol, fname1);
					p.alb[0] = p.art[0] = NULL; p.loop1 = p.loop2 = 0;
					TCHAR kpi[512]; kpi[0] = 0;
					plugs(fname, &p, kpi, kvver);
					if (kpi[0]) {
						ft = fname.Left(fname.ReverseFind('.')); ft += ".m3u";
						char ftt[1024];
						WideCharToMultiByte(CP_ACP, 0, ft, -1, ftt, 2000, " ", FALSE);
						ss = fname.Right(4); ss.MakeLower();
						if (ss == ".kss") {
							FILE *f; if (f = fopen(ftt, "r")) {
								char buf[256];  int st, ed, tmp;
								for (;;) {
									if (fgets(buf, sizeof(buf), f) == NULL) break;
									//							if (f.Read(buf1, 250) == FALSE) break;
									if (buf[0] == '#' || buf[0] == '\r' || buf[0] == '\n') continue;
									ss = buf;
									st = ss.Find(',', 0); ed = ss.Find(',', st + 1); s = ss.Mid(st + 1, (ed - 1) - st);
									if (s.Left(1) == _T("$")) {
										int num = 0;
										CString s3 = s.Mid(1, 1);
										if (_T("0") <= s3 && _T("9") >= s3) num = s3.GetAt(0) - _T('0');
										if (_T("a") <= s3 && _T("f") >= s3) num = s3.GetAt(0) - _T('a') + 10;
										if (_T("A") <= s3 && _T("F") >= s3) num = s3.GetAt(0) - _T('A') + 10;
										s3 = s.Mid(2, 1); num *= 16;
										if (_T("0") <= s3 && _T("9") >= s3) num += s3.GetAt(0) - _T('0');
										if (_T("a") <= s3 && _T("f") >= s3) num += s3.GetAt(0) - _T('a') + 10;
										if (_T("A") <= s3 && _T("F") >= s3) num += s3.GetAt(0) - _T('A') + 10;
										ft.Format(_T("%s::%04d"), fname, num + 1);
									}
									else
										ft.Format(_T("%s::%04d"), fname, _tstoi(s) + 1);
									_tcscpy(p.fol, ft);
									//TCHAR ss1[2001];
									//MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
									//ss = ss1;
									st = ss.Find(L',', ed); ed = ss.Find(L',', st + 1); s = ss.Mid(st + 1, (ed - 1) - st);
									_tcscpy(p.name, s);
									if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
									Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
								}
								fclose(f);
								return;
							}
						}
						ft = fname.Left(fname.ReverseFind('.')); ft += ".frm";
						if (ss == ".nsf") {
							CStdioFile f; if (f.Open(ft, CFile::modeRead | CFile::typeText | CFile::shareDenyWrite, NULL)) {
								TCHAR buf[256]; int st, ed, tmp;
								f.ReadString(buf, 256);
								f.ReadString(buf, 256);
								_tcscpy(p.alb, buf);
								f.ReadString(buf, 256);
								s = buf; int j = s.Find(_T("songs")); if (j >= 0) {
									int k = s.Find(_T("S.E."));
									int l = s.ReverseFind('('); ss = s.Mid(l + 1, 3); j = _tstoi(ss);
									if (k >= 0) { l = s.ReverseFind('&'); ss = s.Mid(l + 1, 3); j += _tstoi(ss); }
									for (l = 0; l < j; l++) {
										s = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
										ss.Format(_T("%s::%04d"), s, l + 1);
										_tcscpy(p.name, ss);
										ss.Format(_T("%s::%04d"), fname, l + 1);
										_tcscpy(p.fol, ss);
										if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
										Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
									}
								}
								f.Close();
								return;
							}
						}
						if (ss == ".gbs") {
							CFile f; if (f.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL)) {
								char buf[32];
								f.Read(buf, 16); int i = buf[4];
								f.Read(buf, 32);
#if UNICODE
								TCHAR ss1[512];
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
								_tcscpy(p.name, ss1);
#else
								_tcscpy(p.name, buf);
#endif
								f.Read(buf, 32);
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
								_tcscpy(p.alb, ss1);
#else
								_tcscpy(p.alb, buf);
#endif
								f.Read(buf, 32);
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
								_tcscpy(p.art, ss1);
#else
								_tcscpy(p.art, buf);
#endif
								f.Close();
								for (int j = 0; j < i; j++) {
									ss.Format(_T("%s::%04d"), fname, j + 1); _tcscpy(p.fol, ss);
									if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
									Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
								}
								return;
							}
						}
						if (ss == ".hes" || ss == ".nes") {
							ft = fname1.Right(fname1.GetLength() - fname1.ReverseFind(L'\\') - 1);
							_tcscpy(p.name, ft);
							_tcscpy(p.fol, fname1);
							_tchdir(fname);
							CString ftt0 = ft;
							p.alb[0] = p.art[0] = NULL; p.loop1 = p.loop2 = 0;
							FILE *f; if (f = fopen(ftt, "r")) {
								char buf[256];  int st, ed;
								for (;;) {
									if (fgets(buf, sizeof(buf), f) == NULL) break;
									if (buf[0] == _T('#') || buf[0] == _T('\r') || buf[0] == _T('\n')) continue;
									ss = buf;
									int z = 0;
									ss.Replace(L"\n", L"");
									ss.Replace(L"\r", L"");

									if ((z = ss.Find(',', 0)) != -1) {
										s = ss.Mid(z + 1);
										if (s.Left(1) == _T("$")) {
											int num = 0;
											CString s3 = s.Mid(1, 1);
											if (_T("0") <= s3 && _T("9") >= s3) num = s3.GetAt(0) - _T('0');
											if (_T("a") <= s3 && _T("f") >= s3) num = s3.GetAt(0) - _T('a') + 10;
											if (_T("A") <= s3 && _T("F") >= s3) num = s3.GetAt(0) - _T('A') + 10;
											s3 = s.Mid(2, 1); num *= 16;
											if (_T("0") <= s3 && _T("9") >= s3) num += s3.GetAt(0) - _T('0');
											if (_T("a") <= s3 && _T("f") >= s3) num += s3.GetAt(0) - _T('a') + 10;
											if (_T("A") <= s3 && _T("F") >= s3) num += s3.GetAt(0) - _T('A') + 10;
											ftt0.Format(_T("%s::%04d"), fname, num + 1);
										}
										else
											ftt0.Format(_T("%s::%04d"), fname, _tstoi(s) + 1);
										_tcscpy(p.fol, ftt0);
										st = ss.Find(L',', z + 1); ed = ss.Find(L',', st + 1); s = ss.Mid(st + 1, (ed - 1) - st);
										_tcscpy(p.name, s);
										p.sub = -3;
										if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
										Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
									}
									else {
										s = fname1.Left(fname1.ReverseFind(L'\\')+1);
										ftt0.Format(_T("%s%s"), s,ss);
										_tcscpy(p.fol, ftt0);
										ftt0.Format(_T("%s"), ss);
										_tcscpy(p.name, ftt0);
										p.sub = -10;
										if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
										Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
									}
								}
								fclose(f);
							}
							else {
								ft = ftt0;
								for (int i = 1; i < 255; i++) {
									ftt0.Format(_T("%s::%04d"), fname, i + 1);
									_tcscpy(p.fol, ftt0);
									ftt0.Format(_T("%s::%04d"), ft, i + 1);
									_tcscpy(p.name, ftt0);
									if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
									Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
								}
							}
							return;
						}
						if (ss == ".ovi" || ss == ".opi" || ss == ".ozi") {
							CFile f; char buf[512], *buf2;
							f.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL);
							if (f.GetLength() > 512)
								f.Seek(-512, CFile::end);
							else
								f.SeekToBegin();
							f.Read(buf, 512);
							int i = 0;
							f.Close();
							for (; i < 500; i++) {
								if (buf[i] == 'F'&&buf[i + 1] == 'M'&&buf[i + 2] == 'C') break;
							}
							if (i != 500) {
								buf2 = buf + i + 4; ss = buf2;
								int st = ss.Find(0x0d, 0);
								ft = ss.Left(st); _tcscpy(p.name, ft);
								int ed = ss.Find(0x0d, st + 2);
								ft = ss.Mid(st + 1, ed - st - 1); _tcscpy(p.art, ft);
								st = ss.Find(0x0d, ed + 2);
								ft = ss.Mid(ed + 1, st - ed - 1); _tcscpy(p.alb, ft);
								if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub; fnn = p.name; }
								Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
								return;
							}
						}
						ss = fname.Right(2); ss.MakeLower();
						ft = fname.Right(3); ft.MakeLower();
						if (ss == ".m" || ft == ".mz") {
							CFile ff; char buf[512], *buf2;
							ff.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL);
							if (ff.GetLength() > 512)
								ff.Seek(-512, CFile::end);
							else
								ff.SeekToBegin();
							ff.Read(buf, 512);
							int jj = ff.GetLength(); if (jj > 510) jj = 510;
							jj -= 3;
							int i;
							for (i = jj; i > 0; i--) {
								if (buf[i] == 0 && (buf[i + 1] == 0 || (BYTE)buf[i + 1] == 255) && buf[i + 2] == 0)break;
							}
							ff.Close();
							if (i != 0) {
								buf2 = buf + i + 3;
								int j = 0;
								for (;; j++)if (buf2[j] == 0)break;
#if UNICODE
								TCHAR ss1[512];
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								_tcscpy(p.name, ss1); buf2 += j;
#else
								_tcscpy(p.name, buf2); buf2 += j;
#endif
								for (j = 0;; j++)if (buf2[j] != 0)break;
								buf2 += j;
								for (j = 0;; j++)if (buf2[j] == 0)break;
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								_tcscpy(p.art, ss1); buf2 += j;
#else
								_tcscpy(p.art, buf2); buf2 += j;
#endif
								for (j = 0;; j++)if (buf2[j] != 0)break;
								buf2 += j;
								for (j = 0;; j++)if (buf2[j] == 0)break;
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								_tcscpy(p.alb, ss1); buf2 += j;
#else
								_tcscpy(p.alb, buf2); buf2 += j;
#endif
								if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub; fnn = p.name; }
								Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
							}
							return;
						}
						ft = fname.Right(4); ft.MakeLower();
						if (ft == ".tta") {
							CFile ff; char buf[512], *buf2;
							ff.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL);
							if (ff.GetLength() > 0x80)
								ff.Seek(-0x80, CFile::end);
							else
								ff.SeekToBegin();
							ff.Read(buf, 0x80);
							int i = 0;
							for (; i < 0x80; i++) {
								if (buf[i + 0] == 'T'&&buf[i + 1] == 'A'&&buf[i + 2] == 'G')break;
							}
							ff.Close();
							if (i != 0x80) {
								buf2 = buf + i + 3;
#if UNICODE
								TCHAR ss1[512];
								TCHAR buf3 = buf2[30]; buf2[30] = 0;
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								buf2[30] = buf3;
								_tcscpy(p.name, ss1); buf2 += 30;
#else
								_tcscpy(p.name, buf2); buf2 += 30;
#endif
#if UNICODE
								buf3 = buf2[30]; buf2[30] = 0;
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								buf2[30] = buf3;
								_tcscpy(p.art, ss1); buf2 += 30;
#else
								_tcscpy(p.art, buf2); buf2 += 30;
#endif
#if UNICODE
								buf3 = buf2[30]; buf2[30] = 0;
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								buf2[30] = buf3;
								_tcscpy(p.alb, ss1); buf2 += 30;
#else
								_tcscpy(p.alb, buf2); buf2 += 30;
#endif
								if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub; fnn = s; }
								Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
							}
							return;
						}
					}

				}
				CString sL = s;
				s.MakeLower();
				if (s.Right(4) == ".png" || s.Right(4) == ".url" || s.Right(4) == ".jpg" || s.Right(4) == ".bmp" || s.Right(4) == ".cue" || s.Right(4) == ".iso" || s.Right(4) == ".bin" || s.Right(4) == ".img" || s.Right(4) == ".mds" || s.Right(4) == ".mdf" || s.Right(4) == ".ccd" || s.Right(4) == ".sub" || s.Right(4) == ".pdf" || s.Right(4) == ".com" || s.Right(4) == ".exe" || s.Right(4) == ".dll" || s.Right(4) == ".bat" || s.Right(4) == ".reg" || s.Right(4) == ".msi" || s.Right(4) == ".nfo" || s.Right(4) == ".diz" || s.Right(4) == ".gif" || s.Right(4) == ".ico" ||
					s.Right(4) == ".lrc" || s.Right(4) == ".zip" || s.Right(4) == ".lzh" || s.Right(4) == ".cab" || s.Right(4) == ".rar" || s.Right(4) == ".txt" || s.Right(4) == ".doc" || s.Right(4) == "html" || s.Right(4) == ".htm" || s.Right(4) == ".ini" || s.Right(4) == ".xml" || s.Right(4) == ".kar" || s.Right(4) == ".hed" || s.Right(4) == ".mzi" || s.Right(4) == ".mag" || s.Right(4) == ".mvi" || s.Right(4) == ".lvi" || s.Right(4) == ".mpi" || s.Right(4) == ".pvi" || s.Right(4) == ".pzi" || s.Right(4) == ".p86" || s.Right(4) == ".mml" || s.Right(4) == ".m3u" || s.Right(4) == ".frm" || s.Right(7) == ".psflib" || s.Right(8) == ".psf2lib" || s.Right(7) == ".usflib" || s.Right(7) == ".2sflib" || s.Right(3) == ".gb" || s.Right(7) == ".gsflib" || s.Right(4) == ".pdx") {
				}
				else {
					if (syo == 0) {
						syo = 1; syos = p.fol; modesub = p.sub; fnn = sL;
					}
					CString fol = p.fol;
					if (PathIsDirectory(fol)) {
						fol += L"\\" + sL;
					}
					

					Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, fol, 0, 0);
					fol = p.fol;
					if (PathIsDirectory(fname_full) == FALSE) {
						return;
					}
				}
			}
		}
	}
	f.Close();
	fname = fname1;
	int cdd=0;
	if(PathIsDirectory(fname)){
		CFileFind cf1;
		if (cf1.FindFile(_T("*.*")) != 0) {
			int r = 1;
			for (; r;) {
				r = cf1.FindNextFile();
				CString ss, sss;
				ss = cf1.GetFileName();
				sss = cf1.GetFilePath();
				if (!(ss == L"." || ss == L"..")) {
					if ((cf1.IsHidden() == 0)) {
						if (cf1.IsDirectory() != 0) { //フォルダ？
							Fol(cf1.GetFilePath());//*/fname+cf1.GetFileName();
						}
					}
				}
			}
		}
		cf1.Close();
	}
	else {

	}
}



void CPlayList::plugs(CString fff, playlistdata *p,TCHAR* kpi, BYTE& kv)
{
	if (IsDougaVideoFile(fff)) {
		if (kpi)
			kpi[0] = 0;
		if (p)
			p->sub = -2;
		return;
	}

	CString ss,ft;
	int flg=0;
	for(int i=0;i<kpicnt;i++){
		for(int j=0;;j++){
			if(ext[i][j]=="") break;
			ss=fff.Right(fff.GetLength()-fff.ReverseFind('.'));ss.MakeLower();
			if(ext[i][j]==ss){
				ss=kpif[i];
				if (kpichk[i] == 1) {
					flg = 1;
					kv = kvar[i][j];
					break;
				}
			}
		}
		if(flg==1)break;
	}
	if(flg==1){
		_tcscpy(p->fol,fff);
		p->sub=-3;
		ft=fff.Right(fff.GetLength()-fff.ReverseFind('\\')-1);
		_tcscpy(p->name,ft);
		p->alb[0]=NULL;p->art[0]=NULL;p->loop1=p->loop2=p->ret2=0;
		_tcscpy(kpi,ss);
	}
}

void CPlayList::Save()
{
	TCHAR tmp[1024];int cnt,j;CString s;
	int cx,cy,x,y;RECT r;
	int c;
	_tgetcwd(tmp,1000);
	_tchdir(karento2);
	if(IsIconic()){
		ShowWindow(SW_RESTORE);
		GetWindowRect(&r);
		ShowWindow(SW_MINIMIZE);
	}else
		GetWindowRect(&r);
	x=r.left;y=r.top;cx=r.right-x;cy=r.bottom-y;
#if _UNICODE
	int lcnt = savedata.playlistnum;
	CString s0;
	if (lcnt == 0)
		s0.Format(L"playlistu.dat");
	else
		s0.Format(L"playlistu%d.dat", lcnt);
	CFile f;if(f.Open(s0,CFile::modeCreate|CFile::modeWrite | CFile::shareExclusive,NULL)==TRUE){
#else
	CFile f;if(f.Open(_T("playlist.dat"),CFile::modeCreate|CFile::modeWrite | CFile::shareExclusive,NULL)==TRUE){
#endif
		cnt=playcnt;
		f.Write(&cnt,4);
		f.Write(&x,4);
		f.Write(&y,4);
		f.Write(&cx,4);
		f.Write(&cy,4);
		c=m_lc.GetColumnWidth(0);f.Write(&c,4); // 名前
		c=m_lc.GetColumnWidth(2);f.Write(&c,4); // ゲーム(旧 col1 スロット互換)
		c=m_lc.GetColumnWidth(4);f.Write(&c,4); // アーティスト(旧 col3)
		c=m_lc.GetColumnWidth(5);f.Write(&c,4); // アルバム(旧 col4)
		c=m_lc.GetColumnWidth(1);f.Write(&c,4); // ★(旧未使用 col7 スロットへ)
		playlistdata pld;
		for(int i=0;i<cnt;i++){ZeroMemory(&pld,sizeof(pld));
			_tcscpy(pld.alb,pc[i].alb);
			_tcscpy(pld.art,pc[i].art);
			_tcscpy(pld.fol,pc[i].fol);
			_tcscpy(pld.name,pc[i].name);
			pld.loop1=pc[i].loop1;
			pld.loop2=pc[i].loop2;
			pld.sub=pc[i].sub;
			pld.ret2=pc[i].ret2;
			pld.time=pc[i].time;
			f.Write(&pld,sizeof(pld));
		}
		c=m_loop.GetCheck();f.Write(&c,4);
		c=m_renzoku.GetCheck();f.Write(&c,4);
		c=m_tool.GetCheck();f.Write(&c,4);
		c=m_saisyo.GetCheck();f.Write(&c,4);
		c=m_lc.GetColumnWidth(3);f.Write(&c,4); // 時間(旧 col2)
		c=m_lc.GetColumnWidth(6);f.Write(&c,4); // フォルダ(旧 col5)
		f.Write(&pnt,4);
		f.Close();

		savedata.saveloop = m_loop.GetCheck();
		savedata.saverenzoku = m_renzoku.GetCheck();
		savedata.savecheck = m_savecheck.GetCheck();
		savedata.savecheck_mp3 = m_save_mp3.GetCheck();
		savedata.savecheck_dshow = m_save_kpi.GetCheck();

		CFile ab;
#if _UNICODE
		if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
		if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
			ab.Write(&savedata, sizeof(save));
			ab.Close();
		}
	}
	_tchdir(tmp);
}


void CPlayList::Load(BOOL restoreSavedRow)
{
	TCHAR tmp[1024];int cnt;
	int cx,cy,x=-10000,y,c;
	playcnt = 0;
	if (pc) {
		free(pc);
		pc = NULL;
	}
	_tgetcwd(tmp,1000);
	_tchdir(karento2);
#if _UNICODE
	int lcnt = savedata.playlistnum;
	CString s;
	if (lcnt == 0)
		s.Format(L"playlistu.dat");
	else
		s.Format(L"playlistu%d.dat", lcnt);
	CFile f;if(f.Open(s,CFile::modeRead | CFile::shareDenyWrite,NULL)==TRUE){
#else
	CFile f;if(f.Open(_T("playlist.dat"),CFile::modeRead | CFile::shareDenyWrite,NULL)==TRUE){
#endif
		f.Read(&cnt,4);
		pc = (playlistdata0*)malloc(sizeof(playlistdata0) * (cnt + 1));
		f.Read(&x,4);
		f.Read(&y,4);
		f.Read(&cx,4);
		f.Read(&cy,4);
		f.Read(&c,4);m_lc.SetColumnWidth(0,c); // 名前
		f.Read(&c,4);m_lc.SetColumnWidth(2,c); // ゲーム
		f.Read(&c,4);m_lc.SetColumnWidth(4,c); // アーティスト
		f.Read(&c,4);m_lc.SetColumnWidth(5,c); // アルバム
		f.Read(&c,4); // 印列([SAV]/[LRC]。旧★幅は狭いので下限を上げる)
		if (c > 0 && c <= 400) {
			const int minMark = (int)(72 * hD2);
			if (c < minMark) c = minMark;
			m_lc.SetColumnWidth(1, c);
		}
		playlistdata pld;
		m_lc.SetItemCount(cnt);
		for(int i=0;i<cnt;i++){
			f.Read(&pld,sizeof(pld));
			Add(pld.name,pld.sub,pld.loop1,pld.loop2,pld.art,pld.alb,pld.fol,pld.ret2,pld.time,FALSE,FALSE);			
		}
		c=0;f.Read(&c,4);m_loop.SetCheck(c);
		c=0;f.Read(&c,4);m_renzoku.SetCheck(c);
		c=1;f.Read(&c,4);m_tool.SetCheck(c);
		c=1;f.Read(&c,4);m_saisyo.SetCheck(c);
		c=-1;f.Read(&c,4);if(c!=-1){ if(c<72) c=72; m_lc.SetColumnWidth(3,c); } // 時間
		c=-1;f.Read(&c,4);if(c!=-1)m_lc.SetColumnWidth(6,c); // フォルダ
		pnt = -1;
		pnt1 = -1;
		f.Read(&pnt, 4);
		f.Close();
	}
	ClampPlaylistSelectionIndices(this);
	if (restoreSavedRow)
		RestoreSavedPlaybackRow();
	_tchdir(tmp);
	if(GetAsyncKeyState(VK_LCONTROL)&0x8000){
		x=-10000;
	}
	if(x!=-10000){
		MoveWindow(x,y,cx,cy,TRUE);
		RECT r;
		GetClientRect(&r);
	m_lc.SetWindowPos(&wndNoTopMost,0,0,(int)(r.right-20*(hD2)),(int)(r.bottom-80 * (hD2 )),SWP_NOMOVE|SWP_NOOWNERZORDER|SWP_NOZORDER);
	}
	// Load/realloc 後の実体ポインタをリストへ再同期(ツールチップ等が参照)
	m_lc.pc = pc;
	// ディスク済ジャケを一括メモリ化(順々表示に見せない)
	if (::IsWindow(m_lc.GetSafeHwnd()) && playcnt > 0)
		PlJacketLoadVisible(this, FALSE, TRUE);
}
int SC=0;
void CPlayList::SIcon(int i){
	if(i<0) return;
	if(i>=playcnt) return;
	if(i==pnt) return;
	RECT r;
	pc[i].icon=0; if(pnt>=0&&pnt<playcnt){ pc[pnt].icon=1;
			// 名前列(ジャケ+♪)。UPDATENOW/ERASE 禁止: Opaque と競合して名前列だけちらつく
			if (m_lc.GetItemRect(pnt, &r, LVIR_BOUNDS)) {
				r.right = r.left + kPlJakPx + 42;
				m_lc.RedrawWindow(&r, NULL, RDW_INVALIDATE | RDW_NOERASE);
			}
	}
	pnt=i;
	if (m_lc.GetItemRect(pnt, &r, LVIR_BOUNDS)) {
		r.right = r.left + kPlJakPx + 42;
		m_lc.RedrawWindow(&r, NULL, RDW_INVALIDATE | RDW_NOERASE);
	}
	m_lc.EnsureVisible(i,FALSE);
	SC=0;
}

void CPlayList::SIconTimer(int i){
	CString s; s.Format(L"%d", i);
	if(pnt<0) return;
	if(pnt>=playcnt) return;
	if(IsBadReadPtr(&pc[pnt],sizeof(playlistdata0))) return;
	//_try{
	if(i==0)
		pc[pnt].icon=2;
	else
		pc[pnt].icon=0;
	//}__except(EXCEPTION_EXECUTE_HANDLER){}
	// 名前列のみ。即時描画は Opaque とぶつかりジャケ/♪周りがちらつく
	{
		RECT r;
		if (m_lc.GetItemRect(pnt, &r, LVIR_BOUNDS)) {
			r.right = r.left + kPlJakPx + 42;
			m_lc.RedrawWindow(&r, NULL, RDW_INVALIDATE | RDW_NOERASE);
		}
	}
	// MP画面は 250ms ポーリングだと点滅が欠けるので即通知
	extern CMediaPlayerDlg* mp;
	if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->NotifyPlayIconChanged();
}
extern int ps;
extern void DoEvent();
void CPlayList::OnNMDblclkList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	*pResult = 0;
	CString s;int i,j;
	int Lindex=-1;
	Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);i=Lindex;
	if(Lindex>=playcnt) return;
	if(Lindex==-1) return;
	//SIcon(i);
	fnn=pc[Lindex].name;
	filen=pc[Lindex].fol;
	modesub=pc[Lindex].sub;
	loop1=pc[Lindex].loop1;
	loop2=pc[Lindex].loop2;
	ret2=pc[Lindex].ret2;
	plcnt=i;
	gameon = 0;
	RequestPlaylistRestartAsync();
}
extern CDouga *pMainFrame1;
extern long height, width;
int ip1 = 0;
void CPlayList::OnSize(UINT nType, int cx, int cy)
{
	try {
	CCustomBlurDialogBase::OnSize(nType, cx, cy);

	// TODO: ここにメッセージ ハンドラ コードを追加します。
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
	RECT r;
	GetClientRect(&r);
	if( ::IsWindow( this->GetSafeHwnd()) == TRUE &&  this->IsWindowVisible() == TRUE){
		if (!::IsWindow(m_lc.GetSafeHwnd())) {
			CCC_MainLockBringToFront(m_hWnd);
			return;
		}
		m_lc.SetWindowPos(&wndNoTopMost, 0, 0, (int)(r.right - 20 * (hD2 )), (int)(r.bottom - 80 * (hD2 )), SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
		// アルバム/コメント列(列5)をリスト右端へぴたりとフィットさせる。
		// 後ろのフォルダ列(列6)は 0 幅にして余白(空列)を残さない。
		// 他列の合計を引いた残り幅を割り当て、最低幅を下回る狭い窓では
		// 最低幅に固定して横スクロールバーが出るに任せる。
		if (::IsWindow(m_lc.GetSafeHwnd())) {
			m_lc.SetColumnWidth(6, 0);
			CRect lcr; m_lc.GetClientRect(&lcr);   // 縦スクロールバー分を除いた可視幅
			int used = 0;
			used += m_lc.GetColumnWidth(0); // 名前
			used += m_lc.GetColumnWidth(1); // ★
			used += m_lc.GetColumnWidth(2); // ゲーム
			used += m_lc.GetColumnWidth(3); // 時間
			used += m_lc.GetColumnWidth(4); // アーティスト
			int minLast = (int)(80 * hD2);
			int last = lcr.Width() - used;
			if (last < minLast) last = minLast;
			if (m_lc.GetColumnWidth(5) != last) m_lc.SetColumnWidth(5, last);
		}
	}
	if(pl){
		if (nType == SIZE_MINIMIZED){
			if(m_saisyo.GetSafeHwnd() && m_saisyo.GetCheck())
				og->ShowWindow(SW_MINIMIZE);
			if(pMainFrame1){
				pMainFrame1->ShowWindow(SW_HIDE);
			}
			if (playbase)
				playbase->ShowWindow(SW_MINIMIZE);
		}
		if(nType== SIZE_RESTORED){
			if (ogpl == 1) {
				ogpl = 0;
//				return;
			}
			if(m_saisyo.GetSafeHwnd() && m_saisyo.GetCheck())
				og->ShowWindow(SW_RESTORE);
			if(pMainFrame1 && height!=0){
				pMainFrame1->ShowWindow(SW_SHOWNORMAL);
			}
			if (playbase)
				playbase->ShowWindow(SW_RESTORE);
	//		ip1 = 0;
//			SetTimer(4923, 100, NULL);
		}
	}
	CCC_MainLockBringToFront(m_hWnd);
	}
	catch (CException* e)
	{
		OutputDebugString(_T("[CPlayList::OnSize] CException swallowed\n"));
		e->Delete();
	}
}
int kk=0;
extern int lenl;
int tlg=0;

extern int aaaa,aaaa1;
extern CPlayList*pl;
void timerpl(UINT nIDEvent,CPlayList* pl);
void timerpl1(UINT nIDEvent,CPlayList* pl);
void timerpl1(UINT nIDEvent,CPlayList* pl)
{
	if (nIDEvent == 4927) {
		pl->KillTimer(4927);
		if (ip1 > 0) {
			 return;
		}
		if (playbase)
				::SetWindowPos(playbase->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			::SetWindowPos(pl->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			pl->SetTimer(4930, 10, NULL);
			ip1 = 3;
	}
	if (nIDEvent == 4924) {
		pl->KillTimer(4924);
		if (ip1 <= 0) return;
		if (playbase)
			::SetWindowPos(playbase->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		::SetWindowPos(pl->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		pl->SetTimer(4930, 10, NULL);
		ip1 = 3;
	}
	if (nIDEvent == 4930) {
		ip1--;
		if (ip1 <= 0) {
			ip1 = 0;
			aaaa = 0;
			pl->KillTimer(4930);
		}
	}
	// Timer 5000: pnt1 はあいまい検索用。SIcon(pnt1) すると再生中♪が検索行へ飛び、
	// スクロール位置が元に戻る不具合の原因になるため何もしない。
	if(nIDEvent==5000){
		pl->KillTimer(5000);
	}
	if(nIDEvent==40){
		pl->KillTimer(40);
		plw=1;
	}

	if(nIDEvent==3000){
		pl->SIconTimer(SC);
		SC++; SC = SC % 2;
	}
	if(nIDEvent==20){
		
		if(pl->w_flg==FALSE) return;
		if(pl->GetFocus()==NULL){return;}
		if(pl->m_find.GetFocus()->m_hWnd==pl->m_find.m_hWnd){return;}
		{
			HWND rtn;
			TCHAR Name[1024];
			long Leng = sizeof(Name);
			rtn = GetActiveWindow();
			GetWindowTextW(rtn, Name, Leng);
			CString sss;
			sss = Name;
			if (sss != _T("プレイリスト")) {
				return;
			}
			if((GetKeyState(VK_RETURN)&0x8000)==0 && kk==1)
				kk=0;
			if(GetKeyState(VK_RETURN)&0x8000 && kk==0){
				kk=1;
				CString s;int i,j;
				int Lindex=-1;
				Lindex=pl->m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				i=Lindex;
				if(Lindex>=pl->playcnt) return;
				if(Lindex==-1) return;
//				pl->SIcon(i);
				fnn=pl->pc[Lindex].name;
				filen=pl->pc[Lindex].fol;
				modesub=pl->pc[Lindex].sub;
				loop1=pl->pc[Lindex].loop1;
				loop2=pl->pc[Lindex].loop2;
				ret2=pl->pc[Lindex].ret2;
				plcnt=i;
				RequestPlaylistRestartAsync();
			}
			if((GetKeyState(VK_CONTROL)&0x8000) && (GetKeyState('A')&0x8000)){
				int i=pl->m_lc.GetItemCount();
				for(int j=0;j<i;j++){
					pl->m_lc.SetItemState(j,LVIS_SELECTED,LVIS_SELECTED);
				}
			}
		}
		int tl=pl->m_tool.GetCheck();
		if(tl!=tlg){
			tlg=tl;
			if(tlg){
				pl->m_lc.EnableToolTips(TRUE);
				tl=pl->m_lc.GetExtendedStyle();
				tl = tl & ~LVS_EX_INFOTIP;
				pl->m_lc.SetExtendedStyle(tl);
			}else{
				pl->m_lc.EnableToolTips(FALSE);
				tl=pl->m_lc.GetExtendedStyle();
				tl |=LVS_EX_INFOTIP;
				pl->m_lc.SetExtendedStyle(tl);
			}
		}
	}
}
void timerpl(UINT nIDEvent,CPlayList* pl)
{
	try{
		_set_se_translator( trans_func1 );
		timerpl1(nIDEvent,pl);
//	}__except(EXCEPTION_EXECUTE_HANDLER){}
	}catch(SE_Exception1 e){
	}
	catch(_EXCEPTION_POINTERS *ep){
	}
	catch(...){}
}

extern int stflg;

#if WIN64
void CPlayList::OnTimer(UINT_PTR nIDEvent) 
#else
void CPlayList::OnTimer(UINT nIDEvent) 
#endif
{
	if (nIDEvent == kPlayListNavRefreshTimer) {
		KillTimer(kPlayListNavRefreshTimer);
		RefreshNavControls();
		return;
	}
	if (nIDEvent == 41) {
		PlMissTickScan(this);
		static int s_plJakLastTop = -1;
		const int topNow = ::IsWindow(m_lc.GetSafeHwnd()) ? m_lc.GetTopIndex() : -1;
		if (topNow != s_plJakLastTop) {
			s_plJakLastTop = topNow;
			if (s_plJakTimerMs != 125) {
				s_plJakTimerMs = 125;
				SetTimer(41, 125, NULL);
			}
		}
		const BOOL work = PlJacketLoadVisible(this, TRUE, FALSE);
		const UINT want = work ? 125u : 500u;
		if (want != s_plJakTimerMs) {
			s_plJakTimerMs = want;
			SetTimer(41, want, NULL);
		}
	}
	if (nIDEvent == 40) {
		// 互換: 旧経路。継続抽出は Timer41
		PlMissTickScan(this);
		PlJacketLoadVisible(this, TRUE, FALSE);
	}
	savedata.saveloop = m_loop.GetCheck();
	savedata.saverenzoku = m_renzoku.GetCheck();
	savedata.savecheck=m_savecheck.GetCheck();
	savedata.savecheck_mp3 = m_save_mp3.GetCheck();
	savedata.savecheck_dshow = m_save_kpi.GetCheck();
	CPlayList* pl = (CPlayList*)this;
	if(stflg == FALSE)
		timerpl(nIDEvent,pl);
	CCustomBlurDialogBase::OnTimer(nIDEvent);
}

void CPlayList::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	CCustomBlurDialogBase::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CPlayList::OnBnClickedCheck4()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	Save();
}

void CPlayList::OnBnClickedCheck1()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	Save();
}

void CPlayList::OnLvnBegindragList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNM = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	POINT ptPos,ptPos2;
    HIMAGELIST hOneImageList;
    HIMAGELIST hTempImageList;
	IMAGEINFO imf;
	long iHeight;
	m_hDragImage = ListView_CreateDragImage(m_lc.m_hWnd,pNM->iItem,&ptPos);
	ImageList_GetImageInfo(m_hDragImage, 0, &imf);
	iHeight = imf.rcImage.bottom;
	for(int Lindex=-1;;){
		Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);//pNM->iItem
		if(Lindex==-1) break;
		if(pNM->iItem==Lindex){
		}else{
            hOneImageList= ListView_CreateDragImage(m_lc.m_hWnd,Lindex,&ptPos2);
            hTempImageList = ImageList_Merge(m_hDragImage, 
                             0, hOneImageList, 0, 0, iHeight);
            ImageList_Destroy(m_hDragImage);
            ImageList_Destroy(hOneImageList);
            m_hDragImage = hTempImageList;
            ImageList_GetImageInfo(m_hDragImage, 0, &imf);
            iHeight = imf.rcImage.bottom;		}
	}
 	// ドラッグ開始
	POINT ptCursor;
	GetCursorPos(&ptCursor);
	m_lc.ScreenToClient(&ptCursor);

	long lX = ptCursor.x- ptPos.x;
	long lY = ptCursor.y- ptPos.y;

	ImageList_BeginDrag(m_hDragImage,0,lX,lY);
	ImageList_DragEnter(m_hWnd,0,0);
	SetCapture();


	*pResult = 0;
}

void CPlayList::OnDrag(int x,int y)
{
	POINT Point={x,y};
	ClientToScreen(&Point);
	RECT Rect;
	GetWindowRect(&Rect);
	ImageList_DragMove(Point.x-Rect.left,Point.y-Rect.top);
	{
		CPoint  point,point2;CRect rect;
		GetCursorPos(&point);
		ScreenToClient(&point);
		m_lc.GetWindowRect(&rect);
		point2.y=rect.top; point2.x=rect.left;
		ScreenToClient(&point2);
		point-=point2;
		int hItem = m_lc.HitTest(point ,NULL);
	}
}

void CPlayList::OnEndDrag()
{
	// ドラッグ終了
	ImageList_DragLeave(m_hWnd);
	ImageList_EndDrag();
	ImageList_Destroy(m_hDragImage);
	m_hDragImage = NULL;

	// カーソル表示
	ShowCursor(TRUE);
}
void CPlayList::OnMouseMove(UINT nFlags, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	if(GetCapture()==this){
		OnDrag(point.x,point.y);
	}
	CCustomBlurDialogBase::OnMouseMove(nFlags, point);
}

void CPlayList::OnLButtonUp(UINT nFlags, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	if(GetCapture()==this){
		OnEndDrag();
		// キャプチャ解除
		ReleaseCapture();
		//実際の移動のための座標割りだし
		CPoint  point,point2;CRect rect;
		GetCursorPos(&point);
		ScreenToClient(&point);
		m_lc.GetWindowRect(&rect);
		point2.y=rect.top; point2.x=rect.left;
		ScreenToClient(&point2);
		point-=point2;
		int hItem = m_lc.HitTest(point,NULL);
		if( hItem != -1){
			playlistdata *p;int cnt=0,j,cnt2=0,*cn;
			int Lindex = -1, Lindexx;
			for(;;){
				Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				if(Lindex==-1) break;
				cnt++;
			}
			//転送データをあらかじめ作っておく
			p = (playlistdata*)malloc(sizeof(playlistdata)*cnt);
			for(Lindexx=-1;;cnt2++){
				Lindexx=m_lc.GetNextItem(Lindexx,LVNI_ALL |LVNI_SELECTED);
				if(Lindexx==-1) break;
			}
			//転送するインテックス番号を獲得する
			int cn1;
			cn =(int*)malloc(sizeof(int)*cnt2);
			for(cn1=0,Lindexx=-1;;cn1++){
				Lindexx=m_lc.GetNextItem(Lindexx,LVNI_ALL |LVNI_SELECTED);
				if(Lindexx==-1) break;
				cn[cn1]=Lindexx;
			}
			CString s;
			for(cnt=0,Lindex=-1;;cnt++){
				Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				if(Lindex==-1) break;
				_tcscpy(p[cnt].name,pc[Lindex].name);
				_tcscpy(p[cnt].fol,pc[Lindex].fol);
				p[cnt].sub=pc[Lindex].sub;
				p[cnt].ret2=pc[Lindex].ret2;
			}
			for(Lindex=-1;;){
				playlistdata pp;
				Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				if(Lindex==-1) break;
				_tcscpy(pp.name,pc[hItem].name);
				_tcscpy(pp.fol,pc[hItem].fol);
				pp.sub=pc[hItem].sub;
				pp.ret2=pc[hItem].ret2;
				int cnt1 = 0;
				for(;cnt1<cnt;cnt1++){
					if(_tcscmp(p[cnt1].name,pp.name)==0 && _tcscmp(p[cnt1].fol,pp.fol)==0 && p[cnt1].sub==pp.sub && p[cnt1].ret2==pp.ret2){
						break;
					}
				}
				if(cnt1!=cnt) break;
				if(hItem<Lindex){//選択項目が下　ドロップ位置が上
					int k=Lindex-hItem;
					m_lc.SetItemState(Lindex,m_lc.GetItemState(Lindex,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
					for(int kk=0;kk<k;kk++){
						OnXCHG(Lindex-kk,Lindex-kk-1);
					}
					m_lc.SetItemState(hItem  ,m_lc.GetItemState(hItem,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
				}else{//選択項目が上　ドロップ位置が下
					int Lindexx = -1;
					for(;;){
						Lindexx=m_lc.GetNextItem(Lindexx,LVNI_ALL |LVNI_SELECTED);
						if(Lindexx==-1) break;
						m_lc.SetItemState(Lindexx,m_lc.GetItemState(Lindexx,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
					}
					int i;
					for(i=0;i<cn1;i++){
						int k=hItem-cn[i];
						for(int kk=0;kk<k;kk++){
								OnXCHG(cn[i]+kk+1,cn[i]+kk);
						}
						for(int j=0;j<cn1;j++) cn[j]--;
					}
					hItem-=cn1;
					for(i=0;i<cn1;i++){
						hItem++;
						m_lc.SetItemState(hItem  ,m_lc.GetItemState(hItem,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
					}
					break;
				}
				hItem++;
			}
			free(cn);
			free(p);
			m_lc.RedrawWindow();
			m_lDragTopItem=0;m_lDragTopItemt=0;
		 }
	}

	CCustomBlurDialogBase::OnLButtonUp(nFlags, point);
}

void CPlayList::OnLvnGetdispinfoList1(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVDISPINFO* lpDInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);

	// 基本的なNULLチェックを行います
	if (lpDInfo == NULL) return;

	*pResult = 0; // 初期化

	try {
		_set_se_translator(trans_func1);

		// pcがまだ確保されていない、あるいは要素数が0の場合は何もせず安全に終了します
		// ※ m_nPcCount は pc配列の要素数を管理している変数に置き換えてください
		if (pc == NULL || playcnt <= 0) {
			return;
		}

		// 要求されたインデックスを取得します
		int nTargetIndex = lpDInfo->item.iItem;

		// インデックスが範囲外（個数オーバー）の場合の処理
		if (nTargetIndex < 0 || nTargetIndex >= playcnt) {
			// お嬢様のご指示通り、範囲外なら0番目を参照するようにします
			nTargetIndex = 0;
		}

		// テキスト情報の要求に対する処理
		if (lpDInfo->item.mask & LVIF_TEXT) {
			// 安全確保済みの nTargetIndex を使用して pc にアクセスします
			switch (lpDInfo->item.iSubItem) {
			case 0: {
				TCHAR buf[1100];
				buf[0] = 0;
				// 描画中にディスクI/Oしない(メモリのみ)。MP 走査結果を優先。
				char miss = 0;
				extern CMediaPlayerDlg* mp;
				if (mp && mp->m_miss && nTargetIndex < mp->m_missCap)
					miss = mp->m_miss[nTargetIndex];
				else if (s_plMiss && nTargetIndex < s_plMissCap)
					miss = s_plMiss[nTargetIndex];
				if (miss == 1)
					_tcscpy_s(buf, _T("⚠ "));
				_tcscat_s(buf, pc[nTargetIndex].name);
				_tcsncpy_s(lpDInfo->item.pszText, lpDInfo->item.cchTextMax, buf, _TRUNCATE);
			} break;
			case 1: {
				CString marks;
				PlFormatRowMarks(nTargetIndex, pc[nTargetIndex].fol, marks);
				_tcsncpy_s(lpDInfo->item.pszText, lpDInfo->item.cchTextMax, marks, _TRUNCATE);
			} break;
			case 2:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].game);
				break;
			case 3: {
				CString s;
				if (pc[nTargetIndex].time >= 3600)
					s.Format(_T("%d:%02d:%02d"), pc[nTargetIndex].time / 3600, (pc[nTargetIndex].time / 60) % 60, pc[nTargetIndex].time % 60);
				else
					s.Format(_T("%d:%02d"), pc[nTargetIndex].time / 60, pc[nTargetIndex].time % 60);

				if (pc[nTargetIndex].time == 0) s = "";
				if (pc[nTargetIndex].time == -1) s = LL14(L"取得不能", L"Unable to fetch", L"Impossible de récupérer", L"Impossibile recuperare", L"No se pudo obtener", L"가져올 수 없음", L"无法获取", L"تعذر الجلب", L"Не удалось получить", L"Abruf nicht möglich", L"Não foi possível obter", L"Ophalen mislukt", L"Nie można pobrać", L"Alınamadı");
				_tcscpy(lpDInfo->item.pszText, s);
			} break;
			case 4:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].art);
				break;
			case 5:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].alb);
				break;
			case 6:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].fol);
				break;
			default:
				break;
			}
		}

		// 画像情報の要求に対する処理
		if (lpDInfo->item.mask & LVIF_IMAGE) {
			// 空アイコンのみ返す。既定ILの♪先描きを抑止し、自前カスタム描画で描く。
			lpDInfo->item.iImage = 1;
		}
	}
	catch (SE_Exception1 e) {
		// 例外発生時の処理
	}
	catch (_EXCEPTION_POINTERS* ep) {
		// 例外発生時の処理
	}
	catch (...) {
		// その他の例外
	}
}
void CPlayList::OnNMRclickList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	*pResult = 0;

	CPoint point;
	GetCursorPos(&point);
	PlSelectItemAtScreenPoint(m_lc, point);

	CWnd* pWndPopupOwner = this;
	while (pWndPopupOwner->GetStyle() & WS_CHILD)
		pWndPopupOwner = pWndPopupOwner->GetParent();

	const int cmd = ShowTrackContextMenu(point, pWndPopupOwner);
	if (cmd != 0)
		HandleTrackContextCmd(cmd);
}

void CPlayList::OnList()
{
	std::vector<int> sel;
	int Lindex = -1;
	while ((Lindex = m_lc.GetNextItem(Lindex, LVNI_ALL | LVNI_SELECTED)) >= 0) {
		if (Lindex >= 0 && Lindex < playcnt)
			sel.push_back(Lindex);
	}
	if (sel.empty())
		return;

	CListSyosai* a = new CListSyosai(GetPlaylistModalOwner(this));
	w_flg = FALSE;
	CWnd::PostMessage(0x118);
	memcpy(&a->pc, &pc[sel[0]], sizeof(playlistdata0));
	if (sel.size() > 1)
		a->m_batchIndices = sel;

	const int ret = a->DoModal();
	const bool apply = (ret == IDOK || ret == IDC_SYOSAI_BTN_PROTOOLS);
	if (apply) {
		if (a->m_batchIndices.size() > 1) {
			for (int idx : a->m_batchIndices) {
				if (idx < 0 || idx >= playcnt) continue;
				_tcscpy(pc[idx].art, a->pc.art);
				_tcscpy(pc[idx].alb, a->pc.alb);
				RECT r;
				m_lc.GetItemRect(idx, &r, LVIR_BOUNDS);
				m_lc.RedrawWindow(&r);
			}
		}
		else {
			const int idx = sel[0];
			_tcscpy(pc[idx].name, a->pc.name);
			_tcscpy(pc[idx].art, a->pc.art);
			_tcscpy(pc[idx].alb, a->pc.alb);
			pc[idx].loop1 = a->pc.loop1;
			pc[idx].loop2 = a->pc.loop2;
			const CString folStored = PlStorePlaylistFol(a->pc.fol, pc[idx].sub);
			_tcscpy(pc[idx].fol, folStored);
			RECT r;
			m_lc.GetItemRect(idx, &r, LVIR_BOUNDS);
			m_lc.RedrawWindow(&r);
		}
		Save();
	}
	w_flg = TRUE;
	delete a;

	if (ret == IDC_SYOSAI_BTN_PROTOOLS) {
		extern void OpenProToolsForSelection();
		OpenProToolsForSelection();
	}
}
#define ID_HOTKEY0 8000
#define ID_HOTKEY1 8001
#define ID_HOTKEY2 8002
#define ID_HOTKEY3 8003
void CPlayList::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CCustomBlurDialogBase::OnActivate(nState, pWndOther, bMinimized);
	int l = 5;
	if(plw){
		if ((nState == WA_ACTIVE || nState == WA_CLICKACTIVE) && bMinimized == 0 && pl->m_saisyo.GetCheck()) {
			l = 20;
			ogpl = 1;
			og->ShowWindow(SW_RESTORE);
		}
	}
	if (nState == WA_ACTIVE || nState == WA_CLICKACTIVE) {
		SetTimer(4927, 10, NULL);
		ScheduleRefreshNavControls();
	}
	else {
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY0);
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY1);
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY2);
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY3);
	}
//	else {
//		if (nState == WA_INACTIVE) {
//			SetTimer(4924, l, NULL);
//		}
//	}
	// TODO: ここにメッセージ ハンドラ コードを追加します。
}

void CPlayList::OnPop32787()//ファイル名変更（統合画面へ）
{
	OnList();
}

void CPlayList::OnPopWavExport()
{
	std::vector<int> indices;
	int Lindex = -1;
	while ((Lindex = m_lc.GetNextItem(Lindex, LVNI_ALL | LVNI_SELECTED)) >= 0) {
		if (Lindex < playcnt) indices.push_back(Lindex);
	}
	if (indices.empty()) return;
	CTranscodeExport* a = new CTranscodeExport(GetPlaylistModalOwner(this));
	a->m_initialTab = -1; // 前回形式(mp3/FLAC)。WAVはタブで切替
	a->m_preferXfade = false;
	w_flg = FALSE;
	if (indices.size() == 1) {
		a->multiFile = false;
		memcpy(&a->pc, &pc[indices[0]], sizeof(playlistdata0));
	}
	else {
		a->multiFile = true;
		a->pcs.reserve(indices.size());
		for (size_t i = 0; i < indices.size(); ++i) {
			a->pcs.push_back(pc[indices[i]]);
		}
		memcpy(&a->pc, &pc[indices[0]], sizeof(playlistdata0));
	}
	CWnd::PostMessage(0x118);
	a->DoModal();
	w_flg = TRUE;
	delete a;
}

void CPlayList::OnPopXfadeExport()
{
	std::vector<int> indices;
	int Lindex = -1;
	while ((Lindex = m_lc.GetNextItem(Lindex, LVNI_ALL | LVNI_SELECTED)) >= 0) {
		if (Lindex < playcnt) indices.push_back(Lindex);
	}
	if (indices.size() < 2) return;
	CTranscodeExport* a = new CTranscodeExport(GetPlaylistModalOwner(this));
	a->m_initialTab = -1;
	a->m_preferXfade = true;
	w_flg = FALSE;
	a->multiFile = true;
	a->pcs.reserve(indices.size());
	for (size_t i = 0; i < indices.size(); ++i)
		a->pcs.push_back(pc[indices[i]]);
	memcpy(&a->pc, &pc[indices[0]], sizeof(playlistdata0));
	CWnd::PostMessage(0x118);
	a->DoModal();
	w_flg = TRUE;
	delete a;
}

void CPlayList::OnPopTranscode()
{
	std::vector<int> indices;
	int Lindex = -1;
	while ((Lindex = m_lc.GetNextItem(Lindex, LVNI_ALL | LVNI_SELECTED)) >= 0) {
		if (Lindex < playcnt) indices.push_back(Lindex);
	}
	if (indices.empty()) return;
	CTranscodeExport* a = new CTranscodeExport(GetPlaylistModalOwner(this));
	a->m_initialTab = (savedata.tc_format == 1) ? 2 : 1; // FLAC or mp3
	a->m_preferXfade = false;
	w_flg = FALSE;
	if (indices.size() == 1) {
		a->multiFile = false;
		memcpy(&a->pc, &pc[indices[0]], sizeof(playlistdata0));
	}
	else {
		a->multiFile = true;
		a->pcs.reserve(indices.size());
		for (size_t i = 0; i < indices.size(); ++i) {
			a->pcs.push_back(pc[indices[i]]);
		}
		memcpy(&a->pc, &pc[indices[0]], sizeof(playlistdata0));
	}
	CWnd::PostMessage(0x118);
	a->DoModal();
	w_flg = TRUE;
	delete a;
}

void CPlayList::OnPopTagEdit()
{
	std::vector<int> indices;
	int Lindex = -1;
	while ((Lindex = m_lc.GetNextItem(Lindex, LVNI_ALL | LVNI_SELECTED)) >= 0) {
		if (Lindex < playcnt) indices.push_back(Lindex);
	}
	if (indices.empty()) return;
	CTagEditDlg* a = new CTagEditDlg(GetPlaylistModalOwner(this));
	w_flg = FALSE;
	if (indices.size() == 1) {
		a->multiFile = false;
		memcpy(&a->pc, &pc[indices[0]], sizeof(playlistdata0));
	}
	else {
		a->multiFile = true;
		a->pcs.reserve(indices.size());
		for (size_t i = 0; i < indices.size(); ++i) {
			a->pcs.push_back(pc[indices[i]]);
		}
		memcpy(&a->pc, &pc[indices[0]], sizeof(playlistdata0));
	}
	CWnd::PostMessage(0x118);
	const INT_PTR ret = a->DoModal();
	w_flg = TRUE;
	if (ret == IDOK) {
		if (!a->multiFile) {
			const int idx = indices[0];
			if (idx >= 0 && idx < playcnt) {
				_tcscpy(pc[idx].name, a->pc.name);
				_tcscpy(pc[idx].art, a->pc.art);
				_tcscpy(pc[idx].alb, a->pc.alb);
			}
		}
		else {
			const size_t n = (indices.size() < a->pcs.size()) ? indices.size() : a->pcs.size();
			for (size_t i = 0; i < n; ++i) {
				const int idx = indices[i];
				if (idx < 0 || idx >= playcnt) continue;
				_tcscpy(pc[idx].name, a->pcs[i].name);
				_tcscpy(pc[idx].art, a->pcs[i].art);
				_tcscpy(pc[idx].alb, a->pcs[i].alb);
			}
		}
		// 再生中曲の表示名も追従
		extern CString fnn;
		extern CString filen;
		if (pnt >= 0 && pnt < playcnt) {
			CString cur = PlPhysicalMediaPath(filen);
			if (cur.IsEmpty()) cur = filen;
			CString row = PlPhysicalMediaPath(pc[pnt].fol);
			if (row.IsEmpty()) row = pc[pnt].fol;
			if (!cur.IsEmpty() && !row.IsEmpty() && _tcsicmp(cur, row) == 0)
				fnn = pc[pnt].name;
		}
		Save();
		m_lc.Invalidate(FALSE);
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->RefreshList(TRUE);
	}
	else {
		m_lc.Invalidate(FALSE);
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->m_list.Invalidate(FALSE);
	}
	delete a;
}

void CPlayList::OnFindUp()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	CString s;
	m_find.GetWindowText(s);
	s.MakeLower();
	if(s==_T("")) return;

	ClampPlaylistSelectionIndices(this);
	const int pnt2 = GetFuzzySearchAnchor(this);

	int flg=0;
	int i = -1;
	for(int k = pnt2 + 1; k < playcnt; k++){
		if(PlaylistItemMatchesKeyword(pc[k], s)) { i = k; flg = 1; break; }
	}

	if(flg){
		for(int k=0;k<playcnt;k++){
			m_lc.SetItemState(k,m_lc.GetItemState(k,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}

		pnt1=i;

		m_lc.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
		m_lc.EnsureVisible(i,FALSE);
	}
	RefreshNavControls();
	if (m_find.GetSafeHwnd())
		m_find.SetFocus();
}

void CPlayList::OnFindDown()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	CString s;
	m_find.GetWindowText(s);
	s.MakeLower();
	if(s==_T("")) return;

	ClampPlaylistSelectionIndices(this);
	const int pnt2 = GetFuzzySearchAnchor(this);

	int flg=0;
	int i = -1;
	for(int k = (pnt2 < 0 ? playcnt - 1 : pnt2 - 1); k >= 0; k--){
		if(PlaylistItemMatchesKeyword(pc[k], s)) { i = k; flg = 1; break; }
	}

	if(flg){
		for(int k=0;k<playcnt;k++){
			m_lc.SetItemState(k,m_lc.GetItemState(k,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
		pnt1=i;

		m_lc.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
		m_lc.EnsureVisible(i,FALSE);
	}
	RefreshNavControls();
	if (m_find.GetSafeHwnd())
		m_find.SetFocus();
}


void CPlayList::OnBnClickedCheck6mp3()
{
	extern CMediaPlayerDlg* mp;
	savedata.savecheck_mp3 = m_save_mp3.GetCheck() ? 1 : 0;
	// MP はマスター無しのため、サブONでマスターも立てる(OFF時はもう一方を見る)
	if (savedata.savecheck_mp3 || savedata.savecheck_dshow)
		savedata.savecheck = 1;
	else
		savedata.savecheck = m_savecheck.GetCheck() ? 1 : 0;
	if (m_savecheck.GetSafeHwnd())
		m_savecheck.SetCheck(savedata.savecheck);
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_savemp3.GetSafeHwnd())
		mp->m_savemp3.SetCheck(savedata.savecheck_mp3);
	MpPersistSavedataQuick();
}


void CPlayList::OnBnClickedCheck7dshow()
{
	extern CMediaPlayerDlg* mp;
	savedata.savecheck_dshow = m_save_kpi.GetCheck() ? 1 : 0;
	if (savedata.savecheck_mp3 || savedata.savecheck_dshow)
		savedata.savecheck = 1;
	else
		savedata.savecheck = m_savecheck.GetCheck() ? 1 : 0;
	if (m_savecheck.GetSafeHwnd())
		m_savecheck.SetCheck(savedata.savecheck);
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_saveds.GetSafeHwnd())
		mp->m_saveds.SetCheck(savedata.savecheck_dshow);
	MpPersistSavedataQuick();
}


HBRUSH CPlayList::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{

	HBRUSH hbr = CCustomControlUtility::ApplyControlColors(pDC, pWnd, nCtlColor);
	if (hbr)
		return hbr;

	hbr = CCustomBlurDialogBase::OnCtlColor(pDC, pWnd, nCtlColor);

	// TODO: ここで DC の属性を変更してください。
	if (savedata.aero == 2) {
		if (nCtlColor == CTLCOLOR_DLG)
		{
			return m_brDlg;
		}
		if (nCtlColor == CTLCOLOR_STATIC)
		{
			SetBkMode(pDC->m_hDC, TRANSPARENT);
			return m_brDlg;
		}
	}
	// TODO: 既定値を使用したくない場合は別のブラシを返します。
	return hbr;
}


void CPlayList::RefreshNavControls()
{
	if (GetSafeHwnd()) {
		CCC_GroupBoxesBack(m_hWnd);
		const HWND topCtrls[] = {
			m_finddown.GetSafeHwnd(),
			m_findup.GetSafeHwnd(),
			m_find.GetSafeHwnd(),
		};
		for (HWND h : topCtrls) {
			if (h && ::IsWindow(h))
				::SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}
	}
	if (m_find.GetSafeHwnd())
		m_find.RepaintClient();
	if (m_e.GetSafeHwnd())
		m_e.RepaintClient();

	const CWnd* btns[] = {
		&m_lsup, &m_lup, &m_lsdown, &m_ldown,
		&m_findup, &m_finddown,
		&m_namechage, &m_listdelete, &m_pianorollBtn
	};
	for (const CWnd* p : btns)
	{
		if (p && p->GetSafeHwnd())
			CCC_ForceRepaintHwnd(p->m_hWnd);
	}
}

void CPlayList::ScheduleRefreshNavControls()
{
	if (!GetSafeHwnd())
		return;
	SetTimer(kPlayListNavRefreshTimer, 50, NULL);
}

void CPlayList::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogBase::OnShowWindow(bShow, nStatus);
	// メディアプレイヤーモード中はプレイリスト単独ウィンドウを出さない(裏で生かすのみ)
	if (bShow && savedata.playerMode == 1 && GetSafeHwnd()) {
		ShowWindow(SW_HIDE);
		return;
	}
	if (bShow)
		ScheduleRefreshNavControls();
	UNREFERENCED_PARAMETER(nStatus);
}

#if CCUSTOM_AERO_SUPPORT
LRESULT CPlayList::OnReapplyOpaqueFixers(WPARAM wParam, LPARAM lParam)
{
	LRESULT r = CCustomBlurDialogBase::OnReapplyOpaqueFixers(wParam, lParam);
	ScheduleRefreshNavControls();
	return r;
}
#endif

LRESULT CPlayList::OnPlMissDone(WPARAM wParam, LPARAM lParam)
{
	PlMissJob* job = (PlMissJob*)lParam;
	InterlockedExchange(&s_plMissBusy, 0);
	if (!job) return 0;
	const LONG gen = (LONG)wParam;
	const BOOL accept = (gen == s_plMissGen) && s_plMiss && pc
		&& job->count > 0 && job->indices && job->result;
	if (accept) {
		for (int k = 0; k < job->count; ++k) {
			const int i = job->indices[k];
			if (i >= 0 && i < s_plMissCap && i < playcnt)
				s_plMiss[i] = job->result[k] ? 1 : 0;
		}
		s_plMissDoneCnt = playcnt;
		if (::IsWindow(m_lc.GetSafeHwnd()))
			m_lc.Invalidate(FALSE);
	}
	else {
		s_plMissDoneCnt = -1;
	}
	free(job->indices);
	free(job->subs);
	free(job->fols);
	free(job->result);
	free(job);
	return 0;
}

LRESULT CPlayList::OnPlJakDone(WPARAM wParam, LPARAM lParam)
{
	PlJakJob* job = (PlJakJob*)lParam;
	s_plJakPend[0] = 0;
	InterlockedExchange(&s_plJakBusy, 0);
	if (!job) return 0;
	const LONG gen = (LONG)wParam;
	const BOOL accept = (gen == s_plJakGen) && job->path[0];
	if (accept) {
		PlJakMemInit();
		int freeSlot = -1, lruSlot = 0;
		DWORD lruTick = 0xFFFFFFFF;
		int hit = -1;
		BOOL didImage = FALSE;
		for (int i = 0; i < kPlJakN; ++i) {
			if (s_plJakKey[i][0] && _tcsicmp(s_plJakKey[i], job->path) == 0) { hit = i; break; }
			if (!s_plJakKey[i][0] && freeSlot < 0) freeSlot = i;
			if (s_plJakTick[i] < lruTick) { lruTick = s_plJakTick[i]; lruSlot = i; }
		}
		if (job->noneFlag || !job->hb) {
			PlJakMarkNone(job->path);
			if (hit >= 0) {
				if (s_plJakBmp[hit]) { ::DeleteObject(s_plJakBmp[hit]); s_plJakBmp[hit] = NULL; }
				s_plJakKey[hit][0] = 0;
				s_plJakTick[hit] = 0;
			}
			if (job->hb) { ::DeleteObject(job->hb); job->hb = NULL; }
		}
		else {
			int slot = (hit >= 0) ? hit : freeSlot;
			if (slot < 0 && !job->bgOnly)
				slot = lruSlot;
			if (slot < 0) {
				if (job->hb) { ::DeleteObject(job->hb); job->hb = NULL; }
			}
			else {
				if (s_plJakBmp[slot] && s_plJakBmp[slot] != job->hb) {
					::DeleteObject(s_plJakBmp[slot]);
					s_plJakBmp[slot] = NULL;
				}
				_tcsncpy_s(s_plJakKey[slot], job->path, _TRUNCATE);
				s_plJakBmp[slot] = job->hb;
				job->hb = NULL;
				s_plJakTick[slot] = GetTickCount();
				didImage = TRUE;
				PlJakMarkDiskSettled(job->path);
			}
		}
		for (int ni = 0; ni < s_plJakNoDiskN; ++ni) {
			if (_tcsicmp(s_plJakNoDisk[ni], job->path) == 0) {
				s_plJakNoDisk[ni][0] = 0;
				break;
			}
		}
		if (didImage && ::IsWindow(m_lc.GetSafeHwnd()) && job->row >= 0) {
			CRect rIcon;
			if (m_lc.GetItemRect(job->row, &rIcon, LVIR_BOUNDS)) {
				rIcon.right = rIcon.left + kPlJakPx + 28;
				m_lc.RedrawWindow(&rIcon, NULL, RDW_INVALIDATE | RDW_NOERASE);
			}
		}
		// 本編 Adopt/LoadJacket は UI 飢餓の元。曲切替側に任せる。
	}
	else if (job->hb) {
		::DeleteObject(job->hb);
		job->hb = NULL;
	}
	free(job);
	return 0;
}


void CPlayList::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogBase::OnMoving(fwSide, pRect);
	CRect r;
	GetWindowRect(&r);
	if (playbase)
	playbase->MoveWindow(&r);
//	if (playbase)
//		::SetWindowPos(playbase->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
//	::SetWindowPos(og->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	// TODO: ここにメッセージ ハンドラー コードを追加します。
}


void CPlayList::OnSizing(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogBase::OnSizing(fwSide, pRect);
	CRect r;
	GetWindowRect(&r);
	if(playbase)
		playbase->MoveWindow(&r);
	// TODO: ここにメッセージ ハンドラー コードを追加します。
}


void CPlayList::OnSetFocus(CWnd* pOldWnd)
{
	CCustomBlurDialogBase::OnSetFocus(pOldWnd);
}


BOOL CPlayList::OnNcActivate(BOOL bActive)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
		// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	UINT_PTR aa = 0;
	DWORD aaa = 0;
	if (plw) {
		if (bActive && pl->m_saisyo.GetCheck()) {
		//	og->ShowWindow(SW_RESTORE);
		}
	}
	if (bActive) {
		aa = SetTimer(4927, 10, NULL);
		aaa = GetLastError();
		aaa = aaa;
		ScheduleRefreshNavControls();
	}
	else {
		//if(!bActive)
		//	SetTimer(4924, 10, NULL);
	}
	return CCustomBlurDialogBase::OnNcActivate(bActive);
}

BOOL changeflg = FALSE;
void CPlayList::OnCbnSelchangeCombo1()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	if (changeflg == TRUE) return;
	int num = m_listchange.GetCurSel();
	if (num < 0) return;
	// 起動時/同期時の同一選択通知で Save→Load を繰り返さない(時間列の消失防止)
	if (num == savedata.playlistnum && pc != NULL && playcnt > 0)
		return;
	// 再生中/一時停止中は filen・mode を維持(Load の RestoreSavedPlaybackRow で上書きしない)
	const BOOL keepPlayback = (plf != 0 || playf != 0) && filen.GetLength() > 0;
	CString keepFol, keepFnn;
	int keepMode = 0, keepModesub = 0, keepLoop1 = 0, keepLoop2 = 0, keepRet2 = 0;
	if (keepPlayback) {
		keepFol = filen;
		keepFnn = fnn;
		keepMode = mode;
		keepModesub = modesub;
		keepLoop1 = loop1;
		keepLoop2 = loop2;
		keepRet2 = ret2;
	}
	if (pc != NULL && playcnt > 0)
		Save();
	savedata.playlistnum = num;
	playcnt = 0;
	pnt = -1;
	pnt1 = -1;
	playlistdata0* tmp; tmp = pc;
	free(pc);
	pc = NULL;
	Load(!keepPlayback);
	if (pc == NULL) {
		pc = (playlistdata0*)malloc(sizeof(playlistdata0));
	}
	m_lc.SetItemCount(playcnt);
	for (int j = 0; j < playcnt; j++) pc[j].icon = 1;
	if (keepPlayback) {
		filen = keepFol;
		fnn = keepFnn;
		mode = keepMode;
		modesub = keepModesub;
		loop1 = keepLoop1;
		loop2 = keepLoop2;
		ret2 = keepRet2;
		int match = -1;
		for (int j = 0; j < playcnt; j++) {
			if (keepFol.CompareNoCase(pc[j].fol) == 0) {
				match = j;
				break;
			}
		}
		if (match >= 0) {
			plcnt = match;
			SIcon(match);
		}
		else {
			pnt = -1;
			plcnt = -1;
		}
	}
	ClampPlaylistSelectionIndices(this);
	m_lc.RedrawWindow();
	Save();

	loadplaylistname();
}

void CPlayList::loadplaylistname()
{
	m_listchange.ResetContent();
	int lcnt = 0;
	for (lcnt = 0;; lcnt++) {
		CString s;
		if (lcnt == 0)
			s.Format(L"playlistu.dat");
		else
			s.Format(L"playlistu%d.dat", lcnt);
		if (!PathFileExists(GetModulePath() + s))
			break;
	}
	if (lcnt >= 999) lcnt = 999;
	for (int ii = 0; ii < lcnt; ii++) {
		CString s, ss;
		ss = savedata.playlistname[ii];
		if (ss == "") {
			ss.Format(LL14(
				L"プレイリスト：%d",             /* 日本語 */
				L"Playlist: %d",               /* 英語 */
				L"Liste de lecture : %d",      /* フランス語 */
				L"Playlist: %d",               /* イタリア語 */
				L"Lista de reproducción: %d",   /* スペイン語 */
				L"플레이리스트: %d",             /* 韓国語 */
				L"播放列表：%d",               /* 中国語 */
				L"قائمة التشغيل: %d",          /* アラビア語 */
				L"Плейлист: %d",               /* ロシア語 */
				L"Wiedergabeliste: %d",        /* ドイツ語 */
				L"Lista de reprodução: %d",     /* ポルトガル語 */
				L"Afspeellijst: %d",           /* オランダ語 */
				L"Lista odtwarzania: %d",      /* ポーランド語 */
				L"Oynatma Listesi: %d"),       /* トルコ語 */
				ii + 1);
		}
		s.Format(L"%s", ss);
		m_listchange.AddString(s);
	}

	m_listchange.AddString(LL14(
		L"<新しいプレイリスト>",              /* 日本語 */
		L"<New playlist>",                   /* 英語 */
		L"<Nouvelle liste de lecture>",      /* フランス語 */
		L"<Nuova playlist>",                 /* イタリア語 */
		L"<Nueva lista de reproducción>",    /* スペイン語 */
		L"<새 플레이리스트>",                 /* 韓国語 */
		L"<新建播放列表>",                   /* 中国語 */
		L"<قائمة تشغيل جديدة>",              /* アラビア語 */
		L"<Новый плейлист>",                 /* ロシア語 */
		L"<Neue Wiedergabeliste>",           /* ドイツ語 */
		L"<Nova lista de reprodução>",       /* ポルトガル語 */
		L"<Nieuwe afspeellijst>",            /* オランダ語 */
		L"<Nowa lista odtwarzania>",         /* ポーランド語 */
		L"<Yeni oynatma listesi>"));         /* トルコ語 */

	changeflg = TRUE;
	m_listchange.SetCurSel(savedata.playlistnum);
	int num = m_listchange.GetCurSel();
	if (num != savedata.playlistnum) {
		savedata.playlistnum = 0;
		m_listchange.SetCurSel(savedata.playlistnum);
	}
	changeflg = FALSE;
}

CString CPlayList::GetModulePath()
{
	// 実行ファイルのパス
	CString modulePath = _T("");
	// ドライブ名、ディレクトリ名、ファイル名、拡張子
	wchar_t path[_MAX_PATH], drive[_MAX_PATH], dir[_MAX_PATH], fname[_MAX_PATH], ext[_MAX_PATH];

	// 実行ファイルのファイルパスを取得
	if (::GetModuleFileName(NULL, path, _MAX_PATH) != 0)
	{
		// ファイルパスを分割
		::_wsplitpath_s(path, drive, dir, fname, ext);
		// ドライブとディレクトリ名を結合して実行ファイルパスとする
		modulePath = CString(drive) + CString(dir);
	}

	return modulePath;
}

void CPlayList::OnBnClickedButton3()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CPlayListNew pn;
	pn.name = savedata.playlistname[savedata.playlistnum];
	if (pn.name == L"") pn.name = GetPlaylistDisplayName(savedata.playlistnum);
	if (pn.DoModal() == IDOK) {
		CString newName = pn.name;
		newName.Trim();
		// 他プレイリスト名(および未命名の表示名 / #N)と重複なら拒否
		if (!newName.IsEmpty()) {
			const int plCnt = GetPlaylistFileCount();
			const int cur = savedata.playlistnum;
			for (int i = 0; i < plCnt; i++) {
				if (i == cur) continue;
				CString other = savedata.playlistname[i];
				CString otherKey = other;
				if (otherKey.IsEmpty())
					otherKey.Format(L"#%d", i);
				CString otherDisp = GetPlaylistDisplayName(i);
				if (other.CompareNoCase(newName) == 0
					|| otherKey.CompareNoCase(newName) == 0
					|| otherDisp.CompareNoCase(newName) == 0) {
					AfxMessageBox(LL14(
						L"同じ名前のプレイリストが既にあります。",
						L"A playlist with the same name already exists.",
						L"Une liste portant le meme nom existe deja.",
						L"Esiste gia una playlist con lo stesso nome.",
						L"Ya existe una lista con el mismo nombre.",
						L"같은 이름의 플레이리스트가 이미 있습니다.",
						L"已存在同名播放列表。",
						L"توجد بالفعل قائمة بنفس الاسم.",
						L"Плейлист с таким именем уже существует.",
						L"Eine Playlist mit diesem Namen existiert bereits.",
						L"Ja existe uma playlist com o mesmo nome.",
						L"Er bestaat al een playlist met dezelfde naam.",
						L"Playlist o tej nazwie juz istnieje.",
						L"Ayni isimde bir calma listesi zaten var."),
						MB_ICONWARNING | MB_OK);
					return;
				}
			}
		}
		// 曲ごと保存パラメータのキー(リスト名)を旧名→新名へ移行
		// (未命名 "#N" や旧表示名エイリアスも SongParams_RenameList 側で走査)
		CString oldKey = SongParams_CurrentListName();
		wcscpy(savedata.playlistname[savedata.playlistnum], newName);
		CString newKey = SongParams_CurrentListName();
		if (oldKey.CompareNoCase(newKey) != 0)
			SongParams_RenameList(oldKey, newKey);
		loadplaylistname();
	}

}


void CPlayList::OnBnClickedPlaydelete()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	if (MessageBox(LL14(
		L"現在のリストを削除しますがよろしいですか？", /* 日本語 */
		L"Are you sure you want to delete the current list?", /* 英語 */
		L"Voulez-vous vraiment supprimer la liste actuelle ?", /* フランス語 */
		L"Eliminare la lista corrente?", /* イタリア語 */
		L"¿Eliminar la lista actual?", /* スペイン語 */
		L"현재 리스트를 삭제하시겠습니까?", /* 韓国語 */
		L"确定要删除当前列表吗？", /* 中国語 */
		L"هل أنت متأكد من حذف القائمة الحالية؟", /* アラビア語 */
		L"Вы действительно хотите удалить текущий список?", /* ロシア語 */
		L"Aktuelle Liste löschen?", /* ドイツ語 */
		L"Excluir a lista atual?", /* ポルトガル語 */
		L"Huidige lijst verwijderen?", /* オランダ語 */
		L"Czy na pewno chcesz usunąć bieżącą listę?", /* ポーランド語 */
		L"Mevcut liste silinsin mi?"), /* トルコ語 */
		LL14(
			L"削除確認", /* 日本語タイトル */
			L"Confirm Delete",
			L"Confirmer la suppression",
			L"Conferma eliminazione",
			L"Confirmar eliminación",
			L"삭제 확인",
			L"确认删除",
			L"تأكيد الحذف",
			L"Подтверждение удаления",
			L"Löschung bestätigen",
			L"Confirmar exclusão",
			L"Verwijdering bevestigen",
			L"Potwierdź usunięcie",
			L"Silmeyi Onayla"), /* トルコ語タイトル */
		MB_YESNO) == IDNO) {
		return;
	}
	// 削除するリストに紐づく曲ごと保存パラメータも破棄(名前でキー)
	const int numDel = m_listchange.GetCurSel();
	CString deletedKey = SongParams_CurrentListName();
	SongParams_DeleteList(deletedKey);
	changeflg = TRUE;
	Save();
	CString s;
	int num = numDel;

	int lcnt = 0;
	for (lcnt = 0;; lcnt++) {
		CString s;
		if (lcnt == 0)
			s.Format(L"playlistu.dat");
		else
			s.Format(L"playlistu%d.dat", lcnt);
		if (!PathFileExists(GetModulePath() + s))
			break;
	}
	if (lcnt >= 999) lcnt = 999;

	// インデックス繰り上げに伴い、未命名キー "#N" を詰め直す
	// (カスタム名のリストは名前キーのままなので不要)
	if (lcnt > 1 && numDel >= 0 && numDel < lcnt) {
		for (int j = numDel; j < lcnt - 1; j++) {
			const int jj = j + 1;
			if (savedata.playlistname[jj][0] != 0)
				continue;
			CString oldK, newK;
			oldK.Format(L"#%d", jj);
			newK.Format(L"#%d", j);
			SongParams_RenameList(oldK, newK);
		}
	}

	if (lcnt == 0 && num == 0) {//まだ追加してなくて、一番最初のを削除されたとき、
		m_listchange.ResetContent();
		s.Format(L"playlistu.dat");
		savedata.playlistname[0][0] = 0;
		CFile::Remove(GetModulePath() + s);
		savedata.playlistnum = 0;
		Load();
		savedata.playlistnum = 0;
		loadplaylistname();
		changeflg = FALSE;
		return;
	}

	if (lcnt != 0) {//削除されたとき
		CString s1, s2;
		if (num == 0)
			s1.Format(L"playlistu.dat");
		else
			s1.Format(L"playlistu%d.dat", num);
		if (PathFileExists(GetModulePath() + s1))
			CFile::Remove(GetModulePath() + s1);
		for (int j = num; j < lcnt - 1; j++) {
			if (j == 0)
				s1.Format(L"playlistu.dat");
			else
				s1.Format(L"playlistu%d.dat", j);
			const int jj = j + 1;
			if (jj == 0)
				s2.Format(L"playlistu.dat");
			else
				s2.Format(L"playlistu%d.dat", jj);

			CFile::Rename(GetModulePath() + s2, GetModulePath() + s1);
			wcscpy(savedata.playlistname[j], savedata.playlistname[jj]);
		}
		lcnt = 0;
		for (lcnt = 0;; lcnt++) {
			CString s;
			if (lcnt == 0)
				s.Format(L"playlistu.dat");
			else
				s.Format(L"playlistu%d.dat", lcnt);
			if (!PathFileExists(GetModulePath() + s))
				break;
		}
		if (lcnt >= 999) lcnt = 999;

		savedata.playlistname[lcnt][0] = 0;
		if (num == lcnt) {
			savedata.playlistnum = 0;
			num = 0;
		}
		loadplaylistname();
		int num = m_listchange.GetCurSel();
		savedata.playlistnum = num;
		playcnt = 0;
		pnt = -1;
		pnt1 = -1;
		playlistdata0* tmp; tmp = pc;
		free(pc);
		pc = NULL;
		Load();
		if (pc == NULL) {
			pc = (playlistdata0*)malloc(sizeof(playlistdata0));
		}
		m_lc.SetItemCount(playcnt);
		for (int j = 0; j < playcnt; j++) pc[j].icon = 1;
		ClampPlaylistSelectionIndices(this);
		m_lc.RedrawWindow();
		Save();
		changeflg = FALSE;
		return;
	}
}

void CPlayList::OnBnClickedPianoroll()
{
	if (og) {
		og->TogglePianoRoll();
	}
}

