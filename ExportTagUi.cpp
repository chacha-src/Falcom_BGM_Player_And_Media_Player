#include "stdafx.h"
#include "ExportTagUi.h"
#include "FileTagInfo.h"
#include "PlayList.h"

CString ExportTagUi_CoverHint()
{
	return LL14(L"JPG/PNGをここにドロップ", L"Drop JPG/PNG here", L"Deposer JPG/PNG ici", L"Trascina JPG/PNG qui",
		L"Suelta JPG/PNG aqui", L"JPG/PNG를 여기에 드롭", L"将 JPG/PNG 拖到此处", L"Drop JPG/PNG here",
		L"Перетащите JPG/PNG сюда", L"JPG/PNG hier ablegen", L"Solte JPG/PNG aqui", L"Zet JPG/PNG hier neer",
		L"Uprusc JPG/PNG tutaj", L"JPG/PNG buraya birakin");
}

static CString ExportTagUi_ExistingCoverHint()
{
	return LL14(L"既存ジャケット\r\n(変更なし)", L"Existing cover\r\n(unchanged)", L"Pochette existante\r\n(inchange)",
		L"Copertina esistente\r\n(invariata)", L"Portada existente\r\n(sin cambio)", L"기존 커버\r\n(변경 없음)",
		L"已有封面\r\n(不更改)", L"Existing cover\r\n(unchanged)", L"Существующая обложка\r\n(без изменений)",
		L"Vorhandenes Cover\r\n(unverändert)", L"Capa existente\r\n(sem mudanca)", L"Bestaand omslag\r\n(ongewijzigd)",
		L"Istniejaca okladka\r\n(bez zmian)", L"Mevcut kapak\r\n(degismedi)");
}

BOOL ExportTagUi_IsImagePath(const CString& path)
{
	CString e = path;
	e.MakeLower();
	if (e.GetLength() >= 4 && e.Right(4) == L".png") return TRUE;
	if (e.GetLength() >= 4 && e.Right(4) == L".jpg") return TRUE;
	if (e.GetLength() >= 5 && e.Right(5) == L".jpeg") return TRUE;
	return FALSE;
}

static void ExportTagUi_ReleaseBmp(CStatic& pic, HBITMAP& coverBmp)
{
	HBITMAP old = (HBITMAP)pic.SetBitmap(NULL);
	if (old && old == coverBmp)
		old = NULL;
	if (coverBmp) {
		::DeleteObject(coverBmp);
		coverBmp = NULL;
	}
	if (old)
		::DeleteObject(old);
}

static BOOL ExportTagUi_SetCoverBmpFromImage(CStatic& pic, HBITMAP& coverBmp, CImage& img)
{
	if (img.IsNull() || img.GetWidth() <= 0 || img.GetHeight() <= 0 || !pic.GetSafeHwnd())
		return FALSE;
	ExportTagUi_ReleaseBmp(pic, coverBmp);
	CRect rc;
	pic.GetClientRect(&rc);
	int dw = rc.Width();
	int dh = rc.Height();
	if (dw < 8) dw = 64;
	if (dh < 8) dh = 64;
	CImage scaled;
	if (FAILED(scaled.Create(dw, dh, 24)))
		return FALSE;
	HDC hdc = scaled.GetDC();
	SetStretchBltMode(hdc, HALFTONE);
	img.StretchBlt(hdc, 0, 0, dw, dh, SRCCOPY);
	scaled.ReleaseDC();
	coverBmp = scaled.Detach();
	pic.SetBitmap(coverBmp);
	pic.ShowWindow(SW_SHOW);
	return TRUE;
}

