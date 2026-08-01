#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "TagEditDlg.h"
#include "ExportTagUi.h"
#include "FileTagInfo.h"
#include "PlayList.h"
#include "CMediaPlayerDlg.h"
#include <shlwapi.h>
#include <algorithm>

extern COggDlg* og;
extern CString filen;
extern int plf;
extern int playf;
extern save savedata;

IMPLEMENT_DYNAMIC(CTagEditDlg, CCustomBlurDialogBase)

CTagEditDlg::CTagEditDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CTagEditDlg::IDD, pParent)
	, multiFile(false)
	, m_coverBmp(NULL)
{
}

CTagEditDlg::~CTagEditDlg()
{
	if (m_coverBmp) {
		::DeleteObject(m_coverBmp);
		m_coverBmp = NULL;
	}
}

void CTagEditDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TAGEDIT_TITLE_L, m_titleL);
	DDX_Control(pDX, IDC_TAGEDIT_TITLE, m_title);
	DDX_Control(pDX, IDC_TAGEDIT_ARTIST_L, m_artistL);
	DDX_Control(pDX, IDC_TAGEDIT_ARTIST, m_artist);
	DDX_Control(pDX, IDC_TAGEDIT_ALBUM_L, m_albumL);
	DDX_Control(pDX, IDC_TAGEDIT_ALBUM, m_album);
	DDX_Control(pDX, IDC_TAGEDIT_YEAR_L, m_yearL);
	DDX_Control(pDX, IDC_TAGEDIT_YEAR, m_year);
	DDX_Control(pDX, IDC_TAGEDIT_TRACK_L, m_trackL);
	DDX_Control(pDX, IDC_TAGEDIT_TRACK, m_track);
	DDX_Control(pDX, IDC_TAGEDIT_GENRE_L, m_genreL);
	DDX_Control(pDX, IDC_TAGEDIT_GENRE, m_genre);
	DDX_Control(pDX, IDC_TAGEDIT_COMMENT_L, m_commentL);
	DDX_Control(pDX, IDC_TAGEDIT_COMMENT, m_comment);
	DDX_Control(pDX, IDC_TAGEDIT_COVER_L, m_coverL);
	DDX_Control(pDX, IDC_TAGEDIT_COVER_PIC, m_coverPic);
	DDX_Control(pDX, IDC_TAGEDIT_COVER, m_cover);
	DDX_Control(pDX, IDC_TAGEDIT_COVER_CLEAR, m_coverClear);
	DDX_Control(pDX, IDC_TAGEDIT_HINT, m_hint);
	DDX_Control(pDX, IDC_TAGEDIT_STATUS, m_status);
	DDX_Control(pDX, IDC_TAGEDIT_SAVE, m_save);
	DDX_Control(pDX, IDC_TAGEDIT_CLOSE, m_close);
}

BEGIN_MESSAGE_MAP(CTagEditDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_TAGEDIT_SAVE, &CTagEditDlg::OnBnClickedSave)
	ON_BN_CLICKED(IDC_TAGEDIT_CLOSE, &CTagEditDlg::OnBnClickedClose)
	ON_BN_CLICKED(IDC_TAGEDIT_COVER_CLEAR, &CTagEditDlg::OnBnClickedCoverClear)
	ON_WM_DROPFILES()
END_MESSAGE_MAP()

static CString TagEdit_ExtLower(LPCTSTR path)
{
	CString e = path ? path : _T("");
	const int slash = (std::max)(e.ReverseFind(_T('\\')), e.ReverseFind(_T('/')));
	if (slash >= 0) e = e.Mid(slash + 1);
	e.MakeLower();
	return e;
}

static BOOL TagEdit_IsUnsupportedWrite(LPCTSTR path)
{
	CString e = TagEdit_ExtLower(path);
	return (e.GetLength() >= 4 && (e.Right(4) == _T(".dff") || e.Right(4) == _T(".wsd")));
}

