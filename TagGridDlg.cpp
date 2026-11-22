#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "TagGridDlg.h"
#include "ExportTagUi.h"
#include "PlayList.h"
#include <set>
#include <algorithm>

extern COggDlg* og;
extern CString filen;
extern int plf;
extern int playf;
extern save savedata;
extern CPlayList* pl;

IMPLEMENT_DYNAMIC(CTagGridDlg, CCustomBlurDialogBase)

enum {
	TG_FILE = 0, TG_TITLE, TG_ARTIST, TG_ALBUM, TG_AA, TG_TRACK, TG_DISC, TG_YEAR,
	TG_GENRE, TG_COMP, TG_LYR, TG_COPY, TG_ENC, TG_BPM, TG_COMMENT, TG_COLS
};

static LPCTSTR TgCol(int col)
{
	switch (col) {
	case TG_FILE: return LL14(L"ファイル名", L"Filename", L"Fichier", L"File", L"Archivo", L"파일명", L"文件名", L"الملف", L"Файл", L"Datei", L"Ficheiro", L"Bestand", L"Plik", L"Dosya");
	case TG_TITLE: return LL14(L"曲名", L"Title", L"Titre", L"Titolo", L"Titulo", L"곡명", L"曲名", L"العنوان", L"Название", L"Titel", L"Titulo", L"Titel", L"Tytul", L"Baslik");
	case TG_ARTIST: return LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺人", L"الفنان", L"Артист", L"Kuenstler", L"Artista", L"Artiest", L"Artysta", L"Sanatci");
	case TG_ALBUM: return LL14(L"アルバム", L"Album", L"Album", L"Album", L"Album", L"앨범", L"专辑", L"الألبوم", L"Альбом", L"Album", L"Album", L"Album", L"Album", L"Album");
	case TG_AA: return LL14(L"アルバムの人", L"Album artist", L"Artiste album", L"Artista album", L"Artista album", L"앨범 아티스트", L"专辑艺人", L"فنان الألبوم", L"Артист альбома", L"Albumkuenstler", L"Artista do album", L"Albumartiest", L"Artysta albumu", L"Album sanatcisi");
	case TG_TRACK: return LL14(L"トラック", L"Track", L"Piste", L"Traccia", L"Pista", L"트랙", L"音轨", L"المسار", L"Трек", L"Titel", L"Faixa", L"Track", L"Sciezka", L"Parca");
	case TG_DISC: return LL14(L"ディスク", L"Disc", L"Disque", L"Disco", L"Disco", L"디스크", L"碟", L"القرص", L"Диск", L"Disc", L"Disco", L"Schijf", L"Plyta", L"Disk");
	case TG_YEAR: return LL14(L"年", L"Year", L"Annee", L"Anno", L"Ano", L"연도", L"年", L"السنة", L"Год", L"Jahr", L"Ano", L"Jaar", L"Rok", L"Yil");
	case TG_GENRE: return LL14(L"ジャンル", L"Genre", L"Genre", L"Genere", L"Genero", L"장르", L"流派", L"النوع", L"Жанр", L"Genre", L"Genero", L"Genre", L"Gatunek", L"Tur");
	case TG_COMP: return LL14(L"作曲", L"Composer", L"Compositeur", L"Compositore", L"Compositor", L"작곡", L"作曲", L"الملحن", L"Композитор", L"Komponist", L"Compositor", L"Componist", L"Kompozytor", L"Besteci");
	case TG_LYR: return LL14(L"作詞", L"Lyricist", L"Parolier", L"Paroliere", L"Letrista", L"작사", L"作词", L"كاتب الكلمات", L"Автор текста", L"Texter", L"Letrista", L"Tekstschrijver", L"Autor tekstu", L"Soz yazari");
	case TG_COPY: return LL14(L"著作権", L"Copyright", L"Copyright", L"Copyright", L"Copyright", L"저작권", L"版权", L"الحقوق", L"Права", L"Copyright", L"Copyright", L"Copyright", L"Prawa", L"Telif");
	case TG_ENC: return LL14(L"エンコーダ", L"Encoder", L"Encodeur", L"Encoder", L"Codificador", L"인코더", L"编码器", L"المرمز", L"Кодер", L"Encoder", L"Codificador", L"Encoder", L"Koder", L"Kodlayici");
	case TG_BPM: return L"BPM";
	case TG_COMMENT: return LL14(L"コメント", L"Comment", L"Commentaire", L"Commento", L"Comentario", L"설명", L"注释", L"تعليق", L"Комментарий", L"Kommentar", L"Comentario", L"Opmerking", L"Komentarz", L"Yorum");
	default: return L"";
	}
}

static CString* TgField(CTagGridDlg::Row& r, int col)
{
	switch (col) {
	case TG_FILE: return &r.fileName;
	case TG_TITLE: return &r.tags.title;
	case TG_ARTIST: return &r.tags.artist;
	case TG_ALBUM: return &r.tags.album;
	case TG_AA: return &r.tags.albumArtist;
	case TG_TRACK: return &r.tags.track;
	case TG_DISC: return &r.tags.disc;
	case TG_YEAR: return &r.tags.year;
	case TG_GENRE: return &r.tags.genre;
	case TG_COMP: return &r.tags.composer;
	case TG_LYR: return &r.tags.lyricist;
	case TG_COPY: return &r.tags.copyright;
	case TG_ENC: return &r.tags.encoder;
	case TG_BPM: return &r.tags.bpm;
	case TG_COMMENT: return &r.tags.comment;
	default: return NULL;
	}
}

static CString TgExtOf(const CString& name)
{
	int dot = name.ReverseFind(L'.');
	if (dot <= 0) return L"";
	return name.Mid(dot);
}

static BOOL TgAudio(const CString& path)
{
	CString e = path;
	e.MakeLower();
	const int slash = (std::max)(e.ReverseFind(L'\\'), e.ReverseFind(L'/'));
	if (slash >= 0) e = e.Mid(slash + 1);
	static const wchar_t* k[] = {
		L".mp3", L".mp2", L".mp1", L".wav", L".flac", L".ogg", L".m4a", L".aac", L".dsf"
	};
	for (int i = 0; i < 9; ++i) {
		const int n = (int)wcslen(k[i]);
		if (e.GetLength() >= n && e.Right(n) == k[i]) return TRUE;
	}
	return FALSE;
}

static void TgCollect(const CString& path, std::vector<CString>& out, int& guard)
{
	if (guard >= 8000) return;
	DWORD a = GetFileAttributes(path);
	if (a == INVALID_FILE_ATTRIBUTES) return;
	if (!(a & FILE_ATTRIBUTE_DIRECTORY)) {
		if (TgAudio(path)) { out.push_back(path); guard++; }
		return;
	}
	CString spec = path;
	if (spec.Right(1) != L"\\" && spec.Right(1) != L"/") spec += L"\\";
	CFileFind ff;
	BOOL more = ff.FindFile(spec + L"*.*");
	while (more && guard < 8000) {
		more = ff.FindNextFile();
		if (ff.IsDots()) continue;
		TgCollect(ff.GetFilePath(), out, guard);
	}
}

static CString TgCleanName(CString s)
{
	const wchar_t* bad = L"\\/:*?\"<>|";
	for (int i = 0; i < s.GetLength(); ++i)
		if (wcschr(bad, s[i])) s.SetAt(i, L'_');
	s.Trim();
	while (!s.IsEmpty() && (s[s.GetLength() - 1] == L' ' || s[s.GetLength() - 1] == L'.'))
		s.Truncate(s.GetLength() - 1);
	return s;
}

static CString TgApplyPat(CString pat, const FileTagFields& f)
{
	struct Tok { const wchar_t* k; const CString* v; };
	Tok t[] = {
		{ L"%title%", &f.title }, { L"%artist%", &f.artist }, { L"%album%", &f.album },
		{ L"%albumartist%", &f.albumArtist }, { L"%year%", &f.year }, { L"%track%", &f.track },
		{ L"%disc%", &f.disc }, { L"%genre%", &f.genre }, { L"%composer%", &f.composer }, { L"%bpm%", &f.bpm }
	};
	for (int i = 0; i < 10; ++i)
		pat.Replace(t[i].k, *t[i].v);
	return TgCleanName(pat);
}

static BOOL TgSidecar(LPCTSTR media, LPCTSTR image)
{
	CString m = media;
	const int slash = (std::max)(m.ReverseFind(L'\\'), m.ReverseFind(L'/'));
	if (slash < 0) return FALSE;
	CString dir = m.Left(slash + 1);
	return CopyFile(image, dir + L"folder.jpg", FALSE) || CopyFile(image, dir + L"cover.jpg", FALSE);
}

enum {
	TG_M_ADD = 1,
	TG_M_DIR,
	TG_M_NUM,
	TG_M_COVER,
	TG_M_UNCOVER,
	TG_M_LYR,
	TG_M_EDITCELL,
	TG_M_SAVE,
	TG_M_APPLY,
	TG_M_PRESET_EDIT,
	TG_M_PRESET0 = 100,
	TG_PRESET_MAX = 40
};

static CString TgOneLine(CString s)
{
	s.Replace(L"\t", L" ");
	s.Replace(L"\r", L" ");
	s.Replace(L"\n", L" ");
	s.Trim();
	return s;
}

static CString TgPresetPath()
{
	TCHAR base[MAX_PATH] = { 0 };
	if (FAILED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base)) || !base[0])
		return CString();
	CString dir = base;
	dir += _T("\\oggYSED");
	CreateDirectory(dir, NULL);
	return dir + _T("\\tag-name-presets.txt");
}

