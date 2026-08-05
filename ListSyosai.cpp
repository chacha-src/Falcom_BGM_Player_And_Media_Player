// ListSyosai.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "ListCtrlA.h"
#include "PlayList.h"
#include "ListSyosai.h"
#include "FileTagInfo.h"
#include "SongParams.h"
#include <shlwapi.h>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

// ワードラップ解除コールバック(前方宣言)
static int CALLBACK EditWordBreakProc(LPTSTR lpch, int ichCurrent, int cch, int code);

namespace {

class CSyHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_SY_HELP };
	explicit CSyHelpDlg(CWnd* pParent = nullptr)
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

static CSyHelpDlg* g_syHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CSyHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CSyHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"ファイル情報操作ガイド", L"File Info Guide", L"Guide infos fichier", L"Guida info file",
		L"Guía info. archivo", L"파일 정보 가이드", L"文件信息指南", L"دليل معلومات الملف",
		L"Руководство сведений о файле", L"Dateiinfo-Anleitung", L"Guia info. arquivo", L"Bestandsinfo-gids",
		L"Przewodnik info. o pliku", L"Dosya bilgisi kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CSyHelpDlg::OnOK() { DestroyWindow(); }
void CSyHelpDlg::OnCancel() { DestroyWindow(); }
void CSyHelpDlg::OnClose() { DestroyWindow(); }

void CSyHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_syHelpDlg == this)
		g_syHelpDlg = nullptr;
	delete this;
}

