// ListSyosai.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "ListCtrlA.h"
#include "PlayList.h"
#include "ListSyosai.h"
#include "FileTagInfo.h"


// CListSyosai ダイアログ

IMPLEMENT_DYNAMIC(CListSyosai, CCustomBlurDialogBase)

CListSyosai::CListSyosai(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CListSyosai::IDD, pParent)
{

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
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CListSyosai, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDOK999, &CListSyosai::OnBnClickedOk2)
	ON_BN_CLICKED(ID_OK, &CListSyosai::OnBnClickedOk)
	ON_WM_CLOSE()
	cmn(CListSyosai);


// CListSyosai メッセージ ハンドラ
void CListSyosai::OnClose()
{
	EndDialog(0);
}

void CListSyosai::OnBnClickedOk2()
{
	CString s, ss;
	s = pc.fol;
	ss = s.Left(s.ReverseFind('\\'));
	ShellExecute(NULL, _T("open"), ss, _T(""), NULL, SW_SHOWNORMAL);
}

void CListSyosai::OnBnClickedOk()
{
	CString s;
	m_name.GetWindowText(s);
	_tcscpy(pc.name, s);
	m_art.GetWindowText(s);
	_tcscpy(pc.art, s);
	m_alb.GetWindowText(s);
	_tcscpy(pc.alb, s);
	m_fol.GetWindowText(s);
	_tcscpy(pc.fol, s);
	OnOK();
}

int CALLBACK EditWordBreakProc(LPTSTR lpch, int ichCurrent, int cch, int code);

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