static void TgDefaultPresets(std::vector<std::pair<CString, CString> >& out)
{
	struct Item { LPCTSTR name; LPCTSTR pat; };
	const Item d[] = {
		{ LL14(L"アーティスト - 曲名", L"Artist - title", L"Artiste - titre", L"Artista - titolo", L"Artista - titulo", L"아티스트 - 곡명", L"艺人 - 曲名", L"الفنان - العنوان", L"Артист - название", L"Kuenstler - Titel", L"Artista - titulo", L"Artiest - titel", L"Artysta - tytul", L"Sanatci - baslik"), L"%artist% - %title%" },
		{ LL14(L"曲名だけ", L"Title only", L"Titre seul", L"Solo titolo", L"Solo titulo", L"곡명만", L"仅曲名", L"العنوان فقط", L"Только название", L"Nur Titel", L"So titulo", L"Alleen titel", L"Sam tytul", L"Sadece baslik"), L"%title%" },
		{ LL14(L"番号 曲名", L"Number title", L"Numero titre", L"Numero titolo", L"Numero titulo", L"번호 곡명", L"编号 曲名", L"رقم العنوان", L"Номер название", L"Nummer Titel", L"Numero titulo", L"Nummer titel", L"Numer tytul", L"Numara baslik"), L"%track% %title%" },
		{ LL14(L"番号. アーティスト - 曲名", L"Number. artist - title", L"Numero. artiste - titre", L"Numero. artista - titolo", L"Numero. artista - titulo", L"번호. 아티스트 - 곡명", L"编号. 艺人 - 曲名", L"رقم. الفنان - العنوان", L"Номер. артист - название", L"Nummer. Kuenstler - Titel", L"Numero. artista - titulo", L"Nummer. artiest - titel", L"Numer. artysta - tytul", L"Numara. sanatci - baslik"), L"%track%. %artist% - %title%" },
		{ LL14(L"アルバム - 番号 曲名", L"Album - number title", L"Album - numero titre", L"Album - numero titolo", L"Album - numero titulo", L"앨범 - 번호 곡명", L"专辑 - 编号 曲名", L"الألبوم - رقم العنوان", L"Альбом - номер название", L"Album - Nummer Titel", L"Album - numero titulo", L"Album - nummer titel", L"Album - numer tytul", L"Album - numara baslik"), L"%album% - %track% %title%" },
		{ LL14(L"ディスク-番号 曲名", L"Disc-number title", L"Disque-numero titre", L"Disco-numero titolo", L"Disco-numero titulo", L"디스크-번호 곡명", L"碟-编号 曲名", L"القرص-رقم العنوان", L"Диск-номер название", L"Disc-Nummer Titel", L"Disco-numero titulo", L"Schijf-nummer titel", L"Plyta-numer tytul", L"Disk-numara baslik"), L"%disc%-%track% %title%" }
	};
	for (int i = 0; i < (int)(sizeof(d) / sizeof(d[0])); ++i)
		out.push_back(std::pair<CString, CString>(d[i].name, d[i].pat));
}

static void TgLoadPresets(std::vector<std::pair<CString, CString> >& out)
{
	out.clear();
	const CString path = TgPresetPath();
	CStdioFile f;
	if (!path.IsEmpty() && f.Open(path, CFile::modeRead | CFile::typeUnicode | CFile::shareDenyWrite)) {
		CString line;
		while (f.ReadString(line) && (int)out.size() < TG_PRESET_MAX) {
			line.Trim();
			const int tab = line.Find(L'\t');
			if (tab <= 0) continue;
			CString name = TgOneLine(line.Left(tab));
			CString pat = TgOneLine(line.Mid(tab + 1));
			if (name.IsEmpty() || pat.IsEmpty()) continue;
			if (name.GetLength() > 120) name = name.Left(120);
			if (pat.GetLength() > 240) pat = pat.Left(240);
			out.push_back(std::pair<CString, CString>(name, pat));
		}
		f.Close();
	}
	if (out.empty()) TgDefaultPresets(out);
}

static void TgSavePresets(const std::vector<std::pair<CString, CString> >& in)
{
	const CString path = TgPresetPath();
	if (path.IsEmpty()) return;
	CStdioFile f;
	if (!f.Open(path, CFile::modeCreate | CFile::modeWrite | CFile::typeUnicode))
		return;
	for (size_t i = 0; i < in.size() && i < (size_t)TG_PRESET_MAX; ++i) {
		CString name = TgOneLine(in[i].first);
		CString pat = TgOneLine(in[i].second);
		if (name.IsEmpty() || pat.IsEmpty()) continue;
		f.WriteString(name + L"\t" + pat + L"\r\n");
	}
	f.Close();
}

static LPCTSTR TgTokenHelp()
{
	return LL14(
		L"%title% 曲名    %artist% アーティスト    %album% アルバム\n%albumartist% アルバムの人    %track% 番号    %disc% ディスク\n%year% 年    %genre% ジャンル    %composer% 作曲    %bpm% BPM\nこの並びにファイル名が変わります。拡張子はそのままです。",
		L"%title% title    %artist% artist    %album% album\n%albumartist% album artist    %track% number    %disc% disc\n%year% year    %genre% genre    %composer% composer    %bpm% BPM\nThe filename becomes this order. The extension stays.",
		L"%title% titre    %artist% artiste    %album% album\n%albumartist% artiste album    %track% numero    %disc% disque\n%year% annee    %genre% genre    %composer% compositeur    %bpm% BPM\nLe fichier prend cet ordre. L'extension reste.",
		L"%title% titolo    %artist% artista    %album% album\n%albumartist% artista album    %track% numero    %disc% disco\n%year% anno    %genre% genere    %composer% compositore    %bpm% BPM\nIl file diventa questo ordine. L'estensione resta.",
		L"%title% titulo    %artist% artista    %album% album\n%albumartist% artista album    %track% numero    %disc% disco\n%year% ano    %genre% genero    %composer% compositor    %bpm% BPM\nEl archivo queda en este orden. La extension se queda.",
		L"%title% 곡명    %artist% 아티스트    %album% 앨범\n%albumartist% 앨범 아티스트    %track% 번호    %disc% 디스크\n%year% 연도    %genre% 장르    %composer% 작곡    %bpm% BPM\n파일 이름이 이 순서가 됩니다. 확장자는 그대로입니다.",
		L"%title% 曲名    %artist% 艺人    %album% 专辑\n%albumartist% 专辑艺人    %track% 编号    %disc% 碟\n%year% 年    %genre% 流派    %composer% 作曲    %bpm% BPM\n文件名会变成这个顺序。扩展名保持不变。",
		L"%title% العنوان    %artist% الفنان    %album% الألبوم\n%albumartist% فنان الألبوم    %track% الرقم    %disc% القرص\n%year% السنة    %genre% النوع    %composer% الملحن    %bpm% BPM\nيصير اسم الملف بهذا الترتيب. الامتداد يبقى.",
		L"%title% название    %artist% артист    %album% альбом\n%albumartist% артист альбома    %track% номер    %disc% диск\n%year% год    %genre% жанр    %composer% композитор    %bpm% BPM\nИмя файла станет этим порядком. Расширение остаётся.",
		L"%title% Titel    %artist% Kuenstler    %album% Album\n%albumartist% Albumkuenstler    %track% Nummer    %disc% Disc\n%year% Jahr    %genre% Genre    %composer% Komponist    %bpm% BPM\nDer Dateiname wird diese Reihenfolge. Die Endung bleibt.",
		L"%title% titulo    %artist% artista    %album% album\n%albumartist% artista do album    %track% numero    %disc% disco\n%year% ano    %genre% genero    %composer% compositor    %bpm% BPM\nO ficheiro fica nesta ordem. A extensao mantem-se.",
		L"%title% titel    %artist% artiest    %album% album\n%albumartist% albumartiest    %track% nummer    %disc% schijf\n%year% jaar    %genre% genre    %composer% componist    %bpm% BPM\nDe bestandsnaam wordt deze volgorde. De extensie blijft.",
		L"%title% tytul    %artist% artysta    %album% album\n%albumartist% artysta albumu    %track% numer    %disc% plyta\n%year% rok    %genre% gatunek    %composer% kompozytor    %bpm% BPM\nNazwa pliku przyjmie te kolejnosc. Rozszerzenie zostaje.",
		L"%title% baslik    %artist% sanatci    %album% album\n%albumartist% album sanatcisi    %track% numara    %disc% disk\n%year% yil    %genre% tur    %composer% besteci    %bpm% BPM\nDosya adi bu siraya donusur. Uzanti ayni kalir.");
}

class CTagPresetDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CTagPresetDlg)
public:
	explicit CTagPresetDlg(const CString& seedPat, CWnd* pParent)
		: CCustomBlurDialogBase(IDD_TAGPRESET, pParent)
		, m_seed(seedPat)
	{
	}
	enum { IDD = IDD_TAGPRESET };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual void OnOK();
	virtual void OnCancel();
	std::vector<std::pair<CString, CString> > m_items;
	CString m_seed;
	CCustomListBox m_list;
	CCustomEdit m_name;
	CCustomEdit m_pat;
	CCustomStatic m_legend;
	CCustomStandardButton m_add, m_upd, m_del, m_close;

	void Fill();
	void Remember(int sel);
	afx_msg void OnAdd();
	afx_msg void OnUpd();
	afx_msg void OnDel();
	afx_msg void OnCloseBtn();
	afx_msg void OnSel();
	DECLARE_MESSAGE_MAP()
};

IMPLEMENT_DYNAMIC(CTagPresetDlg, CCustomBlurDialogBase)

void CTagPresetDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TP_LIST, m_list);
	DDX_Control(pDX, IDC_TP_NAME, m_name);
	DDX_Control(pDX, IDC_TP_PAT, m_pat);
	DDX_Control(pDX, IDC_TP_LEGEND, m_legend);
	DDX_Control(pDX, IDC_TP_ADD, m_add);
	DDX_Control(pDX, IDC_TP_UPD, m_upd);
	DDX_Control(pDX, IDC_TP_DEL, m_del);
	DDX_Control(pDX, IDC_TP_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CTagPresetDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_TP_ADD, OnAdd)
	ON_BN_CLICKED(IDC_TP_UPD, OnUpd)
	ON_BN_CLICKED(IDC_TP_DEL, OnDel)
	ON_BN_CLICKED(IDC_TP_CLOSE, OnCloseBtn)
	ON_LBN_SELCHANGE(IDC_TP_LIST, OnSel)
END_MESSAGE_MAP()