BOOL CSyHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CSyHelpDlg::OnPaint()
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
	title(L, y, LL14(L"ファイル情報操作ガイド", L"File Info — Guide", L"Guide infos fichier", L"Guida info file",
		L"Guía info. archivo", L"파일 정보 가이드", L"文件信息指南", L"دليل معلومات الملف",
		L"Сведения о файле — руководство", L"Dateiinfo-Guide", L"Guia info. arquivo", L"Bestandsinfo-gids",
		L"Info. o pliku — przewodnik", L"Dosya bilgisi kılavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"プレイリスト表示とファイルタグを分けて編集できます。OK で PL 側を保存します。",
		L"Edit playlist display and file tags separately. OK saves playlist fields.",
		L"Éditer affichage playlist et tags séparément. OK = playlist.",
		L"Modifica playlist e tag separatamente. OK salva la playlist.",
		L"Edite lista y etiquetas por separado. OK guarda la lista.",
		L"재생 목록 표시와 파일 태그를 나눠 편집. OK로 PL 쪽 저장.",
		L"可分别编辑播放列表显示与文件标签。确定保存播放列表侧。",
		L"حرّر عرض القائمة والوسوم منفصلين. OK يحفظ القائمة.",
		L"Редактируйте плейлист и теги отдельно. OK сохраняет плейлист.",
		L"Playlist-Anzeige und Tags getrennt. OK speichert Playlist.",
		L"Edite lista e tags separadamente. OK salva a playlist.",
		L"Bewerk afspeellijst en tags apart. OK slaat playlist op.",
		L"Edytuj listę i tagi osobno. OK zapisuje playlistę.",
		L"Çalma listesi ve etiketleri ayrı düzenle. Tamam PL kaydeder."));
	y += lh + 4;

	title(L, y, LL14(L"プレイリスト欄 vs タグ", L"Playlist fields vs tags", L"Playlist vs tags", L"Playlist vs tag",
		L"Lista vs etiquetas", L"재생 목록 vs 태그", L"播放列表栏 vs 标签", L"الحقول vs الوسوم",
		L"Плейлист vs теги", L"Playlist vs Tags", L"Playlist vs tags", L"Afspeellijst vs tags",
		L"Playlista vs tagi", L"Liste vs etiketler"));
	y += titleLh;
	body(L, y, LL14(
		L"・上段(名前/アーティスト/アルバム/パス) …… プレイリスト表示。OK で保存",
		L"· Top (name/artist/album/path) …… playlist display. Saved with OK",
		L"· Haut (nom/artiste/album/chemin) …… affichage playlist. OK",
		L"· Alto (nome/artista/album/percorso) …… playlist. OK",
		L"· Superior (nombre/artista/álbum/ruta) …… lista. OK",
		L"· 상단(이름/아티스트/앨범/경로) …… 재생 목록 표시. OK로 저장",
		L"· 上段（名称/艺术家/专辑/路径）…… 播放列表显示。确定保存",
		L"· أعلى (اسم/فنان/ألبوم/مسار) …… عرض القائمة. يُحفظ مع OK",
		L"· Верх (имя/исполнитель/альбом/путь) …… плейлист. OK",
		L"· Oben (Name/Artist/Album/Pfad) …… Playlist. Mit OK speichern",
		L"· Topo (nome/artista/álbum/caminho) …… playlist. OK",
		L"· Boven (naam/artiest/album/pad) …… afspeellijst. OK",
		L"· Góra (nazwa/artysta/album/ścieżka) …… playlista. OK",
		L"· Üst (ad/sanatçı/albüm/yol) …… çalma listesi. Tamam ile kaydet")); y += lh;
	body(L, y, LL14(
		L"・中段(年/Track/ジャンル/コメント) …… ファイルタグ。タグ書込でファイルへ",
		L"· Middle (year/track/genre/comment) …… file tags. Write tag → file",
		L"· Milieu (année/piste/genre/commentaire) …… tags. Écrire → fichier",
		L"· Centro (anno/traccia/genere/commento) …… tag. Scrivi → file",
		L"· Medio (año/pista/género/comentario) …… etiquetas. Escribir → archivo",
		L"· 중단(연도/트랙/장르/코멘트) …… 파일 태그. 태그 쓰기로 파일에",
		L"· 中段（年/曲目/流派/注释）…… 文件标签。写入标签到文件",
		L"· وسط (سنة/مسار/نوع/تعليق) …… وسوم. الكتابة → الملف",
		L"· Середина (год/трек/жанр/коммент.) …… теги. Запись → файл",
		L"· Mitte (Jahr/Track/Genre/Kommentar) …… Tags. Schreiben → Datei",
		L"· Meio (ano/faixa/gênero/comentário) …… tags. Gravar → arquivo",
		L"· Midden (jaar/track/genre/opmerking) …… tags. Schrijven → bestand",
		L"· Środek (rok/utwór/gatunek/komentarz) …… tagi. Zapisz → plik",
		L"· Orta (yıl/parça/tür/yorum) …… etiketler. Yaz → dosya")); y += lh;
	body(L, y, LL14(
		L"・タグ→PL …… タグのタイトル/アーティスト/アルバムを上段へコピー",
		L"· Tag→PL …… copy tag title/artist/album into playlist fields",
		L"· Tag→PL …… copier titre/artiste/album vers la playlist",
		L"· Tag→PL …… copia titolo/artista/album nella playlist",
		L"· Tag→PL …… copiar título/artista/álbum a la lista",
		L"· 태그→PL …… 태그 제목/아티스트/앨범을 상단으로 복사",
		L"· 标签→列表 …… 将标签标题/艺术家/专辑复制到上段",
		L"· وسم→قائمة …… نسخ العنوان/الفنان/الألبوم إلى الأعلى",
		L"· Тег→PL …… копировать название/исполнителя/альбом вверх",
		L"· Tag→PL …… Titel/Artist/Album nach oben kopieren",
		L"· Tag→PL …… copiar título/artista/álbum para cima",
		L"· Tag→PL …… titel/artiest/album naar boven kopiëren",
		L"· Tag→PL …… kopiuj tytuł/artystę/album w górę",
		L"· Etiket→PL …… başlık/sanatçı/albümü üste kopyala")); y += lh + 4;

	title(L, y, LL14(L"タグの読み書き", L"Tag read / write", L"Lecture / écriture tags", L"Lettura / scrittura tag",
		L"Lectura / escritura de etiquetas", L"태그 읽기/쓰기", L"标签读写", L"قراءة/كتابة الوسوم",
		L"Чтение / запись тегов", L"Tags lesen / schreiben", L"Leitura / gravação de tags", L"Tags lezen / schrijven",
		L"Odczyt / zapis tagów", L"Etiket oku / yaz"));
	y += titleLh;
	body(L, y, LL14(
		L"・再読込 …… ファイルからタグを再取得（編集中のタグ欄を上書き）",
		L"· Reload …… re-read tags from file (overwrites tag fields)",
		L"· Recharger …… relire les tags (écrase les champs)",
		L"· Ricarica …… rileggi i tag (sovrascrive i campi)",
		L"· Recargar …… releer etiquetas (sobrescribe campos)",
		L"· 다시 읽기 …… 파일에서 태그를 다시 읽음(태그란 덮어씀)",
		L"· 重新加载 …… 从文件重读标签（覆盖标签栏）",
		L"· إعادة تحميل …… إعادة قراءة الوسوم (تستبدل الحقول)",
		L"· Обновить …… перечитать теги (перезапишет поля)",
		L"· Neu laden …… Tags neu lesen (überschreibt Felder)",
		L"· Recarregar …… reler tags (sobrescreve campos)",
		L"· Herladen …… tags opnieuw lezen (overschrijft velden)",
		L"· Wczytaj …… odczytaj tagi ponownie (nadpisuje pola)",
		L"· Yenile …… dosyadan etiketleri yeniden oku (alanları yazar)")); y += lh;
	body(L, y, LL14(
		L"・タグ書込 …… 表示中のメタデータをファイルへ。MP3/FLAC/WAV/M4A/Ogg 等",
		L"· Write tag …… save displayed metadata to file. MP3/FLAC/WAV/M4A/Ogg…",
		L"· Écrire tag …… métadonnées → fichier. MP3/FLAC/WAV/M4A/Ogg…",
		L"· Scrivi tag …… metadati → file. MP3/FLAC/WAV/M4A/Ogg…",
		L"· Escribir tag …… metadatos → archivo. MP3/FLAC/WAV/M4A/Ogg…",
		L"· 태그 쓰기 …… 표시 메타데이터를 파일에. MP3/FLAC/WAV/M4A/Ogg 등",
		L"· 写入标签 …… 将显示的元数据写入文件。MP3/FLAC/WAV/M4A/Ogg 等",
		L"· كتابة وسم …… البيانات المعروضة → الملف. MP3/FLAC/WAV/M4A/Ogg…",
		L"· Записать тег …… метаданные → файл. MP3/FLAC/WAV/M4A/Ogg…",
		L"· Tag schreiben …… Metadaten → Datei. MP3/FLAC/WAV/M4A/Ogg…",
		L"· Gravar tag …… metadados → arquivo. MP3/FLAC/WAV/M4A/Ogg…",
		L"· Tag schrijven …… metadata → bestand. MP3/FLAC/WAV/M4A/Ogg…",
		L"· Zapisz tag …… metadane → plik. MP3/FLAC/WAV/M4A/Ogg…",
		L"· Etiket yaz …… görünen metadata → dosya. MP3/FLAC/WAV/M4A/Ogg…")); y += lh + 4;

	title(L, y, LL14(L"ループ欄", L"Loop fields", L"Champs de boucle", L"Campi loop",
		L"Campos de bucle", L"루프 란", L"循环字段", L"حقول التكرار",
		L"Поля цикла", L"Schleifenfelder", L"Campos de loop", L"Loopvelden",
		L"Pola pętli", L"Döngü alanları"));
	y += titleLh;
	body(L, y, LL14(
		L"・開始/終了はサンプル単位。OK でプレイリストへ保存されます",
		L"· Start/end are in samples. Saved to the playlist with OK",
		L"· Début/fin en échantillons. Enregistrés avec OK",
		L"· Inizio/fine in campioni. Salvati con OK",
		L"· Inicio/fin en muestras. Se guardan con OK",
		L"· 시작/종료는 샘플 단위. OK로 재생 목록에 저장",
		L"· 开始/结束以采样为单位。确定保存到播放列表",
		L"· البداية/النهاية بالعينات. تُحفظ مع OK",
		L"· Начало/конец в сэмплах. Сохраняется по OK",
		L"· Start/Ende in Samples. Mit OK in Playlist speichern",
		L"· Início/fim em amostras. Salvos com OK",
		L"· Start/einde in samples. Opslaan met OK",
		L"· Start/koniec w próbkach. Zapis po OK",
		L"· Başlangıç/bitiş örnek cinsinden. Tamam ile kaydet")); y += lh + 4;

	title(L, y, LL14(L"パス操作", L"Path actions", L"Actions chemin", L"Azioni percorso",
		L"Acciones de ruta", L"경로 조작", L"路径操作", L"إجراءات المسار",
		L"Действия с путём", L"Pfad-Aktionen", L"Ações de caminho", L"Padacties",
		L"Akcje ścieżki", L"Yol işlemleri"));
	y += titleLh;
	body(L, y, LL14(
		L"・Explorer …… エクスプローラーでファイルを選択表示（無ければフォルダ）",
		L"· Explorer …… select the file in Explorer (folder if missing)",
		L"· Explorateur …… sélectionner le fichier (dossier sinon)",
		L"· Esplora …… seleziona il file (cartella se assente)",
		L"· Explorador …… seleccionar el archivo (carpeta si falta)",
		L"· 탐색기 …… 탐색기에서 파일 선택 표시(없으면 폴더)",
		L"· 资源管理器 …… 在资源管理器中选中文件（否则打开文件夹）",
		L"· المستكشف …… تحديد الملف (المجلد إن لم يوجد)",
		L"· Проводник …… выделить файл (папку, если нет)",
		L"· Explorer …… Datei auswählen (Ordner falls fehlend)",
		L"· Explorer …… selecionar o arquivo (pasta se ausente)",
		L"· Verkenner …… bestand selecteren (map als ontbreekt)",
		L"· Eksplorator …… zaznacz plik (folder jeśli brak)",
		L"· Gezgin …… dosyayı seç (yoksa klasör)")); y += lh;
	body(L, y, LL14(
		L"・パスコピー / 名コピー …… クリップボードへ。参照でパス差し替えも可",
		L"· Copy path / name …… to clipboard. Browse can replace the path",
		L"· Copier chemin / nom …… presse-papiers. Parcourir remplace",
		L"· Copia percorso / nome …… appunti. Sfoglia sostituisce",
		L"· Copiar ruta / nombre …… portapapeles. Examinar reemplaza",
		L"· 경로/이름 복사 …… 클립보드로. 참조로 경로 교체 가능",
		L"· 复制路径/文件名 …… 到剪贴板。浏览可替换路径",
		L"· نسخ المسار/الاسم …… إلى الحافظة. الاستعراض يستبدل",
		L"· Копировать путь / имя …… в буфер. Обзор заменяет путь",
		L"· Pfad/Name kopieren …… Zwischenablage. Durchsuchen ersetzt",
		L"· Copiar caminho / nome …… área de transferência. Procurar troca",
		L"· Pad/naam kopiëren …… klembord. Bladeren vervangt pad",
		L"· Kopiuj ścieżkę / nazwę …… schowek. Przeglądaj zamienia",
		L"· Yol/ad kopyala …… panoya. Gözat yolu değiştirir")); y += lh + 4;

	title(L, y, LL14(L"再生詳細", L"Playback details", L"Détails lecture", L"Dettagli riproduzione",
		L"Detalles reproducción", L"재생 상세", L"播放详情", L"تفاصيل التشغيل",
		L"Детали воспроизведения", L"Wiedergabedetails", L"Detalhes de reprodução", L"Afspeeldetails",
		L"Szczegóły odtwarzania", L"Oynatma ayrıntıları"));
	y += titleLh;
	body(L, y, LL14(
		L"・再生詳細 …… ループ/キュー/タグ等の詳細ダイアログを開きます",
		L"· Playback details …… opens loop/cues/tags detail dialog",
		L"· Détails lecture …… ouvre boucle/repères/tags",
		L"· Dettagli …… apre loop/cue/tag",
		L"· Detalles …… abre bucle/cues/etiquetas",
		L"· 재생 상세 …… 루프/큐/태그 등 상세 대화상자를 엽니다",
		L"· 播放详情 …… 打开循环/标记/标签等详细对话框",
		L"· تفاصيل التشغيل …… يفتح حوار الحلقة/العلامات/الوسوم",
		L"· Детали …… открывает цикл/метки/теги",
		L"· Wiedergabedetails …… öffnet Schleife/Cues/Tags",
		L"· Detalhes …… abre loop/cues/tags",
		L"· Afspeeldetails …… opent loop/cues/tags",
		L"· Szczegóły …… otwiera pętlę/cue/tagi",
		L"· Oynatma ayrıntıları …… döngü/cue/etiket penceresini açar")); y += lh;
	muted(L, y, LL14(
		L"[SAV]削除は曲ごとの音量・EQ 等の記憶パラメータを消します。閉じるは保存しません。",
		L"Clear [SAV] removes per-track volume/EQ memory. Close does not save.",
		L"Effacer [SAV] supprime volume/EQ mémorisés. Fermer n'enregistre pas.",
		L"Cancella [SAV] rimuove volume/EQ salvati. Chiudi non salva.",
		L"Borrar [SAV] quita volumen/EQ guardados. Cerrar no guarda.",
		L"[SAV]삭제는 곡별 볼륨·EQ 기억을 지웁니다. 닫기는 저장하지 않습니다.",
		L"删除[SAV]会清除逐曲音量/EQ记忆。关闭不保存。",
		L"مسح [SAV] يحذف ذاكرة الصوت/EQ. الإغلاق لا يحفظ.",
		L"Удалить [SAV] стирает громкость/EQ трека. Закрыть не сохраняет.",
		L"[SAV] löschen entfernt Lautstärke/EQ. Schließen speichert nicht.",
		L"Limpar [SAV] remove volume/EQ. Fechar não salva.",
		L"[SAV] wissen verwijdert volume/EQ. Sluiten slaat niet op.",
		L"Usuń [SAV] kasuje głośność/EQ. Zamknij nie zapisuje.",
		L"[SAV] sil parça ses/EQ belleğini siler. Kapat kaydetmez."));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

// CListSyosai ダイアログ

IMPLEMENT_DYNAMIC(CListSyosai, CCustomBlurDialogBase)

CListSyosai::CListSyosai(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CListSyosai::IDD, pParent)
{
	ZeroMemory(&pc, sizeof(pc));
}

CListSyosai::~CListSyosai()
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.DestroyWindow();
}

