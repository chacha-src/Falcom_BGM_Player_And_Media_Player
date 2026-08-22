#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "TagEditDlg.h"
#include "TagBatchEditDlg.h"
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
	, m_selN(0)
	, m_selIdx(NULL)
{
}

CTagEditDlg::~CTagEditDlg()
{
	if (m_coverBmp) {
		::DeleteObject(m_coverBmp);
		m_coverBmp = NULL;
	}
	if (m_selIdx) {
		free(m_selIdx);
		m_selIdx = NULL;
		m_selN = 0;
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
	DDX_Control(pDX, IDC_TAGEDIT_BATCH, m_batch);
	DDX_Control(pDX, IDC_TE_HELP, m_help);
}

BEGIN_MESSAGE_MAP(CTagEditDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_TAGEDIT_SAVE, &CTagEditDlg::OnBnClickedSave)
	ON_BN_CLICKED(IDC_TAGEDIT_CLOSE, &CTagEditDlg::OnBnClickedClose)
	ON_BN_CLICKED(IDC_TAGEDIT_BATCH, &CTagEditDlg::OnBnClickedBatch)
	ON_BN_CLICKED(IDC_TAGEDIT_COVER_CLEAR, &CTagEditDlg::OnBnClickedCoverClear)
	ON_BN_CLICKED(IDC_TE_HELP, &CTagEditDlg::OnBnClickedHelp)
	ON_WM_DROPFILES()
	ON_WM_SIZE()
	ON_WM_DESTROY()
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
	m_batch.SetWindowText(LL14(L"まとめて編集", L"Batch edit", L"Edition groupée", L"Modifica in blocco",
		L"Edición por lote", L"일괄 편집", L"批量编辑", L"تحرير دفعي",
		L"Пакетное правки", L"Sammelbearbeitung", L"Edicao em lote", L"Batch bewerken",
		L"Edycja zbiorcza", L"Toplu duzenleme"));
	if (multiFile && m_selIdx && m_selN >= 2)
		m_batch.ShowWindow(SW_SHOW);
	else
		m_batch.ShowWindow(SW_HIDE);

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
		if (m_batch.GetSafeHwnd() && m_batch.IsWindowVisible())
			placeY(m_batch, y);
		y += heightOf(m_save) + 6;
		placeY(m_status, y);
		y += heightOf(m_status) + 8;

		CRect rcClient; GetClientRect(&rcClient);
		CRect rcWin; GetWindowRect(&rcWin);
		const int chrome = rcWin.Height() - rcClient.Height();
		if (y + chrome > 120)
			SetWindowPos(NULL, 0, 0, rcWin.Width(), y + chrome, SWP_NOMOVE | SWP_NOZORDER);
	}

	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();

	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		auto addTip = [this](CWnd& w, LPCWSTR text) {
			if (w.GetSafeHwnd() && text && text[0])
				m_tooltip.AddTool(&w, text);
		};
		addTip(m_help, LL14(
			L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida",
			L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل",
			L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen",
			L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
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
		if (m_batch.GetSafeHwnd() && m_batch.IsWindowVisible()) {
			addTip(m_batch, LL14(
				L"選択曲のファイル名・アーティスト・アルバムを6つの連動欄でまとめて書き換えます",
				L"Rewrite filename, artist, and album for the selection in six linked boxes",
				L"Reecrire nom, artiste et album de la selection dans 6 zones liees",
				L"Riscrivi nome, artista e album della selezione in 6 caselle collegate",
				L"Reescribir nombre, artista y album de la seleccion en 6 cajas enlazadas",
				L"선택 곡의 파일명·아티스트·앨범을 연동 6칸에서 한꺼번에 고칩니다",
				L"用六个联动栏一次改写所选的文件名、艺术家、专辑",
				L"إعادة كتابة الاسم والفنان والألبوم في 6 حقول مرتبطة",
				L"Правка имени, исполнителя и альбома выбора в 6 связанных полях",
				L"Dateiname, Artist und Album der Auswahl in 6 gekoppelten Feldern aendern",
				L"Reescrever nome, artista e album da selecao em 6 caixas ligadas",
				L"Bestandsnaam, artiest en album van de selectie in 6 gekoppelde vakken",
				L"Zmien nazwe, artyste i album zaznaczenia w 6 polach",
				L"Secimin ad, sanatci ve albumunu 6 bagli kutuda toplu duzenle"));
		}
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

	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
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

void CTagEditDlg::OnBnClickedBatch()
{
	if (!m_selIdx || m_selN < 2)
		return;
	CTagBatchEditDlg dlg(this);
	dlg.m_idx = m_selIdx;
	dlg.m_n = m_selN;
	dlg.m_te = this;
	dlg.DoModal();
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

	// 演奏中の曲を含むとき: 位置保存→停止→書込→途中から再開(確認ダイアログ無し)
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
		OggArmSilentResumeFromCurrent(); // stop1 前に位置を残す
		savedata.savecheck = 0; // stop()経路の途中再生ダイアログ抑止(互換)
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
			// 空欄=変更なし → 既存を読み、入力ありだけ上書き。
			// タイトルは複数時プレースホルダのため書かない。
			ReadFileTagFields(path, fields);
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

		bool textOk = true;
		if (multiFile) {
			const bool anyInput = !artist.IsEmpty() || !album.IsEmpty()
				|| !year.IsEmpty() || !track.IsEmpty() || !genre.IsEmpty() || !comment.IsEmpty();
			if (anyInput)
				textOk = WriteFileTagFields(path, fields);
		}
		else {
			// 単一: 空でも Write（対応形式は空＝タグ削除）
			textOk = WriteFileTagFields(path, fields);
		}

		bool coverOk = true;
		if (!m_coverPath.IsEmpty())
			coverOk = TagEdit_ApplyCover(path, m_coverPath) ? true : false;

		if (textOk && coverOk) {
			okN++;
			TagEdit_ForgetJacket(fol);
			playlistdata0* t = targets[i];
			if (!multiFile && !title.IsEmpty()) {
				_tcsncpy(t->name, title, _countof(t->name) - 1);
				t->name[_countof(t->name) - 1] = 0;
			}
			if (!artist.IsEmpty()) {
				_tcsncpy(t->art, artist, _countof(t->art) - 1);
				t->art[_countof(t->art) - 1] = 0;
			}
			if (!album.IsEmpty()) {
				_tcsncpy(t->alb, album, _countof(t->alb) - 1);
				t->alb[_countof(t->alb) - 1] = 0;
			}
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
		// MessageBox 後に再開(途中位置は OggArmSilentResumeFromCurrent 済み)
		if (needResume && og && ::IsWindow(og->GetSafeHwnd()))
			RequestPlaybackRestart(og->GetSafeHwnd());
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

namespace {

class CTeHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_TE_HELP };
	explicit CTeHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
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

static CTeHelpDlg* g_teHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CTeHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CTeHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"タグ編集操作ガイド", L"Tag Edit Guide", L"Guide d'édition des tags", L"Guida modifica tag",
		L"Guía de edición de etiquetas", L"태그 편집 가이드", L"标签编辑指南", L"دليل تحرير الوسوم",
		L"Руководство по тегам", L"Tag-Bearbeitungsanleitung", L"Guia de edição de tags", L"Tag-bewerkingsgids",
		L"Przewodnik edycji tagów", L"Etiket düzenleme kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CTeHelpDlg::OnOK() { DestroyWindow(); }
void CTeHelpDlg::OnCancel() { DestroyWindow(); }
void CTeHelpDlg::OnClose() { DestroyWindow(); }

void CTeHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_teHelpDlg == this)
		g_teHelpDlg = nullptr;
	delete this;
}

BOOL CTeHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CTeHelpDlg::OnPaint()
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
	title(L, y, LL14(L"タグ編集操作ガイド", L"Tag Edit — Guide", L"Guide tags", L"Guida tag",
		L"Guía etiquetas", L"태그 편집 가이드", L"标签编辑指南", L"دليل الوسوم",
		L"Руководство по тегам", L"Tag-Guide", L"Guia tags", L"Tag-gids",
		L"Przewodnik tagów", L"Etiket kılavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"タイトルやアーティストなどのタグとジャケットを編集してファイルへ書き込みます。",
		L"Edit title, artist, and cover art, then write them to the file.",
		L"Modifiez titre, artiste, pochette, puis écrivez dans le fichier.",
		L"Modifica titolo, artista, copertina e scrivi nel file.",
		L"Edita título, artista y portada; escribe en el archivo.",
		L"제목·아티스트·재킷 등을 편집해 파일에 씁니다.",
		L"编辑标题、艺术家、封面等，并写入文件。",
		L"حرّر العنوان والفنان والغلاف، ثم اكتب إلى الملف.",
		L"Правите название, исполнителя и обложку, затем пишете в файл.",
		L"Titel, Artist und Cover bearbeiten und in die Datei schreiben.",
		L"Edite título, artista e capa; grave no arquivo.",
		L"Bewerk titel, artiest en cover; schrijf naar het bestand.",
		L"Edytuj tytuł, artystę i okładkę; zapisz do pliku.",
		L"Başlık, sanatçı ve kapağı düzenleyip dosyaya yazın."));
	y += lh + 4;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, rc.Width() - L * 2, min(140, max(112, rc.Height() / 5)),
		CCC_HELPDEMO_KGENERIC);


	title(L, y, LL14(L"フィールド", L"Fields", L"Champs", L"Campi", L"Campos", L"필드", L"字段", L"الحقول",
		L"Поля", L"Felder", L"Campos", L"Velden", L"Pola", L"Alanlar"));
	y += titleLh;
	body(L, y, LL14(L"・タイトル / アーティスト / アルバム …… 基本の曲情報", L"· Title / Artist / Album …… basic track info", L"· Titre / Artiste / Album …… infos de base", L"· Titolo / Artista / Album …… info base",
		L"· Título / Artista / Álbum …… info básica", L"· 제목 / 아티스트 / 앨범 …… 기본 곡 정보", L"· 标题 / 艺术家 / 专辑 …… 基本曲目信息", L"· العنوان / الفنان / الألبوم …… معلومات أساسية",
		L"· Название / Исполнитель / Альбом …… базовая информация", L"· Titel / Artist / Album …… Basisinfos", L"· Título / Artista / Álbum …… info básica", L"· Titel / Artiest / Album …… basisinfo",
		L"· Tytuł / Artysta / Album …… podstawowe info", L"· Başlık / Sanatçı / Albüm …… temel bilgi")); y += lh;
	body(L, y, LL14(L"・年 / トラック / ジャンル / コメント …… 詳細メタデータ", L"· Year / Track / Genre / Comment …… extra metadata", L"· Année / Piste / Genre / Commentaire …… métadonnées", L"· Anno / Traccia / Genere / Commento …… metadati",
		L"· Año / Pista / Género / Comentario …… metadatos", L"· 연도 / 트랙 / 장르 / 설명 …… 상세 메타데이터", L"· 年份 / 音轨 / 流派 / 注释 …… 详细元数据", L"· السنة / المسار / النوع / التعليق …… بيانات إضافية",
		L"· Год / Трек / Жанр / Комментарий …… доп. метаданные", L"· Jahr / TitelNr / Genre / Kommentar …… Metadaten", L"· Ano / Faixa / Gênero / Comentário …… metadados", L"· Jaar / Nummer / Genre / Opmerking …… metadata",
		L"· Rok / Utwór / Gatunek / Komentarz …… metadane", L"· Yıl / Parça / Tür / Yorum …… ek meta veri")); y += lh + 4;

	title(L, y, LL14(L"ジャケット", L"Cover art", L"Pochette", L"Copertina", L"Portada", L"재킷", L"封面", L"الغلاف",
		L"Обложка", L"Cover", L"Capa", L"Omslag", L"Okładka", L"Kapak"));
	y += titleLh;
	const int gx = L, gy = y, gw = min(260, rc.Width() - L * 2), gh = lh * 3 + 12;
	dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
	dc.FillSolidRect(gx + 8, gy + 8, gh - 16, gh - 16, RGB(90, 120, 170));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + 14, gy + gh / 2 - lh / 2, L"JPG");
	dc.FillSolidRect(gx + gh, gy + 10, gw - gh - 12, lh + 4, RGB(255, 255, 255));
	dc.FrameRect(CRect(gx + gh, gy + 10, gx + gw - 12, gy + 10 + lh + 4), &frameBrush);
	dc.SetTextColor(RGB(80, 80, 95));
	dc.TextOut(gx + gh + 6, gy + 12, L"cover.png");
	dc.FillSolidRect(gx + gh, gy + 10 + lh + 10, 70, lh, RGB(180, 140, 60));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + gh + 8, gy + 10 + lh + 10, L"drop");
	dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
	y = gy + gh + 4;
	body(L, y, LL14(L"・JPG / PNG をプレビュー枠へドロップして指定", L"· Drop a JPG/PNG onto the preview to set cover", L"· Déposez JPG/PNG sur l'aperçu", L"· Trascina JPG/PNG sull'anteprima",
		L"· Suelte JPG/PNG en la vista previa", L"· JPG/PNG를 미리보기 영역에 드롭", L"· 将 JPG/PNG 拖到预览区指定封面", L"· أفلت JPG/PNG على المعاينة",
		L"· Перетащите JPG/PNG на превью", L"· JPG/PNG auf die Vorschau ziehen", L"· Solte JPG/PNG na prévia", L"· Drop JPG/PNG op de preview",
		L"· Upuść JPG/PNG na podgląd", L"· JPG/PNG'yi önizlemeye bırakın")); y += lh;
	body(L, y, LL14(L"・クリア …… 指定したジャケットを解除", L"· Clear …… remove the selected cover", L"· Effacer …… retirer la pochette", L"· Cancella …… rimuovi la copertina",
		L"· Quitar …… quitar la portada", L"· 지우기 …… 지정한 재킷 해제", L"· 清除 …… 取消已选封面", L"· مسح …… إزالة الغلاف",
		L"· Сброс …… убрать обложку", L"· Löschen …… Cover entfernen", L"· Limpar …… remover a capa", L"· Wissen …… omslag verwijderen",
		L"· Wyczyść …… usuń okładkę", L"· Temizle …… kapağı kaldır")); y += lh + 4;

	title(L, y, LL14(L"保存と閉じる", L"Save & Close", L"Enregistrer & Fermer", L"Salva e Chiudi", L"Guardar y Cerrar", L"저장과 닫기", L"保存与关闭", L"حفظ وإغلاق",
		L"Сохранить и закрыть", L"Speichern & Schließen", L"Salvar e Fechar", L"Opslaan & Sluiten", L"Zapisz i Zamknij", L"Kaydet ve Kapat"));
	y += titleLh;
	body(L, y, LL14(L"・保存 …… 編集内容をファイルタグへ書き込み、ダイアログを閉じます", L"· Save …… write tags to the file and close", L"· Enregistrer …… écrire et fermer", L"· Salva …… scrivi e chiudi",
		L"· Guardar …… escribir y cerrar", L"· 저장 …… 태그를 파일에 쓰고 닫습니다", L"· 保存 …… 写入文件标签并关闭", L"· حفظ …… اكتب الوسوم وأغلق",
		L"· Сохранить …… записать теги и закрыть", L"· Speichern …… Tags schreiben und schließen", L"· Salvar …… gravar tags e fechar", L"· Opslaan …… tags schrijven en sluiten",
		L"· Zapisz …… zapisz tagi i zamknij", L"· Kaydet …… etiketleri yazıp kapat")); y += lh;
	body(L, y, LL14(L"・閉じる …… 保存せずに閉じます", L"· Close …… dismiss without saving", L"· Fermer …… sans enregistrer", L"· Chiudi …… senza salvare",
		L"· Cerrar …… sin guardar", L"· 닫기 …… 저장하지 않고 닫습니다", L"· 关闭 …… 不保存并关闭", L"· إغلاق …… دون حفظ",
		L"· Закрыть …… без сохранения", L"· Schließen …… ohne Speichern", L"· Fechar …… sem salvar", L"· Sluiten …… zonder opslaan",
		L"· Zamknij …… bez zapisu", L"· Kapat …… kaydetmeden")); y += lh + 4;
	muted(L, y, LL14(
		L"複数選択時は空欄の項目は変更しません。入力した項目だけ全選択へ適用します。",
		L"In multi-select, blank fields are unchanged; filled fields apply to all.",
		L"Sélection multiple: champs vides inchangés; remplis = tous.",
		L"Selezione multipla: vuoti invariati; compilati = a tutti.",
		L"Multiselección: vacíos sin cambio; rellenos = a todos.",
		L"다중 선택 시 빈칸은 유지되고, 입력한 항목만 전체에 적용됩니다.",
		L"多选时，空栏不改；有内容的项应用到全部。",
		L"في التحديد المتعدد: الفراغ لا يتغير؛ المعبأ يُطبَّق على الكل.",
		L"При множественном выборе пустые поля не меняются.",
		L"Mehrfach: leere Felder unverändert; ausgefüllte für alle.",
		L"Multi: vazios inalterados; preenchidos = para todos.",
		L"Multi: lege velden ongewijzigd; ingevuld = voor allen.",
		L"Wiele: puste bez zmian; wypełnione = do wszystkich.",
		L"Çoklu seçimde boş alanlar değişmez; dolular hepsine uygulanır."));
	y += lh;
	body(L, y, LL14(
		L"・まとめて編集 …… 複数選択時、左右6つの連動欄で TITLE / アーティスト / アルバムを一括書き換え",
		L"· Batch edit …… with a multi-select, rewrite TITLE / artist / album in six linked boxes",
		L"· Edition groupée …… en multi-sélection, réécrire TITLE / artiste / album dans 6 zones liées",
		L"· Modifica in blocco …… in selezione multipla, riscrivi TITLE / artista / album in 6 caselle",
		L"· Edición por lote …… en multiselección, reescribe TITLE / artista / álbum en 6 cajas",
		L"· 일괄 편집 …… 다중 선택 시 연동 6칸에서 TITLE/아티스트/앨범을 한꺼번에 고칩니다",
		L"· 批量编辑 …… 多选时用六个联动栏一次改写 TITLE/艺术家/专辑",
		L"· تحرير دفعي …… عند التحديد المتعدد، أعد كتابة TITLE/الفنان/الألبوم في 6 حقول",
		L"· Пакетное правки …… при множественном выборе правьте TITLE / исполнителя / альбом в 6 полях",
		L"· Sammelbearbeitung …… bei Mehrfachauswahl TITLE / Artist / Album in 6 Feldern aendern",
		L"· Edicao em lote …… na selecao multipla, reescreva TITLE / artista / album em 6 caixas",
		L"· Batch bewerken …… bij multi-selectie TITLE / artiest / album in 6 vakken herschrijven",
		L"· Edycja zbiorcza …… przy wielokrotnym zaznaczeniu zmien TITLE / artyste / album w 6 polach",
		L"· Toplu duzenleme …… coklu secimde TITLE / sanatci / albumu 6 kutuda birden yaz"));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

void CTagEditDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CTagEditDlg::ShowHelpSheet()
{
	if (g_teHelpDlg && ::IsWindow(g_teHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_teHelpDlg, this);
		return;
	}
	if (g_teHelpDlg && !::IsWindow(g_teHelpDlg->GetSafeHwnd()))
		g_teHelpDlg = nullptr;
	CTeHelpDlg* dlg = new CTeHelpDlg(this);
	if (!dlg->Create(IDD_TE_HELP, this)) {
		delete dlg;
		return;
	}
	g_teHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CTagEditDlg::OnBnClickedHelp()
{
	ShowHelpSheet();
}

void CTagEditDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

void CTagEditDlg::OnDestroy()
{
	if (g_teHelpDlg && ::IsWindow(g_teHelpDlg->GetSafeHwnd()))
		g_teHelpDlg->DestroyWindow();
	CCustomBlurDialogBase::OnDestroy();
}