void CTagPresetDlg::Fill()
{
	m_list.ResetContent();
	for (size_t i = 0; i < m_items.size(); ++i) {
		CString line = m_items[i].first + L"    " + m_items[i].second;
		m_list.AddString(line);
	}
}

void CTagPresetDlg::Remember(int sel)
{
	TgSavePresets(m_items);
	Fill();
	if (sel >= (int)m_items.size()) sel = (int)m_items.size() - 1;
	if (sel >= 0) m_list.SetCurSel(sel);
}

BOOL CTagPresetDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"ファイル名の型", L"Filename pattern", L"Modele de nom", L"Modello nome", L"Patron de nombre", L"파일 이름 형식", L"文件名格式", L"نمط اسم الملف", L"Шаблон имени", L"Dateinamensmuster", L"Padrao do nome", L"Bestandsnaampatroon", L"Wzor nazwy", L"Dosya adi kalibi"));
	if (CWnd* w = GetDlgItem(IDC_TP_NAME_L))
		w->SetWindowText(LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"));
	if (CWnd* w = GetDlgItem(IDC_TP_PAT_L))
		w->SetWindowText(LL14(L"型", L"Pattern", L"Modele", L"Modello", L"Patron", L"형식", L"格式", L"النمط", L"Шаблон", L"Muster", L"Padrao", L"Patroon", L"Wzor", L"Kalip"));
	m_add.SetWindowText(LL14(L"追加", L"Add", L"Ajouter", L"Aggiungi", L"Anadir", L"추가", L"添加", L"إضافة", L"Добавить", L"Hinzufuegen", L"Adicionar", L"Toevoegen", L"Dodaj", L"Ekle"));
	m_upd.SetWindowText(LL14(L"更新", L"Update", L"Modifier", L"Aggiorna", L"Actualizar", L"고치기", L"更新", L"تحديث", L"Обновить", L"Aktualisieren", L"Atualizar", L"Bijwerken", L"Zmien", L"Guncelle"));
	m_del.SetWindowText(LL14(L"削除", L"Delete", L"Supprimer", L"Elimina", L"Borrar", L"삭제", L"删除", L"حذف", L"Удалить", L"Loeschen", L"Apagar", L"Wissen", L"Usun", L"Sil"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_legend.SetWindowText(TgTokenHelp());
	TgLoadPresets(m_items);
	Fill();
	CString seed = TgOneLine(m_seed);
	int found = -1;
	if (!seed.IsEmpty()) {
		for (int i = 0; i < (int)m_items.size(); ++i) {
			if (m_items[i].second.Compare(seed) == 0) { found = i; break; }
		}
	}
	if (found >= 0) {
		m_list.SetCurSel(found);
		OnSel();
		return TRUE;
	}
	if (!seed.IsEmpty()) m_pat.SetWindowText(seed);
	m_name.SetFocus();
	return FALSE;
}

void CTagPresetDlg::OnSel()
{
	const int i = m_list.GetCurSel();
	if (i < 0 || i >= (int)m_items.size()) return;
	m_name.SetWindowText(m_items[i].first);
	m_pat.SetWindowText(m_items[i].second);
}

void CTagPresetDlg::OnAdd()
{
	CString name, pat;
	m_name.GetWindowText(name);
	m_pat.GetWindowText(pat);
	name = TgOneLine(name);
	pat = TgOneLine(pat);
	if (pat.IsEmpty()) return;
	if (name.IsEmpty()) name = pat.Left(40);
	if (name.GetLength() > 120) name = name.Left(120);
	if (pat.GetLength() > 240) pat = pat.Left(240);
	int found = -1;
	for (int i = 0; i < (int)m_items.size(); ++i) {
		if (m_items[i].first.Compare(name) == 0) { found = i; break; }
	}
	if (found >= 0) m_items[found].second = pat;
	else {
		if ((int)m_items.size() >= TG_PRESET_MAX) return;
		m_items.push_back(std::pair<CString, CString>(name, pat));
		found = (int)m_items.size() - 1;
	}
	Remember(found);
}

void CTagPresetDlg::OnUpd()
{
	int i = m_list.GetCurSel();
	if (i < 0 || i >= (int)m_items.size()) return;
	CString name, pat;
	m_name.GetWindowText(name);
	m_pat.GetWindowText(pat);
	name = TgOneLine(name);
	pat = TgOneLine(pat);
	if (name.IsEmpty() || pat.IsEmpty()) return;
	if (name.GetLength() > 120) name = name.Left(120);
	if (pat.GetLength() > 240) pat = pat.Left(240);
	for (int j = 0; j < (int)m_items.size(); ) {
		if (j != i && m_items[j].first.Compare(name) == 0) {
			m_items.erase(m_items.begin() + j);
			if (j < i) --i;
		}
		else ++j;
	}
	if (i < 0 || i >= (int)m_items.size()) return;
	m_items[i].first = name;
	m_items[i].second = pat;
	Remember(i);
}

void CTagPresetDlg::OnDel()
{
	const int i = m_list.GetCurSel();
	if (i < 0 || i >= (int)m_items.size()) return;
	m_items.erase(m_items.begin() + i);
	Remember(i);
}

void CTagPresetDlg::OnCloseBtn() { EndDialog(IDCANCEL); }
void CTagPresetDlg::OnOK() { OnAdd(); }
void CTagPresetDlg::OnCancel() { EndDialog(IDCANCEL); }

CTagGridDlg::CTagGridDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CTagGridDlg::IDD, pParent)
	, m_coverBmp(NULL)
	, m_editRow(-1)
	, m_editCol(-1)
	, m_inCommit(false)
	, m_fillLyrics(false)
	, m_lyrDirty(false)
	, m_adding(false)
	, m_menuTick(0)
{
}

CTagGridDlg::~CTagGridDlg()
{
	if (m_coverBmp) ::DeleteObject(m_coverBmp);
}

void CTagGridDlg::Seed(const std::vector<CString>& paths)
{
	m_seed = paths;
}

void CTagGridDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TG_LIST, m_list);
	DDX_Control(pDX, IDC_TG_ADD, m_add);
	DDX_Control(pDX, IDC_TG_DIR, m_dir);
	DDX_Control(pDX, IDC_TG_PAT, m_pat);
	DDX_Control(pDX, IDC_TG_NAME, m_name);
	DDX_Control(pDX, IDC_TG_NUM, m_num);
	DDX_Control(pDX, IDC_TG_SAVE, m_save);
	DDX_Control(pDX, IDC_TG_CLOSE, m_close);
	DDX_Control(pDX, IDC_TG_COVER, m_coverPic);
	DDX_Control(pDX, IDC_TG_COVER_H, m_coverH);
	DDX_Control(pDX, IDC_TG_COVER_BROWSE, m_coverBrowse);
	DDX_Control(pDX, IDC_TG_COVER_CLEAR, m_coverClear);
	DDX_Control(pDX, IDC_TG_LYRICS_L, m_lyricsL);
	DDX_Control(pDX, IDC_TG_LYRICS, m_lyrics);
	DDX_Control(pDX, IDC_TG_LYR_APPLY, m_lyrApply);
	DDX_Control(pDX, IDC_TG_STATUS, m_status);
	DDX_Control(pDX, IDC_TG_HINT, m_hint);
}