void CListSyosai::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT1, m_name);
	DDX_Control(pDX, IDC_EDIT2, m_id);
	DDX_Control(pDX, IDC_EDIT3, m_game);
	DDX_Control(pDX, IDC_EDIT4, m_art);
	DDX_Control(pDX, IDC_EDIT5, m_alb);
	DDX_Control(pDX, IDC_EDIT6, m_fol);
	DDX_Control(pDX, IDOK999, m_ok2);
	DDX_Control(pDX, IDC_EDIT11, m_cmt);
	DDX_Control(pDX, IDC_EDIT7, m_year);
	DDX_Control(pDX, IDC_EDIT9, m_track);
	DDX_Control(pDX, IDC_EDIT10, m_j);
	DDX_Control(pDX, ID_OK, m_ok);
	DDX_Control(pDX, IDCANCEL, m_cancel);
	DDX_Control(pDX, IDC_EDIT12, m_time);
	DDX_Control(pDX, IDC_EDIT13, m_loop1);
	DDX_Control(pDX, IDC_EDIT14, m_loop2);
	DDX_Control(pDX, IDC_EDIT15, m_ret2);
	DDX_Control(pDX, IDC_SYOSAI_LBL_NAME, m_lblName);
	DDX_Control(pDX, IDC_SYOSAI_LBL_ID, m_lblId);
	DDX_Control(pDX, IDC_SYOSAI_LBL_GAME, m_lblGame);
	DDX_Control(pDX, IDC_SYOSAI_LBL_ART, m_lblArt);
	DDX_Control(pDX, IDC_SYOSAI_LBL_ALB, m_lblAlb);
	DDX_Control(pDX, IDC_SYOSAI_LBL_FILE, m_lblFile);
	DDX_Control(pDX, IDC_SYOSAI_LBL_YEAR, m_lblYear);
	DDX_Control(pDX, IDC_SYOSAI_LBL_TRACK, m_lblTrack);
	DDX_Control(pDX, IDC_SYOSAI_LBL_GENRE, m_lblGenre);
	DDX_Control(pDX, IDC_SYOSAI_LBL_CMT, m_lblCmt);
	DDX_Control(pDX, IDC_SYOSAI_LBL_TIME, m_lblTime);
	DDX_Control(pDX, IDC_SYOSAI_LBL_LOOP, m_lblLoop);
	DDX_Control(pDX, IDC_SYOSAI_LBL_RET2, m_lblRet2);
	DDX_Control(pDX, IDC_SYOSAI_LBL_STATUS, m_lblStatus);
	DDX_Control(pDX, IDC_SYOSAI_LBL_PARAM, m_lblParam);
	DDX_Control(pDX, IDC_SYOSAI_BTN_BROWSE, m_btnBrowse);
	DDX_Control(pDX, IDC_SYOSAI_BTN_TAG2PL, m_btnTag2Pl);
	DDX_Control(pDX, IDC_SYOSAI_BTN_RELOADTAG, m_btnReloadTag);
	DDX_Control(pDX, IDC_SYOSAI_BTN_WRITETAG, m_btnWriteTag);
	DDX_Control(pDX, IDC_SYOSAI_BTN_COPYPATH, m_btnCopyPath);
	DDX_Control(pDX, IDC_SYOSAI_BTN_COPYNAME, m_btnCopyName);
	DDX_Control(pDX, IDC_SYOSAI_BTN_PROTOOLS, m_btnProTools);
	DDX_Control(pDX, IDC_SYOSAI_BTN_CLEARPARAM, m_btnClearParam);
	DDX_Control(pDX, IDC_SY_HELP, m_help);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CListSyosai, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDOK999, &CListSyosai::OnBnClickedExplorer)
	ON_BN_CLICKED(ID_OK, &CListSyosai::OnBnClickedOk)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_BROWSE, &CListSyosai::OnBnClickedBrowse)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_TAG2PL, &CListSyosai::OnBnClickedTag2Pl)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_RELOADTAG, &CListSyosai::OnBnClickedReloadTag)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_WRITETAG, &CListSyosai::OnBnClickedWriteTag)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_COPYPATH, &CListSyosai::OnBnClickedCopyPath)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_COPYNAME, &CListSyosai::OnBnClickedCopyName)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_PROTOOLS, &CListSyosai::OnBnClickedProTools)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_CLEARPARAM, &CListSyosai::OnBnClickedClearParam)
	ON_BN_CLICKED(IDC_SY_HELP, &CListSyosai::OnBnClickedHelp)
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	cmn(CListSyosai);


// CListSyosai メッセージ ハンドラ

extern CPlayList* pl;
extern int plcnt;
extern int loop1, loop2;

static bool TrackMatchesPlaying(const playlistdata0& pc)
{
	if (!pl || plcnt < 0 || plcnt >= pl->playcnt)
		return false;
	const playlistdata0& cur = pl->pc[plcnt];
	if (_tcscmp(cur.fol, pc.fol) != 0 || cur.sub != pc.sub)
		return false;
	if (pc.sub == -10 || pc.sub == -2 || pc.sub == -3 || pc.sub == 30 || pc.sub == 999)
		return _tcscmp(cur.name, pc.name) == 0;
	return cur.ret2 == pc.ret2;
}

static void RefreshPcDetails(playlistdata0& pc)
{
	if (TrackMatchesPlaying(pc) && (loop1 || loop2)) {
		pc.loop1 = loop1;
		pc.loop2 = loop2;
	}

	if (pc.loop1 == 0 && pc.loop2 == 0) {
		FileTagFields tags;
		ReadFileTagFields(pc.fol, tags);
		pc.loop1 = tags.loop1;
		pc.loop2 = tags.loop2;
	}
}

static void CopyTextToClipboard(HWND hwnd, const CString& text)
{
	if (!::OpenClipboard(hwnd))
		return;
	::EmptyClipboard();
	const SIZE_T bytes = (SIZE_T)(text.GetLength() + 1) * sizeof(TCHAR);
	HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (h) {
		void* p = ::GlobalLock(h);
		if (p) {
			memcpy(p, (LPCTSTR)text, bytes);
			::GlobalUnlock(h);
#if defined(UNICODE) || defined(_UNICODE)
			::SetClipboardData(CF_UNICODETEXT, h);
#else
			::SetClipboardData(CF_TEXT, h);
#endif
		}
		else {
			::GlobalFree(h);
		}
	}
	::CloseClipboard();
}

static CString FormatFileSize(ULONGLONG bytes)
{
	CString s;
	if (bytes < 1024ull)
		s.Format(_T("%llu B"), bytes);
	else if (bytes < 1024ull * 1024ull)
		s.Format(_T("%.1f KB"), bytes / 1024.0);
	else if (bytes < 1024ull * 1024ull * 1024ull)
		s.Format(_T("%.2f MB"), bytes / (1024.0 * 1024.0));
	else
		s.Format(_T("%.2f GB"), bytes / (1024.0 * 1024.0 * 1024.0));
	return s;
}

CString CListSyosai::CurrentPathText() const
{
	CString s;
	if (m_fol.GetSafeHwnd())
		m_fol.GetWindowText(s);
	else
		s = pc.fol;
	s.Trim();
	return s;
}

void CListSyosai::CollectPlaylistFields()
{
	CString s;
	m_name.GetWindowText(s);
	_tcsncpy(pc.name, s, _countof(pc.name) - 1);
	pc.name[_countof(pc.name) - 1] = 0;
	m_art.GetWindowText(s);
	_tcsncpy(pc.art, s, _countof(pc.art) - 1);
	pc.art[_countof(pc.art) - 1] = 0;
	m_alb.GetWindowText(s);
	_tcsncpy(pc.alb, s, _countof(pc.alb) - 1);
	pc.alb[_countof(pc.alb) - 1] = 0;
	m_fol.GetWindowText(s);
	_tcsncpy(pc.fol, s, _countof(pc.fol) - 1);
	pc.fol[_countof(pc.fol) - 1] = 0;

	m_loop1.GetWindowText(s);
	pc.loop1 = _tstoi(s);
	m_loop2.GetWindowText(s);
	pc.loop2 = _tstoi(s);
}

void CListSyosai::CollectTagAndLoopFields(FileTagFields& tags, int& loopStart, int& loopEnd)
{
	tags.Clear();
	m_name.GetWindowText(tags.title);
	m_art.GetWindowText(tags.artist);
	m_alb.GetWindowText(tags.album);
	m_year.GetWindowText(tags.year);
	m_track.GetWindowText(tags.track);
	m_j.GetWindowText(tags.genre);
	m_cmt.GetWindowText(tags.comment);
	CString s;
	m_loop1.GetWindowText(s);
	loopStart = _tstoi(s);
	m_loop2.GetWindowText(s);
	loopEnd = _tstoi(s);
	tags.loop1 = loopStart > 0 ? loopStart : 0;
	tags.loop2 = loopEnd > 0 ? loopEnd : 0;
}

void CListSyosai::ApplyTagsToControls(const FileTagFields& tags, bool forceEmpty)
{
	if (forceEmpty || tags.year.GetLength()) m_year.SetWindowText(tags.year);
	if (forceEmpty || tags.track.GetLength()) m_track.SetWindowText(tags.track);
	if (forceEmpty || tags.genre.GetLength()) m_j.SetWindowText(tags.genre);
	if (forceEmpty || tags.comment.GetLength()) m_cmt.SetWindowText(tags.comment);
	if (tags.loop1 || tags.loop2) {
		CString s;
		s.Format(_T("%d"), tags.loop1);
		m_loop1.SetWindowText(s);
		s.Format(_T("%d"), tags.loop2);
		m_loop2.SetWindowText(s);
		pc.loop1 = tags.loop1;
		pc.loop2 = tags.loop2;
	}
}