static BOOL TagEdit_WriteSidecarCover(LPCTSTR mediaPath, LPCTSTR imagePath)
{
	if (!mediaPath || !*mediaPath || !imagePath || !*imagePath)
		return FALSE;
	CString src = imagePath;
	CString media = mediaPath;
	const int slash = (std::max)(media.ReverseFind(_T('\\')), media.ReverseFind(_T('/')));
	if (slash < 0) return FALSE;
	CString dir = media.Left(slash + 1);
	CString base = media.Mid(slash + 1);
	const int dot = base.ReverseFind(_T('.'));
	if (dot > 0) base = base.Left(dot);

	CString ext = src;
	ext.MakeLower();
	CString imgExt = _T(".jpg");
	if (ext.GetLength() >= 4 && ext.Right(4) == _T(".png"))
		imgExt = _T(".png");

	const CString folderJpg = dir + _T("folder.jpg");
	const CString sameName = dir + base + imgExt;
	if (::CopyFile(src, folderJpg, FALSE))
		return TRUE;
	if (::CopyFile(src, sameName, FALSE))
		return TRUE;
	return FALSE;
}

static BOOL TagEdit_ApplyCover(LPCTSTR mediaPath, LPCTSTR imagePath)
{
	if (!imagePath || !*imagePath)
		return TRUE;
	CFile f;
	if (!f.Open(imagePath, CFile::modeRead | CFile::shareDenyWrite))
		return FALSE;
	const ULONGLONG len64 = f.GetLength();
	if (len64 == 0 || len64 > (ULONGLONG)FILETAG_COVER_MAX) {
		f.Close();
		return FALSE;
	}
	const int len = (int)len64;
	BYTE* buf = (BYTE*)malloc((size_t)len);
	if (!buf) {
		f.Close();
		return FALSE;
	}
	const UINT got = f.Read(buf, (UINT)len);
	f.Close();
	CString ext = imagePath;
	ext.MakeLower();
	const char* mime = "image/jpeg";
	if (ext.GetLength() >= 4 && ext.Right(4) == _T(".png"))
		mime = "image/png";
	BOOL ok = FALSE;
	if ((int)got == len)
		ok = EmbedCoverArt(mediaPath, buf, len, mime) ? TRUE : FALSE;
	free(buf);
	if (!ok)
		ok = TagEdit_WriteSidecarCover(mediaPath, imagePath);
	return ok;
}

static void TagEdit_ForgetJacket(LPCTSTR fol)
{
	if (!fol || !*fol) return;
	PlJakDiskForget(fol);
	extern CMediaPlayerDlg* mp;
	if (mp) {
		for (int j = 0; j < CMediaPlayerDlg::kMpJakN; ++j) {
			if (mp->m_jakKey[j][0] && _tcsicmp(mp->m_jakKey[j], fol) == 0) {
				if (mp->m_jakBmp[j]) { ::DeleteObject(mp->m_jakBmp[j]); mp->m_jakBmp[j] = NULL; }
				mp->m_jakKey[j][0] = 0;
				mp->m_jakTick[j] = 0;
				mp->m_jakRow[j] = -1;
				break;
			}
		}
	}
}