BEGIN_MESSAGE_MAP(CTagGridDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_TG_ADD, OnAdd)
	ON_BN_CLICKED(IDC_TG_DIR, OnDir)
	ON_BN_CLICKED(IDC_TG_NAME, OnName)
	ON_BN_CLICKED(IDC_TG_NUM, OnNum)
	ON_BN_CLICKED(IDC_TG_SAVE, OnSave)
	ON_BN_CLICKED(IDC_TG_CLOSE, OnCloseBtn)
	ON_BN_CLICKED(IDC_TG_COVER_BROWSE, OnCoverBrowse)
	ON_BN_CLICKED(IDC_TG_COVER_CLEAR, OnCoverClear)
	ON_BN_CLICKED(IDC_TG_LYR_APPLY, OnLyrApply)
	ON_EN_CHANGE(IDC_TG_LYRICS, OnLyricsChange)
	ON_EN_KILLFOCUS(IDC_TG_CELL, OnCellKill)
	ON_NOTIFY(NM_RCLICK, IDC_TG_LIST, OnListRClick)
	ON_NOTIFY(NM_CLICK, IDC_TG_LIST, OnListClick)
	ON_WM_CONTEXTMENU()
	ON_NOTIFY(NM_DBLCLK, IDC_TG_LIST, OnListDbl)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_TG_LIST, OnListItemChanged)
	ON_WM_DROPFILES()
	ON_WM_SIZE()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CTagGridDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"タグ一覧", L"Tag grid", L"Grille de tags", L"Griglia tag", L"Tabla de etiquetas", L"태그 목록", L"标签表", L"جدول الوسوم", L"Сетка тегов", L"Tag-Tabelle", L"Grelha de tags", L"Tagtabel", L"Tabela tagow", L"Etiket tablosu"));
	m_add.SetWindowText(LL14(L"ファイル", L"Files", L"Fichiers", L"File", L"Archivos", L"파일", L"文件", L"ملفات", L"Файлы", L"Dateien", L"Ficheiros", L"Bestanden", L"Pliki", L"Dosyalar"));
	m_dir.SetWindowText(LL14(L"フォルダ", L"Folder", L"Dossier", L"Cartella", L"Carpeta", L"폴더", L"文件夹", L"مجلد", L"Папка", L"Ordner", L"Pasta", L"Map", L"Folder", L"Klasor"));
	m_name.SetWindowText(LL14(L"名前へ", L"To name", L"Vers le nom", L"Nel nome", L"Al nombre", L"이름에", L"写入名", L"إلى الاسم", L"В имя", L"In Name", L"Para o nome", L"Naar naam", L"Do nazwy", L"Ada"));
	m_num.SetWindowText(LL14(L"連番", L"Number", L"Numeroter", L"Numera", L"Numerar", L"번호", L"编号", L"ترقيم", L"Номера", L"Nummer", L"Numerar", L"Nummer", L"Numeruj", L"Numara"));
	m_save.SetWindowText(LL14(L"保存", L"Save", L"Enregistrer", L"Salva", L"Guardar", L"저장", L"保存", L"حفظ", L"Сохранить", L"Speichern", L"Guardar", L"Opslaan", L"Zapisz", L"Kaydet"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_coverBrowse.SetWindowText(LL14(L"ジャケット", L"Cover", L"Pochette", L"Copertina", L"Portada", L"재킷", L"封面", L"الغلاف", L"Обложка", L"Cover", L"Capa", L"Hoes", L"Okładka", L"Kapak"));
	m_coverClear.SetWindowText(LL14(L"外す", L"Undo", L"Retirer", L"Togli", L"Quitar", L"해제", L"取消", L"إلغاء", L"Снять", L"Weg", L"Tirar", L"Weg", L"Cofnij", L"Kaldir"));
	m_lyrApply.SetWindowText(LL14(L"歌詞を選択へ", L"Lyrics to selection", L"Paroles vers la selection", L"Testo alla selezione", L"Letra a la seleccion", L"가사를 선택에", L"歌词写入所选", L"الكلمات إلى التحديد", L"Текст в выбор", L"Text in Auswahl", L"Letra na selecao", L"Tekst naar selectie", L"Tekst do zaznaczenia", L"Sozler secime"));
	m_lyricsL.SetWindowText(LL14(L"歌詞", L"Lyrics", L"Paroles", L"Testo", L"Letra", L"가사", L"歌词", L"الكلمات", L"Текст", L"Text", L"Letra", L"Tekst", L"Tekst", L"Sozler"));
	m_pat.SetWindowText(L"%artist% - %title%");
	m_hint.SetWindowText(LL14(
		L"右クリックで追加、型、連番、ジャケット、保存。下の欄が今の型です。\n%title% 曲名   %artist% アーティスト   %album% アルバム   %albumartist% アルバムの人\n%track% 番号   %disc% ディスク   %year% 年   %genre% ジャンル   %composer% 作曲   %bpm% BPM",
		L"Right-click to add, pick a pattern, number, set a cover, or save. The box below is the current pattern.\n%title% title   %artist% artist   %album% album   %albumartist% album artist\n%track% number   %disc% disc   %year% year   %genre% genre   %composer% composer   %bpm% BPM",
		L"Clic droit : ajouter, modele, numero, pochette, enregistrer. La case est le modele actuel.\n%title% titre   %artist% artiste   %album% album   %albumartist% artiste album\n%track% numero   %disc% disque   %year% annee   %genre% genre   %composer% compositeur   %bpm% BPM",
		L"Clic destro: aggiungi, modello, numero, copertina, salva. La casella e il modello attuale.\n%title% titolo   %artist% artista   %album% album   %albumartist% artista album\n%track% numero   %disc% disco   %year% anno   %genre% genere   %composer% compositore   %bpm% BPM",
		L"Clic derecho: anadir, patron, numero, portada, guardar. La casilla es el patron actual.\n%title% titulo   %artist% artista   %album% album   %albumartist% artista album\n%track% numero   %disc% disco   %year% ano   %genre% genero   %composer% compositor   %bpm% BPM",
		L"오른쪽 클릭으로 추가, 형식, 번호, 재킷, 저장. 아래 칸이 현재 형식입니다.\n%title% 곡명   %artist% 아티스트   %album% 앨범   %albumartist% 앨범 아티스트\n%track% 번호   %disc% 디스크   %year% 연도   %genre% 장르   %composer% 작곡   %bpm% BPM",
		L"右键可以添加、选格式、编号、封面、保存。下面一栏是当前格式。\n%title% 曲名   %artist% 艺人   %album% 专辑   %albumartist% 专辑艺人\n%track% 编号   %disc% 碟   %year% 年   %genre% 流派   %composer% 作曲   %bpm% BPM",
		L"النقرة اليمنى للإضافة والنمط والترقيم والغلاف والحفظ. المربع هو النمط الحالي.\n%title% العنوان   %artist% الفنان   %album% الألبوم   %albumartist% فنان الألبوم\n%track% الرقم   %disc% القرص   %year% السنة   %genre% النوع   %composer% الملحن   %bpm% BPM",
		L"Правый щелчок: добавить, шаблон, номера, обложку, сохранить. Поле ниже — текущий шаблон.\n%title% название   %artist% артист   %album% альбом   %albumartist% артист альбома\n%track% номер   %disc% диск   %year% год   %genre% жанр   %composer% композитор   %bpm% BPM",
		L"Rechtsklick: hinzufuegen, Muster, Nummer, Cover, speichern. Das Feld ist das aktuelle Muster.\n%title% Titel   %artist% Kuenstler   %album% Album   %albumartist% Albumkuenstler\n%track% Nummer   %disc% Disc   %year% Jahr   %genre% Genre   %composer% Komponist   %bpm% BPM",
		L"Clique direito: adicionar, padrao, numero, capa, guardar. A caixa e o padrao atual.\n%title% titulo   %artist% artista   %album% album   %albumartist% artista do album\n%track% numero   %disc% disco   %year% ano   %genre% genero   %composer% compositor   %bpm% BPM",
		L"Rechtsklikken: toevoegen, patroon, nummer, hoes, opslaan. Het vak is het huidige patroon.\n%title% titel   %artist% artiest   %album% album   %albumartist% albumartiest\n%track% nummer   %disc% schijf   %year% jaar   %genre% genre   %composer% componist   %bpm% BPM",
		L"Prawy przycisk: dodaj, wzor, numer, okladke, zapisz. Pole ponizej to biezacy wzor.\n%title% tytul   %artist% artysta   %album% album   %albumartist% artysta albumu\n%track% numer   %disc% plyta   %year% rok   %genre% gatunek   %composer% kompozytor   %bpm% BPM",
		L"Sag tik: ekle, kalip, numara, kapak, kaydet. Alttaki kutu simdiki kaliptir.\n%title% baslik   %artist% sanatci   %album% album   %albumartist% album sanatcisi\n%track% numara   %disc% disk   %year% yil   %genre% tur   %composer% besteci   %bpm% BPM"));
	m_add.ShowWindow(SW_HIDE);
	m_dir.ShowWindow(SW_HIDE);
	m_name.ShowWindow(SW_HIDE);
	m_num.ShowWindow(SW_HIDE);
	m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP | LVS_EX_LABELTIP);
	static const int w[] = { 180, 160, 120, 140, 120, 52, 44, 48, 80, 100, 100, 90, 80, 48, 140 };
	for (int c = 0; c < TG_COLS; ++c)
		m_list.InsertColumn(c, TgCol(c), LVCFMT_LEFT, w[c]);
	m_list.EnableGroupView(TRUE);
	m_cell.Create(WS_CHILD | ES_AUTOHSCROLL | WS_BORDER, CRect(0, 0, 10, 16), &m_list, IDC_TG_CELL);
	m_cell.SetFont(m_list.GetFont());
	DragAcceptFiles(TRUE);
	m_list.ModifyStyleEx(0, WS_EX_ACCEPTFILES);
	m_list.DragAcceptFiles(TRUE);
	if (!m_seed.empty()) AddPaths(m_seed);
	SetWindowPos(NULL, 0, 0, 1180, 760, SWP_NOMOVE | SWP_NOZORDER);
	CenterWindow();
	StatusCount();
	return TRUE;
}

void CTagGridDlg::AddPaths(const std::vector<CString>& paths)
{
	m_adding = true;
	std::set<CString> seen;
	for (size_t i = 0; i < m_rows.size(); ++i) {
		CString k = m_rows[i].path;
		k.MakeLower();
		seen.insert(k);
	}
	std::vector<CString> files;
	int guard = (int)m_rows.size();
	for (size_t i = 0; i < paths.size(); ++i)
		TgCollect(paths[i], files, guard);
	for (size_t i = 0; i < files.size(); ++i) {
		CString key = files[i];
		key.MakeLower();
		if (seen.find(key) != seen.end()) continue;
		seen.insert(key);
		Row r;
		r.path = files[i];
		int slash = (std::max)(r.path.ReverseFind(L'\\'), r.path.ReverseFind(L'/'));
		r.fileName = slash >= 0 ? r.path.Mid(slash + 1) : r.path;
		CString folder = slash >= 0 ? r.path.Left(slash) : L"";
		ReadFileTagFields(r.path, r.tags);
		m_rows.push_back(r);
		const int idx = (int)m_rows.size() - 1;
		CString gkey = folder;
		gkey.MakeLower();
		int gid = 0;
		std::map<CString, int>::iterator it = m_groups.find(gkey);
		if (it == m_groups.end()) {
			gid = (int)m_groups.size() + 1;
			m_groups[gkey] = gid;
			LVGROUP g;
			memset(&g, 0, sizeof(g));
			g.cbSize = sizeof(g);
			g.mask = LVGF_HEADER | LVGF_GROUPID;
			g.iGroupId = gid;
			g.pszHeader = (LPWSTR)(LPCWSTR)folder;
			g.cchHeader = folder.GetLength();
			m_list.InsertGroup(m_list.GetGroupCount(), &g);
		}
		else gid = it->second;
		int item = m_list.InsertItem(idx, r.fileName);
		LVITEM lv;
		memset(&lv, 0, sizeof(lv));
		lv.mask = LVIF_GROUPID;
		lv.iItem = item;
		lv.iGroupId = gid;
		m_list.SetItem(&lv);
		RefreshRow(idx);
	}
	m_adding = false;
	StatusCount();
}

void CTagGridDlg::RefreshRow(int i)
{
	if (i < 0 || i >= (int)m_rows.size()) return;
	Row& r = m_rows[(size_t)i];
	for (int c = 0; c < TG_COLS; ++c) {
		CString* f = TgField(r, c);
		CString s = f ? *f : L"";
		if (c == TG_FILE && r.dirty) s = L"*" + s;
		m_list.SetItemText(i, c, s);
	}
}