void CListSyosai::RefreshStatusLines()
{
	if (IsBatchMode()) {
		CString s;
		s.Format(LL14(
			L"選択 %d 曲のアーティスト/アルバムを一括変更します。",
			L"Batch-edit artist/album for %d selected tracks.",
			L"Modification groupée artiste/album pour %d morceaux.",
			L"Modifica artist/album per %d brani selezionati.",
			L"Editar artista/álbum de %d pistas seleccionadas.",
			L"선택 %d곡의 아티스트/앨범을 일괄 변경합니다.",
			L"批量修改所选 %d 首的艺术家/专辑。",
			L"تعديل الفنان/الألبوم لـ %d مسارات.",
			L"Пакетное изменение исполнителя/альбома для %d треков.",
			L"Artist/Album für %d Titel gemeinsam ändern.",
			L"Editar artista/álbum de %d faixas selecionadas.",
			L"Artiest/album voor %d nummers wijzigen.",
			L"Zmiana artysty/albumu dla %d utworów.",
			L"%d seçili parçanın sanatçı/albümünü toplu değiştir."),
			(int)m_batchIndices.size());
		m_lblStatus.SetWindowText(s);
		m_lblParam.SetWindowText(_T(""));
		return;
	}

	CString path = CurrentPathText();
	CString phys = PlPhysicalMediaPath(path);
	if (phys.IsEmpty())
		phys = path;

	const BOOL exists = (!phys.IsEmpty() && PathFileExists(phys));
	CString ext;
	if (!phys.IsEmpty()) {
		ext = PathFindExtension(phys);
		ext.MakeLower();
	}
	CString sizeStr = _T("-");
	if (exists) {
		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		if (::GetFileAttributesEx(phys, GetFileExInfoStandard, &fad)) {
			ULARGE_INTEGER ul;
			ul.HighPart = fad.nFileSizeHigh;
			ul.LowPart = fad.nFileSizeLow;
			sizeStr = FormatFileSize(ul.QuadPart);
		}
	}

	CString existLabel = exists
		? LL14(L"存在する", L"Exists", L"Existe", L"Esiste", L"Existe", L"존재", L"存在", L"موجود", L"Есть", L"Vorhanden", L"Existe", L"Bestaat", L"Istnieje", L"Var")
		: LL14(L"欠損", L"Missing", L"Manquant", L"Mancante", L"Falta", L"없음", L"缺失", L"مفقود", L"Нет", L"Fehlt", L"Ausente", L"Ontbreekt", L"Brak", L"Yok");

	CString status;
	status.Format(_T("%s | %s | %s"),
		(LPCTSTR)existLabel,
		ext.IsEmpty() ? _T("?") : (LPCTSTR)ext,
		(LPCTSTR)sizeStr);
	m_lblStatus.SetWindowText(status);

	CString list = SongParams_CurrentListName();
	CString tip = SongParams_BuildTipExtra(list, path.IsEmpty() ? pc.fol : path, pc.sub, pc.ret2);
	const bool has = SongParams_HasEntry(list, path.IsEmpty() ? pc.fol : path, pc.sub, pc.ret2);
	CString param;
	if (has) {
		param = LL14(L"[SAV] 曲ごと設定あり", L"[SAV] Per-song settings saved", L"[SAV] Réglages/morceau", L"[SAV] Impost. per brano",
			L"[SAV] Ajustes por pista", L"[SAV] 곡별 설정 있음", L"[SAV] 有逐曲设置", L"[SAV] إعدادات لكل أغنية",
			L"[SAV] Есть настройки трека", L"[SAV] Pro-Titel-Einstellungen", L"[SAV] Config. por faixa",
			L"[SAV] Per-nummer-instellingen", L"[SAV] Ustawienia utworu", L"[SAV] Parça ayarları var");
		if (!tip.IsEmpty()) {
			param += _T(": ");
			param += tip;
		}
	}
	else {
		param = LL14(L"[SAV] なし（未保存）", L"[SAV] none (not saved)", L"[SAV] aucun (non sauvé)", L"[SAV] nessuno (non salvato)",
			L"[SAV] ninguno (no guardado)", L"[SAV] 없음(미저장)", L"[SAV] 无（未保存）", L"[SAV] لا (غير محفوظ)",
			L"[SAV] нет (не сохранено)", L"[SAV] keine (nicht gespeichert)", L"[SAV] nenhum (não salvo)",
			L"[SAV] geen (niet opgeslagen)", L"[SAV] brak (niezapisane)", L"[SAV] yok (kaydedilmedi)");
	}
	m_lblParam.SetWindowText(param);

	if (m_btnClearParam.GetSafeHwnd())
		m_btnClearParam.EnableWindow(has ? TRUE : FALSE);
	if (m_ok2.GetSafeHwnd()) {
		const bool canExplore = (phys.Find(_T('\\')) >= 0 || phys.Find(_T('/')) >= 0);
		m_ok2.EnableWindow(canExplore ? TRUE : FALSE);
	}
}

void CListSyosai::ApplyBatchUi()
{
	if (!IsBatchMode())
		return;

	SetWindowText(LL14(L"一括編集", L"Batch edit", L"Edition groupée", L"Modifica multipla", L"Edición por lotes",
		L"일괄 편집", L"批量编辑", L"تحرير جماعي", L"Пакетное прав.", L"Stapelbearbeitung",
		L"Edição em lote", L"Batchbewerking", L"Edycja zbiorcza", L"Toplu düzenleme"));

	// 一括は art/alb のみ。他は無効化。
	m_name.EnableWindow(FALSE);
	m_fol.EnableWindow(FALSE);
	m_year.EnableWindow(FALSE);
	m_track.EnableWindow(FALSE);
	m_j.EnableWindow(FALSE);
	m_cmt.EnableWindow(FALSE);
	m_loop1.EnableWindow(FALSE);
	m_loop2.EnableWindow(FALSE);
	m_btnBrowse.EnableWindow(FALSE);
	m_btnTag2Pl.EnableWindow(FALSE);
	m_btnReloadTag.EnableWindow(FALSE);
	m_btnWriteTag.EnableWindow(FALSE);
	m_btnCopyPath.EnableWindow(FALSE);
	m_btnCopyName.EnableWindow(FALSE);
	m_btnProTools.EnableWindow(FALSE);
	m_btnClearParam.EnableWindow(FALSE);
	m_ok2.EnableWindow(FALSE);

	if (pl && pl->pc) {
		CString commonArt = pl->pc[m_batchIndices[0]].art;
		CString commonAlb = pl->pc[m_batchIndices[0]].alb;
		bool artSame = true, albSame = true;
		for (size_t i = 1; i < m_batchIndices.size(); ++i) {
			const int idx = m_batchIndices[i];
			if (idx < 0 || idx >= pl->playcnt) continue;
			if (commonArt.Compare(pl->pc[idx].art) != 0) artSame = false;
			if (commonAlb.Compare(pl->pc[idx].alb) != 0) albSame = false;
		}
		m_art.SetWindowText(artSame ? commonArt : _T(""));
		m_alb.SetWindowText(albSame ? commonAlb : _T(""));
	}
}

void CListSyosai::OnClose()
{
	EndDialog(IDCANCEL);
}

void CListSyosai::OnDestroy()
{
	if (g_syHelpDlg && ::IsWindow(g_syHelpDlg->GetSafeHwnd()))
		g_syHelpDlg->DestroyWindow();
	CCustomBlurDialogBase::OnDestroy();
}

void CListSyosai::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

void CListSyosai::OnBnClickedExplorer()
{
	CString path = CurrentPathText();
	CString phys = PlPhysicalMediaPath(path);
	if (phys.IsEmpty())
		phys = path;
	if (phys.IsEmpty())
		return;

	if (PathFileExists(phys)) {
		CString params;
		params.Format(_T("/select,\"%s\""), (LPCTSTR)phys);
		ShellExecute(NULL, _T("open"), _T("explorer.exe"), params, NULL, SW_SHOWNORMAL);
		return;
	}

	CString folder = phys;
	const int slash = (std::max)(folder.ReverseFind(_T('\\')), folder.ReverseFind(_T('/')));
	if (slash >= 0)
		folder = folder.Left(slash);
	if (!folder.IsEmpty())
		ShellExecute(NULL, _T("open"), folder, _T(""), NULL, SW_SHOWNORMAL);
}

void CListSyosai::OnBnClickedOk()
{
	CollectPlaylistFields();
	OnOK();
}

void CListSyosai::OnBnClickedBrowse()
{
	CString cur = CurrentPathText();
	CString phys = PlPhysicalMediaPath(cur);
	if (phys.IsEmpty())
		phys = cur;

	CFileDialog dlg(TRUE, NULL, phys.IsEmpty() ? NULL : (LPCTSTR)phys,
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER,
		_T("All Files (*.*)|*.*||"), this);
	if (dlg.DoModal() != IDOK)
		return;

	CString chosen = dlg.GetPathName();
	const CString stored = PlStorePlaylistFol(chosen, pc.sub);
	m_fol.SetWindowText(stored);
	_tcsncpy(pc.fol, stored, _countof(pc.fol) - 1);
	pc.fol[_countof(pc.fol) - 1] = 0;
	RefreshStatusLines();
}

void CListSyosai::OnBnClickedTag2Pl()
{
	FileTagFields tags;
	ReadFileTagFields(CurrentPathText(), tags);
	if (!tags.title.IsEmpty())
		m_name.SetWindowText(tags.title);
	if (!tags.artist.IsEmpty())
		m_art.SetWindowText(tags.artist);
	if (!tags.album.IsEmpty())
		m_alb.SetWindowText(tags.album);
	ApplyTagsToControls(tags, false);
}

void CListSyosai::OnBnClickedReloadTag()
{
	FileTagFields tags;
	ReadFileTagFields(CurrentPathText(), tags);
	ApplyTagsToControls(tags, true);
	if (tags.loop1 == 0 && tags.loop2 == 0) {
		// ループはタグに無ければ現状維持
	}
	RefreshStatusLines();
}