BOOL CTagEditDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	DragAcceptFiles(TRUE);

	SetWindowText(LL14(L"タグ編集", L"Edit tags", L"Modifier les tags", L"Modifica tag",
		L"Editar etiquetas", L"태그 편집", L"编辑标签", L"تحرير الوسوم",
		L"Редактировать теги", L"Tags bearbeiten", L"Editar tags", L"Tags bewerken",
		L"Edytuj tagi", L"Etiketleri duzenle"));

	m_yearL.SetWindowText(LL14(L"年", L"Year", L"Annee", L"Anno", L"Ano", L"연도", L"年份", L"Year",
		L"Год", L"Jahr", L"Ano", L"Jaar", L"Rok", L"Yil"));
	m_trackL.SetWindowText(LL14(L"トラック", L"Track", L"Piste", L"Traccia", L"Pista", L"트랙", L"音轨", L"Track",
		L"Трек", L"Titel", L"Faixa", L"Nummer", L"Utwor", L"Parca"));
	m_genreL.SetWindowText(LL14(L"ジャンル", L"Genre", L"Genre", L"Genere", L"Genero", L"장르", L"流派", L"Genre",
		L"Жанр", L"Genre", L"Genero", L"Genre", L"Gatunek", L"Tur"));
	m_commentL.SetWindowText(LL14(L"コメント", L"Comment", L"Commentaire", L"Commento", L"Comentario", L"설명", L"注释", L"Comment",
		L"Комментарий", L"Kommentar", L"Comentario", L"Opmerking", L"Komentarz", L"Yorum"));
	m_save.SetWindowText(LL14(L"保存", L"Save", L"Enregistrer", L"Salva", L"Guardar", L"저장", L"保存", L"Save",
		L"Сохранить", L"Speichern", L"Salvar", L"Opslaan", L"Zapisz", L"Kaydet"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"Close",
		L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));

	ExportTagUi_InitFields(multiFile, pc,
		m_title, m_artist, m_album,
		m_titleL, m_artistL, m_albumL,
		m_coverL, m_coverPic, m_cover,
		m_coverClear, m_coverPath, m_coverBmp);

	if (multiFile) {
		m_year.SetWindowText(L"");
		m_track.SetWindowText(L"");
		m_genre.SetWindowText(L"");
		m_comment.SetWindowText(L"");
		m_hint.SetWindowText(LL14(
			L"複数選択: 空欄は変更しません。入力した項目だけ全選択へ適用します。",
			L"Multi-select: blank = no change. Filled fields apply to all.",
			L"Selection multiple: vide = inchangé. Remplis = appliqués à tous.",
			L"Selezione multipla: vuoto = nessuna modifica. Compilati = a tutti.",
			L"Multiseleccion: vacio = sin cambio. Rellenos = a todos.",
			L"다중 선택: 빈칸은 변경 없음. 입력한 항목만 전체에 적용.",
			L"多选: 空栏不改。有内容的项应用到全部。",
			L"Multi: blank = no change. Filled apply to all.",
			L"Несколько: пусто = без изменений. Заполненные — ко всем.",
			L"Mehrfach: leer = unveraendert. Ausgefuellt = fuer alle.",
			L"Multi: vazio = sem mudanca. Preenchidos = para todos.",
			L"Multi: leeg = ongewijzigd. Ingevuld = voor allen.",
			L"Wiele: puste = bez zmian. Wypelnione = do wszystkich.",
			L"Coklu: bos = degismez. Dolu alanlar hepsine."));
	}
	else {
		FileTagFields tags;
		if (pc.fol[0])
			ReadFileTagFields(pc.fol, tags);
		m_year.SetWindowText(tags.year);
		m_track.SetWindowText(tags.track);
		m_genre.SetWindowText(tags.genre);
		m_comment.SetWindowText(tags.comment);
		m_hint.SetWindowText(L"");
	}
	m_status.SetWindowText(L"");

	// ジャケット下〜ボタン〜ステータスの空きを詰め、ダイアログ高さを合わせる
	{
		auto placeY = [this](CWnd& w, int y) {
			if (!w.GetSafeHwnd()) return;
			CRect r; w.GetWindowRect(&r); ScreenToClient(&r);
			w.SetWindowPos(NULL, r.left, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
		};
		auto heightOf = [this](CWnd& w) -> int {
			if (!w.GetSafeHwnd()) return 18;
			CRect r; w.GetWindowRect(&r); return r.Height();
		};
		CRect rCover;
		m_coverPic.GetWindowRect(&rCover); ScreenToClient(&rCover);
		int y = rCover.bottom + 8;
		placeY(m_hint, y);
		y += (std::max)(heightOf(m_hint), 14) + 6;
		placeY(m_save, y);
		placeY(m_close, y);
		y += heightOf(m_save) + 6;
		placeY(m_status, y);
		y += heightOf(m_status) + 8;

		CRect rcClient; GetClientRect(&rcClient);
		CRect rcWin; GetWindowRect(&rcWin);
		const int chrome = rcWin.Height() - rcClient.Height();
		if (y + chrome > 120)
			SetWindowPos(NULL, 0, 0, rcWin.Width(), y + chrome, SWP_NOMOVE | SWP_NOZORDER);
	}

	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		auto addTip = [this](CWnd& w, LPCWSTR text) {
			if (w.GetSafeHwnd() && text && text[0])
				m_tooltip.AddTool(&w, text);
		};
		addTip(m_coverClear, LL14(
			L"ジャケット画像の指定を解除します",
			L"Clear the cover image selection",
			L"Effacer la pochette",
			L"Rimuovi la copertina",
			L"Quitar la portada",
			L"재킷 이미지 지정을 해제합니다",
			L"清除封面图指定",
			L"Clear cover image",
			L"Сбросить обложку",
			L"Cover entfernen",
			L"Limpar capa",
			L"Omslag wissen",
			L"Wyczysc okladke",
			L"Kapak secimini kaldirir"));
		addTip(m_save, LL14(
			L"編集内容をファイルタグへ書き込みます",
			L"Write the edited fields to file tags",
			L"Ecrire les champs modifies dans les tags",
			L"Scrivi i campi modificati nei tag",
			L"Escribir los campos editados en etiquetas",
			L"편집 내용을 파일 태그에 씁니다",
			L"将编辑内容写入文件标签",
			L"Write edits to file tags",
			L"Записать изменения в теги файла",
			L"Aenderungen in Datei-Tags schreiben",
			L"Gravar edicoes nas tags do arquivo",
			L"Bewerkingen naar bestandstags schrijven",
			L"Zapisz edycje do tagow pliku",
			L"Duzenlemeleri dosya etiketlerine yazar"));
		addTip(m_close, LL14(
			L"保存せずに閉じます",
			L"Close without saving",
			L"Fermer sans enregistrer",
			L"Chiudi senza salvare",
			L"Cerrar sin guardar",
			L"저장하지 않고 닫습니다",
			L"不保存并关闭",
			L"Close without saving",
			L"Закрыть без сохранения",
			L"Schliessen ohne Speichern",
			L"Fechar sem salvar",
			L"Sluiten zonder opslaan",
			L"Zamknij bez zapisu",
			L"Kaydetmeden kapatir"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 10000);
	}
	return TRUE;
}