void CTagGridDlg::StatusCount()
{
	int dirty = 0;
	for (size_t i = 0; i < m_rows.size(); ++i)
		if (m_rows[i].dirty) dirty++;
	CString s;
	s.Format(LL14(
		L"%d 曲、未保存 %d。Ctrl か Shift で複数選択し、セルをダブルクリックすると選択中の全行がその列になります。Ctrl+S で保存。ファイルとフォルダはドロップできます。",
		L"%d tracks, %d unsaved. Ctrl/Shift select rows; double-click a cell to fill that column on the selection. Ctrl+S saves. Drop files or folders.",
		L"%d titres, %d non enregistres. Ctrl/Maj selectionnent. Double-clic remplit la colonne. Ctrl+S enregistre. Deposez fichiers ou dossiers.",
		L"%d brani, %d non salvati. Ctrl/Maiusc selezionano. Doppio clic riempie la colonna. Ctrl+S salva. Trascina file o cartelle.",
		L"%d pistas, %d sin guardar. Ctrl/Mayus seleccionan. Doble clic llena la columna. Ctrl+S guarda. Suelta archivos o carpetas.",
		L"%d곡, 미저장 %d. Ctrl/Shift로 여러 행. 칸을 더블클릭하면 선택 행 전체가 그 열로. Ctrl+S 저장. 파일·폴더 드롭.",
		L"%d 首，未保存 %d。Ctrl/Shift 多选，双击单元格写入所选行的该列。Ctrl+S 保存。可拖入文件或文件夹。",
		L"%d مقطعًا، %d غير محفوظ. Ctrl/Shift للتحديد. النقر المزدوج يملأ العمود. Ctrl+S للحفظ. أفلت ملفات أو مجلدات.",
		L"%d треков, не сохранено %d. Ctrl/Shift — выбор. Двойной щелчок заполняет столбец. Ctrl+S — сохранить. Перетащите файлы или папки.",
		L"%d Titel, %d ungespeichert. Strg/Umschalt waehlt. Doppelklick fuellt die Spalte. Strg+S speichert. Dateien oder Ordner ablegen.",
		L"%d faixas, %d por guardar. Ctrl/Shift selecionam. Duplo clique preenche a coluna. Ctrl+S grava. Largue ficheiros ou pastas.",
		L"%d nummers, %d niet opgeslagen. Ctrl/Shift selecteert. Dubbelklik vult de kolom. Ctrl+S slaat op. Sleep bestanden of mappen.",
		L"%d utworow, %d niezapisanych. Ctrl/Shift zaznacza. Podwojne klikniecie wypelnia kolumne. Ctrl+S zapisuje. Upusc pliki lub foldery.",
		L"%d parca, %d kaydedilmedi. Ctrl/Shift secer. Cift tik hucreyi secime yazar. Ctrl+S kaydeder. Dosya veya klasor birakin."),
		(int)m_rows.size(), dirty);
	m_status.SetWindowText(s);
}

void CTagGridDlg::Selected(std::vector<int>& ix)
{
	ix.clear();
	for (int i = -1; (i = m_list.GetNextItem(i, LVNI_SELECTED)) != -1; )
		if (i >= 0 && i < (int)m_rows.size()) ix.push_back(i);
}

void CTagGridDlg::BeginEdit(int row, int col)
{
	if (row < 0 || col < 0 || col >= TG_COLS || row >= (int)m_rows.size()) return;
	if (m_cell.IsWindowVisible()) CommitCell();
	CString* f = TgField(m_rows[(size_t)row], col);
	if (!f) return;
	CRect rc;
	m_list.GetSubItemRect(row, col, col == 0 ? LVIR_LABEL : LVIR_BOUNDS, rc);
	m_editRow = row;
	m_editCol = col;
	m_cell.SetWindowText(*f);
	m_cell.MoveWindow(rc);
	m_cell.ShowWindow(SW_SHOW);
	m_cell.SetFocus();
	m_cell.SetSel(0, -1);
}

void CTagGridDlg::CommitCell()
{
	if (m_inCommit || !m_cell.GetSafeHwnd() || !m_cell.IsWindowVisible()) return;
	m_inCommit = true;
	CString text;
	m_cell.GetWindowText(text);
	const int row = m_editRow;
	const int col = m_editCol;
	m_cell.ShowWindow(SW_HIDE);
	m_editRow = m_editCol = -1;
	if (row >= 0 && col >= 0 && row < (int)m_rows.size()) {
		if (col == TG_FILE) text = TgCleanName(text);
		std::vector<int> ix;
		Selected(ix);
		bool inSel = false;
		for (size_t i = 0; i < ix.size(); ++i) if (ix[i] == row) inSel = true;
		if (!inSel) { ix.clear(); ix.push_back(row); }
		for (size_t i = 0; i < ix.size(); ++i) {
			CString* f = TgField(m_rows[(size_t)ix[i]], col);
			if (!f) continue;
			if (*f == text) continue;
			*f = text;
			m_rows[(size_t)ix[i]].dirty = true;
			RefreshRow(ix[i]);
		}
	}
	m_inCommit = false;
	StatusCount();
}

void CTagGridDlg::CancelCell()
{
	m_inCommit = true;
	if (m_cell.GetSafeHwnd()) m_cell.ShowWindow(SW_HIDE);
	m_editRow = m_editCol = -1;
	m_inCommit = false;
}

void CTagGridDlg::FlushLyrics()
{
	if (!m_lyrDirty) return;
	CString s;
	m_lyrics.GetWindowText(s);
	for (size_t i = 0; i < m_lyrSel.size(); ++i) {
		int ix = m_lyrSel[i];
		if (ix < 0 || ix >= (int)m_rows.size()) continue;
		if (m_rows[(size_t)ix].tags.lyrics == s) continue;
		m_rows[(size_t)ix].tags.lyrics = s;
		m_rows[(size_t)ix].dirty = true;
	}
	m_lyrDirty = false;
	StatusCount();
}

void CTagGridDlg::ShowSide()
{
	FlushLyrics();
	int focus = m_list.GetNextItem(-1, LVNI_FOCUSED);
	if (focus < 0 || focus >= (int)m_rows.size()) {
		m_fillLyrics = true;
		m_lyrics.SetWindowText(L"");
		m_fillLyrics = false;
		return;
	}
	std::vector<int> ix;
	Selected(ix);
	if (ix.empty()) ix.push_back(focus);
	CString lyrics = m_rows[(size_t)ix[0]].tags.lyrics;
	bool same = true;
	for (size_t i = 1; i < ix.size(); ++i)
		if (m_rows[(size_t)ix[i]].tags.lyrics != lyrics) same = false;
	m_fillLyrics = true;
	m_lyrics.SetWindowText(same ? lyrics : L"");
	m_fillLyrics = false;
	m_lyrSel = ix;
	Row& r = m_rows[(size_t)focus];
	if (!r.coverPath.IsEmpty())
		ExportTagUi_SetCover(m_coverPic, m_coverH, r.coverPath, m_coverBmp, r.coverPath);
	else
		ExportTagUi_ShowExistingCover(m_coverPic, m_coverH, m_coverBmp, r.path);
}

void CTagGridDlg::Mark(const std::vector<int>& ix)
{
	for (size_t i = 0; i < ix.size(); ++i) {
		if (ix[i] < 0 || ix[i] >= (int)m_rows.size()) continue;
		m_rows[(size_t)ix[i]].dirty = true;
		RefreshRow(ix[i]);
	}
	StatusCount();
}

void CTagGridDlg::OnAdd()
{
	CFileDialog dlg(TRUE, NULL, NULL, OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT,
		LL14(L"音声\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"音频\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0",
			L"Audio\0*.mp3;*.wav;*.flac;*.ogg;*.m4a;*.aac;*.dsf;*.mp2\0\0"), this);
	const DWORD cap = 256 * 1024;
	std::vector<TCHAR> buf(cap, 0);
	dlg.GetOFN().lpstrFile = &buf[0];
	dlg.GetOFN().nMaxFile = cap;
	if (dlg.DoModal() != IDOK) return;
	std::vector<CString> paths;
	POSITION pos = dlg.GetStartPosition();
	while (pos) paths.push_back(dlg.GetNextPathName(pos));
	AddPaths(paths);
}

void CTagGridDlg::OnDir()
{
	BROWSEINFO bi;
	memset(&bi, 0, sizeof(bi));
	bi.hwndOwner = GetSafeHwnd();
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	bi.lpszTitle = LL14(L"フォルダを選ぶと、中の音声を一覧へ入れます。", L"Pick a folder. Audio files inside are added.", L"Choisissez un dossier. Les audio dedans sont ajoutes.", L"Scegli una cartella. Gli audio dentro vengono aggiunti.", L"Elige una carpeta. Se anaden los audios.", L"폴더를 고르면 안의 음성이 목록에 들어갑니다.", L"选择文件夹，其中的音频会加入列表。", L"اختر مجلدًا. تُضاف الملفات الصوتية.", L"Выберите папку. Аудио внутри будут добавлены.", L"Ordner waehlen. Audio darin wird aufgenommen.", L"Escolha uma pasta. Os audios entram na lista.", L"Kies een map. Audio erin wordt toegevoegd.", L"Wybierz folder. Audio w srodku trafi na liste.", L"Klasor secin. Icindeki sesler listeye eklenir.");
	PIDLIST_ABSOLUTE pidl = SHBrowseForFolder(&bi);
	if (!pidl) return;
	TCHAR path[MAX_PATH];
	path[0] = 0;
	SHGetPathFromIDList(pidl, path);
	CoTaskMemFree(pidl);
	if (!path[0]) return;
	std::vector<CString> paths;
	paths.push_back(path);
	AddPaths(paths);
}