BOOL CListSyosai::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	RefreshPcDetails(pc);
	SetWindowText(LL14(L"ファイル情報", L"File Info", L"Infos fichier", L"Info file", L"Info. de archivo", L"파일 정보", L"文件信息", L"معلومات الملف", L"Сведения о файле", L"Dateiinfo", L"Info. do arquivo", L"Bestandsinfo", L"Informacje o pliku", L"Dosya bilgisi"));
	SetDlgItemText(ID_OK, LL14(L"OK", L"OK", L"OK", L"OK", L"OK", L"OK", L"确定", L"موافق", L"ОК", L"OK", L"OK", L"OK", L"OK", L"Tamam"));
	SetDlgItemText(IDCANCEL, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	SetDlgItemText(IDOK999, LL14(L"フォルダを開く", L"Open folder", L"Ouvrir le dossier", L"Apri cartella", L"Abrir carpeta", L"폴더 열기", L"打开文件夹", L"فتح المجلد", L"Открыть папку", L"Ordner öffnen", L"Abrir pasta", L"Map openen", L"Otwórz folder", L"Klasörü aç"));
	SetDlgItemText(IDC_SYOSAI_GRP_EDIT, LL14(L"プレイリスト", L"Playlist", L"Liste de lecture", L"Playlist", L"Lista", L"재생 목록", L"播放列表", L"قائمة التشغيل", L"Плейлист", L"Wiedergabeliste", L"Lista", L"Afspeellijst", L"Playlista", L"Çalma listesi"));
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
	SetDlgItemText(IDC_SYOSAI_LBL_LOOP, LL14(L"ループ", L"Loop", L"Boucle", L"Loop", L"Bucle", L"루프", L"循环", L"التكرار", L"Петля", L"Schleife", L"Loop", L"Loop", L"Pętla", L"Döngü"));
	SetDlgItemText(IDC_SYOSAI_LBL_RET2, LL14(L"Idx", L"Idx", L"Idx", L"Idx", L"Idx", L"Idx", L"Idx", L"Idx", L"Idx", L"Idx", L"Idx", L"Idx", L"Idx", L"Idx"));

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

	s = pc.fol;
	if (s.Find('\\', 0) == -1 && s.Find('/', 0) == -1)
		m_ok2.EnableWindow(FALSE);
	{
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
		if (tags.year.GetLength()) m_year.SetWindowText(tags.year);
		if (tags.track.GetLength()) m_track.SetWindowText(tags.track);
		if (tags.genre.GetLength()) m_j.SetWindowText(tags.genre);
		if (tags.comment.GetLength()) m_cmt.SetWindowText(tags.comment);
	}

	m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_BALLOON);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT1), LL14(L"プレイリストに表示される曲名です。\nOKで保存されます。", L"Track name shown in the playlist.\nSaved when you click OK.", L"Nom affiche dans la liste.\nEnregistre avec OK.", L"Nome mostrato nella playlist.\nSalvato con OK.", L"Nombre mostrado en la lista.\nSe guarda con OK.", L"재생 목록에 표시되는 곡명입니다.\nOK로 저장됩니다.", L"播放列表中显示的曲名。\n点击确定保存。", L"اسم المسار المعروض في القائمة.\nيُحفظ عند OK.", L"Название в плейлисте.\nСохраняется по OK.", L"Titel in der Wiedergabeliste.\nMit OK speichern.", L"Nome exibido na lista.\nSalvo com OK.", L"Naam in afspeellijst.\nOpslaan met OK.", L"Nazwa w playliście.\nZapis po OK.", L"Calma listesinde gorunen ad.\nTamam ile kaydedilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT4), LL14(L"プレイリストに表示されるアーティスト名です。\nOKで保存されます。", L"Artist name shown in the playlist.\nSaved when you click OK.", L"Artiste affiche dans la liste.\nEnregistre avec OK.", L"Artista mostrato nella playlist.\nSalvato con OK.", L"Artista mostrado en la lista.\nSe guarda con OK.", L"재생 목록에 표시되는 아티스트명입니다.\nOK로 저장됩니다.", L"播放列表中显示的艺术家。\n点击确定保存。", L"اسم الفنان المعروض في القائمة.\nيُحفظ عند OK.", L"Исполнитель в плейлисте.\nСохраняется по OK.", L"Kunstler in der Wiedergabeliste.\nMit OK speichern.", L"Artista exibido na lista.\nSalvo com OK.", L"Artiest in afspeellijst.\nOpslaan met OK.", L"Artysta w playliście.\nZapis po OK.", L"Calma listesinde gorunen sanatci.\nTamam ile kaydedilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT5), LL14(L"プレイリストに表示されるアルバム名です。\nOKで保存されます。", L"Album name shown in the playlist.\nSaved when you click OK.", L"Album affiche dans la liste.\nEnregistre avec OK.", L"Album mostrato nella playlist.\nSalvato con OK.", L"Album mostrado en la lista.\nSe guarda con OK.", L"재생 목록에 표시되는 앨범명입니다.\nOK로 저장됩니다.", L"播放列表中显示的专辑名。\n点击确定保存。", L"اسم الألبوم المعروض في القائمة.\nيُحفظ عند OK.", L"Альбом в плейлисте.\nСохраняется по OK.", L"Album in der Wiedergabeliste.\nMit OK speichern.", L"Album exibido na lista.\nSalvo com OK.", L"Album in afspeellijst.\nOpslaan met OK.", L"Album w playliście.\nZapis po OK.", L"Calma listesinde gorunen album.\nTamam ile kaydedilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT6), LL14(L"曲ファイルのパスです。\n変更するとプレイリストの参照先が変わります。\nOKで保存されます。", L"Path to the track file.\nChanging it updates the playlist reference.\nSaved when you click OK.", L"Chemin du fichier.\nLa modification change la reference dans la liste.\nEnregistre avec OK.", L"Percorso del file.\nModificarlo cambia il riferimento nella playlist.\nSalvato con OK.", L"Ruta del archivo.\nCambiarla actualiza la referencia en la lista.\nSe guarda con OK.", L"곡 파일 경로입니다.\n변경하면 재생 목록 참조가 바뀝니다.\nOK로 저장됩니다.", L"曲文件路径。\n更改后会更新播放列表引用。\n点击确定保存。", L"مسار ملف المسار.\nتغييره يحدّث المرجع في القائمة.\nيُحفظ عند OK.", L"Путь к файлу.\nИзменение обновляет ссылку в плейлисте.\nСохраняется по OK.", L"Pfad zur Datei.\nAnderung aktualisiert den Verweis.\nMit OK speichern.", L"Caminho do arquivo.\nAlterar atualiza a referencia na lista.\nSalvo com OK.", L"Pad naar bestand.\nWijzigen past referentie aan.\nOpslaan met OK.", L"Sciezka pliku.\nZmiana aktualizuje odniesienie.\nZapis po OK.", L"Dosya yolu.\nDegistirmek listedeki referansi gunceller.\nTamam ile kaydedilir."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT7), LL14(L"ファイルのタグから読み取った年です。\n読取専用です。", L"Year from the file tags.\nRead-only.", L"Annee lue depuis les balises du fichier.\nLecture seule.", L"Anno dai tag del file.\nSola lettura.", L"Ano de las etiquetas del archivo.\nSolo lectura.", L"파일 태그에서 읽은 연도입니다.\n읽기 전용.", L"从文件标签读取的年份。\n只读。", L"السنة من وسوم الملف.\nللقراءة فقط.", L"Год из тегов файла.\nТолько чтение.", L"Jahr aus Datei-Tags.\nNur Lesen.", L"Ano das tags do arquivo.\nSomente leitura.", L"Jaar uit bestandstags.\nAlleen lezen.", L"Rok z tagow pliku.\nTylko odczyt.", L"Dosya etiketinden yil.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT9), LL14(L"ファイルのタグから読み取ったトラック番号です。\n読取専用です。", L"Track number from the file tags.\nRead-only.", L"Numero de piste depuis les balises du fichier.\nLecture seule.", L"Numero traccia dai tag del file.\nSola lettura.", L"Numero de pista de las etiquetas.\nSolo lectura.", L"파일 태그의 트랙 번호입니다.\n읽기 전용.", L"从文件标签读取的曲目编号。\n只读。", L"رقم المسار من وسوم الملف.\nللقراءة فقط.", L"Номер трека из тегов файла.\nТолько чтение.", L"Titelnummer aus Datei-Tags.\nNur Lesen.", L"Numero da faixa das tags.\nSomente leitura.", L"Tracknummer uit bestandstags.\nAlleen lezen.", L"Numer utworu z tagow.\nTylko odczyt.", L"Dosya etiketindeki parca numarasi.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT10), LL14(L"ファイルのタグから読み取ったジャンルです。\n読取専用です。", L"Genre from the file tags.\nRead-only.", L"Genre depuis les balises du fichier.\nLecture seule.", L"Genere dai tag del file.\nSola lettura.", L"Genero de las etiquetas.\nSolo lectura.", L"파일 태그의 장르입니다.\n읽기 전용.", L"从文件标签读取的流派。\n只读。", L"النوع من وسوم الملف.\nللقراءة فقط.", L"Жанр из тегов файла.\nТолько чтение.", L"Genre aus Datei-Tags.\nNur Lesen.", L"Genero das tags.\nSomente leitura.", L"Genre uit bestandstags.\nAlleen lezen.", L"Gatunek z tagow.\nTylko odczyt.", L"Dosya etiketindeki tur.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT11), LL14(L"ファイルのタグから読み取ったコメントです。\n読取専用です。", L"Comment from the file tags.\nRead-only.", L"Commentaire depuis les balises du fichier.\nLecture seule.", L"Commento dai tag del file.\nSola lettura.", L"Comentario de las etiquetas.\nSolo lectura.", L"파일 태그의 코멘트입니다.\n읽기 전용.", L"从文件标签读取的注释。\n只读。", L"تعليق من وسوم الملف.\nللقراءة فقط.", L"Комментарий из тегов файла.\nТолько чтение.", L"Kommentar aus Datei-Tags.\nNur Lesen.", L"Comentario das tags.\nSomente leitura.", L"Opmerking uit bestandstags.\nAlleen lezen.", L"Komentarz z tagow.\nTylko odczyt.", L"Dosya etiketindeki yorum.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT2), LL14(L"曲の内部識別ID（ゲーム/形式ごとの番号）です。\n読取専用です。", L"Internal track ID (number per game/format).\nRead-only.", L"ID interne de la piste.\nLecture seule.", L"ID interno della traccia.\nSola lettura.", L"ID interno de la pista.\nSolo lectura.", L"곡의 내부 식별 ID입니다.\n읽기 전용.", L"曲目的内部识别 ID。\n只读。", L"المعرّف الداخلي للمسار.\nللقراءة فقط.", L"Внутренний ID трека.\nТолько чтение.", L"Interne Titel-ID.\nNur Lesen.", L"ID interno da faixa.\nSomente leitura.", L"Intern track-ID.\nAlleen lezen.", L"Wewnetrzne ID utworu.\nTylko odczyt.", L"Parcanin dahili kimligi.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT3), LL14(L"曲が属するゲーム名です。\n読取専用です。", L"Game this track belongs to.\nRead-only.", L"Jeu auquel appartient la piste.\nLecture seule.", L"Gioco di appartenenza.\nSola lettura.", L"Juego al que pertenece la pista.\nSolo lectura.", L"곡이 속한 게임 이름입니다.\n읽기 전용.", L"曲目所属的游戏名。\n只读。", L"اللعبة التي ينتمي إليها المسار.\nللقراءة فقط.", L"Игра, к которой относится трек.\nТолько чтение.", L"Spiel, zu dem der Titel gehort.\nNur Lesen.", L"Jogo ao qual a faixa pertence.\nSomente leitura.", L"Spel waartoe het nummer behoort.\nAlleen lezen.", L"Gra, do ktorej nalezy utwor.\nTylko odczyt.", L"Parcanin ait oldugu oyun.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT12), LL14(L"曲の再生時間です。\n読取専用です。", L"Track duration.\nRead-only.", L"Duree de la piste.\nLecture seule.", L"Durata della traccia.\nSola lettura.", L"Duracion de la pista.\nSolo lectura.", L"곡 재생 시간입니다.\n읽기 전용.", L"曲目时长。\n只读。", L"مدة المسار.\nللقراءة فقط.", L"Длительность трека.\nТолько чтение.", L"Titeldauer.\nNur Lesen.", L"Duracao da faixa.\nSomente leitura.", L"Duur van het nummer.\nAlleen lezen.", L"Czas trwania utworu.\nTylko odczyt.", L"Parca suresi.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT13), LL14(L"ループ開始位置（サンプル）です。\n左が開始、右が終了です。\n読取専用です。", L"Loop start position (samples).\nLeft = start, right = end.\nRead-only.", L"Debut de boucle (echantillons).\nGauche = debut, droite = fin.\nLecture seule.", L"Inizio loop (campioni).\nSinistra = inizio, destra = fine.\nSola lettura.", L"Inicio de bucle (muestras).\nIzquierda = inicio, derecha = fin.\nSolo lectura.", L"루프 시작 위치(샘플)입니다.\n왼쪽=시작, 오른쪽=종료.\n읽기 전용.", L"循环起始位置（采样）。\n左=开始，右=结束。\n只读。", L"بداية التكرار (عينات).\nاليسار=البداية، اليمين=النهاية.\nللقراءة فقط.", L"Начало петли (сэмплы).\nСлева — начало, справа — конец.\nТолько чтение.", L"Schleifenstart (Samples).\nLinks = Start, rechts = Ende.\nNur Lesen.", L"Inicio do loop (amostras).\nEsquerda = inicio, direita = fim.\nSomente leitura.", L"Loopstart (samples).\nLinks = start, rechts = einde.\nAlleen lezen.", L"Poczatek petli (probki).\nLewo = start, prawo = koniec.\nTylko odczyt.", L"Dongu baslangici (ornek).\nSol=baslangic, sag=bitis.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT14), LL14(L"ループ終了位置（サンプル）です。\n左が開始、右が終了です。\n読取専用です。", L"Loop end position (samples).\nLeft = start, right = end.\nRead-only.", L"Fin de boucle (echantillons).\nGauche = debut, droite = fin.\nLecture seule.", L"Fine loop (campioni).\nSinistra = inizio, destra = fine.\nSola lettura.", L"Fin de bucle (muestras).\nIzquierda = inicio, derecha = fin.\nSolo lectura.", L"루프 종료 위치(샘플)입니다.\n왼쪽=시작, 오른쪽=종료.\n읽기 전용.", L"循环结束位置（采样）。\n左=开始，右=结束。\n只读。", L"نهاية التكرار (عينات).\nاليسار=البداية، اليمين=النهاية.\nللقراءة فقط.", L"Конец петли (сэмплы).\nСлева — начало, справа — конец.\nТолько чтение.", L"Schleifenende (Samples).\nLinks = Start, rechts = Ende.\nNur Lesen.", L"Fim do loop (amostras).\nEsquerda = inicio, direita = fim.\nSomente leitura.", L"Loopeinde (samples).\nLinks = start, rechts = einde.\nAlleen lezen.", L"Koniec petli (probki).\nLewo = start, prawo = koniec.\nTylko odczyt.", L"Dongu bitisi (ornek).\nSol=baslangic, sag=bitis.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(IDC_EDIT15), LL14(L"同一ファイル内の曲インデックスです。\n読取専用です。", L"Track index within the same file.\nRead-only.", L"Index de piste dans le meme fichier.\nLecture seule.", L"Indice traccia nello stesso file.\nSola lettura.", L"Indice de pista en el mismo archivo.\nSolo lectura.", L"동일 파일 내 곡 인덱스입니다.\n읽기 전용.", L"同一文件内的曲目索引。\n只读。", L"فهرس المسار داخل نفس الملف.\nللقراءة فقط.", L"Индекс трека внутри файла.\nТолько чтение.", L"Titelindex in derselben Datei.\nNur Lesen.", L"Indice da faixa no mesmo arquivo.\nSomente leitura.", L"Trackindex in hetzelfde bestand.\nAlleen lezen.", L"Indeks utworu w tym samym pliku.\nTylko odczyt.", L"Ayni dosyadaki parca indeksi.\nSalt okunur."));
	m_tooltip.AddTool(GetDlgItem(ID_OK), LL14(L"プレイリスト表示の変更を保存して閉じます。", L"Save playlist display changes and close.", L"Enregistrer les modifications et fermer.", L"Salva le modifiche alla playlist e chiudi.", L"Guardar cambios de la lista y cerrar.", L"재생 목록 변경을 저장하고 닫습니다.", L"保存播放列表更改并关闭。", L"حفظ التغييرات وإغلاق النافذة.", L"Сохранить изменения и закрыть.", L"Anderungen speichern und schliessen.", L"Salvar alteracoes e fechar.", L"Wijzigingen opslaan en sluiten.", L"Zapisz zmiany i zamknij.", L"Degisiklikleri kaydet ve kapat."));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL), LL14(L"変更を保存せずに閉じます。", L"Close without saving changes.", L"Fermer sans enregistrer.", L"Chiudi senza salvare.", L"Cerrar sin guardar.", L"변경을 저장하지 않고 닫습니다.", L"不保存更改并关闭。", L"إغلاق دون حفظ التغييرات.", L"Закрыть без сохранения.", L"Ohne Speichern schliessen.", L"Fechar sem salvar.", L"Sluiten zonder opslaan.", L"Zamknij bez zapisywania.", L"Kaydetmeden kapat."));
	m_tooltip.AddTool(GetDlgItem(IDOK999), LL14(L"曲ファイルが入っているフォルダをエクスプローラーで開きます。", L"Open the folder containing the track in Explorer.", L"Ouvrir le dossier du fichier dans l'explorateur.", L"Apri la cartella del file in Explorer.", L"Abrir la carpeta del archivo en el explorador.", L"곡 파일이 있는 폴더를 탐색기로 엽니다.", L"在资源管理器中打开曲目所在文件夹。", L"فتح مجلد الملف في المستكشف.", L"Открыть папку файла в проводнике.", L"Ordner der Datei im Explorer offnen.", L"Abrir pasta do arquivo no Explorer.", L"Map van bestand openen in Verkenner.", L"Otworz folder pliku w Eksploratorze.", L"Dosyanin klasorunu Gezgin'de ac."));
	m_tooltip.SetDelayTime(TTDT_AUTOPOP, 10000);
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);

	return TRUE;
}

BOOL CListSyosai::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

//ワードラップを解除するためのコールバック関数
int CALLBACK EditWordBreakProc(LPTSTR lpch, int ichCurrent, int cch, int code)
{
	return (WB_ISDELIMITER == code) ? 0 : ichCurrent;
}