void CListSyosai::OnBnClickedWriteTag()
{
	FileTagFields tags;
	int loopStart = 0, loopEnd = 0;
	CollectTagAndLoopFields(tags, loopStart, loopEnd);
	CString path = CurrentPathText();
	if (!WriteFileTagFields(path, tags)) {
		AfxMessageBox(LL14(
			L"タグの書き込みに失敗しました。\n対応: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Failed to write tags.\nSupported: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Echec ecriture tags.\nPris en charge: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Scrittura tag non riuscita.\nSupportati: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Error al escribir etiquetas.\nCompatible: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"태그 쓰기 실패.\n지원: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"写入标签失败。\n支持: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"فشل كتابة الوسوم.\nالمدعوم: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Не удалось записать теги.\nПоддержка: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Tag schreiben fehlgeschlagen.\nUnterstuetzt: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Falha ao gravar tags.\nSuportado: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Tags schrijven mislukt.\nOndersteund: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Nie udało się zapisać tagów.\nObsługa: MP3 / FLAC / WAV / M4A / Ogg Vorbis",
			L"Etiket yazılamadı.\nDestek: MP3 / FLAC / WAV / M4A / Ogg Vorbis"), MB_ICONWARNING);
		return;
	}
	pc.loop1 = loopStart;
	pc.loop2 = loopEnd;
	AfxMessageBox(LL14(L"タグを書き込みました。", L"Tags written.", L"Tags ecrits.", L"Tag scritti.",
		L"Etiquetas escritas.", L"태그 저장됨.", L"标签已写入。", L"تمت الكتابة.",
		L"Теги записаны.", L"Tags geschrieben.", L"Tags gravadas.", L"Tags geschreven.",
		L"Tagi zapisane.", L"Etiketler yazıldı."), MB_ICONINFORMATION);
}

void CListSyosai::OnBnClickedCopyPath()
{
	CopyTextToClipboard(GetSafeHwnd(), CurrentPathText());
}

void CListSyosai::OnBnClickedCopyName()
{
	CString path = CurrentPathText();
	CString name = path;
	const int slash = (std::max)(name.ReverseFind(_T('\\')), name.ReverseFind(_T('/')));
	if (slash >= 0)
		name = name.Mid(slash + 1);
	CopyTextToClipboard(GetSafeHwnd(), name);
}

void CListSyosai::OnBnClickedProTools()
{
	CollectPlaylistFields();
	EndDialog(IDC_SYOSAI_BTN_PROTOOLS);
}