void CTagGridDlg::OnName()
{
	CommitCell();
	FlushLyrics();
	CString pat;
	m_pat.GetWindowText(pat);
	if (pat.IsEmpty()) return;
	std::vector<int> ix;
	Selected(ix);
	if (ix.empty()) {
		for (int i = 0; i < (int)m_rows.size(); ++i) ix.push_back(i);
	}
	for (size_t i = 0; i < ix.size(); ++i) {
		Row& r = m_rows[(size_t)ix[i]];
		CString base = TgApplyPat(pat, r.tags);
		if (base.IsEmpty()) continue;
		CString ext = TgExtOf(r.fileName);
		CString name = base;
		if (name.Right(ext.GetLength()).CompareNoCase(ext) != 0) name += ext;
		if (name == r.fileName) continue;
		r.fileName = name;
		r.dirty = true;
		RefreshRow(ix[i]);
	}
	StatusCount();
}

void CTagGridDlg::OnNum()
{
	CommitCell();
	std::vector<int> ix;
	Selected(ix);
	if (ix.empty()) {
		for (int i = 0; i < (int)m_rows.size(); ++i) ix.push_back(i);
	}
	for (size_t i = 0; i < ix.size(); ++i) {
		CString t;
		t.Format(L"%02d", (int)i + 1);
		Row& r = m_rows[(size_t)ix[i]];
		if (r.tags.track == t) continue;
		r.tags.track = t;
		r.dirty = true;
		RefreshRow(ix[i]);
	}
	StatusCount();
}

void CTagGridDlg::OnLyrApply()
{
	std::vector<int> ix;
	Selected(ix);
	m_lyrSel = ix;
	m_lyrDirty = true;
	FlushLyrics();
}

void CTagGridDlg::OnLyricsChange()
{
	if (m_fillLyrics) return;
	if (!m_lyrDirty) Selected(m_lyrSel);
	m_lyrDirty = true;
}

void CTagGridDlg::ApplyCoverFile(const CString& image)
{
	if (!ExportTagUi_IsImagePath(image)) return;
	std::vector<int> ix;
	Selected(ix);
	if (ix.empty()) {
		m_status.SetWindowText(LL14(L"ジャケットを付ける行を選んでください。", L"Select rows for the cover.", L"Choisissez des lignes.", L"Seleziona le righe.", L"Elige filas.", L"재킷을 넣을 행을 고르세요.", L"请选择要换封面的行。", L"اختر الصفوف.", L"Выберите строки.", L"Zeilen waehlen.", L"Escolha as linhas.", L"Kies rijen.", L"Wybierz wiersze.", L"Satirlari secin."));
		return;
	}
	for (size_t i = 0; i < ix.size(); ++i) {
		m_rows[(size_t)ix[i]].coverPath = image;
		m_rows[(size_t)ix[i]].dirty = true;
		RefreshRow(ix[i]);
	}
	CString keep = image;
	ExportTagUi_SetCover(m_coverPic, m_coverH, keep, m_coverBmp, image);
	StatusCount();
}

void CTagGridDlg::OnCoverBrowse()
{
	CFileDialog dlg(TRUE, NULL, NULL, OFN_FILEMUSTEXIST,
		L"JPEG/PNG\0*.jpg;*.jpeg;*.png\0\0", this);
	if (dlg.DoModal() != IDOK) return;
	ApplyCoverFile(dlg.GetPathName());
}

void CTagGridDlg::OnCoverClear()
{
	std::vector<int> ix;
	Selected(ix);
	for (size_t i = 0; i < ix.size(); ++i)
		m_rows[(size_t)ix[i]].coverPath.Empty();
	ShowSide();
}

bool CTagGridDlg::AnyDirty() const
{
	for (size_t i = 0; i < m_rows.size(); ++i)
		if (m_rows[i].dirty) return true;
	return m_lyrDirty;
}

void CTagGridDlg::DoSave()
{
	CommitCell();
	FlushLyrics();
	CString playPhys;
	BOOL needResume = FALSE;
	if (og && (plf || playf) && !filen.IsEmpty()) {
		playPhys = PlPhysicalMediaPath(filen);
		if (playPhys.IsEmpty()) playPhys = filen;
		for (size_t i = 0; i < m_rows.size(); ++i) {
			if (!m_rows[i].dirty) continue;
			if (playPhys.CompareNoCase(m_rows[i].path) == 0) { needResume = TRUE; break; }
		}
	}
	int bak = savedata.savecheck;
	if (needResume && og) {
		OggArmSilentResumeFromCurrent();
		savedata.savecheck = 0;
		og->stop1();
		savedata.savecheck = bak;
	}
	int okN = 0, failN = 0;
	CString resumedPath;
	for (size_t i = 0; i < m_rows.size(); ++i) {
		Row& r = m_rows[i];
		if (!r.dirty) continue;
		const CString oldPath = r.path;
		bool textOk = WriteFileTagFields(r.path, r.tags);
		bool coverOk = true;
		if (textOk && !r.coverPath.IsEmpty()) {
			CFile f;
			if (f.Open(r.coverPath, CFile::modeRead | CFile::shareDenyNone)) {
				ULONGLONG n64 = f.GetLength();
				if (n64 > 0 && n64 <= (ULONGLONG)FILETAG_COVER_MAX) {
					int n = (int)n64;
					BYTE* buf = (BYTE*)malloc((size_t)n);
					if (buf && (int)f.Read(buf, (UINT)n) == n) {
						CString ext = r.coverPath;
						ext.MakeLower();
						const char* mime = (ext.Right(4) == L".png") ? "image/png" : "image/jpeg";
						coverOk = EmbedCoverArt(r.path, buf, n, mime);
						if (!coverOk) coverOk = TgSidecar(r.path, r.coverPath) ? true : false;
					}
					else coverOk = false;
					free(buf);
				}
				else coverOk = false;
				f.Close();
			}
			else coverOk = false;
		}
		CString newPath = r.path;
		int slash = (std::max)(r.path.ReverseFind(L'\\'), r.path.ReverseFind(L'/'));
		CString dir = slash >= 0 ? r.path.Left(slash + 1) : L"";
		CString want = dir + r.fileName;
		if (textOk && want.CompareNoCase(r.path) != 0) {
			if (GetFileAttributes(want) != INVALID_FILE_ATTRIBUTES) textOk = false;
			else if (!MoveFile(r.path, want)) textOk = false;
			else { newPath = want; r.path = want; }
		}
		if (textOk && coverOk) {
			r.dirty = false;
			r.coverPath.Empty();
			okN++;
			if (pl && pl->pc) {
				for (int p = 0; p < pl->playcnt; ++p) {
					CString phys = PlPhysicalMediaPath(pl->pc[p].fol);
					if (phys.IsEmpty()) phys = pl->pc[p].fol;
					if (phys.CompareNoCase(oldPath) != 0) continue;
					if (!r.tags.title.IsEmpty()) {
						_tcsncpy_s(pl->pc[p].name, r.tags.title, _TRUNCATE);
					}
					if (!r.tags.artist.IsEmpty())
						_tcsncpy_s(pl->pc[p].art, r.tags.artist, _TRUNCATE);
					if (!r.tags.album.IsEmpty())
						_tcsncpy_s(pl->pc[p].alb, r.tags.album, _TRUNCATE);
					if (newPath.CompareNoCase(oldPath) != 0)
						_tcsncpy_s(pl->pc[p].fol, newPath, _TRUNCATE);
				}
			}
			if (needResume && oldPath.CompareNoCase(playPhys) == 0)
				resumedPath = newPath;
			RefreshRow((int)i);
		}
		else failN++;
	}
	if (pl && okN > 0) pl->Save();
	if (needResume && og && ::IsWindow(og->GetSafeHwnd())) {
		if (!resumedPath.IsEmpty()) filen = resumedPath;
		RequestPlaybackRestart(og->GetSafeHwnd());
	}
	CString msg;
	if (failN == 0)
		msg.Format(LL14(L"%d 件保存しました", L"Saved %d", L"Enregistre %d", L"Salvati %d", L"Guardados %d", L"%d건 저장", L"已保存 %d", L"حُفظ %d", L"Сохранено %d", L"%d gespeichert", L"Guardados %d", L"%d opgeslagen", L"Zapisano %d", L"%d kaydedildi"), okN);
	else
		msg.Format(LL14(L"保存 %d、失敗 %d", L"Saved %d, failed %d", L"Ok %d, echec %d", L"Ok %d, falliti %d", L"Ok %d, fallos %d", L"저장 %d, 실패 %d", L"成功 %d，失败 %d", L"نجح %d فشل %d", L"Ок %d, ошибок %d", L"Ok %d, Fehler %d", L"Ok %d, falhas %d", L"Ok %d, mislukt %d", L"Ok %d, bledy %d", L"Tamam %d, hata %d"), okN, failN);
	m_status.SetWindowText(msg);
}

void CTagGridDlg::OnSave() { DoSave(); }

void CTagGridDlg::OnCloseBtn() { OnCancel(); }

void CTagGridDlg::OnOK()
{
	DoSave();
}

void CTagGridDlg::OnCancel()
{
	CommitCell();
	if (AnyDirty()) {
		int a = MessageBox(LL14(
			L"保存していない変更があります。閉じますか？",
			L"There are unsaved changes. Close?",
			L"Modifications non enregistrees. Fermer ?",
			L"Modifiche non salvate. Chiudere?",
			L"Hay cambios sin guardar. Cerrar?",
			L"저장하지 않은 변경이 있습니다. 닫을까요?",
			L"有未保存的更改。要关闭吗？",
			L"هناك تغييرات غير محفوظة. إغلاق؟",
			L"Есть несохраненные правки. Закрыть?",
			L"Ungespeicherte Aenderungen. Schliessen?",
			L"Ha alteracoes por guardar. Fechar?",
			L"Er zijn niet-opgeslagen wijzigingen. Sluiten?",
			L"Sa niezapisane zmiany. Zamknac?",
			L"Kaydedilmemis degisiklik var. Kapatilsin mi?"),
			LL14(L"タグ一覧", L"Tag grid", L"Tags", L"Tag", L"Etiquetas", L"태그", L"标签", L"وسوم", L"Теги", L"Tags", L"Tags", L"Tags", L"Tagi", L"Etiket"),
			MB_OKCANCEL | MB_ICONQUESTION);
		if (a != IDOK) return;
	}
	EndDialog(IDCANCEL);
}