void ExportTagUi_SetCover(CStatic& pic, CCustomStatic& hint, CString& coverPath, HBITMAP& coverBmp, const CString& path)
{
	coverPath = path;
	CString name = path;
	const int slash = name.ReverseFind(L'\\');
	if (slash >= 0) name = name.Mid(slash + 1);

	CImage img;
	if (SUCCEEDED(img.Load(path)) && ExportTagUi_SetCoverBmpFromImage(pic, coverBmp, img)) {
		CString s;
		s.Format(L"%s\r\n(%s)",
			LL14(L"ジャケット設定済", L"Cover set", L"Pochette OK", L"Copertina OK", L"Portada OK",
				L"커버 설정됨", L"封面已设置", L"Cover set", L"Обложка задана", L"Cover gesetzt",
				L"Capa definida", L"Omslag gezet", L"Okładka ustawiona", L"Kapak ayarlandi"),
			(LPCTSTR)name);
		hint.SetWindowText(s);
		return;
	}
	ExportTagUi_ClearCover(pic, hint, coverPath, coverBmp);
	coverPath = path;
	hint.SetWindowText(LL14(L"ジャケット設定済(プレビュー不可)", L"Cover set (no preview)", L"Pochette OK (pas d'apercu)",
		L"Copertina OK (niente anteprima)", L"Portada OK (sin vista)", L"커버 설정됨(미리보기 불가)",
		L"封面已设置(无法预览)", L"Cover set (no preview)", L"Обложка задана (без превью)",
		L"Cover gesetzt (keine Vorschau)", L"Capa OK (sem preview)", L"Omslag gezet (geen preview)",
		L"Okładka ustawiona (brak podgladu)", L"Kapak ayarlandi (onizleme yok)"));
}

void ExportTagUi_ClearCover(CStatic& pic, CCustomStatic& hint, CString& coverPath, HBITMAP& coverBmp)
{
	coverPath.Empty();
	ExportTagUi_ReleaseBmp(pic, coverBmp);
	hint.SetWindowText(ExportTagUi_CoverHint());
}

BOOL ExportTagUi_ShowExistingCover(CStatic& pic, CCustomStatic& hint, HBITMAP& coverBmp, LPCTSTR mediaPath)
{
	if (!mediaPath || !*mediaPath)
		return FALSE;
	CString path = PlPhysicalMediaPath(mediaPath);
	if (path.IsEmpty())
		path = mediaPath;

	BYTE* buf = (BYTE*)malloc(FILETAG_COVER_MAX);
	if (!buf)
		return FALSE;
	char mime[64] = {};
	const int n = ExtractCoverArt(path, buf, FILETAG_COVER_MAX, mime, (int)sizeof(mime));
	if (n <= 0) {
		free(buf);
		return FALSE;
	}

	HGLOBAL hG = ::GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)n);
	if (!hG) {
		free(buf);
		return FALSE;
	}
	void* pMem = ::GlobalLock(hG);
	if (!pMem) {
		::GlobalFree(hG);
		free(buf);
		return FALSE;
	}
	memcpy(pMem, buf, (size_t)n);
	::GlobalUnlock(hG);
	free(buf);

	IStream* stream = NULL;
	if (FAILED(::CreateStreamOnHGlobal(hG, TRUE, &stream)) || !stream) {
		::GlobalFree(hG);
		return FALSE;
	}

	BOOL ok = FALSE;
	CImage img;
	if (SUCCEEDED(img.Load(stream)) && ExportTagUi_SetCoverBmpFromImage(pic, coverBmp, img)) {
		hint.SetWindowText(ExportTagUi_ExistingCoverHint());
		ok = TRUE;
	}
	stream->Release();
	return ok;
}

BOOL ExportTagUi_OnDropFiles(HDROP hDrop, CStatic& pic, CCustomStatic& hint, CString& coverPath, HBITMAP& coverBmp)
{
	if (!hDrop) return FALSE;
	const UINT n = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);
	BOOL ok = FALSE;
	for (UINT i = 0; i < n; ++i) {
		wchar_t path[MAX_PATH] = {};
		if (DragQueryFile(hDrop, i, path, MAX_PATH) == 0) continue;
		if (!ExportTagUi_IsImagePath(path)) continue;
		ExportTagUi_SetCover(pic, hint, coverPath, coverBmp, path);
		ok = TRUE;
		break;
	}
	DragFinish(hDrop);
	return ok;
}