BOOL CTagEditDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CTagEditDlg::OnBnClickedCoverClear()
{
	ExportTagUi_ClearCover(m_coverPic, m_cover, m_coverPath, m_coverBmp);
}

void CTagEditDlg::OnDropFiles(HDROP hDropInfo)
{
	ExportTagUi_OnDropFiles(hDropInfo, m_coverPic, m_cover, m_coverPath, m_coverBmp);
}

void CTagEditDlg::OnBnClickedClose()
{
	EndDialog(IDCANCEL);
}

void CTagEditDlg::OnBnClickedSave()
{
	CString title, artist, album, year, track, genre, comment;
	m_title.GetWindowText(title);
	m_artist.GetWindowText(artist);
	m_album.GetWindowText(album);
	m_year.GetWindowText(year);
	m_track.GetWindowText(track);
	m_genre.GetWindowText(genre);
	m_comment.GetWindowText(comment);

	std::vector<playlistdata0*> targets;
	if (multiFile) {
		for (size_t i = 0; i < pcs.size(); ++i)
			targets.push_back(&pcs[i]);
	}
	else {
		targets.push_back(&pc);
	}
	if (targets.empty()) return;

	// 演奏中の曲を含むときだけ停止→書込→再開
	BOOL needResume = FALSE;
	CString playPhys;
	if (og && (plf || playf) && !filen.IsEmpty()) {
		playPhys = PlPhysicalMediaPath(filen);
		if (playPhys.IsEmpty()) playPhys = filen;
		for (size_t i = 0; i < targets.size(); ++i) {
			CString p = PlPhysicalMediaPath(targets[i]->fol);
			if (p.IsEmpty()) p = targets[i]->fol;
			if (!p.IsEmpty() && _tcsicmp(p, playPhys) == 0) {
				needResume = TRUE;
				break;
			}
		}
	}
	int savecheckBak = savedata.savecheck;
	if (needResume) {
		savedata.savecheck = 0; // 途中再生ダイアログを出さない
		og->stop1();
		savedata.savecheck = savecheckBak;
	}

	int okN = 0, skipN = 0, failN = 0;
	for (size_t i = 0; i < targets.size(); ++i) {
		LPCTSTR fol = targets[i]->fol;
		CString path = PlPhysicalMediaPath(fol);
		if (path.IsEmpty()) path = fol;
		if (path.IsEmpty()) { failN++; continue; }

		if (TagEdit_IsUnsupportedWrite(path)) {
			skipN++;
			continue;
		}

		FileTagFields fields;
		if (multiFile) {
			// 空欄=変更なし → 既存を読み、入力ありだけ上書き
			ReadFileTagFields(path, fields);
			if (!title.IsEmpty()) fields.title = title;
			if (!artist.IsEmpty()) fields.artist = artist;
			if (!album.IsEmpty()) fields.album = album;
			if (!year.IsEmpty()) fields.year = year;
			if (!track.IsEmpty()) fields.track = track;
			if (!genre.IsEmpty()) fields.genre = genre;
			if (!comment.IsEmpty()) fields.comment = comment;
		}
		else {
			fields.title = title;
			fields.artist = artist;
			fields.album = album;
			fields.year = year;
			fields.track = track;
			fields.genre = genre;
			fields.comment = comment;
		}

		const bool hasText = fields.HasTitleArtistAlbum() || fields.HasAnyTagField()
			|| (!multiFile); // 単一は空でも書く意図（全クリア相当は未対応なので非空のみ）
		bool textOk = true;
		if (multiFile) {
			const bool anyInput = !title.IsEmpty() || !artist.IsEmpty() || !album.IsEmpty()
				|| !year.IsEmpty() || !track.IsEmpty() || !genre.IsEmpty() || !comment.IsEmpty();
			if (anyInput)
				textOk = WriteFileTagFields(path, fields);
		}
		else if (hasText) {
			textOk = WriteFileTagFields(path, fields);
		}

		bool coverOk = true;
		if (!m_coverPath.IsEmpty())
			coverOk = TagEdit_ApplyCover(path, m_coverPath) ? true : false;

		if (textOk && coverOk) {
			okN++;
			TagEdit_ForgetJacket(fol);
			if (!multiFile) {
				if (!title.IsEmpty()) {
					_tcsncpy(pc.name, title, _countof(pc.name) - 1);
					pc.name[_countof(pc.name) - 1] = 0;
				}
				if (!artist.IsEmpty()) {
					_tcsncpy(pc.art, artist, _countof(pc.art) - 1);
					pc.art[_countof(pc.art) - 1] = 0;
				}
				if (!album.IsEmpty()) {
					_tcsncpy(pc.alb, album, _countof(pc.alb) - 1);
					pc.alb[_countof(pc.alb) - 1] = 0;
				}
			}
		}
		else {
			failN++;
		}
	}

	if (needResume && og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());

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
			L"Zapis tagow nieobsługiwany (.dff/.wsd)",
			L"Bu bicimde etiket yazimi desteklenmiyor (.dff/.wsd)");
		m_status.SetWindowText(msg);
		MessageBox(msg, LL14(L"タグ編集", L"Edit tags", L"Tags", L"Tag", L"Etiquetas", L"태그", L"标签", L"Tags",
			L"Теги", L"Tags", L"Tags", L"Tags", L"Tagi", L"Etiket"), MB_OK | MB_ICONWARNING);
		return;
	}

	if (failN == 0 && okN > 0) {
		msg = LL14(L"保存しました", L"Saved", L"Enregistre", L"Salvato", L"Guardado", L"저장됨", L"已保存", L"Saved",
			L"Сохранено", L"Gespeichert", L"Salvo", L"Opgeslagen", L"Zapisano", L"Kaydedildi");
		if (skipN > 0) {
			CString extra;
			extra.Format(LL14(L" (スキップ: %d)", L" (skipped: %d)", L" (ignores: %d)", L" (saltati: %d)",
				L" (omitidos: %d)", L" (건너뜀: %d)", L" (跳过: %d)", L" (skipped: %d)",
				L" (пропущено: %d)", L" (uebersprungen: %d)", L" (ignorados: %d)", L" (overgeslagen: %d)",
				L" (pominieto: %d)", L" (atlandi: %d)"), skipN);
			msg += extra;
		}
		m_status.SetWindowText(msg);
		MessageBox(msg, LL14(L"タグ編集", L"Edit tags", L"Tags", L"Tag", L"Etiquetas", L"태그", L"标签", L"Tags",
			L"Теги", L"Tags", L"Tags", L"Tags", L"Tagi", L"Etiket"), MB_OK | MB_ICONINFORMATION);
		EndDialog(IDOK);
	}
	else {
		msg = LL14(L"保存に失敗したファイルがあります", L"Some files failed to save",
			L"Echec pour certains fichiers", L"Alcuni file non salvati", L"Algunos archivos fallaron",
			L"일부 파일 저장 실패", L"部分文件保存失败", L"Some files failed",
			L"Часть файлов не сохранена", L"Einige Dateien fehlgeschlagen", L"Alguns arquivos falharam",
			L"Sommige bestanden mislukt", L"Niektore pliki nieudane", L"Bazi dosyalar basarisiz");
		m_status.SetWindowText(msg);
		MessageBox(msg, LL14(L"タグ編集", L"Edit tags", L"Tags", L"Tag", L"Etiquetas", L"태그", L"标签", L"Tags",
			L"Теги", L"Tags", L"Tags", L"Tags", L"Tagi", L"Etiket"), MB_OK | MB_ICONERROR);
	}
}