BOOL CTagGridDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_KEYDOWN) {
		const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		if (ctrl && (pMsg->wParam == 'S' || pMsg->wParam == 's')) {
			DoSave();
			return TRUE;
		}
		if (m_cell.GetSafeHwnd() && m_cell.IsWindowVisible() && pMsg->hwnd == m_cell.GetSafeHwnd()) {
			if (pMsg->wParam == VK_RETURN) { CommitCell(); return TRUE; }
			if (pMsg->wParam == VK_ESCAPE) { CancelCell(); return TRUE; }
		}
		if (pMsg->wParam == VK_RETURN && m_pat.GetSafeHwnd() && pMsg->hwnd == m_pat.GetSafeHwnd()) {
			OnName();
			return TRUE;
		}
		if (pMsg->wParam == VK_F2 && GetFocus() != &m_lyrics && GetFocus() != &m_pat) {
			int row = m_list.GetNextItem(-1, LVNI_FOCUSED);
			int col = m_editCol >= 0 ? m_editCol : TG_TITLE;
			BeginEdit(row, col);
			return TRUE;
		}
	}
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CTagGridDlg::ShowGridMenu(CPoint screenPt)
{
	std::vector<std::pair<CString, CString> > presets;
	TgLoadPresets(presets);
	CString cur;
	m_pat.GetWindowText(cur);
	cur = TgOneLine(cur);

	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	menu.SetSkipChrome(TRUE);
	menu.AddCommand(TG_M_ADD,
		LL14(L"ファイルを追加", L"Add files", L"Ajouter des fichiers", L"Aggiungi file", L"Anadir archivos", L"파일 추가", L"添加文件", L"إضافة ملفات", L"Добавить файлы", L"Dateien hinzufuegen", L"Adicionar ficheiros", L"Bestanden toevoegen", L"Dodaj pliki", L"Dosya ekle"),
		LL14(L"一覧へ音声ファイルを足します。", L"Add audio files to the grid.", L"Ajoute des fichiers audio.", L"Aggiunge file audio.", L"Anade archivos de audio.", L"목록에 음성 파일을 넣습니다.", L"把音频文件加入列表。", L"يضيف ملفات صوتية.", L"Добавить аудио в список.", L"Audiodateien aufnehmen.", L"Adiciona ficheiros de audio.", L"Audio aan de lijst toevoegen.", L"Dodaje pliki audio.", L"Ses dosyalarini ekler."));
	menu.AddCommand(TG_M_DIR,
		LL14(L"フォルダを追加", L"Add folder", L"Ajouter un dossier", L"Aggiungi cartella", L"Anadir carpeta", L"폴더 추가", L"添加文件夹", L"إضافة مجلد", L"Добавить папку", L"Ordner hinzufuegen", L"Adicionar pasta", L"Map toevoegen", L"Dodaj folder", L"Klasor ekle"),
		LL14(L"フォルダの中の音声を一覧へ入れます。", L"Add audio files from a folder.", L"Ajoute l'audio d'un dossier.", L"Aggiunge l'audio di una cartella.", L"Anade el audio de una carpeta.", L"폴더 안의 음성을 목록에 넣습니다.", L"把文件夹里的音频加入列表。", L"يضيف صوت المجلد.", L"Добавить аудио из папки.", L"Audio aus einem Ordner aufnehmen.", L"Adiciona o audio de uma pasta.", L"Audio uit een map toevoegen.", L"Dodaje audio z folderu.", L"Klasordeki sesleri ekler."));
	menu.AddSeparator();
	CCustomPopupMenu* sub = menu.AddSubMenu(
		LL14(L"ファイル名の型", L"Filename pattern", L"Modele de nom", L"Modello nome", L"Patron de nombre", L"파일 이름 형식", L"文件名格式", L"نمط اسم الملف", L"Шаблон имени", L"Dateinamensmuster", L"Padrao do nome", L"Bestandsnaampatroon", L"Wzor nazwy", L"Dosya adi kalibi"),
		TgTokenHelp());
	if (sub) {
		for (size_t i = 0; i < presets.size() && i < (size_t)TG_PRESET_MAX; ++i) {
			CString tip = presets[i].second;
			if (tip.GetLength() > 250) tip = tip.Left(250);
			sub->AddCheck(TG_M_PRESET0 + (UINT)i, presets[i].first,
				cur.Compare(presets[i].second) == 0, tip);
		}
		sub->AddSeparator();
		sub->AddCommand(TG_M_APPLY,
			LL14(L"今の型で名前を付ける", L"Apply current pattern", L"Appliquer le modele", L"Applica il modello", L"Aplicar el patron", L"현재 형식으로 이름", L"用当前格式命名", L"طبّق النمط الحالي", L"Применить текущий шаблон", L"Aktuelles Muster anwenden", L"Aplicar o padrao atual", L"Huidig patroon toepassen", L"Zastosuj biezacy wzor", L"Simdiki kalibi uygula"),
			cur.IsEmpty() ? TgTokenHelp() : (LPCTSTR)cur);
		sub->AddCommand(TG_M_PRESET_EDIT,
			LL14(L"プリセットを編集…", L"Edit presets…", L"Modifier les modeles…", L"Modifica modelli…", L"Editar patrones…", L"프리셋 편집…", L"编辑预设…", L"تحرير الأنماط…", L"Править шаблоны…", L"Muster bearbeiten…", L"Editar padroes…", L"Patronen bewerken…", L"Edytuj wzory…", L"Kaliplari duzenle…"),
			LL14(L"名前と型を追加、更新、削除します。", L"Add, update, or delete a name and pattern.", L"Ajouter, modifier ou supprimer un modele.", L"Aggiungi, aggiorna o elimina un modello.", L"Anadir, actualizar o borrar un patron.", L"이름과 형식을 추가, 수정, 삭제합니다.", L"添加、更新或删除名称和格式。", L"إضافة أو تحديث أو حذف اسم ونمط.", L"Добавить, обновить или удалить шаблон.", L"Muster hinzufuegen, aendern oder loeschen.", L"Adicionar, atualizar ou apagar um padrao.", L"Patroon toevoegen, wijzigen of wissen.", L"Dodaj, zmien lub usun wzor.", L"Ad ve kalip ekle, guncelle veya sil."));
	}
	menu.AddCommand(TG_M_NUM,
		LL14(L"連番", L"Number tracks", L"Numeroter", L"Numera", L"Numerar", L"번호 매기기", L"编号", L"ترقيم", L"Пронумеровать", L"Nummerieren", L"Numerar", L"Nummeren", L"Numeruj", L"Numarala"),
		LL14(L"選択行のトラックを 01 から順にします。未選択なら全行です。", L"Number the selected tracks from 01. All rows if none are selected.", L"Numerote la selection a partir de 01. Toutes les lignes sinon.", L"Numera la selezione da 01. Tutte le righe se nessuna.", L"Numera la seleccion desde 01. Todas si no hay seleccion.", L"선택 행의 트랙을 01부터 매깁니다. 선택이 없으면 전체.", L"从 01 给所选行编号。没选则是全部。", L"يرقّم التحديد من 01. الكل إن لم يوجد تحديد.", L"Нумерация выбора с 01. Все строки, если ничего не выбрано.", L"Auswahl ab 01 nummerieren. Alle Zeilen, wenn nichts gewaehlt ist.", L"Numera a selecao a partir de 01. Todas se nada estiver selecionado.", L"Nummer de selectie vanaf 01. Alle rijen als niets gekozen is.", L"Numeruj zaznaczenie od 01. Wszystkie wiersze, gdy nic nie zaznaczono.", L"Secili parcalari 01'den numarala. Secim yoksa hepsi."));
	menu.AddSeparator();
	menu.AddCommand(TG_M_COVER,
		LL14(L"ジャケットを付ける", L"Set cover", L"Mettre une pochette", L"Metti copertina", L"Poner portada", L"재킷 넣기", L"设置封面", L"وضع غلاف", L"Поставить обложку", L"Cover setzen", L"Por capa", L"Hoes zetten", L"Ustaw okladke", L"Kapak koy"),
		LL14(L"選択行へ付ける画像を選びます。保存するまでファイルには書きません。", L"Pick an image for the selection. It is written when you save.", L"Choisit une image. Elle est ecrite a l'enregistrement.", L"Sceglie un'immagine. Viene scritta al salvataggio.", L"Elige una imagen. Se escribe al guardar.", L"선택 행에 넣을 이미지를 고릅니다. 저장할 때 파일에 씁니다.", L"为所选行选图片。保存时才写入文件。", L"اختر صورة للتحديد. تُكتب عند الحفظ.", L"Выбрать картинку. В файл пишется при сохранении.", L"Bild waehlen. Geschrieben wird beim Speichern.", L"Escolher uma imagem. E gravada ao guardar.", L"Kies een afbeelding. Die wordt bij opslaan geschreven.", L"Wybierz obraz. Zapis do pliku przy zapisie.", L"Secim icin resim sec. Kaydedince dosyaya yazilir."));
	menu.AddCommand(TG_M_UNCOVER,
		LL14(L"ジャケットの指定を外す", L"Clear pending cover", L"Retirer la pochette choisie", L"Togli la copertina scelta", L"Quitar la portada elegida", L"재킷 지정 해제", L"取消封面指定", L"إلغاء الغلاف المحدد", L"Снять выбранную обложку", L"Gewaehltes Cover weg", L"Tirar a capa escolhida", L"Gekozen hoes weg", L"Cofnij wybrana okladke", L"Secilen kapagi kaldir"),
		LL14(L"まだ保存していないジャケット指定だけを外します。", L"Clears a cover that has not been saved yet.", L"Retire une pochette pas encore enregistree.", L"Toglie una copertina non ancora salvata.", L"Quita una portada que aun no se guardo.", L"아직 저장하지 않은 재킷 지정만 해제합니다.", L"只取消还没保存的封面。", L"يلغي غلافًا لم يُحفظ بعد.", L"Снимает обложку, которая ещё не сохранена.", L"Entfernt ein Cover, das noch nicht gespeichert ist.", L"Tira uma capa que ainda nao foi guardada.", L"Haalt een hoes weg die nog niet is opgeslagen.", L"Cofa okladke, ktorej jeszcze nie zapisano.", L"Henuz kaydedilmemis kapagi kaldirir."));
	menu.AddCommand(TG_M_LYR,
		LL14(L"歌詞を選択行へ", L"Lyrics to selection", L"Paroles vers la selection", L"Testo alla selezione", L"Letra a la seleccion", L"가사를 선택에", L"歌词写入所选", L"الكلمات إلى التحديد", L"Текст в выбор", L"Text in Auswahl", L"Letra na selecao", L"Tekst naar selectie", L"Tekst do zaznaczenia", L"Sozler secime"),
		LL14(L"右の歌詞を、選択している行へ入れます。", L"Put the lyrics on the right into the selected rows.", L"Met les paroles de droite dans la selection.", L"Mette il testo a destra nella selezione.", L"Pone la letra de la derecha en la seleccion.", L"오른쪽 가사를 선택 행에 넣습니다.", L"把右边的歌词写入所选行。", L"يضع كلمات اليمين في الصفوف المحددة.", L"Текст справа попадает в выбранные строки.", L"Den Text rechts in die Auswahl schreiben.", L"Poem a letra da direita na selecao.", L"Zet de tekst rechts in de selectie.", L"Wstawia tekst z prawej do zaznaczenia.", L"Sagdaki sozu secili satirlar koyar."));
	menu.AddSeparator();
	menu.AddCommand(TG_M_EDITCELL,
		LL14(L"このセルを編集", L"Edit this cell", L"Modifier cette cellule", L"Modifica questa cella", L"Editar esta celda", L"이 칸 편집", L"编辑此格", L"تحرير هذه الخلية", L"Править эту ячейку", L"Diese Zelle bearbeiten", L"Editar esta celula", L"Deze cel bewerken", L"Edytuj te komorke", L"Bu hucreyi duzenle"),
		LL14(L"選んだ列をその場で編集します。", L"Edit the chosen column in place.", L"Modifie la colonne choisie sur place.", L"Modifica la colonna scelta sul posto.", L"Edita la columna elegida en el sitio.", L"고른 열을 그 자리에서 고칩니다.", L"就地编辑所选列。", L"يحرر العمود المختار في مكانه.", L"Править выбранный столбец на месте.", L"Die gewaehlte Spalte an Ort und Stelle bearbeiten.", L"Edita a coluna escolhida no lugar.", L"Bewerk de gekozen kolom ter plekke.", L"Edytuj wybrana kolumne w miejscu.", L"Secilen sutunu yerinde duzenler."));
	menu.AddCommand(TG_M_SAVE,
		LL14(L"保存", L"Save", L"Enregistrer", L"Salva", L"Guardar", L"저장", L"保存", L"حفظ", L"Сохранить", L"Speichern", L"Guardar", L"Opslaan", L"Zapisz", L"Kaydet"),
		LL14(L"変えた行だけをファイルへ書きます。Ctrl+S と同じです。", L"Write only changed rows. Same as Ctrl+S.", L"Ecrit seulement les lignes changees. Comme Ctrl+S.", L"Scrive solo le righe cambiate. Come Ctrl+S.", L"Escribe solo las filas cambiadas. Igual que Ctrl+S.", L"바뀐 행만 파일에 씁니다. Ctrl+S 와 같습니다.", L"只把改过的行写入文件。与 Ctrl+S 相同。", L"يكتب الصفوف المتغيرة فقط. مثل Ctrl+S.", L"Пишет только изменённые строки. Как Ctrl+S.", L"Schreibt nur geaenderte Zeilen. Wie Strg+S.", L"Grava so as linhas alteradas. Igual a Ctrl+S.", L"Schrijft alleen gewijzigde rijen. Hetzelfde als Ctrl+S.", L"Zapisuje tylko zmienione wiersze. Jak Ctrl+S.", L"Sadece degisen satirlari yazar. Ctrl+S ile ayni."));

	const UINT cmd = menu.Track(screenPt, this);
	if (cmd == TG_M_ADD) OnAdd();
	else if (cmd == TG_M_DIR) OnDir();
	else if (cmd == TG_M_NUM) OnNum();
	else if (cmd == TG_M_COVER) OnCoverBrowse();
	else if (cmd == TG_M_UNCOVER) OnCoverClear();
	else if (cmd == TG_M_LYR) OnLyrApply();
	else if (cmd == TG_M_SAVE) OnSave();
	else if (cmd == TG_M_APPLY) OnName();
	else if (cmd == TG_M_EDITCELL) {
		int row = m_list.GetNextItem(-1, LVNI_FOCUSED);
		if (row < 0) row = m_list.GetNextItem(-1, LVNI_SELECTED);
		if (row >= 0) BeginEdit(row, m_editCol >= 0 ? m_editCol : TG_TITLE);
	}
	else if (cmd == TG_M_PRESET_EDIT) {
		CTagPresetDlg dlg(cur, this);
		dlg.DoModal();
	}
	else if (cmd >= TG_M_PRESET0 && cmd < TG_M_PRESET0 + (UINT)presets.size()) {
		const CString pat = presets[cmd - TG_M_PRESET0].second;
		m_pat.SetWindowText(pat);
		OnName();
	}
	m_menuTick = GetTickCount();
}