void CListSyosai::OnBnClickedClearParam()
{
	CString msg = LL14(
		L"この曲の記憶パラメータ(音量・EQ等)を削除します。よろしいですか？",
		L"Clear saved parameters (volume, EQ, etc.) for this track?",
		L"Effacer les parametres enregistres de ce morceau ?",
		L"Cancellare i parametri salvati di questo brano?",
		L"Borrar los parametros guardados de esta pista?",
		L"이 곡의 저장 파라미터를 삭제할까요?",
		L"删除此曲的已存参数？",
		L"مسح المعلمات المحفوظة لهذا المسار؟",
		L"Удалить сохранённые параметры этого трека?",
		L"Gespeicherte Parameter dieses Titels loeschen?",
		L"Limpar parametros salvos desta faixa?",
		L"Opgeslagen parameters van dit nummer wissen?",
		L"Usunąć zapisane parametry tego utworu?",
		L"Bu parçanın kayıtlı parametreleri silinsin mi?");
	if (AfxMessageBox(msg, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;

	playlistdata0 item = pc;
	CString path = CurrentPathText();
	_tcsncpy(item.fol, path, _countof(item.fol) - 1);
	item.fol[_countof(item.fol) - 1] = 0;
	CString listKey = SongParams_CurrentListName();
	SongParams_RebindEntries(listKey, NULL, &item, 1, false);
	RefreshStatusLines();
}

BOOL CListSyosai::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	if (!IsBatchMode())
		RefreshPcDetails(pc);

	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();

	SetWindowText(LL14(L"ファイル情報", L"File Info", L"Infos fichier", L"Info file", L"Info. de archivo", L"파일 정보", L"文件信息", L"معلومات الملف", L"Сведения о файле", L"Dateiinfo", L"Info. do arquivo", L"Bestandsinfo", L"Informacje o pliku", L"Dosya bilgisi"));
	SetDlgItemText(ID_OK, LL14(L"OK", L"OK", L"OK", L"OK", L"OK", L"OK", L"确定", L"موافق", L"ОК", L"OK", L"OK", L"OK", L"OK", L"Tamam"));
	SetDlgItemText(IDCANCEL, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	SetDlgItemText(IDOK999, LL14(L"Explorer", L"Explorer", L"Explorateur", L"Esplora", L"Explorador", L"탐색기", L"资源管理器", L"المستكشف", L"Проводник", L"Explorer", L"Explorer", L"Verkenner", L"Eksplorator", L"Gezgin"));
	SetDlgItemText(IDC_SYOSAI_BTN_BROWSE, LL14(L"参照", L"Browse", L"Parcourir", L"Sfoglia", L"Examinar", L"찾아보기", L"浏览", L"استعراض", L"Обзор", L"Durchsuchen", L"Procurar", L"Bladeren", L"Przeglądaj", L"Gözat"));
	SetDlgItemText(IDC_SYOSAI_BTN_TAG2PL, LL14(L"タグ→PL", L"Tag→PL", L"Tag→PL", L"Tag→PL", L"Tag→PL", L"태그→PL", L"标签→列表", L"وسم→قائمة", L"Тег→PL", L"Tag→PL", L"Tag→PL", L"Tag→PL", L"Tag→PL", L"Etiket→PL"));
	SetDlgItemText(IDC_SYOSAI_BTN_RELOADTAG, LL14(L"再読込", L"Reload", L"Recharger", L"Ricarica", L"Recargar", L"다시 읽기", L"重新加载", L"إعادة تحميل", L"Обновить", L"Neu laden", L"Recarregar", L"Herladen", L"Wczytaj", L"Yenile"));
	SetDlgItemText(IDC_SYOSAI_BTN_WRITETAG, LL14(L"タグ書込", L"Write tag", L"Ecrire tag", L"Scrivi tag", L"Escribir tag", L"태그 쓰기", L"写入标签", L"كتابة وسم", L"Записать тег", L"Tag schreiben", L"Gravar tag", L"Tag schrijven", L"Zapisz tag", L"Etiket yaz"));
	SetDlgItemText(IDC_SYOSAI_BTN_COPYPATH, LL14(L"パスコピー", L"Copy path", L"Copier chemin", L"Copia percorso", L"Copiar ruta", L"경로 복사", L"复制路径", L"نسخ المسار", L"Копировать путь", L"Pfad kopieren", L"Copiar caminho", L"Pad kopiëren", L"Kopiuj ścieżkę", L"Yolu kopyala"));
	SetDlgItemText(IDC_SYOSAI_BTN_COPYNAME, LL14(L"名コピー", L"Copy name", L"Copier nom", L"Copia nome", L"Copiar nombre", L"이름 복사", L"复制文件名", L"نسخ الاسم", L"Копировать имя", L"Name kopieren", L"Copiar nome", L"Naam kopiëren", L"Kopiuj nazwę", L"Adı kopyala"));
	SetDlgItemText(IDC_SYOSAI_BTN_PROTOOLS, LL14(L"再生詳細", L"Playback details", L"Details lecture", L"Dettagli riproduzione", L"Detalles reproducción", L"재생 상세", L"播放详情", L"تفاصيل التشغيل", L"Детали воспроизведения", L"Wiedergabedetails", L"Detalhes de reprodução", L"Afspeeldetails", L"Szczegóły odtwarzania", L"Oynatma ayrıntıları"));
	SetDlgItemText(IDC_SYOSAI_BTN_CLEARPARAM, LL14(L"[SAV]削除", L"Clear [SAV]", L"Effacer [SAV]", L"Cancella [SAV]", L"Borrar [SAV]", L"[SAV] 삭제", L"删除[SAV]", L"مسح [SAV]", L"Удалить [SAV]", L"[SAV] löschen", L"Limpar [SAV]", L"[SAV] wissen", L"Usuń [SAV]", L"[SAV] sil"));
	SetDlgItemText(IDC_SYOSAI_GRP_EDIT, LL14(L"プレイリスト", L"Playlist", L"Liste de lecture", L"Playlist", L"Lista de reproducción", L"재생 목록", L"播放列表", L"قائمة التشغيل", L"Плейлист", L"Wiedergabeliste", L"Lista de reprodução", L"Afspeellijst", L"Playlista", L"Çalma listesi"));
	SetDlgItemText(IDC_SYOSAI_GRP_TAG, LL14(L"タグ情報", L"Tag info", L"Infos balises", L"Info tag", L"Info. de etiquetas", L"태그 정보", L"标签信息", L"معلومات الوسم", L"Теги", L"Tag-Info", L"Info. de tags", L"Taginfo", L"Info. o tagach", L"Etiket bilgisi"));
	SetDlgItemText(IDC_SYOSAI_GRP_INTERNAL, LL14(L"内部情報", L"Internal info", L"Infos internes", L"Info interne", L"Info. interna", L"내부 정보", L"内部信息", L"معلومات داخلية", L"Служебная информация", L"Interne Info", L"Info. interna", L"Interne info", L"Info. wewnętrzne", L"Dahili bilgi"));
	SetDlgItemText(IDC_SYOSAI_LBL_NAME, LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"));
	SetDlgItemText(IDC_SYOSAI_LBL_ART, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Künstler", L"Artista", L"Artiest", L"Artysta", L"Sanatçı"));
	SetDlgItemText(IDC_SYOSAI_LBL_ALB, LL14(L"アルバム", L"Album", L"Album", L"Album", L"Álbum", L"앨범", L"专辑", L"الألبوم", L"Альбом", L"Album", L"Álbum", L"Album", L"Album", L"Albüm"));
	SetDlgItemText(IDC_SYOSAI_LBL_FILE, LL14(L"パス", L"Path", L"Chemin", L"Percorso", L"Ruta", L"경로", L"路径", L"المسار", L"Путь", L"Pfad", L"Caminho", L"Pad", L"Ścieżka", L"Yol"));
	SetDlgItemText(IDC_SYOSAI_LBL_YEAR, LL14(L"年", L"Year", L"Année", L"Anno", L"Año", L"연도", L"年份", L"السنة", L"Год", L"Jahr", L"Ano", L"Jaar", L"Rok", L"Yıl"));
	SetDlgItemText(IDC_SYOSAI_LBL_TRACK, LL14(L"Track", L"Track", L"Piste", L"Traccia", L"Pista", L"트랙", L"曲目", L"المسار", L"Трек", L"Track", L"Faixa", L"Track", L"Utwór", L"Parça"));
	SetDlgItemText(IDC_SYOSAI_LBL_GENRE, LL14(L"ジャンル", L"Genre", L"Genre", L"Genere", L"Género", L"장르", L"流派", L"النوع", L"Жанр", L"Genre", L"Gênero", L"Genre", L"Gatunek", L"Tür"));
	SetDlgItemText(IDC_SYOSAI_LBL_CMT, LL14(L"コメント", L"Comment", L"Commentaire", L"Commento", L"Comentario", L"코멘트", L"注释", L"تعليق", L"Комментарий", L"Kommentar", L"Comentário", L"Opmerking", L"Komentarz", L"Yorum"));
	SetDlgItemText(IDC_SYOSAI_LBL_ID, LL14(L"内部ID", L"Internal ID", L"ID interne", L"ID interno", L"ID interno", L"내부 ID", L"内部 ID", L"المعرّف الداخلي", L"Внутр. ID", L"Interne ID", L"ID interno", L"Intern ID", L"ID wewn.", L"Dahili ID"));
	SetDlgItemText(IDC_SYOSAI_LBL_GAME, LL14(L"Game", L"Game", L"Jeu", L"Gioco", L"Juego", L"게임", L"游戏", L"اللعبة", L"Игра", L"Spiel", L"Jogo", L"Spel", L"Gra", L"Oyun"));
	SetDlgItemText(IDC_SYOSAI_LBL_TIME, LL14(L"時間", L"Duration", L"Durée", L"Durata", L"Duración", L"재생 시간", L"时长", L"المدة", L"Длительность", L"Dauer", L"Duração", L"Duur", L"Czas", L"Süre"));
	SetDlgItemText(IDC_SYOSAI_LBL_LOOP, LL14(L"ループ", L"Loop", L"Boucle", L"Loop", L"Bucle", L"루프", L"循环", L"التكرار", L"Цикл", L"Schleife", L"Loop", L"Loop", L"Pętla", L"Döngü"));
	SetDlgItemText(IDC_SYOSAI_LBL_RET2, LL14(L"Idx", L"Idx", L"N°", L"Ind", L"Idx", L"Idx", L"索引", L"فهر", L"Инд", L"Idx", L"Idx", L"Idx", L"Ind", L"Diz"));

	TCHAR dy[256];
	::SendMessage(m_fol.m_hWnd, EM_SETWORDBREAKPROC, 0, (LPARAM)EditWordBreakProc);
	::SendMessage(m_cmt.m_hWnd, EM_SETWORDBREAKPROC, 0, (LPARAM)EditWordBreakProc);

	m_name.SetWindowText(pc.name);
	m_art.SetWindowText(pc.art);
	m_alb.SetWindowText(pc.alb);
	m_fol.SetWindowText(pc.fol);
	m_id.SetWindowText(_itot(pc.sub, dy, 10));
	m_game.SetWindowText(pc.game);

	CString s;
	if (pc.time >= 3600)
		s.Format(_T("%d:%02d:%02d"), pc.time / 3600, (pc.time / 60) % 60, pc.time % 60);
	else
		s.Format(_T("%d:%02d"), pc.time / 60, pc.time % 60);
	if (pc.time == 0) s = _T("");
	if (pc.time == -1) s = LL14(L"取得不能", L"Unable to fetch", L"Indisponible", L"Non disponibile", L"No disponible", L"가져올 수 없음", L"无法获取", L"تعذّر الجلب", L"Недоступно", L"Nicht verfügbar", L"Indisponível", L"Niet beschikbaar", L"Niedostępne", L"Alınamadı");
	m_time.SetWindowText(s);

	s.Format(_T("%d"), pc.loop1);
	m_loop1.SetWindowText(s);
	s.Format(_T("%d"), pc.loop2);
	m_loop2.SetWindowText(s);
	s.Format(_T("%d"), pc.ret2);
	m_ret2.SetWindowText(s);

	if (!IsBatchMode()) {
		FileTagFields tags;
		ReadFileTagFields(pc.fol, tags);
		if (pc.loop1 == 0 && pc.loop2 == 0 && (tags.loop1 || tags.loop2)) {
			pc.loop1 = tags.loop1;
			pc.loop2 = tags.loop2;
			s.Format(_T("%d"), pc.loop1);
			m_loop1.SetWindowText(s);
			s.Format(_T("%d"), pc.loop2);
			m_loop2.SetWindowText(s);
		}
		ApplyTagsToControls(tags);
	}

	ApplyBatchUi();
	RefreshStatusLines();

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT1), LL14(L"プレイリストに表示される曲名です。\nOKで保存されます。", L"Track name shown in the playlist.\nSaved when you click OK.", L"Nom affiche dans la liste.\nEnregistre avec OK.", L"Nome mostrato nella playlist.\nSalvato con OK.", L"Nombre mostrado en la lista.\nSe guarda con OK.", L"재생 목록에 표시되는 곡명입니다.\nOK로 저장됩니다.", L"播放列表中显示的曲名。\n点击确定保存。", L"اسم المسار المعروض في القائمة.\nيُحفظ عند OK.", L"Название в плейлисте.\nСохраняется по OK.", L"Titel in der Wiedergabeliste.\nMit OK speichern.", L"Nome exibido na lista.\nSalvo com OK.", L"Naam in afspeellijst.\nOpslaan met OK.", L"Nazwa w playliście.\nZapis po OK.", L"Calma listesinde gorunen ad.\nTamam ile kaydedilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT4), LL14(L"プレイリストに表示されるアーティスト名です。\nOKで保存されます。", L"Artist name shown in the playlist.\nSaved when you click OK.", L"Artiste affiche dans la liste.\nEnregistre avec OK.", L"Artista mostrato nella playlist.\nSalvato con OK.", L"Artista mostrado en la lista.\nSe guarda con OK.", L"재생 목록에 표시되는 아티스트명입니다.\nOK로 저장됩니다.", L"播放列表中显示的艺术家。\n点击确定保存。", L"اسم الفنان المعروض في القائمة.\nيُحفظ عند OK.", L"Исполнитель в плейлисте.\nСохраняется по OK.", L"Kunstler in der Wiedergabeliste.\nMit OK speichern.", L"Artista exibido na lista.\nSalvo com OK.", L"Artiest in afspeellijst.\nOpslaan met OK.", L"Artysta w playliście.\nZapis po OK.", L"Calma listesinde gorunen sanatci.\nTamam ile kaydedilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT5), LL14(L"プレイリストに表示されるアルバム名です。\nOKで保存されます。", L"Album name shown in the playlist.\nSaved when you click OK.", L"Album affiche dans la liste.\nEnregistre avec OK.", L"Album mostrato nella playlist.\nSalvato con OK.", L"Album mostrado en la lista.\nSe guarda con OK.", L"재생 목록에 표시되는 앨범명입니다.\nOK로 저장됩니다.", L"播放列表中显示的专辑名。\n点击确定保存。", L"اسم الألبوم المعروض في القائمة.\nيُحفظ عند OK.", L"Альбом в плейлисте.\nСохраняется по OK.", L"Album in der Wiedergabeliste.\nMit OK speichern.", L"Album exibido na lista.\nSalvo com OK.", L"Album in afspeellijst.\nOpslaan met OK.", L"Album w playliście.\nZapis po OK.", L"Calma listesinde gorunen album.\nTamam ile kaydedilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT6), LL14(L"曲ファイルのパスです。\n変更するとプレイリストの参照先が変わります。\nOKで保存されます。", L"Path to the track file.\nChanging it updates the playlist reference.\nSaved when you click OK.", L"Chemin du fichier.\nLa modification change la reference dans la liste.\nEnregistre avec OK.", L"Percorso del file.\nModificarlo cambia il riferimento nella playlist.\nSalvato con OK.", L"Ruta del archivo.\nCambiarla actualiza la referencia en la lista.\nSe guarda con OK.", L"곡 파일 경로입니다.\n변경하면 재생 목록 참조가 바뀝니다.\nOK로 저장됩니다.", L"曲文件路径。\n更改后会更新播放列表引用。\n点击确定保存。", L"مسار ملف المسار.\nتغييره يحدّث المرجع في القائمة.\nيُحفظ عند OK.", L"Путь к файлу.\nИзменение обновляет ссылку в плейлисте.\nСохраняется по OK.", L"Pfad zur Datei.\nAnderung aktualisiert den Verweis.\nMit OK speichern.", L"Caminho do arquivo.\nAlterar atualiza a referencia na lista.\nSalvo com OK.", L"Pad naar bestand.\nWijzigen past referentie aan.\nOpslaan met OK.", L"Sciezka pliku.\nZmiana aktualizuje odniesienie.\nZapis po OK.", L"Dosya yolu.\nDegistirmek listedeki referansi gunceller.\nTamam ile kaydedilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT7), LL14(L"ファイルタグの年。編集可。タグ書込でファイルへ反映(MP3/FLAC/WAV/M4A/Ogg等)。", L"Year from file tags. Editable. Write tag saves to file (MP3/FLAC/WAV/M4A/Ogg etc.).", L"Annee des tags. Editable.", L"Anno dai tag. Modificabile.", L"Ano de etiquetas. Editable.", L"파일 태그 연도. 편집 가능.", L"文件标签年份。可编辑。", L"سنة الوسوم. قابلة للتحرير.", L"Год из тегов. Редактируемо.", L"Jahr aus Tags. Editierbar.", L"Ano das tags. Editavel.", L"Jaar uit tags. Bewerkbaar.", L"Rok z tagow. Edytowalny.", L"Etiket yili. Duzenlenebilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT9), LL14(L"ファイルタグのトラック番号。編集可。", L"Track number from file tags. Editable.", L"Numero de piste. Editable.", L"Numero traccia. Modificabile.", L"Numero de pista. Editable.", L"트랙 번호. 편집 가능.", L"曲目编号。可编辑。", L"رقم المسار. قابل للتحرير.", L"Номер трека. Редактируемо.", L"Titelnummer. Editierbar.", L"Numero da faixa. Editavel.", L"Tracknummer. Bewerkbaar.", L"Numer utworu. Edytowalny.", L"Parca no. Duzenlenebilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT10), LL14(L"ファイルタグのジャンル。編集可。", L"Genre from file tags. Editable.", L"Genre. Editable.", L"Genere. Modificabile.", L"Genero. Editable.", L"장르. 편집 가능.", L"流派。可编辑。", L"النوع. قابل للتحرير.", L"Жанр. Редактируемо.", L"Genre. Editierbar.", L"Genero. Editavel.", L"Genre. Bewerkbaar.", L"Gatunek. Edytowalny.", L"Tur. Duzenlenebilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT11), LL14(L"ファイルタグのコメント。編集可。", L"Comment from file tags. Editable.", L"Commentaire. Editable.", L"Commento. Modificabile.", L"Comentario. Editable.", L"코멘트. 편집 가능.", L"注释。可编辑。", L"تعليق. قابل للتحرير.", L"Комментарий. Редактируемо.", L"Kommentar. Editierbar.", L"Comentario. Editavel.", L"Opmerking. Bewerkbaar.", L"Komentarz. Edytowalny.", L"Yorum. Duzenlenebilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT13), LL14(L"ループ開始(サンプル)。OKでプレイリストへ保存。", L"Loop start (samples). Saved to playlist on OK.", L"Debut de boucle. Enregistre avec OK.", L"Inizio loop. Salvato con OK.", L"Inicio de bucle. Se guarda con OK.", L"루프 시작(샘플). OK로 저장.", L"循环起始。确定保存。", L"بداية التكرار. يُحفظ مع OK.", L"Начало петли. Сохраняется по OK.", L"Schleifenstart. Mit OK speichern.", L"Inicio do loop. Salvo com OK.", L"Loopstart. Opslaan met OK.", L"Poczatek petli. Zapis po OK.", L"Dongu baslangici. Tamam ile kaydet."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT14), LL14(L"ループ終了(サンプル)。OKでプレイリストへ保存。", L"Loop end (samples). Saved to playlist on OK.", L"Fin de boucle. Enregistre avec OK.", L"Fine loop. Salvato con OK.", L"Fin de bucle. Se guarda con OK.", L"루프 종료(샘플). OK로 저장.", L"循环结束。确定保存。", L"نهاية التكرار. يُحفظ مع OK.", L"Конец петли. Сохраняется по OK.", L"Schleifenende. Mit OK speichern.", L"Fim do loop. Salvo com OK.", L"Loopeinde. Opslaan met OK.", L"Koniec petli. Zapis po OK.", L"Dongu bitisi. Tamam ile kaydet."));
	m_tooltip.AddTool(GetDlgItem(IDC_SYOSAI_BTN_TAG2PL), LL14(L"ファイルタグのタイトル/アーティスト/アルバムをプレイリスト欄へコピーします。", L"Copy tag title/artist/album into playlist fields.", L"Copier titre/artiste/album des tags vers la playlist.", L"Copia titolo/artista/album dai tag.", L"Copiar titulo/artista/album de etiquetas.", L"태그 제목/아티스트/앨범을 재생 목록란으로 복사.", L"将标签的标题/艺术家/专辑复制到播放列表字段。", L"نسخ العنوان/الفنان/الألبوم من الوسوم.", L"Копировать название/исполнителя/альбом из тегов.", L"Titel/Artist/Album aus Tags in Playlist kopieren.", L"Copiar titulo/artista/album das tags.", L"Titel/artiest/album uit tags kopieren.", L"Kopiuj tytul/artyste/album z tagow.", L"Etiket baslik/sanatci/albumu listeye kopyala."));
	m_tooltip.AddTool(GetDlgItem(IDC_SYOSAI_BTN_RELOADTAG), LL14(L"ファイルからタグ情報を再読み込みします。", L"Reload tag info from the file.", L"Recharger les tags depuis le fichier.", L"Ricarica i tag dal file.", L"Recargar etiquetas del archivo.", L"파일에서 태그 정보를 다시 읽습니다.", L"从文件重新加载标签信息。", L"إعادة تحميل معلومات الوسوم من الملف.", L"Перечитать теги из файла.", L"Tags aus der Datei neu laden.", L"Recarregar tags do arquivo.", L"Tags opnieuw laden uit bestand.", L"Wczytaj ponownie tagi z pliku.", L"Dosyadan etiket bilgisini yeniden yükle."));
	m_tooltip.AddTool(GetDlgItem(IDC_SYOSAI_BTN_WRITETAG), LL14(L"表示中のメタデータをファイルタグへ書き込みます。\n対応: MP3 / FLAC / WAV / M4A / Ogg Vorbis", L"Write displayed metadata to file tags.\nSupported: MP3 / FLAC / WAV / M4A / Ogg Vorbis", L"Ecrire les metadonnees dans les tags.\nMP3 / FLAC / WAV / M4A / Ogg Vorbis", L"Scrivi metadati nei tag.\nMP3 / FLAC / WAV / M4A / Ogg Vorbis", L"Escribir metadatos en etiquetas.\nMP3 / FLAC / WAV / M4A / Ogg Vorbis", L"표시 메타데이터를 파일 태그에 씁니다.\n지원: MP3 / FLAC / WAV / M4A / Ogg Vorbis", L"将显示的元数据写入文件标签。\n支持: MP3 / FLAC / WAV / M4A / Ogg Vorbis", L"كتابة البيانات إلى الوسوم.\nMP3 / FLAC / WAV / M4A / Ogg Vorbis", L"Записать метаданные в теги.\nMP3 / FLAC / WAV / M4A / Ogg Vorbis", L"Metadaten in Tags schreiben.\nMP3 / FLAC / WAV / M4A / Ogg Vorbis", L"Gravar metadados nas tags.\nMP3 / FLAC / WAV / M4A / Ogg Vorbis", L"Metadata naar tags schrijven.\nMP3 / FLAC / WAV / M4A / Ogg Vorbis", L"Zapisz metadane do tagow.\nMP3 / FLAC / WAV / M4A / Ogg Vorbis", L"Metadata etiketlere yaz.\nMP3 / FLAC / WAV / M4A / Ogg Vorbis"));
	m_tooltip.AddTool(GetDlgItem(IDC_SYOSAI_BTN_BROWSE), LL14(L"ファイルを選んでパスを差し替えます。\nOKでプレイリストへ保存されます。", L"Pick a file to replace the path.\nSaved to the playlist on OK.", L"Choisir un fichier pour remplacer le chemin.\nEnregistre avec OK.", L"Scegli un file per sostituire il percorso.\nSalvato con OK.", L"Elegir un archivo para cambiar la ruta.\nSe guarda con OK.", L"파일을 골라 경로를 바꿉니다.\nOK로 재생 목록에 저장.", L"选择文件以替换路径。\n确定后保存到播放列表。", L"اختر ملفاً لاستبدال المسار.\nيُحفظ مع OK.", L"Выбрать файл для замены пути.\nСохраняется по OK.", L"Datei waehlen und Pfad ersetzen.\nMit OK speichern.", L"Escolher arquivo para trocar o caminho.\nSalvo com OK.", L"Kies een bestand om het pad te vervangen.\nOpslaan met OK.", L"Wybierz plik, aby zmienic sciezke.\nZapis po OK.", L"Dosya secerek yolu degistirir.\nTamam ile kaydedilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_SYOSAI_BTN_COPYPATH), LL14(L"パスをクリップボードへコピーします。", L"Copy the path to the clipboard.", L"Copier le chemin dans le presse-papiers.", L"Copia il percorso negli appunti.", L"Copiar la ruta al portapapeles.", L"경로를 클립보드에 복사합니다.", L"将路径复制到剪贴板。", L"نسخ المسار إلى الحافظة.", L"Копировать путь в буфер обмена.", L"Pfad in die Zwischenablage kopieren.", L"Copiar o caminho para a area de transferencia.", L"Pad naar klembord kopieren.", L"Kopiuj sciezke do schowka.", L"Yolu panoya kopyalar."));
	m_tooltip.AddTool(GetDlgItem(IDC_SYOSAI_BTN_COPYNAME), LL14(L"ファイル名(パス末尾)をクリップボードへコピーします。", L"Copy the file name (end of path) to the clipboard.", L"Copier le nom de fichier dans le presse-papiers.", L"Copia il nome file negli appunti.", L"Copiar el nombre de archivo al portapapeles.", L"파일 이름(경로 끝)을 클립보드에 복사합니다.", L"将文件名（路径末尾）复制到剪贴板。", L"نسخ اسم الملف إلى الحافظة.", L"Копировать имя файла в буфер обмена.", L"Dateinamen in die Zwischenablage kopieren.", L"Copiar o nome do arquivo para a area de transferencia.", L"Bestandsnaam naar klembord kopieren.", L"Kopiuj nazwe pliku do schowka.", L"Dosya adini (yol sonu) panoya kopyalar."));
	m_tooltip.AddTool(GetDlgItem(IDOK999), LL14(L"エクスプローラーでファイルを選択表示します。無い場合はフォルダを開きます。", L"Select the file in Explorer. Opens the folder if missing.", L"Selectionner le fichier dans l'explorateur.", L"Seleziona il file in Esplora risorse.", L"Seleccionar el archivo en el explorador.", L"탐색기에서 파일을 선택 표시합니다.", L"在资源管理器中选中文件。", L"تحديد الملف في المستكشف.", L"Выделить файл в проводнике.", L"Datei im Explorer auswaehlen.", L"Selecionar arquivo no Explorer.", L"Bestand selecteren in Verkenner.", L"Zaznacz plik w Eksploratorze.", L"Gezgin'de dosyayi sec."));
	m_tooltip.AddTool(GetDlgItem(IDC_SYOSAI_BTN_PROTOOLS), LL14(L"再生詳細ダイアログを開きます(ループ/キュー/タグ等)。", L"Open playback details (loop/cues/tags).", L"Ouvrir les details de lecture.", L"Apri dettagli riproduzione.", L"Abrir detalles de reproduccion.", L"재생 상세를 엽니다.", L"打开播放详情。", L"فتح تفاصيل التشغيل.", L"Открыть детали воспроизведения.", L"Wiedergabedetails oeffnen.", L"Abrir detalhes de reproducao.", L"Afspeeldetails openen.", L"Otworz szczegoly odtwarzania.", L"Oynatma ayrintilarini ac."));
	m_tooltip.AddTool(GetDlgItem(IDC_SYOSAI_BTN_CLEARPARAM), LL14(L"この曲の記憶パラメータ(音量・EQ等)を削除します。", L"Clear saved parameters (volume, EQ, etc.) for this track.", L"Effacer les parametres enregistres de ce morceau.", L"Cancella i parametri salvati di questo brano.", L"Borrar los parametros guardados de esta pista.", L"이 곡의 저장 파라미터(볼륨·EQ 등)를 삭제합니다.", L"删除此曲的已存参数（音量、EQ等）。", L"مسح المعلمات المحفوظة لهذا المسار.", L"Удалить сохранённые параметры этого трека.", L"Gespeicherte Parameter dieses Titels loeschen.", L"Limpar parametros salvos desta faixa.", L"Opgeslagen parameters van dit nummer wissen.", L"Usun zapisane parametry tego utworu.", L"Bu parçanın kayıtlı parametrelerini siler."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT2), LL14(L"曲の内部識別ID（ゲーム/形式ごとの番号）です。\n読取専用です。", L"Internal track ID (number per game/format).\nRead-only.", L"ID interne de la piste.\nLecture seule.", L"ID interno della traccia.\nSola lettura.", L"ID interno de la pista.\nSolo lectura.", L"곡의 내부 식별 ID입니다.\n읽기 전용.", L"曲目的内部识别 ID。\n只读。", L"المعرّف الداخلي للمسار.\nللقراءة فقط.", L"Внутренний ID трека.\nТолько чтение.", L"Interne Titel-ID.\nNur Lesen.", L"ID interno da faixa.\nSomente leitura.", L"Intern track-ID.\nAlleen lezen.", L"Wewnetrzne ID utworu.\nTylko odczyt.", L"Parcanin dahili kimligi.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT3), LL14(L"曲が属するゲーム名です。\n読取専用です。", L"Game this track belongs to.\nRead-only.", L"Jeu auquel appartient la piste.\nLecture seule.", L"Gioco di appartenenza.\nSola lettura.", L"Juego al que pertenece la pista.\nSolo lectura.", L"곡이 속한 게임 이름입니다.\n읽기 전용.", L"曲目所属的游戏名。\n只读。", L"اللعبة التي ينتمي إليها المسار.\nللقراءة فقط.", L"Игра, к которой относится трек.\nТолько чтение.", L"Spiel, zu dem der Titel gehort.\nNur Lesen.", L"Jogo ao qual a faixa pertence.\nSomente leitura.", L"Spel waartoe het nummer behoort.\nAlleen lezen.", L"Gra, do ktorej nalezy utwor.\nTylko odczyt.", L"Parcanin ait oldugu oyun.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT12), LL14(L"曲の再生時間です。\n読取専用です。", L"Track duration.\nRead-only.", L"Duree de la piste.\nLecture seule.", L"Durata della traccia.\nSola lettura.", L"Duracion de la pista.\nSolo lectura.", L"곡 재생 시간입니다.\n읽기 전용.", L"曲目时长。\n只读。", L"مدة المسار.\nللقراءة فقط.", L"Длительность трека.\nТолько чтение.", L"Titeldauer.\nNur Lesen.", L"Duracao da faixa.\nSomente leitura.", L"Duur van het nummer.\nAlleen lezen.", L"Czas trwania utworu.\nTylko odczyt.", L"Parca suresi.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT15), LL14(L"同一ファイル内の曲インデックスです。\n読取専用です。", L"Track index within the same file.\nRead-only.", L"Index de piste dans le meme fichier.\nLecture seule.", L"Indice traccia nello stesso file.\nSola lettura.", L"Indice de pista en el mismo archivo.\nSolo lectura.", L"동일 파일 내 곡 인덱스입니다.\n읽기 전용.", L"同一文件内的曲目索引。\n只读。", L"فهرس المسار داخل نفس الملف.\nللقراءة فقط.", L"Индекс трека внутри файла.\nТолько чтение.", L"Titelindex in derselben Datei.\nNur Lesen.", L"Indice da faixa no mesmo arquivo.\nSomente leitura.", L"Trackindex in hetzelfde bestand.\nAlleen lezen.", L"Indeks utworu w tym samym pliku.\nTylko odczyt.", L"Ayni dosyadaki parca indeksi.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_SYOSAI_LBL_STATUS), LL14(L"ファイルの有無・拡張子・サイズです。", L"File existence, extension and size.", L"Existence, extension et taille du fichier.", L"Esistenza, estensione e dimensione del file.", L"Existencia, extension y tamano del archivo.", L"파일 존재 여부·확장자·크기입니다.", L"文件是否存在、扩展名与大小。", L"وجود الملف والامتداد والحجم.", L"Наличие, расширение и размер файла.", L"Vorhandensein, Erweiterung und Groesse.", L"Existencia, extensao e tamanho.", L"Bestaan, extensie en grootte.", L"Istnienie, rozszerzenie i rozmiar.", L"Dosya varligi, uzanti ve boyut."));
	m_tooltip.AddTool(GetDlgItem(IDC_SYOSAI_LBL_PARAM), LL14(L"曲ごとに保存した音量・EQ等の有無と内容です。", L"Whether per-song volume/EQ settings exist, and a summary.", L"Presence et resume des reglages par morceau.", L"Presenza e riepilogo impostazioni per brano.", L"Presencia y resumen de ajustes por pista.", L"곡별 볼륨·EQ 설정 유무와 요약입니다.", L"逐曲音量/EQ 等设置的有无与摘要。", L"وجود ملخص إعدادات الصوت/EQ لكل أغنية.", L"Наличие и сводка настроек трека.", L"Vorhandensein und Kurzinfo der Pro-Titel-Einstellungen.", L"Presenca e resumo das config. por faixa.", L"Aanwezigheid en samenvatting per-nummer-instellingen.", L"Obecnosc i skrot ustawien utworu.", L"Parça ayarlarinin varligi ve ozeti."));
	m_tooltip.AddTool(GetDlgItem(ID_OK), LL14(L"プレイリスト表示の変更を保存して閉じます。", L"Save playlist display changes and close.", L"Enregistrer les modifications et fermer.", L"Salva le modifiche alla playlist e chiudi.", L"Guardar cambios de la lista y cerrar.", L"재생 목록 변경을 저장하고 닫습니다.", L"保存播放列表更改并关闭。", L"حفظ التغييرات وإغلاق النافذة.", L"Сохранить изменения и закрыть.", L"Anderungen speichern und schliessen.", L"Salvar alteracoes e fechar.", L"Wijzigingen opslaan en sluiten.", L"Zapisz zmiany i zamknij.", L"Degisiklikleri kaydet ve kapat."));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL), LL14(L"変更を保存せずに閉じます。", L"Close without saving changes.", L"Fermer sans enregistrer.", L"Chiudi senza salvare.", L"Cerrar sin guardar.", L"변경을 저장하지 않고 닫습니다.", L"不保存更改并关闭。", L"إغلاق دون حفظ التغييرات.", L"Закрыть без сохранения.", L"Ohne Speichern schliessen.", L"Fechar sem salvar.", L"Sluiten zonder opslaan.", L"Zamknij bez zapisywania.", L"Kaydetmeden kapat."));
	m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);

	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	return TRUE;
}

void CListSyosai::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CListSyosai::ShowHelpSheet()
{
	if (g_syHelpDlg && ::IsWindow(g_syHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_syHelpDlg, this);
		return;
	}
	if (g_syHelpDlg && !::IsWindow(g_syHelpDlg->GetSafeHwnd()))
		g_syHelpDlg = nullptr;
	CSyHelpDlg* dlg = new CSyHelpDlg(this);
	if (!dlg->Create(IDD_SY_HELP, this)) {
		delete dlg;
		return;
	}
	g_syHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CListSyosai::OnBnClickedHelp()
{
	ShowHelpSheet();
}

BOOL CListSyosai::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

//ワードラップを解除するためのコールバック関数
static int CALLBACK EditWordBreakProc(LPTSTR lpch, int ichCurrent, int cch, int code)
{
	return (WB_ISDELIMITER == code) ? 0 : ichCurrent;
}