void ExportTagUi_InitFields(bool multi, const playlistdata0& pc,
	CCustomEdit& title, CCustomEdit& artist, CCustomEdit& album,
	CCustomStatic& titleL, CCustomStatic& artistL, CCustomStatic& albumL,
	CCustomStatic& coverL, CStatic& coverPic, CCustomStatic& coverHint,
	CCustomStandardButton& coverClear, CString& coverPath, HBITMAP& coverBmp)
{
	titleL.SetWindowText(LL14(L"タイトル", L"Title", L"Titre", L"Titolo", L"Titulo", L"제목", L"标题", L"Title", L"Название", L"Titel", L"Titulo", L"Titel", L"Tytul", L"Baslik"));
	artistL.SetWindowText(LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"Artist", L"Исполнитель", L"Interpret", L"Artista", L"Artiest", L"Artysta", L"Sanatci"));
	albumL.SetWindowText(LL14(L"アルバム", L"Album", L"Album", L"Album", L"Album", L"앨범", L"专辑", L"Album", L"Альбом", L"Album", L"Album", L"Album", L"Album", L"Album"));
	coverL.SetWindowText(LL14(L"ジャケット", L"Cover", L"Pochette", L"Copertina", L"Portada", L"커버", L"封面", L"Cover", L"Обложка", L"Cover", L"Capa", L"Omslag", L"Okładka", L"Kapak"));
	coverClear.SetWindowText(LL14(L"解除", L"Clear", L"Effacer", L"Cancella", L"Quitar", L"해제", L"清除", L"Clear", L"Сброс", L"Entfernen", L"Limpar", L"Wissen", L"Wyczysc", L"Temizle"));
	ExportTagUi_ClearCover(coverPic, coverHint, coverPath, coverBmp);

	FileTagFields src;
	if (pc.fol[0] != 0)
		ReadFileTagFields(pc.fol, src);

	CString t = src.title;
	CString a = src.artist;
	CString al = src.album;
	if (t.IsEmpty() && pc.name[0]) t = pc.name;
	if (a.IsEmpty() && pc.art[0]) a = pc.art;
	if (al.IsEmpty() && pc.alb[0]) al = pc.alb;

	if (multi) {
		title.SetWindowText(LL14(L"(複数のため変更不可)", L"(locked for multi)", L"(verrouille)", L"(bloccato)", L"(bloqueado)",
			L"(다중 선택 잠금)", L"(多选不可改)", L"(locked)", L"(заблокировано)", L"(gesperrt)",
			L"(bloqueado)", L"(vergrendeld)", L"(zablokowane)", L"(kilitli)"));
		title.EnableWindow(FALSE);
		artist.SetWindowText(L"");
		album.SetWindowText(L"");
		artist.EnableWindow(TRUE);
		album.EnableWindow(TRUE);
	}
	else {
		title.SetWindowText(t);
		artist.SetWindowText(a);
		album.SetWindowText(al);
		title.EnableWindow(TRUE);
		artist.EnableWindow(TRUE);
		album.EnableWindow(TRUE);
		if (pc.fol[0] != 0)
			ExportTagUi_ShowExistingCover(coverPic, coverHint, coverBmp, pc.fol);
		// coverPath は空のまま → 保存時は既存ジャケを維持
	}
}

void ExportTagUi_Collect(bool multi, int copyTags,
	CCustomEdit& title, CCustomEdit& artist, CCustomEdit& album,
	const CString& coverPath, WavExportOptions& opts)
{
	opts.copyTags = copyTags ? 1 : 0;
	opts.multiFile = multi ? 1 : 0;
	opts.coverImagePath = coverPath;
	opts.tagTitle.Empty();
	opts.tagArtist.Empty();
	opts.tagAlbum.Empty();
	if (!multi) {
		title.GetWindowText(opts.tagTitle);
		opts.tagTitle.Trim();
	}
	artist.GetWindowText(opts.tagArtist);
	opts.tagArtist.Trim();
	album.GetWindowText(opts.tagAlbum);
	opts.tagAlbum.Trim();
}