void CTagGridDlg::OnListRClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE a = (LPNMITEMACTIVATE)pNMHDR;
	if (a && a->iItem >= 0 && a->iItem < (int)m_rows.size()) {
		if ((m_list.GetItemState(a->iItem, LVIS_SELECTED) & LVIS_SELECTED) == 0) {
			m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
			m_list.SetItemState(a->iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
		if (a->iSubItem >= 0) m_editCol = a->iSubItem;
	}
	CPoint pt;
	GetCursorPos(&pt);
	m_menuTick = GetTickCount();
	ShowGridMenu(pt);
	*pResult = 1;
}

void CTagGridDlg::OnContextMenu(CWnd* pWnd, CPoint pt)
{
	if (GetTickCount() - m_menuTick < 400) return;
	if (pWnd == &m_pat || pWnd == &m_lyrics || pWnd == &m_cell) return;
	if (pt.x == -1 && pt.y == -1) {
		CRect r;
		if (m_list.GetSafeHwnd()) m_list.GetWindowRect(&r);
		else GetWindowRect(&r);
		pt = CPoint(r.left + 16, r.top + 16);
	}
	m_menuTick = GetTickCount();
	ShowGridMenu(pt);
}

void CTagGridDlg::OnListClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE a = (LPNMITEMACTIVATE)pNMHDR;
	if (a && a->iSubItem >= 0) m_editCol = a->iSubItem;
	*pResult = 0;
}

void CTagGridDlg::OnListDbl(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE a = (LPNMITEMACTIVATE)pNMHDR;
	if (a && a->iItem >= 0)
		BeginEdit(a->iItem, a->iSubItem);
	*pResult = 0;
}

void CTagGridDlg::OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLISTVIEW p = (LPNMLISTVIEW)pNMHDR;
	if (!m_adding && p && (p->uChanged & LVIF_STATE) && ((p->uNewState ^ p->uOldState) & LVIS_SELECTED))
		ShowSide();
	*pResult = 0;
}

void CTagGridDlg::OnCellKill()
{
	if (!m_inCommit) CommitCell();
}

void CTagGridDlg::OnDropFiles(HDROP hDrop)
{
	UINT n = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
	std::vector<CString> audio;
	CString image;
	for (UINT i = 0; i < n; ++i) {
		TCHAR buf[MAX_PATH * 4];
		if (!DragQueryFile(hDrop, i, buf, _countof(buf))) continue;
		CString p = buf;
		if (ExportTagUi_IsImagePath(p)) {
			if (image.IsEmpty()) image = p;
		}
		else audio.push_back(p);
	}
	DragFinish(hDrop);
	if (!audio.empty()) AddPaths(audio);
	if (!image.IsEmpty()) ApplyCoverFile(image);
}

void CTagGridDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (!m_list.GetSafeHwnd() || cx < 420 || cy < 220) return;
	const int right = 220;
	const int pad = 8;
	const int hintH = 64;
	const int patH = 24;
	const int top = 4 + hintH + 6 + patH + 8;
	if (m_add.GetSafeHwnd()) m_add.ShowWindow(SW_HIDE);
	if (m_dir.GetSafeHwnd()) m_dir.ShowWindow(SW_HIDE);
	if (m_name.GetSafeHwnd()) m_name.ShowWindow(SW_HIDE);
	if (m_num.GetSafeHwnd()) m_num.ShowWindow(SW_HIDE);
	int hintW = cx - 230;
	if (hintW < 160) hintW = 160;
	if (m_hint.GetSafeHwnd()) m_hint.MoveWindow(pad, 4, hintW, hintH);
	if (m_pat.GetSafeHwnd()) m_pat.MoveWindow(pad, 4 + hintH + 4, hintW, patH);
	m_list.MoveWindow(pad, top, cx - right - pad * 2, cy - top - 28);
	int x = cx - right;
	m_coverPic.MoveWindow(x, top, 190, 120);
	m_coverH.MoveWindow(x, top + 124, 190, 28);
	m_coverBrowse.MoveWindow(x, top + 154, 92, 24);
	m_coverClear.MoveWindow(x + 98, top + 154, 92, 24);
	m_lyricsL.MoveWindow(x, top + 184, 190, 16);
	int lyrH = cy - top - 270;
	if (lyrH < 48) lyrH = 48;
	m_lyrics.MoveWindow(x, top + 202, 190, lyrH);
	m_lyrApply.MoveWindow(x, cy - 60, 190, 24);
	m_status.MoveWindow(pad, cy - 24, cx - right - pad * 2, 18);
	m_save.MoveWindow(cx - 214, 8, 100, 24);
	m_close.MoveWindow(cx - 108, 8, 100, 24);
}

void CTagGridDlg::OnDestroy()
{
	if (m_coverBmp) { ::DeleteObject(m_coverBmp); m_coverBmp = NULL; }
	CCustomBlurDialogBase::OnDestroy();
}
