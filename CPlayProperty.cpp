#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "CPlayProperty.h"
#include "PlayList.h"
#include "CMediaPlayerDlg.h"
#include "FileTagInfo.h"
#include "SongParams.h"
#include "ExportTagUi.h"
#include "PluginKinds.h"
#include "kpi.h"
#include "KpiV5ConfigStore.h"
#include "CCustomPopupMenu.h"
#include "VstMidiEngine.h"
#include "kb_sasami/source/sasami_file.h"
#include <shlwapi.h>
#include <shlobj.h>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

extern CPlayList* pl;
extern COggDlg* og;
extern CMediaPlayerDlg* mp;
extern CString fnn;
extern CString filen;
extern int plf;
extern int playf;
extern int loop1, loop2;
extern save savedata;
extern int kpicnt;
extern CString kpif[400];
extern CString ext[400][300];
extern BYTE plugkind[400];
extern BYTE kpiarch[400];
extern BYTE kvar[400][300];
extern BOOL kpichk[400];
extern void OpenProToolsForSelection();
extern void MpPersistSavedataQuick();
extern void PlRefreshMidiPlayModes();
extern void CEmuRequestMidiEngineReplay();
extern void OggArmSilentResumeFromCurrent();

static void PpTip(CToolTipCtrl& tip, CWnd& w, LPCWSTR s)
{
	if (w.GetSafeHwnd() && s && s[0])
		tip.AddTool(&w, s);
}

static BOOL PpLooksLikeVideo(LPCTSTR path)
{
	if (!path || !path[0]) return FALSE;
	LPCTSTR dot = _tcsrchr(path, _T('.'));
	if (!dot) return FALSE;
	static const LPCTSTR kExts[] = {
		_T(".mp4"), _T(".m4v"), _T(".mkv"), _T(".avi"), _T(".webm"),
		_T(".mov"), _T(".qt"), _T(".wmv"), _T(".asf"), _T(".mpg"),
		_T(".mpeg"), _T(".mpe"), _T(".m1v"), _T(".m2v"), _T(".mpv"),
		_T(".vob"), _T(".ts"), _T(".m2ts"), _T(".mts"), _T(".ogv"),
		_T(".flv"), _T(".f4v"), _T(".3gp"), _T(".3g2"), _T(".divx"),
		_T(".rm"), _T(".rmvb")
	};
	for (int i = 0; i < (int)_countof(kExts); ++i) {
		if (_tcsicmp(dot, kExts[i]) == 0)
			return TRUE;
	}
	return FALSE;
}

static int CALLBACK PpEditWordBreakProc(LPTSTR /*lpch*/, int ichCurrent, int /*cch*/, int code)
{
	if (code == WB_ISDELIMITER) return FALSE;
	if (code == WB_LEFT) return ichCurrent;
	if (code == WB_RIGHT) return ichCurrent;
	return 0;
}

static BOOL PpKpiBoolOn(const wchar_t* s)
{
	if (!s || !s[0]) return FALSE;
	if (s[0] == L'1' && s[1] == 0) return TRUE;
	if (_wcsicmp(s, L"true") == 0 || _wcsicmp(s, L"yes") == 0 || _wcsicmp(s, L"on") == 0)
		return TRUE;
	return FALSE;
}

static void PpKpiPut(wchar_t* dst, int cap, const CString& s)
{
	if (!dst || cap <= 0) return;
	wcsncpy_s(dst, (size_t)cap, s, _TRUNCATE);
}

static CString PpFormatFileSize(ULONGLONG bytes)
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

static void PpCopyText(HWND hwnd, const CString& text)
{
	if (!::OpenClipboard(hwnd)) return;
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
		} else {
			::GlobalFree(h);
		}
	}
	::CloseClipboard();
}

static BOOL PpExtIs(LPCTSTR path, LPCTSTR ext4)
{
	CString e = path ? path : _T("");
	e.MakeLower();
	return (e.GetLength() >= 4 && e.Right(4) == ext4);
}

static BOOL PpWriteSidecarCover(LPCTSTR mediaPath, LPCTSTR imagePath)
{
	if (!mediaPath || !*mediaPath || !imagePath || !*imagePath) return FALSE;
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
	if (PpExtIs(ext, _T(".png"))) imgExt = _T(".png");
	if (::CopyFile(src, dir + _T("folder.jpg"), FALSE)) return TRUE;
	if (::CopyFile(src, dir + base + imgExt, FALSE)) return TRUE;
	return FALSE;
}

static BOOL PpApplyCover(LPCTSTR mediaPath, LPCTSTR imagePath)
{
	if (!imagePath || !*imagePath) return TRUE;
	CFile f;
	if (!f.Open(imagePath, CFile::modeRead | CFile::shareDenyWrite)) return FALSE;
	const ULONGLONG len64 = f.GetLength();
	if (len64 == 0 || len64 > (ULONGLONG)FILETAG_COVER_MAX) { f.Close(); return FALSE; }
	const int len = (int)len64;
	BYTE* buf = (BYTE*)malloc((size_t)len);
	if (!buf) { f.Close(); return FALSE; }
	const UINT got = f.Read(buf, (UINT)len);
	f.Close();
	const char* mime = PpExtIs(imagePath, _T(".png")) ? "image/png" : "image/jpeg";
	BOOL ok = FALSE;
	if ((int)got == len)
		ok = EmbedCoverArt(mediaPath, buf, len, mime) ? TRUE : FALSE;
	free(buf);
	if (!ok) ok = PpWriteSidecarCover(mediaPath, imagePath);
	return ok;
}

static void PpForgetJacket(LPCTSTR fol)
{
	if (!fol || !*fol) return;
	PlJakDiskForget(fol);
	if (mp) {
		for (int j = 0; j < CMediaPlayerDlg::kMpJakN; ++j) {
			if (mp->m_jakKey[j][0] && _tcsicmp(mp->m_jakKey[j], fol) == 0) {
				if (mp->m_jakBmp[j]) { ::DeleteObject(mp->m_jakBmp[j]); mp->m_jakBmp[j] = NULL; }
				mp->m_jakKey[j][0] = 0;
			}
		}
	}
}

static void PpStyleBtn(CCustomStandardButton& b)
{
	b.SetAeroMode(FALSE);
	b.SetGradation(RGB(255, 238, 208), RGB(255, 202, 130), 0, TRUE);
}

static void PpStyleStatic(CCustomStatic& s)
{
	s.SetAeroMode(FALSE);
	s.SetKeepFontHeight(TRUE);
}

static void PpOpenExplorer(LPCTSTR pathIn)
{
	CString phys = PlPhysicalMediaPath(pathIn);
	if (phys.IsEmpty()) phys = pathIn;
	if (phys.IsEmpty()) return;
	if (PathFileExists(phys)) {
		CString params;
		params.Format(_T("/select,\"%s\""), (LPCTSTR)phys);
		ShellExecute(NULL, _T("open"), _T("explorer.exe"), params, NULL, SW_SHOWNORMAL);
		return;
	}
	CString folder = phys;
	const int slash = (std::max)(folder.ReverseFind(_T('\\')), folder.ReverseFind(_T('/')));
	if (slash >= 0) folder = folder.Left(slash);
	if (!folder.IsEmpty())
		ShellExecute(NULL, _T("open"), folder, _T(""), NULL, SW_SHOWNORMAL);
}

/* 再生が内蔵デコーダ固定の拡張子。KPI が同じ拡張子を持っていてもプラグインタブは出さない。 */
static int PpBuiltinDecoderExt(const CString& ext)
{
	return ext == _T(".mp3") || ext == _T(".mp2") || ext == _T(".mp1") || ext == _T(".rmp")
		|| ext == _T(".ogg") || ext == _T(".oga") || ext == _T(".opus") || ext == _T(".qull3")
		|| ext == _T(".flac") || ext == _T(".fla") || ext == _T(".qull3h")
		|| ext == _T(".wav") || ext == _T(".wave")
		|| ext == _T(".dsf") || ext == _T(".dff") || ext == _T(".wsd");
}

int CPlayProperty::FindKpiForPath(LPCTSTR path)
{
	CString media = PlPhysicalMediaPath(path);
	if (media.IsEmpty()) media = path;
	if (media.IsEmpty()) return -1;
	const int dot = media.ReverseFind(_T('.'));
	if (dot < 0) return -1;
	CString extPart = media.Mid(dot);
	extPart.MakeLower();
	if (PpBuiltinDecoderExt(extPart)) return -1;
	/* m4a/AAC は CRender の内蔵チェックがオンのときだけ内蔵。オフなら KPI。 */
	if ((extPart == _T(".m4a") || extPart == _T(".aac")) && savedata.m4a)
		return -1;
	int pick = -1, best = -1;
	for (int i = 0; i < kpicnt && i < 400; ++i) {
		int hit = 0;
		for (int j = 0; j < 299; ++j) {
			if (ext[i][j].IsEmpty()) break;
			if (ext[i][j].CompareNoCase(extPart) == 0) { hit = 1; break; }
		}
		if (!hit) continue;
		int score = 0;
		if (kpichk[i]) score += 100;
#ifdef _WIN64
		if (kpiarch[i] == 64) score += 10;
		else if (kpiarch[i] == 32) score += 1;
#else
		if (kpiarch[i] == 32) score += 10;
		else if (kpiarch[i] == 64) score += 1;
#endif
		if (score > best) {
			best = score;
			pick = i;
		}
	}
	return pick;
}

IMPLEMENT_DYNAMIC(CPlayProperty_tab1, CCustomDialog)

CPlayProperty_tab1::CPlayProperty_tab1(CWnd* pParent)
	: CCustomDialog(CPlayProperty_tab1::IDD, pParent)
	, m_pc(NULL), m_batch(NULL)
{
}

void CPlayProperty_tab1::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_EDIT1, m_name);
	DDX_Control(pDX, IDC_EDIT2, m_id);
	DDX_Control(pDX, IDC_EDIT3, m_game);
	DDX_Control(pDX, IDC_EDIT4, m_art);
	DDX_Control(pDX, IDC_EDIT5, m_alb);
	DDX_Control(pDX, IDC_EDIT6, m_fol);
	DDX_Control(pDX, IDC_EDIT11, m_cmt);
	DDX_Control(pDX, IDC_EDIT7, m_year);
	DDX_Control(pDX, IDC_EDIT9, m_track);
	DDX_Control(pDX, IDC_EDIT10, m_j);
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
	DDX_Control(pDX, IDC_PP_FILE_META, m_lblMeta);
	DDX_Control(pDX, IDC_SYOSAI_BTN_BROWSE, m_btnBrowse);
	DDX_Control(pDX, IDC_SYOSAI_BTN_TAG2PL, m_btnTag2Pl);
	DDX_Control(pDX, IDC_SYOSAI_BTN_RELOADTAG, m_btnReloadTag);
	DDX_Control(pDX, IDC_SYOSAI_BTN_WRITETAG, m_btnWriteTag);
	DDX_Control(pDX, IDC_SYOSAI_BTN_COPYPATH, m_btnCopyPath);
	DDX_Control(pDX, IDC_SYOSAI_BTN_COPYNAME, m_btnCopyName);
	DDX_Control(pDX, IDC_SYOSAI_BTN_PROTOOLS, m_btnProTools);
	DDX_Control(pDX, IDC_SYOSAI_BTN_CLEARPARAM, m_btnClearParam);
	DDX_Control(pDX, IDOK999, m_btnExplorer);
	DDX_Control(pDX, IDC_SYOSAI_GRP_EDIT, m_grpEdit);
	DDX_Control(pDX, IDC_SYOSAI_GRP_TAG, m_grpTag);
	DDX_Control(pDX, IDC_SYOSAI_GRP_INTERNAL, m_grpInternal);
}

BEGIN_MESSAGE_MAP(CPlayProperty_tab1, CCustomDialog)
	ON_BN_CLICKED(IDOK999, OnBnClickedExplorer)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_BROWSE, OnBnClickedBrowse)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_TAG2PL, OnBnClickedTag2Pl)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_RELOADTAG, OnBnClickedReloadTag)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_WRITETAG, OnBnClickedWriteTag)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_COPYPATH, OnBnClickedCopyPath)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_COPYNAME, OnBnClickedCopyName)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_PROTOOLS, OnBnClickedProTools)
	ON_BN_CLICKED(IDC_SYOSAI_BTN_CLEARPARAM, OnBnClickedClearParam)
END_MESSAGE_MAP()

void CPlayProperty_tab1::OnOK() {}
void CPlayProperty_tab1::OnCancel() {}

BOOL CPlayProperty_tab1::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomDialog::PreTranslateMessage(pMsg);
}

CString CPlayProperty_tab1::CurrentPathText() const
{
	CString s;
	if (m_fol.GetSafeHwnd()) m_fol.GetWindowText(s);
	else if (m_pc) s = m_pc->fol;
	s.Trim();
	return s;
}

void CPlayProperty_tab1::CollectPlaylistFields()
{
	if (!m_pc) return;
	CString s;
	m_name.GetWindowText(s); _tcsncpy(m_pc->name, s, _countof(m_pc->name) - 1); m_pc->name[_countof(m_pc->name) - 1] = 0;
	m_art.GetWindowText(s); _tcsncpy(m_pc->art, s, _countof(m_pc->art) - 1); m_pc->art[_countof(m_pc->art) - 1] = 0;
	m_alb.GetWindowText(s); _tcsncpy(m_pc->alb, s, _countof(m_pc->alb) - 1); m_pc->alb[_countof(m_pc->alb) - 1] = 0;
	m_fol.GetWindowText(s); _tcsncpy(m_pc->fol, s, _countof(m_pc->fol) - 1); m_pc->fol[_countof(m_pc->fol) - 1] = 0;
	m_loop1.GetWindowText(s); m_pc->loop1 = _tstoi(s);
	m_loop2.GetWindowText(s); m_pc->loop2 = _tstoi(s);
}

void CPlayProperty_tab1::CollectTagAndLoopFields(FileTagFields& tags, int& loopStart, int& loopEnd)
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
	m_loop1.GetWindowText(s); loopStart = _tstoi(s);
	m_loop2.GetWindowText(s); loopEnd = _tstoi(s);
	tags.loop1 = loopStart > 0 ? loopStart : 0;
	tags.loop2 = loopEnd > 0 ? loopEnd : 0;
}

void CPlayProperty_tab1::ApplyTagsToControls(const FileTagFields& tags, bool forceEmpty)
{
	if (forceEmpty || tags.year.GetLength()) m_year.SetWindowText(tags.year);
	if (forceEmpty || tags.track.GetLength()) m_track.SetWindowText(tags.track);
	if (forceEmpty || tags.genre.GetLength()) m_j.SetWindowText(tags.genre);
	if (forceEmpty || tags.comment.GetLength()) m_cmt.SetWindowText(tags.comment);
	if (tags.loop1 || tags.loop2) {
		CString s;
		s.Format(_T("%d"), tags.loop1); m_loop1.SetWindowText(s);
		s.Format(_T("%d"), tags.loop2); m_loop2.SetWindowText(s);
		if (m_pc) { m_pc->loop1 = tags.loop1; m_pc->loop2 = tags.loop2; }
	}
}

void CPlayProperty_tab1::RefreshStatusLines()
{
	if (IsBatchMode()) {
		CString s;
		s.Format(LL14(L"選択 %d 曲のアーティスト/アルバムを一括変更します。",
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
			(int)m_batch->size());
		m_lblStatus.SetWindowText(s);
		m_lblParam.SetWindowText(_T(""));
		m_lblMeta.SetWindowText(_T(""));
		return;
	}
	CString path = CurrentPathText();
	CString phys = PlPhysicalMediaPath(path);
	if (phys.IsEmpty()) phys = path;
	const BOOL exists = (!phys.IsEmpty() && PathFileExists(phys));
	CString extn;
	if (!phys.IsEmpty()) { extn = PathFindExtension(phys); extn.MakeLower(); }
	CString sizeStr = _T("-");
	CString mtimeStr;
	if (exists) {
		WIN32_FILE_ATTRIBUTE_DATA fad = {};
		if (::GetFileAttributesEx(phys, GetFileExInfoStandard, &fad)) {
			ULARGE_INTEGER ul; ul.HighPart = fad.nFileSizeHigh; ul.LowPart = fad.nFileSizeLow;
			sizeStr = PpFormatFileSize(ul.QuadPart);
			FILETIME local{}; SYSTEMTIME st{};
			if (::FileTimeToLocalFileTime(&fad.ftLastWriteTime, &local) && ::FileTimeToSystemTime(&local, &st))
				mtimeStr.Format(_T("%04d/%02d/%02d %02d:%02d"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
		}
	}
	CString existLabel = exists
		? CString(LL14(L"存在する", L"Exists", L"Existe", L"Esiste", L"Existe", L"존재", L"存在", L"موجود", L"Есть", L"Vorhanden", L"Existe", L"Bestaat", L"Istnieje", L"Var"))
		: CString(LL14(L"欠損", L"Missing", L"Manquant", L"Mancante", L"Falta", L"없음", L"缺失", L"مفقود", L"Нет", L"Fehlt", L"Ausente", L"Ontbreekt", L"Brak", L"Yok"));
	CString status;
	status.Format(_T("%s | %s | %s"), (LPCTSTR)existLabel, extn.IsEmpty() ? _T("?") : (LPCTSTR)extn, (LPCTSTR)sizeStr);
	m_lblStatus.SetWindowText(status);
	CString meta;
	if (!mtimeStr.IsEmpty())
		meta.Format(LL14(L"更新: %s", L"Modified: %s", L"Modifié: %s", L"Modificato: %s", L"Modificado: %s",
			L"수정: %s", L"修改: %s", L"تعديل: %s", L"Изменён: %s", L"Geändert: %s", L"Modificado: %s",
			L"Gewijzigd: %s", L"Zmieniono: %s", L"Değiştirildi: %s"), (LPCTSTR)mtimeStr);
	m_lblMeta.SetWindowText(meta);

	CString list = SongParams_CurrentListName();
	CString tip = SongParams_BuildTipExtra(list, path.IsEmpty() && m_pc ? m_pc->fol : path, m_pc ? m_pc->sub : 0, m_pc ? m_pc->ret2 : 0);
	const bool has = SongParams_HasEntry(list, path.IsEmpty() && m_pc ? m_pc->fol : path, m_pc ? m_pc->sub : 0, m_pc ? m_pc->ret2 : 0);
	CString param;
	if (has) {
		param = LL14(L"[SAV] 曲ごと設定あり", L"[SAV] Per-song settings saved", L"[SAV] Réglages/morceau", L"[SAV] Impost. per brano",
			L"[SAV] Ajustes por pista", L"[SAV] 곡별 설정 있음", L"[SAV] 有逐曲设置", L"[SAV] إعدادات لكل أغنية",
			L"[SAV] Есть настройки трека", L"[SAV] Pro-Titel-Einstellungen", L"[SAV] Config. por faixa",
			L"[SAV] Per-nummer-instellingen", L"[SAV] Ustawienia utworu", L"[SAV] Parça ayarları var");
		if (!tip.IsEmpty()) { param += _T(": "); param += tip; }
	} else {
		param = LL14(L"[SAV] なし（未保存）", L"[SAV] none (not saved)", L"[SAV] aucun (non sauvé)", L"[SAV] nessuno (non salvato)",
			L"[SAV] ninguno (no guardado)", L"[SAV] 없음(미저장)", L"[SAV] 无（未保存）", L"[SAV] لا (غير محفوظ)",
			L"[SAV] нет (не сохранено)", L"[SAV] keine (nicht gespeichert)", L"[SAV] nenhum (não salvo)",
			L"[SAV] geen (niet opgeslagen)", L"[SAV] brak (niezapisane)", L"[SAV] yok (kaydedilmedi)");
	}
	m_lblParam.SetWindowText(param);
	if (m_btnClearParam.GetSafeHwnd()) m_btnClearParam.EnableWindow(has ? TRUE : FALSE);
	if (m_btnExplorer.GetSafeHwnd()) {
		const bool canExplore = (phys.Find(_T('\\')) >= 0 || phys.Find(_T('/')) >= 0);
		m_btnExplorer.EnableWindow(canExplore ? TRUE : FALSE);
	}
}

void CPlayProperty_tab1::ApplyBatchUi()
{
	if (!IsBatchMode() || !pl || !pl->pc) return;
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
	m_btnExplorer.EnableWindow(FALSE);
	CString commonArt = pl->pc[(*m_batch)[0]].art;
	CString commonAlb = pl->pc[(*m_batch)[0]].alb;
	bool artSame = true, albSame = true;
	for (size_t i = 1; i < m_batch->size(); ++i) {
		const int idx = (*m_batch)[i];
		if (idx < 0 || idx >= pl->playcnt) continue;
		if (commonArt.Compare(pl->pc[idx].art) != 0) artSame = false;
		if (commonAlb.Compare(pl->pc[idx].alb) != 0) albSame = false;
	}
	m_art.SetWindowText(artSame ? commonArt : _T(""));
	m_alb.SetWindowText(albSame ? commonAlb : _T(""));
}

BOOL CPlayProperty_tab1::OnInitDialog()
{
	CCustomDialog::OnInitDialog();
	EnableAero(FALSE);
	m_name.SetAeroMode(FALSE); m_art.SetAeroMode(FALSE); m_alb.SetAeroMode(FALSE); m_fol.SetAeroMode(FALSE);
	m_id.SetAeroMode(FALSE); m_game.SetAeroMode(FALSE); m_time.SetAeroMode(FALSE); m_ret2.SetAeroMode(FALSE);
	m_year.SetAeroMode(FALSE); m_track.SetAeroMode(FALSE); m_j.SetAeroMode(FALSE); m_cmt.SetAeroMode(FALSE);
	m_loop1.SetAeroMode(FALSE); m_loop2.SetAeroMode(FALSE);
	m_grpEdit.SetAeroMode(FALSE); m_grpTag.SetAeroMode(FALSE); m_grpInternal.SetAeroMode(FALSE);
	CCustomStatic* labs[] = {
		&m_lblName, &m_lblId, &m_lblGame, &m_lblArt, &m_lblAlb, &m_lblFile,
		&m_lblYear, &m_lblTrack, &m_lblGenre, &m_lblCmt, &m_lblTime, &m_lblLoop, &m_lblRet2,
		&m_lblStatus, &m_lblParam, &m_lblMeta
	};
	for (int i = 0; i < (int)(sizeof(labs) / sizeof(labs[0])); ++i)
		PpStyleStatic(*labs[i]);
	PpStyleBtn(m_btnBrowse); PpStyleBtn(m_btnTag2Pl); PpStyleBtn(m_btnReloadTag); PpStyleBtn(m_btnWriteTag);
	PpStyleBtn(m_btnCopyPath); PpStyleBtn(m_btnCopyName); PpStyleBtn(m_btnProTools); PpStyleBtn(m_btnClearParam); PpStyleBtn(m_btnExplorer);

	SetDlgItemText(IDC_SYOSAI_GRP_EDIT, LL14(L"プレイリスト", L"Playlist", L"Liste de lecture", L"Playlist", L"Lista de reproducción", L"재생 목록", L"播放列表", L"قائمة التشغيل", L"Плейлист", L"Wiedergabeliste", L"Lista de reprodução", L"Afspeellijst", L"Playlista", L"Çalma listesi"));
	SetDlgItemText(IDC_SYOSAI_GRP_TAG, LL14(L"タグ情報", L"Tag info", L"Infos balises", L"Info tag", L"Info. de etiquetas", L"태그 정보", L"标签信息", L"معلومات الوسم", L"Теги", L"Tag-Info", L"Info. de tags", L"Taginfo", L"Info. o tagach", L"Etiket bilgisi"));
	SetDlgItemText(IDC_SYOSAI_GRP_INTERNAL, LL14(L"内部情報", L"Internal info", L"Infos internes", L"Info interne", L"Info. interna", L"내부 정보", L"内部信息", L"معلومات داخلية", L"Служебная информация", L"Interne Info", L"Info. interna", L"Interne info", L"Info. wewnętrzne", L"Dahili bilgi"));
	SetDlgItemText(IDC_SYOSAI_LBL_NAME, LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"));
	SetDlgItemText(IDC_SYOSAI_LBL_ART, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Künstler", L"Artista", L"Artiest", L"Artysta", L"Sanatçı"));
	SetDlgItemText(IDC_SYOSAI_LBL_ALB, LL14(L"アルバム", L"Album", L"Album", L"Album", L"Álbum", L"앨범", L"专辑", L"الألبوم", L"Альбом", L"Album", L"Álbum", L"Album", L"Album", L"Albüm"));
	SetDlgItemText(IDC_SYOSAI_LBL_FILE, LL14(L"パス", L"Path", L"Chemin", L"Percorso", L"Ruta", L"경로", L"路径", L"المسار", L"Путь", L"Pfad", L"Caminho", L"Pad", L"Ścieżka", L"Yol"));
	SetDlgItemText(IDC_SYOSAI_BTN_BROWSE, LL14(L"参照", L"Browse", L"Parcourir", L"Sfoglia", L"Examinar", L"찾아보기", L"浏览", L"استعراض", L"Обзор", L"Durchsuchen", L"Procurar", L"Bladeren", L"Przeglądaj", L"Gözat"));
	SetDlgItemText(IDC_SYOSAI_BTN_TAG2PL, LL14(L"タグ→PL", L"Tag→PL", L"Tag→PL", L"Tag→PL", L"Tag→PL", L"태그→PL", L"标签→列表", L"وسم→قائمة", L"Тег→PL", L"Tag→PL", L"Tag→PL", L"Tag→PL", L"Tag→PL", L"Etiket→PL"));
	SetDlgItemText(IDC_SYOSAI_BTN_RELOADTAG, LL14(L"再読込", L"Reload", L"Recharger", L"Ricarica", L"Recargar", L"다시 읽기", L"重新加载", L"إعادة تحميل", L"Обновить", L"Neu laden", L"Recarregar", L"Herladen", L"Wczytaj", L"Yenile"));
	SetDlgItemText(IDC_SYOSAI_BTN_WRITETAG, LL14(L"タグ書込", L"Write tag", L"Ecrire tag", L"Scrivi tag", L"Escribir tag", L"태그 쓰기", L"写入标签", L"كتابة وسم", L"Записать тег", L"Tag schreiben", L"Gravar tag", L"Tag schrijven", L"Zapisz tag", L"Etiket yaz"));
	SetDlgItemText(IDC_SYOSAI_BTN_COPYPATH, LL14(L"パスコピー", L"Copy path", L"Copier chemin", L"Copia percorso", L"Copiar ruta", L"경로 복사", L"复制路径", L"نسخ المسار", L"Копировать путь", L"Pfad kopieren", L"Copiar caminho", L"Pad kopiëren", L"Kopiuj ścieżkę", L"Yolu kopyala"));
	SetDlgItemText(IDC_SYOSAI_BTN_COPYNAME, LL14(L"名コピー", L"Copy name", L"Copier nom", L"Copia nome", L"Copiar nombre", L"이름 복사", L"复制文件名", L"نسخ الاسم", L"Копировать имя", L"Name kopieren", L"Copiar nome", L"Naam kopiëren", L"Kopiuj nazwę", L"Adı kopyala"));
	SetDlgItemText(IDC_SYOSAI_BTN_PROTOOLS, LL14(L"再生詳細", L"Playback details", L"Details lecture", L"Dettagli riproduzione", L"Detalles reproducción", L"재생 상세", L"播放详情", L"تفاصيل التشغيل", L"Детали воспроизведения", L"Wiedergabedetails", L"Detalhes de reprodução", L"Afspeeldetails", L"Szczegóły odtwarzania", L"Oynatma ayrıntıları"));
	SetDlgItemText(IDC_SYOSAI_BTN_CLEARPARAM, LL14(L"[SAV]削除", L"Clear [SAV]", L"Effacer [SAV]", L"Cancella [SAV]", L"Borrar [SAV]", L"[SAV] 삭제", L"删除[SAV]", L"مسح [SAV]", L"Удалить [SAV]", L"[SAV] löschen", L"Limpar [SAV]", L"[SAV] wissen", L"Usuń [SAV]", L"[SAV] sil"));
	SetDlgItemText(IDOK999, LL14(L"Explorer", L"Explorer", L"Explorateur", L"Esplora", L"Explorador", L"탐색기", L"资源管理器", L"المستكشف", L"Проводник", L"Explorer", L"Explorer", L"Verkenner", L"Eksplorator", L"Gezgin"));

	if (m_fol.GetSafeHwnd())
		::SendMessage(m_fol.m_hWnd, EM_SETWORDBREAKPROC, 0, (LPARAM)PpEditWordBreakProc);
	if (m_cmt.GetSafeHwnd())
		::SendMessage(m_cmt.m_hWnd, EM_SETWORDBREAKPROC, 0, (LPARAM)PpEditWordBreakProc);

	if (m_pc) {
		m_name.SetWindowText(m_pc->name);
		m_art.SetWindowText(m_pc->art);
		m_alb.SetWindowText(m_pc->alb);
		m_fol.SetWindowText(m_pc->fol);
		TCHAR dy[256];
		m_id.SetWindowText(_itot(m_pc->sub, dy, 10));
		m_game.SetWindowText(m_pc->game);
		CString s;
		if (m_pc->time >= 3600)
			s.Format(_T("%d:%02d:%02d"), m_pc->time / 3600, (m_pc->time / 60) % 60, m_pc->time % 60);
		else
			s.Format(_T("%d:%02d"), m_pc->time / 60, m_pc->time % 60);
		if (m_pc->time == 0) s = _T("");
		if (m_pc->time == -1) s = LL14(L"取得不能", L"Unable to fetch", L"Indisponible", L"Non disponibile", L"No disponible", L"가져올 수 없음", L"无法获取", L"تعذّر الجلب", L"Недоступно", L"Nicht verfügbar", L"Indisponível", L"Niet beschikbaar", L"Niedostępne", L"Alınamadı");
		m_time.SetWindowText(s);
		s.Format(_T("%d"), m_pc->loop1); m_loop1.SetWindowText(s);
		s.Format(_T("%d"), m_pc->loop2); m_loop2.SetWindowText(s);
		s.Format(_T("%d"), m_pc->ret2); m_ret2.SetWindowText(s);
		if (!IsBatchMode()) {
			FileTagFields tags;
			ReadFileTagFields(m_pc->fol, tags);
			if (m_pc->loop1 == 0 && m_pc->loop2 == 0 && (tags.loop1 || tags.loop2)) {
				m_pc->loop1 = tags.loop1; m_pc->loop2 = tags.loop2;
				s.Format(_T("%d"), m_pc->loop1); m_loop1.SetWindowText(s);
				s.Format(_T("%d"), m_pc->loop2); m_loop2.SetWindowText(s);
			}
			ApplyTagsToControls(tags);
		}
	}
	ApplyBatchUi();
	RefreshStatusLines();
	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		CString s;
		s = LL14(L"一覧に出す曲名です。OK でプレイリストへ保存します。",
			L"Display name in the list. OK saves it to the playlist.",
			L"Nom affiché dans la liste. OK l'enregistre.",
			L"Nome in elenco. OK lo salva nella playlist.",
			L"Nombre en la lista. OK lo guarda.",
			L"목록에 보이는 곡명. OK로 재생 목록에 저장합니다.",
			L"列表中的曲名。OK 写入播放列表。",
			L"الاسم في القائمة. OK يحفظه.",
			L"Имя в списке. OK пишет в плейлист.",
			L"Name in der Liste. OK speichert es.",
			L"Nome na lista. OK grava na playlist.",
			L"Naam in de lijst. OK slaat het op.",
			L"Nazwa na liscie. OK zapisuje ja.",
			L"Listedeki ad. OK calma listesine yazar.");
		PpTip(m_tooltip, m_lblName, s); PpTip(m_tooltip, m_name, s);
		s = LL14(L"プレイリスト側のアーティストです。ファイルのタグとは別に保存されます。",
			L"Playlist artist. Stored separately from the file tag.",
			L"Artiste de la liste, distinct du tag fichier.",
			L"Artista della playlist, distinto dal tag file.",
			L"Artista de la lista, aparte de la etiqueta del archivo.",
			L"재생 목록의 아티스트. 파일 태그와 따로 저장됩니다.",
			L"播放列表中的艺术家，与文件标签分开保存。",
			L"فنان القائمة، منفصل عن وسم الملف.",
			L"Исполнитель плейлиста, отдельно от тега файла.",
			L"Kuenstler der Liste, getrennt vom Datei-Tag.",
			L"Artista da lista, separado da tag do arquivo.",
			L"Artiest van de lijst, los van de bestandstag.",
			L"Artysta playlisty, osobno od tagu pliku.",
			L"Calma listesi sanatcisi. Dosya etiketinden ayri saklanir.");
		PpTip(m_tooltip, m_lblArt, s); PpTip(m_tooltip, m_art, s);
		s = LL14(L"プレイリスト側のアルバムです。ファイルのタグとは別に保存されます。",
			L"Playlist album. Stored separately from the file tag.",
			L"Album de la liste, distinct du tag fichier.",
			L"Album della playlist, distinto dal tag file.",
			L"Album de la lista, aparte de la etiqueta.",
			L"재생 목록의 앨범. 파일 태그와 따로 저장됩니다.",
			L"播放列表中的专辑，与文件标签分开保存。",
			L"ألبوم القائمة، منفصل عن وسم الملف.",
			L"Альбом плейлиста, отдельно от тега файла.",
			L"Album der Liste, getrennt vom Datei-Tag.",
			L"Album da lista, separado da tag do arquivo.",
			L"Album van de lijst, los van de bestandstag.",
			L"Album playlisty, osobno od tagu pliku.",
			L"Calma listesi albumu. Dosya etiketinden ayri saklanir.");
		PpTip(m_tooltip, m_lblAlb, s); PpTip(m_tooltip, m_alb, s);
		s = LL14(L"再生に使うファイルの場所です。参照で別のファイルに差し替えられます。",
			L"File used for playback. Browse replaces it with another file.",
			L"Fichier lu. Parcourir le remplace.",
			L"File riprodotto. Sfoglia lo sostituisce.",
			L"Archivo que se reproduce. Examinar lo cambia.",
			L"재생에 쓰는 파일 위치. 찾아보기로 바꿉니다.",
			L"用于播放的文件路径。浏览可换成别的文件。",
			L"ملف التشغيل. استعراض يستبدله.",
			L"Файл воспроизведения. Обзор заменяет его.",
			L"Datei fuer die Wiedergabe. Durchsuchen ersetzt sie.",
			L"Arquivo da reproducao. Procurar troca o arquivo.",
			L"Bestand dat wordt afgespeeld. Bladeren vervangt het.",
			L"Plik odtwarzania. Przegladaj podmienia go.",
			L"Calinan dosyanin yeri. Gozat baska dosyayla degistirir.");
		PpTip(m_tooltip, m_lblFile, s); PpTip(m_tooltip, m_fol, s);
		PpTip(m_tooltip, m_btnBrowse, LL14(L"別のファイルを選んでパスを差し替えます。",
			L"Pick another file and replace the path.",
			L"Choisir un autre fichier.",
			L"Scegli un altro file.",
			L"Elige otro archivo.",
			L"다른 파일을 골라 경로를 바꿉니다.",
			L"另选文件并替换路径。",
			L"اختيار ملف آخر واستبدال المسار.",
			L"Выбрать другой файл.",
			L"Andere Datei waehlen.",
			L"Escolher outro arquivo.",
			L"Ander bestand kiezen.",
			L"Wybierz inny plik.",
			L"Baska dosya sec."));
		s = LL14(L"ファイルから読んだ年です。タグ書込でファイルへ戻します。",
			L"Year read from the file. Write tag stores it back.",
			L"Annee lue du fichier. Ecrire tag la renvoie.",
			L"Anno letto dal file. Scrivi tag lo salva.",
			L"Ano leido del archivo. Escribir tag lo guarda.",
			L"파일에서 읽은 연도. 태그 쓰기로 파일에 넣습니다.",
			L"从文件读出的年份。写入标签会写回文件。",
			L"السنة من الملف. كتابة الوسم تعيدها.",
			L"Год из файла. Запись тега возвращает его.",
			L"Jahr aus der Datei. Tag schreiben speichert es zurueck.",
			L"Ano lido do arquivo. Gravar tag devolve.",
			L"Jaar uit het bestand. Tag schrijven zet het terug.",
			L"Rok z pliku. Zapisz tag zapisuje go z powrotem.",
			L"Dosyadan okunan yil. Etiket yaz dosyaya geri yazar.");
		PpTip(m_tooltip, m_lblYear, s); PpTip(m_tooltip, m_year, s);
		s = LL14(L"ファイルから読んだトラック番号です。",
			L"Track number read from the file.",
			L"Numero de piste lu du fichier.",
			L"Numero traccia letto dal file.",
			L"Numero de pista leido del archivo.",
			L"파일에서 읽은 트랙 번호입니다.",
			L"从文件读出的曲目号。",
			L"رقم المسار من الملف.",
			L"Номер трека из файла.",
			L"Titelnummer aus der Datei.",
			L"Numero da faixa lido do arquivo.",
			L"Tracknummer uit het bestand.",
			L"Numer utworu z pliku.",
			L"Dosyadan okunan parca numarasi.");
		PpTip(m_tooltip, m_lblTrack, s); PpTip(m_tooltip, m_track, s);
		s = LL14(L"ファイルから読んだジャンルです。",
			L"Genre read from the file.",
			L"Genre lu du fichier.",
			L"Genere letto dal file.",
			L"Genero leido del archivo.",
			L"파일에서 읽은 장르입니다.",
			L"从文件读出的流派。",
			L"النوع من الملف.",
			L"Жанр из файла.",
			L"Genre aus der Datei.",
			L"Genero lido do arquivo.",
			L"Genre uit het bestand.",
			L"Gatunek z pliku.",
			L"Dosyadan okunan tur.");
		PpTip(m_tooltip, m_lblGenre, s); PpTip(m_tooltip, m_j, s);
		s = LL14(L"ファイルから読んだコメントです。",
			L"Comment read from the file.",
			L"Commentaire lu du fichier.",
			L"Commento letto dal file.",
			L"Comentario leido del archivo.",
			L"파일에서 읽은 설명입니다.",
			L"从文件读出的注释。",
			L"التعليق من الملف.",
			L"Комментарий из файла.",
			L"Kommentar aus der Datei.",
			L"Comentario lido do arquivo.",
			L"Opmerking uit het bestand.",
			L"Komentarz z pliku.",
			L"Dosyadan okunan yorum.");
		PpTip(m_tooltip, m_lblCmt, s); PpTip(m_tooltip, m_cmt, s);
		PpTip(m_tooltip, m_btnTag2Pl, LL14(L"ファイルのタグを、上の名前・アーティスト・アルバムへコピーします。",
			L"Copy file tags into the name, artist, and album above.",
			L"Copie les tags fichier vers nom, artiste, album.",
			L"Copia i tag file in nome, artista, album.",
			L"Copia las etiquetas al nombre, artista y album.",
			L"파일 태그를 위의 이름·아티스트·앨범으로 복사합니다.",
			L"把文件标签复制到上面的名称、艺术家、专辑。",
			L"ينسخ وسوم الملف إلى الاسم والفنان والألبوم.",
			L"Копирует теги файла в имя, исполнителя и альбом.",
			L"Kopiert Datei-Tags in Name, Kuenstler und Album.",
			L"Copia tags do arquivo para nome, artista e album.",
			L"Kopieert bestandstags naar naam, artiest en album.",
			L"Kopiuje tagi pliku do nazwy, artysty i albumu.",
			L"Dosya etiketlerini ad, sanatci ve albume kopyalar."));
		PpTip(m_tooltip, m_btnReloadTag, LL14(L"ファイルからタグを読み直します。",
			L"Reload tags from the file.",
			L"Relire les tags du fichier.",
			L"Rilegge i tag dal file.",
			L"Vuelve a leer las etiquetas.",
			L"파일에서 태그를 다시 읽습니다.",
			L"从文件重新读取标签。",
			L"يعيد قراءة الوسوم من الملف.",
			L"Снова читает теги из файла.",
			L"Tags neu aus der Datei lesen.",
			L"Reler as tags do arquivo.",
			L"Tags opnieuw uit het bestand lezen.",
			L"Wczytaj tagi z pliku ponownie.",
			L"Etiketleri dosyadan yeniden okur."));
		PpTip(m_tooltip, m_btnWriteTag, LL14(L"年・トラック・ジャンル・コメント・ループをファイルへ書きます。MP3 / FLAC / WAV / M4A / Ogg Vorbis。",
			L"Write year, track, genre, comment, and loop into the file. MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"Ecrit annee, piste, genre, commentaire et boucle. MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"Scrive anno, traccia, genere, commento e loop. MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"Escribe ano, pista, genero, comentario y bucle. MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"연도·트랙·장르·설명·루프를 파일에 씁니다. MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"把年、曲目、流派、注释、循环写入文件。MP3 / FLAC / WAV / M4A / Ogg Vorbis。",
			L"يكتب السنة والمسار والنوع والتعليق والتكرار. MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"Пишет год, трек, жанр, комментарий и петлю. MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"Schreibt Jahr, Titel, Genre, Kommentar und Loop. MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"Grava ano, faixa, genero, comentario e loop. MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"Schrijft jaar, track, genre, opmerking en loop. MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"Zapisuje rok, utwor, gatunek, komentarz i petle. MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"Yil, parca, tur, yorum ve donguyu dosyaya yazar. MP3 / FLAC / WAV / M4A / Ogg Vorbis."));
		s = LL14(L"再生経路の番号です（内蔵デコーダやプラグインの区別）。変更はできません。",
			L"Playback route number (built-in decoder or plugin). Read-only.",
			L"Numero de voie de lecture (decodeur interne ou plugin). Lecture seule.",
			L"Numero del percorso di lettura. Sola lettura.",
			L"Numero de la via de reproduccion. Solo lectura.",
			L"재생 경로 번호(내장 디코더 또는 플러그인). 변경할 수 없습니다.",
			L"播放路径编号（内置解码或插件）。不能修改。",
			L"رقم مسار التشغيل. للقراءة فقط.",
			L"Номер пути воспроизведения. Только чтение.",
			L"Nummer des Wiedergabewegs. Nur lesen.",
			L"Numero da rota de reproducao. Somente leitura.",
			L"Nummer van de afspeelroute. Alleen lezen.",
			L"Numer sciezki odtwarzania. Tylko do odczytu.",
			L"Calma yolu numarasi. Degistirilemez.");
		PpTip(m_tooltip, m_lblId, s); PpTip(m_tooltip, m_id, s);
		s = LL14(L"曲に付いたゲーム名です。変更はできません。",
			L"Game name stored with the track. Read-only.",
			L"Nom du jeu lie a la piste. Lecture seule.",
			L"Nome del gioco della traccia. Sola lettura.",
			L"Nombre del juego de la pista. Solo lectura.",
			L"곡에 붙은 게임 이름입니다. 변경할 수 없습니다.",
			L"曲目附带的游戏名。不能修改。",
			L"اسم اللعبة المرتبط بالمسار. للقراءة فقط.",
			L"Название игры у трека. Только чтение.",
			L"Spielname des Titels. Nur lesen.",
			L"Nome do jogo da faixa. Somente leitura.",
			L"Spelnaam bij het nummer. Alleen lezen.",
			L"Nazwa gry utworu. Tylko do odczytu.",
			L"Parcaya bagli oyun adi. Degistirilemez.");
		PpTip(m_tooltip, m_lblGame, s); PpTip(m_tooltip, m_game, s);
		s = LL14(L"一覧に出す長さです。変更はできません。",
			L"Length shown in the list. Read-only.",
			L"Duree affichee. Lecture seule.",
			L"Durata in elenco. Sola lettura.",
			L"Duracion en la lista. Solo lectura.",
			L"목록에 보이는 길이입니다. 변경할 수 없습니다.",
			L"列表中的时长。不能修改。",
			L"المدة في القائمة. للقراءة فقط.",
			L"Длительность в списке. Только чтение.",
			L"Laenge in der Liste. Nur lesen.",
			L"Duracao na lista. Somente leitura.",
			L"Duur in de lijst. Alleen lezen.",
			L"Dlugosc na liscie. Tylko do odczytu.",
			L"Listede gorunen sure. Degistirilemez.");
		PpTip(m_tooltip, m_lblTime, s); PpTip(m_tooltip, m_time, s);
		s = LL14(L"ループの開始と終了（サンプル位置）。タグ書込でファイルへ保存できます。",
			L"Loop start and end (sample position). Write tag can store them in the file.",
			L"Debut et fin de boucle (echantillons). Ecrire tag peut les sauver.",
			L"Inizio e fine loop (campioni). Scrivi tag puo salvarli.",
			L"Inicio y fin del bucle (muestras). Escribir tag puede guardarlos.",
			L"루프 시작과 끝(샘플 위치). 태그 쓰기로 파일에 저장할 수 있습니다.",
			L"循环起止（采样位置）。写入标签可存进文件。",
			L"بداية التكرار ونهايته (موضع العينة).",
			L"Начало и конец петли (позиция сэмпла).",
			L"Loop-Anfang und -Ende (Sampleposition).",
			L"Inicio e fim do loop (posicao de amostra).",
			L"Loopbegin en -einde (samplepositie).",
			L"Poczatek i koniec petli (pozycja probki).",
			L"Dongu baslangici ve bitisi (ornek konumu).");
		PpTip(m_tooltip, m_lblLoop, s); PpTip(m_tooltip, m_loop1, s); PpTip(m_tooltip, m_loop2, s);
		s = LL14(L"曲ごとの補助番号です。変更はできません。",
			L"Extra index stored with the track. Read-only.",
			L"Index auxiliaire de la piste. Lecture seule.",
			L"Indice ausiliario della traccia. Sola lettura.",
			L"Indice extra de la pista. Solo lectura.",
			L"곡별 보조 번호입니다. 변경할 수 없습니다.",
			L"曲目的附加编号。不能修改。",
			L"رقم إضافي للمسار. للقراءة فقط.",
			L"Доп. индекс трека. Только чтение.",
			L"Zusatzindex des Titels. Nur lesen.",
			L"Indice extra da faixa. Somente leitura.",
			L"Extra index van het nummer. Alleen lezen.",
			L"Dodatkowy indeks utworu. Tylko do odczytu.",
			L"Parcaya ozel ek numara. Degistirilemez.");
		PpTip(m_tooltip, m_lblRet2, s); PpTip(m_tooltip, m_ret2, s);
		PpTip(m_tooltip, m_lblStatus, LL14(L"ファイルの有無、拡張子、サイズ、更新日時です。",
			L"Whether the file exists, plus extension, size, and modified time.",
			L"Presence du fichier, extension, taille et date.",
			L"Presenza del file, estensione, dimensione e data.",
			L"Si el archivo existe, extension, tamano y fecha.",
			L"파일 존재, 확장자, 크기, 수정 시각입니다.",
			L"文件是否存在、扩展名、大小、修改时间。",
			L"وجود الملف والامتداد والحجم والتاريخ.",
			L"Наличие файла, расширение, размер и дата.",
			L"Datei vorhanden, Erweiterung, Groesse und Datum.",
			L"Se o arquivo existe, extensao, tamanho e data.",
			L"Of het bestand bestaat, plus extensie, grootte en datum.",
			L"Czy plik istnieje, rozszerzenie, rozmiar i data.",
			L"Dosya var mi, uzanti, boyut ve tarih."));
		PpTip(m_tooltip, m_lblParam, LL14(L"この曲だけの音量・EQ（[SAV]）があるとき、その内容を出します。",
			L"Shows this track's own volume and EQ ([SAV]) when one exists.",
			L"Affiche volume et EQ propres a cette piste ([SAV]).",
			L"Mostra volume ed EQ propri di questa traccia ([SAV]).",
			L"Muestra volumen y EQ propios de esta pista ([SAV]).",
			L"이 곡만의 음량·EQ([SAV])가 있으면 내용을 보여 줍니다.",
			L"若这首有单独的音量与 EQ（[SAV]），在此显示。",
			L"يعرض مستوى الصوت وEQ الخاصين بهذا المسار ([SAV]).",
			L"Показывает громкость и EQ только этого трека ([SAV]).",
			L"Zeigt Lautstaerke und EQ nur dieses Titels ([SAV]).",
			L"Mostra volume e EQ so desta faixa ([SAV]).",
			L"Toont volume en EQ van alleen dit nummer ([SAV]).",
			L"Pokazuje glosnosc i EQ tylko tego utworu ([SAV]).",
			L"Yalniz bu parcanin ses ve EQ'sunu ([SAV]) gosterir."));
		PpTip(m_tooltip, m_btnExplorer, LL14(L"エクスプローラーでこのファイルを選択して開きます。ファイルが無いときは使えません。",
			L"Select this file in Explorer. Unavailable if the file is missing.",
			L"Selectionne ce fichier dans l'Explorateur. Indisponible s'il manque.",
			L"Seleziona il file in Esplora. Non disponibile se manca.",
			L"Selecciona el archivo en el Explorador. No disponible si falta.",
			L"탐색기에서 이 파일을 선택해 엽니다. 파일이 없으면 쓸 수 없습니다.",
			L"在资源管理器中选中此文件。文件不在时不可用。",
			L"يحدد الملف في المستكشف. غير متاح إن كان مفقوداً.",
			L"Выделяет файл в Проводнике. Недоступно, если файла нет.",
			L"Waehlt die Datei im Explorer. Nicht verfuegbar, wenn sie fehlt.",
			L"Seleciona o arquivo no Explorer. Indisponivel se faltar.",
			L"Selecteert het bestand in Verkenner. Niet beschikbaar als het ontbreekt.",
			L"Zaznacza plik w Eksploratorze. Niedostepne, gdy pliku brak.",
			L"Gezginde bu dosyayi secer. Dosya yoksa kullanilamaz."));
		PpTip(m_tooltip, m_btnCopyPath, LL14(L"パスをクリップボードへコピーします。",
			L"Copy the path to the clipboard.",
			L"Copie le chemin.",
			L"Copia il percorso.",
			L"Copia la ruta.",
			L"경로를 클립보드에 복사합니다.",
			L"把路径复制到剪贴板。",
			L"ينسخ المسار.",
			L"Копирует путь.",
			L"Pfad kopieren.",
			L"Copia o caminho.",
			L"Kopieert het pad.",
			L"Kopiuje sciezke.",
			L"Yolu kopyalar."));
		PpTip(m_tooltip, m_btnCopyName, LL14(L"ファイル名だけをクリップボードへコピーします。",
			L"Copy only the file name to the clipboard.",
			L"Copie seulement le nom de fichier.",
			L"Copia solo il nome file.",
			L"Copia solo el nombre de archivo.",
			L"파일 이름만 클립보드에 복사합니다.",
			L"只把文件名复制到剪贴板。",
			L"ينسخ اسم الملف فقط.",
			L"Копирует только имя файла.",
			L"Nur den Dateinamen kopieren.",
			L"Copia so o nome do arquivo.",
			L"Kopieert alleen de bestandsnaam.",
			L"Kopiuje sama nazwe pliku.",
			L"Yalnizca dosya adini kopyalar."));
		PpTip(m_tooltip, m_btnProTools, LL14(L"この曲の再生詳細（音量・EQ など）を開きます。",
			L"Open playback details for this track (volume, EQ, and more).",
			L"Ouvre les details de lecture (volume, EQ…).",
			L"Apre i dettagli di riproduzione (volume, EQ…).",
			L"Abre los detalles de reproduccion (volumen, EQ…).",
			L"이 곡의 재생 상세(음량·EQ 등)를 엽니다.",
			L"打开这首的播放详情（音量、EQ 等）。",
			L"يفتح تفاصيل التشغيل (المستوى وEQ).",
			L"Открывает детали воспроизведения (громкость, EQ).",
			L"Oeffnet Wiedergabedetails (Lautstaerke, EQ).",
			L"Abre detalhes de reproducao (volume, EQ).",
			L"Opent afspeeldetails (volume, EQ).",
			L"Otwiera szczegoly odtwarzania (glosnosc, EQ).",
			L"Bu parcanin oynatma ayrintilarini acar (ses, EQ)."));
		PpTip(m_tooltip, m_btnClearParam, LL14(L"この曲だけの音量・EQ（[SAV]）を消します。無いときは押せません。",
			L"Delete this track's own volume and EQ ([SAV]). Disabled when there is none.",
			L"Efface volume et EQ propres ([SAV]). Desactive s'il n'y en a pas.",
			L"Cancella volume ed EQ propri ([SAV]). Disattivo se assenti.",
			L"Borra volumen y EQ propios ([SAV]). Desactivado si no hay.",
			L"이 곡만의 음량·EQ([SAV])를 지웁니다. 없으면 누를 수 없습니다.",
			L"删除这首单独的音量与 EQ（[SAV]）。没有时不可按。",
			L"يحذف مستوى الصوت وEQ الخاصين ([SAV]).",
			L"Удаляет громкость и EQ только этого трека ([SAV]).",
			L"Loescht Lautstaerke und EQ nur dieses Titels ([SAV]).",
			L"Apaga volume e EQ so desta faixa ([SAV]).",
			L"Wist volume en EQ van alleen dit nummer ([SAV]).",
			L"Usuwa glosnosc i EQ tylko tego utworu ([SAV]).",
			L"Yalniz bu parcanin ses ve EQ'sunu ([SAV]) siler."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
	}
	return TRUE;
}

void CPlayProperty_tab1::OnBnClickedExplorer() { PpOpenExplorer(CurrentPathText()); }
void CPlayProperty_tab1::OnBnClickedCopyPath() { PpCopyText(GetSafeHwnd(), CurrentPathText()); }
void CPlayProperty_tab1::OnBnClickedCopyName()
{
	CString name = CurrentPathText();
	const int slash = (std::max)(name.ReverseFind(_T('\\')), name.ReverseFind(_T('/')));
	if (slash >= 0) name = name.Mid(slash + 1);
	PpCopyText(GetSafeHwnd(), name);
}

void CPlayProperty_tab1::OnBnClickedBrowse()
{
	CString cur = CurrentPathText();
	CString phys = PlPhysicalMediaPath(cur);
	if (phys.IsEmpty()) phys = cur;
	CFileDialog dlg(TRUE, NULL, phys.IsEmpty() ? NULL : (LPCTSTR)phys,
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER, _T("All Files (*.*)|*.*||"), this);
	if (dlg.DoModal() != IDOK) return;
	CString chosen = dlg.GetPathName();
	const CString stored = PlStorePlaylistFol(chosen, m_pc ? m_pc->sub : 0);
	m_fol.SetWindowText(stored);
	if (m_pc) {
		_tcsncpy(m_pc->fol, stored, _countof(m_pc->fol) - 1);
		m_pc->fol[_countof(m_pc->fol) - 1] = 0;
	}
	RefreshStatusLines();
}

void CPlayProperty_tab1::OnBnClickedTag2Pl()
{
	FileTagFields tags;
	ReadFileTagFields(CurrentPathText(), tags);
	if (!tags.title.IsEmpty()) m_name.SetWindowText(tags.title);
	if (!tags.artist.IsEmpty()) m_art.SetWindowText(tags.artist);
	if (!tags.album.IsEmpty()) m_alb.SetWindowText(tags.album);
	ApplyTagsToControls(tags, false);
}

void CPlayProperty_tab1::OnBnClickedReloadTag()
{
	FileTagFields tags;
	ReadFileTagFields(CurrentPathText(), tags);
	ApplyTagsToControls(tags, true);
	RefreshStatusLines();
}

void CPlayProperty_tab1::OnBnClickedWriteTag()
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
	if (m_pc) { m_pc->loop1 = loopStart; m_pc->loop2 = loopEnd; }
	AfxMessageBox(LL14(L"タグを書き込みました。", L"Tags written.", L"Tags ecrits.", L"Tag scritti.",
		L"Etiquetas escritas.", L"태그 저장됨.", L"标签已写入。", L"تمت الكتابة.",
		L"Теги записаны.", L"Tags geschrieben.", L"Tags gravadas.", L"Tags geschreven.",
		L"Tagi zapisane.", L"Etiketler yazıldı."), MB_ICONINFORMATION);
}

void CPlayProperty_tab1::OnBnClickedProTools()
{
	CollectPlaylistFields();
	OpenProToolsForSelection();
}

void CPlayProperty_tab1::OnBnClickedClearParam()
{
	if (AfxMessageBox(LL14(
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
		L"Bu parçanın kayıtlı parametreleri silinsin mi?"), MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	if (!m_pc) return;
	playlistdata0 item = *m_pc;
	CString path = CurrentPathText();
	_tcsncpy(item.fol, path, _countof(item.fol) - 1);
	item.fol[_countof(item.fol) - 1] = 0;
	CString listKey = SongParams_CurrentListName();
	SongParams_RebindEntries(listKey, NULL, &item, 1, false);
	RefreshStatusLines();
}

IMPLEMENT_DYNAMIC(CPlayProperty_tab2, CCustomDialog)

CPlayProperty_tab2::CPlayProperty_tab2(CWnd* pParent)
	: CCustomDialog(CPlayProperty_tab2::IDD, pParent)
	, m_pc(NULL), m_multi(false), m_coverBmp(NULL)
{
}

CPlayProperty_tab2::~CPlayProperty_tab2()
{
	if (m_coverBmp) { ::DeleteObject(m_coverBmp); m_coverBmp = NULL; }
}

void CPlayProperty_tab2::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
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
}

BEGIN_MESSAGE_MAP(CPlayProperty_tab2, CCustomDialog)
	ON_BN_CLICKED(IDC_TAGEDIT_SAVE, OnBnClickedSave)
	ON_BN_CLICKED(IDC_TAGEDIT_COVER_CLEAR, OnBnClickedCoverClear)
	ON_WM_DROPFILES()
END_MESSAGE_MAP()

void CPlayProperty_tab2::OnOK() {}
void CPlayProperty_tab2::OnCancel() {}

BOOL CPlayProperty_tab2::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomDialog::PreTranslateMessage(pMsg);
}

BOOL CPlayProperty_tab2::OnInitDialog()
{
	CCustomDialog::OnInitDialog();
	EnableAero(FALSE);
	DragAcceptFiles(TRUE);
	m_title.SetAeroMode(FALSE); m_artist.SetAeroMode(FALSE); m_album.SetAeroMode(FALSE);
	m_year.SetAeroMode(FALSE); m_track.SetAeroMode(FALSE); m_genre.SetAeroMode(FALSE); m_comment.SetAeroMode(FALSE);
	CCustomStatic* labs[] = {
		&m_titleL, &m_artistL, &m_albumL, &m_yearL, &m_trackL, &m_genreL, &m_commentL,
		&m_coverL, &m_cover, &m_hint, &m_status
	};
	for (int i = 0; i < (int)(sizeof(labs) / sizeof(labs[0])); ++i)
		PpStyleStatic(*labs[i]);
	PpStyleBtn(m_coverClear); PpStyleBtn(m_save);
	SetDlgItemText(IDC_TAGEDIT_TITLE_L, LL14(L"タイトル", L"Title", L"Titre", L"Titolo", L"Título", L"제목", L"标题", L"العنوان", L"Название", L"Titel", L"Título", L"Titel", L"Tytuł", L"Başlık"));
	SetDlgItemText(IDC_TAGEDIT_ARTIST_L, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Künstler", L"Artista", L"Artiest", L"Artysta", L"Sanatçı"));
	SetDlgItemText(IDC_TAGEDIT_ALBUM_L, LL14(L"アルバム", L"Album", L"Album", L"Album", L"Álbum", L"앨범", L"专辑", L"الألبوم", L"Альбом", L"Album", L"Álbum", L"Album", L"Album", L"Albüm"));
	SetDlgItemText(IDC_TAGEDIT_SAVE, LL14(L"タグ書込", L"Write tag", L"Ecrire tag", L"Scrivi tag", L"Escribir tag", L"태그 쓰기", L"写入标签", L"كتابة وسم", L"Записать тег", L"Tag schreiben", L"Gravar tag", L"Tag schrijven", L"Zapisz tag", L"Etiket yaz"));
	SetDlgItemText(IDC_TAGEDIT_COVER_CLEAR, LL14(L"解除", L"Clear", L"Effacer", L"Cancella", L"Borrar", L"해제", L"清除", L"مسح", L"Сброс", L"Löschen", L"Limpar", L"Wissen", L"Wyczyść", L"Temizle"));
	m_cover.SetWindowText(ExportTagUi_CoverHint());

	if (m_pc) {
		FileTagFields tags;
		if (!m_multi) {
			ReadFileTagFields(m_pc->fol, tags);
			m_title.SetWindowText(tags.title.IsEmpty() ? m_pc->name : tags.title);
			m_artist.SetWindowText(tags.artist.IsEmpty() ? m_pc->art : tags.artist);
			m_album.SetWindowText(tags.album.IsEmpty() ? m_pc->alb : tags.album);
			m_year.SetWindowText(tags.year);
			m_track.SetWindowText(tags.track);
			m_genre.SetWindowText(tags.genre);
			m_comment.SetWindowText(tags.comment);
			ExportTagUi_ShowExistingCover(m_coverPic, m_cover, m_coverBmp, m_pc->fol);
		} else {
			m_title.SetWindowText(_T(""));
			m_title.EnableWindow(FALSE);
			m_artist.SetWindowText(_T(""));
			m_album.SetWindowText(_T(""));
			m_year.SetWindowText(_T(""));
			m_track.SetWindowText(_T(""));
			m_genre.SetWindowText(_T(""));
			m_comment.SetWindowText(_T(""));
			m_hint.SetWindowText(LL14(L"空欄は変更しません。入力した項目だけ全選択へ書きます。",
				L"Blank fields are left unchanged. Only filled fields are written to all selected tracks.",
				L"Les champs vides ne changent rien.", L"I campi vuoti non cambiano.", L"Los campos vacíos no cambian.",
				L"빈 칸은 바꾸지 않습니다.", L"空白项不改。", L"الحقول الفارغة لا تُغيّر.",
				L"Пустые поля не меняются.", L"Leere Felder bleiben.", L"Campos vazios não mudam.",
				L"Lege velden wijzigen niet.", L"Puste pola bez zmian.", L"Boş alanlar değişmez."));
		}
	}
	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		CString s;
		s = LL14(L"ファイルに書くタイトルです。複数選択では空欄のままにすると変更しません。",
			L"Title written to the file. With several tracks, a blank field is left unchanged.",
			L"Titre ecrit dans le fichier. A plusieurs, un champ vide ne change rien.",
			L"Titolo scritto nel file. Con piu tracce, un campo vuoto non cambia.",
			L"Titulo que se escribe. Con varias pistas, un campo vacio no cambia.",
			L"파일에 쓸 제목입니다. 여러 곡이면 빈 칸은 바꾸지 않습니다.",
			L"写入文件的标题。多选时空白项不改。",
			L"العنوان المكتوب في الملف. مع عدة مسارات الحقل الفارغ لا يتغير.",
			L"Название в файл. При нескольких треках пустое поле не меняется.",
			L"Titel in die Datei. Bei mehreren Titeln bleibt ein leeres Feld.",
			L"Titulo gravado no arquivo. Com varias faixas, campo vazio nao muda.",
			L"Titel naar het bestand. Bij meerdere nummers blijft een leeg veld.",
			L"Tytul zapisywany do pliku. Przy wielu utworach puste pole bez zmian.",
			L"Dosyaya yazilan baslik. Birden cok parcada bos alan degismez.");
		PpTip(m_tooltip, m_titleL, s); PpTip(m_tooltip, m_title, s);
		s = LL14(L"ファイルに書くアーティストです。複数選択では空欄のままにすると変更しません。",
			L"Artist written to the file. With several tracks, a blank field is left unchanged.",
			L"Artiste ecrit dans le fichier. Champ vide = aucun changement.",
			L"Artista scritto nel file. Campo vuoto = nessun cambiamento.",
			L"Artista que se escribe. Campo vacio = sin cambio.",
			L"파일에 쓸 아티스트입니다. 여러 곡이면 빈 칸은 바꾸지 않습니다.",
			L"写入文件的艺术家。多选时空白项不改。",
			L"الفنان المكتوب في الملف. الحقل الفارغ لا يتغير.",
			L"Исполнитель в файл. Пустое поле не меняется.",
			L"Kuenstler in die Datei. Leeres Feld bleibt.",
			L"Artista gravado no arquivo. Campo vazio nao muda.",
			L"Artiest naar het bestand. Leeg veld blijft.",
			L"Artysta zapisywany do pliku. Puste pole bez zmian.",
			L"Dosyaya yazilan sanatci. Bos alan degismez.");
		PpTip(m_tooltip, m_artistL, s); PpTip(m_tooltip, m_artist, s);
		s = LL14(L"ファイルに書くアルバムです。複数選択では空欄のままにすると変更しません。",
			L"Album written to the file. With several tracks, a blank field is left unchanged.",
			L"Album ecrit dans le fichier. Champ vide = aucun changement.",
			L"Album scritto nel file. Campo vuoto = nessun cambiamento.",
			L"Album que se escribe. Campo vacio = sin cambio.",
			L"파일에 쓸 앨범입니다. 여러 곡이면 빈 칸은 바꾸지 않습니다.",
			L"写入文件的专辑。多选时空白项不改。",
			L"الألبوم المكتوب في الملف. الحقل الفارغ لا يتغير.",
			L"Альбом в файл. Пустое поле не меняется.",
			L"Album in die Datei. Leeres Feld bleibt.",
			L"Album gravado no arquivo. Campo vazio nao muda.",
			L"Album naar het bestand. Leeg veld blijft.",
			L"Album zapisywany do pliku. Puste pole bez zmian.",
			L"Dosyaya yazilan album. Bos alan degismez.");
		PpTip(m_tooltip, m_albumL, s); PpTip(m_tooltip, m_album, s);
		s = LL14(L"ファイルに書く年です。",
			L"Year written to the file.",
			L"Annee ecrite dans le fichier.",
			L"Anno scritto nel file.",
			L"Ano que se escribe.",
			L"파일에 쓸 연도입니다.",
			L"写入文件的年份。",
			L"السنة المكتوبة في الملف.",
			L"Год в файл.",
			L"Jahr in die Datei.",
			L"Ano gravado no arquivo.",
			L"Jaar naar het bestand.",
			L"Rok zapisywany do pliku.",
			L"Dosyaya yazilan yil.");
		PpTip(m_tooltip, m_yearL, s); PpTip(m_tooltip, m_year, s);
		s = LL14(L"ファイルに書くトラック番号です。",
			L"Track number written to the file.",
			L"Numero de piste ecrit dans le fichier.",
			L"Numero traccia scritto nel file.",
			L"Numero de pista que se escribe.",
			L"파일에 쓸 트랙 번호입니다.",
			L"写入文件的曲目号。",
			L"رقم المسار المكتوب في الملف.",
			L"Номер трека в файл.",
			L"Titelnummer in die Datei.",
			L"Numero da faixa gravado no arquivo.",
			L"Tracknummer naar het bestand.",
			L"Numer utworu zapisywany do pliku.",
			L"Dosyaya yazilan parca numarasi.");
		PpTip(m_tooltip, m_trackL, s); PpTip(m_tooltip, m_track, s);
		s = LL14(L"ファイルに書くジャンルです。",
			L"Genre written to the file.",
			L"Genre ecrit dans le fichier.",
			L"Genere scritto nel file.",
			L"Genero que se escribe.",
			L"파일에 쓸 장르입니다.",
			L"写入文件的流派。",
			L"النوع المكتوب في الملف.",
			L"Жанр в файл.",
			L"Genre in die Datei.",
			L"Genero gravado no arquivo.",
			L"Genre naar het bestand.",
			L"Gatunek zapisywany do pliku.",
			L"Dosyaya yazilan tur.");
		PpTip(m_tooltip, m_genreL, s); PpTip(m_tooltip, m_genre, s);
		s = LL14(L"ファイルに書くコメントです。",
			L"Comment written to the file.",
			L"Commentaire ecrit dans le fichier.",
			L"Commento scritto nel file.",
			L"Comentario que se escribe.",
			L"파일에 쓸 설명입니다.",
			L"写入文件的注释。",
			L"التعليق المكتوب في الملف.",
			L"Комментарий в файл.",
			L"Kommentar in die Datei.",
			L"Comentario gravado no arquivo.",
			L"Opmerking naar het bestand.",
			L"Komentarz zapisywany do pliku.",
			L"Dosyaya yazilan yorum.");
		PpTip(m_tooltip, m_commentL, s); PpTip(m_tooltip, m_comment, s);
		s = LL14(L"ジャケット画像です。画像ファイルをここにドロップしても入れられます。",
			L"Cover art. You can also drop an image file here.",
			L"Pochette. Vous pouvez aussi deposer une image ici.",
			L"Copertina. Si puo anche rilasciare un'immagine qui.",
			L"Portada. Tambien puedes soltar una imagen aqui.",
			L"재킷 그림입니다. 이미지 파일을 여기에 놓아도 됩니다.",
			L"封面图。也可以把图片拖到这里。",
			L"صورة الغلاف. يمكن إفلات ملف صورة هنا.",
			L"Обложка. Сюда можно перетащить изображение.",
			L"Cover. Ein Bild kann hier abgelegt werden.",
			L"Capa. Tambem da para soltar uma imagem aqui.",
			L"Hoes. Je kunt hier ook een afbeelding neerzetten.",
			L"Okladka. Mozna tu upuscic obraz.",
			L"Kapak resmi. Buraya resim dosyasi da birakilabilir.");
		PpTip(m_tooltip, m_coverL, s); PpTip(m_tooltip, m_cover, s); PpTip(m_tooltip, m_coverPic, s);
		PpTip(m_tooltip, m_coverClear, LL14(L"入れたジャケットを外します。保存するまでファイルは変わりません。",
			L"Remove the cover. The file stays unchanged until you save.",
			L"Retire la pochette. Le fichier change seulement a l'enregistrement.",
			L"Toglie la copertina. Il file cambia solo al salvataggio.",
			L"Quita la portada. El archivo cambia al guardar.",
			L"넣은 재킷을 뺍니다. 저장하기 전에는 파일이 바뀌지 않습니다.",
			L"去掉封面。保存前文件不变。",
			L"يزيل الغلاف. الملف لا يتغير حتى الحفظ.",
			L"Убирает обложку. Файл не меняется до сохранения.",
			L"Cover entfernen. Die Datei aendert sich erst beim Speichern.",
			L"Remove a capa. O arquivo so muda ao salvar.",
			L"Hoes weghalen. Het bestand wijzigt pas bij opslaan.",
			L"Usuwa okladke. Plik zmienia sie dopiero po zapisaniu.",
			L"Kapagi kaldirir. Kaydedene kadar dosya degismez."));
		PpTip(m_tooltip, m_hint, LL14(L"対応は MP3 / FLAC / WAV / M4A / Ogg Vorbis です。再生中の曲が含まれると、書いてから同じ位置で再開します。",
			L"Supported: MP3 / FLAC / WAV / M4A / Ogg Vorbis. If a playing file is included, playback resumes at the same place after writing.",
			L"Pris en charge : MP3 / FLAC / WAV / M4A / Ogg Vorbis. Si un fichier en lecture est inclus, la lecture reprend au meme endroit.",
			L"Supportati: MP3 / FLAC / WAV / M4A / Ogg Vorbis. Se c'e il file in riproduzione, riparte dallo stesso punto.",
			L"Admitidos: MP3 / FLAC / WAV / M4A / Ogg Vorbis. Si incluye el archivo en reproduccion, sigue en el mismo punto.",
			L"지원: MP3 / FLAC / WAV / M4A / Ogg Vorbis. 재생 중인 파일이 들어 있으면 쓴 뒤 같은 위치에서 이어 재생합니다.",
			L"支持 MP3 / FLAC / WAV / M4A / Ogg Vorbis。若包含正在播放的文件，写完后从原位置继续。",
			L"المدعوم: MP3 / FLAC / WAV / M4A / Ogg Vorbis.",
			L"Поддержка: MP3 / FLAC / WAV / M4A / Ogg Vorbis. Если файл играет, воспроизведение продолжится с того же места.",
			L"Unterstuetzt: MP3 / FLAC / WAV / M4A / Ogg Vorbis. Laeuft eine Datei, geht es danach an derselben Stelle weiter.",
			L"Suportado: MP3 / FLAC / WAV / M4A / Ogg Vorbis. Se a faixa tocando entra, a reproducao volta ao mesmo ponto.",
			L"Ondersteund: MP3 / FLAC / WAV / M4A / Ogg Vorbis. Speelt een bestand mee, dan gaat het daarna op dezelfde plek verder.",
			L"Obslugiwane: MP3 / FLAC / WAV / M4A / Ogg Vorbis. Jesli gra plik z zestawu, odtwarzanie wroci w to samo miejsce.",
			L"Destek: MP3 / FLAC / WAV / M4A / Ogg Vorbis. Calan dosya varsa yazinca ayni yerden surer."));
		PpTip(m_tooltip, m_status, LL14(L"直前の保存が成功したか、失敗したかを出します。",
			L"Shows whether the last save succeeded or failed.",
			L"Indique si le dernier enregistrement a reussi.",
			L"Indica se l'ultimo salvataggio e riuscito.",
			L"Indica si el ultimo guardado funciono.",
			L"바로 앞의 저장이 되었는지 실패했는지를 보여 줍니다.",
			L"显示刚才的保存是成功还是失败。",
			L"يبين نجاح آخر حفظ أو فشله.",
			L"Показывает, удалось ли последнее сохранение.",
			L"Zeigt, ob das letzte Speichern gelang.",
			L"Mostra se o ultimo salvamento deu certo.",
			L"Toont of het laatste opslaan lukte.",
			L"Pokazuje, czy ostatni zapis sie udal.",
			L"Son kaydin basarili olup olmadigini gosterir."));
		PpTip(m_tooltip, m_save, LL14(L"入力したタグとジャケットをファイルへ書きます。再生中の曲が含まれると一旦止めて、同じ位置から再開します。",
			L"Write the tags and cover into the file. If a playing file is included, playback stops and resumes at the same place.",
			L"Ecrit tags et pochette. Si un fichier en lecture est inclus, la lecture reprend au meme endroit.",
			L"Scrive tag e copertina. Se c'e il file in riproduzione, riparte dallo stesso punto.",
			L"Escribe etiquetas y portada. Si incluye el archivo en reproduccion, sigue en el mismo punto.",
			L"입력한 태그와 재킷을 파일에 씁니다. 재생 중인 곡이 있으면 멈춘 뒤 같은 위치에서 이어 재생합니다.",
			L"把标签和封面写入文件。若包含正在播放的文件，会先停下再从原位置继续。",
			L"يكتب الوسوم والغلاف في الملف.",
			L"Пишет теги и обложку в файл. Если файл играет, воспроизведение продолжится с того же места.",
			L"Schreibt Tags und Cover. Laeuft eine Datei, geht es danach an derselben Stelle weiter.",
			L"Grava tags e capa. Se a faixa tocando entra, a reproducao volta ao mesmo ponto.",
			L"Schrijft tags en hoes. Speelt een bestand mee, dan gaat het daarna op dezelfde plek verder.",
			L"Zapisuje tagi i okladke. Jesli gra plik z zestawu, odtwarzanie wroci w to samo miejsce.",
			L"Etiket ve kapagi dosyaya yazar. Calan dosya varsa durup ayni yerden surer."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
	}
	return TRUE;
}

void CPlayProperty_tab2::OnBnClickedCoverClear()
{
	ExportTagUi_ClearCover(m_coverPic, m_cover, m_coverPath, m_coverBmp);
}

void CPlayProperty_tab2::OnDropFiles(HDROP hDropInfo)
{
	ExportTagUi_OnDropFiles(hDropInfo, m_coverPic, m_cover, m_coverPath, m_coverBmp);
}

BOOL CPlayProperty_tab2::WriteTags()
{
	if (!m_pc) return TRUE;
	CString title, artist, album, year, track, genre, comment;
	m_title.GetWindowText(title);
	m_artist.GetWindowText(artist);
	m_album.GetWindowText(album);
	m_year.GetWindowText(year);
	m_track.GetWindowText(track);
	m_genre.GetWindowText(genre);
	m_comment.GetWindowText(comment);

	std::vector<playlistdata0*> targets;
	if (m_multi) {
		for (size_t i = 0; i < m_pcs.size(); ++i)
			targets.push_back(&m_pcs[i]);
	} else {
		targets.push_back(m_pc);
	}
	if (targets.empty()) return TRUE;

	BOOL needResume = FALSE;
	CString playPhys;
	if (og && (plf || playf) && !filen.IsEmpty()) {
		playPhys = PlPhysicalMediaPath(filen);
		if (playPhys.IsEmpty()) playPhys = filen;
		for (size_t i = 0; i < targets.size(); ++i) {
			CString p = PlPhysicalMediaPath(targets[i]->fol);
			if (p.IsEmpty()) p = targets[i]->fol;
			if (!p.IsEmpty() && _tcsicmp(p, playPhys) == 0) { needResume = TRUE; break; }
		}
	}
	int savecheckBak = savedata.savecheck;
	if (needResume) {
		OggArmSilentResumeFromCurrent();
		savedata.savecheck = 0;
		og->stop1();
		savedata.savecheck = savecheckBak;
	}

	int okN = 0, failN = 0;
	for (size_t i = 0; i < targets.size(); ++i) {
		CString path = PlPhysicalMediaPath(targets[i]->fol);
		if (path.IsEmpty()) path = targets[i]->fol;
		if (path.IsEmpty()) { failN++; continue; }
		CString e = path; e.MakeLower();
		if (PpExtIs(e, _T(".dff")) || PpExtIs(e, _T(".wsd"))) { failN++; continue; }

		FileTagFields fields;
		if (m_multi) {
			ReadFileTagFields(path, fields);
			if (!artist.IsEmpty()) fields.artist = artist;
			if (!album.IsEmpty()) fields.album = album;
			if (!year.IsEmpty()) fields.year = year;
			if (!track.IsEmpty()) fields.track = track;
			if (!genre.IsEmpty()) fields.genre = genre;
			if (!comment.IsEmpty()) fields.comment = comment;
		} else {
			fields.title = title; fields.artist = artist; fields.album = album;
			fields.year = year; fields.track = track; fields.genre = genre; fields.comment = comment;
		}
		bool textOk = true;
		if (m_multi) {
			const bool anyInput = !artist.IsEmpty() || !album.IsEmpty() || !year.IsEmpty()
				|| !track.IsEmpty() || !genre.IsEmpty() || !comment.IsEmpty();
			if (anyInput) textOk = WriteFileTagFields(path, fields);
		} else {
			textOk = WriteFileTagFields(path, fields);
		}
		bool coverOk = true;
		if (!m_coverPath.IsEmpty())
			coverOk = PpApplyCover(path, m_coverPath) ? true : false;
		if (textOk && coverOk) {
			okN++;
			PpForgetJacket(targets[i]->fol);
			playlistdata0* t = targets[i];
			if (!m_multi && !title.IsEmpty()) { _tcsncpy(t->name, title, _countof(t->name) - 1); t->name[_countof(t->name) - 1] = 0; }
			if (!artist.IsEmpty()) { _tcsncpy(t->art, artist, _countof(t->art) - 1); t->art[_countof(t->art) - 1] = 0; }
			if (!album.IsEmpty()) { _tcsncpy(t->alb, album, _countof(t->alb) - 1); t->alb[_countof(t->alb) - 1] = 0; }
		} else {
			failN++;
		}
	}
	if (needResume && og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());
	CString msg;
	if (failN == 0 && okN > 0)
		msg = LL14(L"保存しました", L"Saved", L"Enregistre", L"Salvato", L"Guardado", L"저장됨", L"已保存", L"Saved",
			L"Сохранено", L"Gespeichert", L"Salvo", L"Opgeslagen", L"Zapisano", L"Kaydedildi");
	else if (okN > 0)
		msg.Format(LL14(L"保存 %d / 失敗 %d", L"Saved %d / failed %d", L"Enregistré %d / échec %d", L"Salvati %d / errori %d",
			L"Guardados %d / fallos %d", L"저장 %d / 실패 %d", L"已保存 %d / 失败 %d", L"Saved %d / failed %d",
			L"Сохранено %d / ошибок %d", L"Gespeichert %d / Fehler %d", L"Salvos %d / falhas %d",
			L"Opgeslagen %d / mislukt %d", L"Zapisano %d / błędów %d", L"Kaydedildi %d / hata %d"), okN, failN);
	else
		msg = LL14(L"タグの書き込みに失敗しました。", L"Failed to write tags.", L"Echec ecriture tags.", L"Scrittura tag non riuscita.",
			L"Error al escribir etiquetas.", L"태그 쓰기 실패.", L"写入标签失败。", L"فشل كتابة الوسوم.",
			L"Не удалось записать теги.", L"Tag schreiben fehlgeschlagen.", L"Falha ao gravar tags.",
			L"Tags schrijven mislukt.", L"Nie udało się zapisać tagów.", L"Etiket yazılamadı.");
	m_status.SetWindowText(msg);
	return failN == 0;
}

void CPlayProperty_tab2::OnBnClickedSave()
{
	WriteTags();
}

IMPLEMENT_DYNAMIC(CPlayPropCfgList, CCustomListCtrl)

BOOL CPlayPropCfgList::WantLiveEditRow(int row)
{
	CPlayProperty_tab3* d = (CPlayProperty_tab3*)GetParent();
	if (!d || row < 0 || row >= d->m_cfgN) return FALSE;
	if (d->m_cfgLock[row]) return FALSE;
	const int t = d->m_cfgType[row];
	if (t == KPI_CFG_TYPE_BOOL || t == KPI_CFG_TYPE_BIN || t == KPI_ROW_WINUI) return FALSE;
	return TRUE;
}

void CPlayPropCfgList::OnLiveEditText(int row, LPCTSTR text)
{
	CPlayProperty_tab3* d = (CPlayProperty_tab3*)GetParent();
	if (!d || row < 0 || row >= d->m_cfgN || !text) return;
	if (wcscmp(d->m_cfgVal[row], text) == 0) return;
	wcsncpy_s(d->m_cfgVal[row], text, _TRUNCATE);
	d->SaveCfgRow(row);
}

IMPLEMENT_DYNAMIC(CPlayProperty_tab3, CCustomDialog)

CPlayProperty_tab3::CPlayProperty_tab3(CWnd* pParent)
	: CCustomDialog(CPlayProperty_tab3::IDD, pParent)
	, m_kpiIdx(-1), m_bFillingDetail(FALSE), m_pluginLoaded(FALSE), m_inN(0), m_cfgN(0)
{
	ZeroMemory(m_cfgType, sizeof(m_cfgType));
	ZeroMemory(m_cfgLock, sizeof(m_cfgLock));
}

void CPlayProperty_tab3::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PP_INFO, m_info);
	DDX_Control(pDX, IDC_PP_CFG, m_cfg);
	DDX_Control(pDX, IDC_PP_CFGHELP, m_cfgHelp);
	DDX_Control(pDX, IDC_PP_KPI_NAME, m_kpiName);
	DDX_Control(pDX, IDC_PP_INFO_L, m_infoLbl);
	DDX_Control(pDX, IDC_PP_CFG_L, m_cfgLbl);
}

BEGIN_MESSAGE_MAP(CPlayProperty_tab3, CCustomDialog)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_PP_CFG, OnLvnItemchangedCfg)
	ON_NOTIFY(NM_DBLCLK, IDC_PP_CFG, OnCfgDblClk)
END_MESSAGE_MAP()

void CPlayProperty_tab3::OnOK() {}
void CPlayProperty_tab3::OnCancel() {}

BOOL CPlayProperty_tab3::OnInitDialog()
{
	CCustomDialog::OnInitDialog();
	EnableAero(FALSE);
	m_info.SetAeroMode(FALSE);
	m_cfg.SetAeroMode(FALSE);
	m_cfgHelp.SetAeroMode(FALSE);
	PpStyleStatic(m_kpiName);
	PpStyleStatic(m_infoLbl);
	PpStyleStatic(m_cfgLbl);
	m_info.SetExtendedStyle(m_info.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_INFOTIP);
	m_info.ModifyStyle(0, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
	m_info.InsertColumn(0, LL14(L"項目", L"Item", L"Element", L"Voce", L"Elemento", L"항목", L"项目", L"عنصر", L"Поле", L"Feld", L"Item", L"Veld", L"Pole", L"Alan"), LVCFMT_LEFT, 120, 0);
	m_info.InsertColumn(1, LL14(L"値", L"Value", L"Valeur", L"Valore", L"Valor", L"값", L"值", L"القيمة", L"Значение", L"Wert", L"Valor", L"Waarde", L"Wartość", L"Değer"), LVCFMT_LEFT, 300, 0);
	m_cfg.SetExtendedStyle(m_cfg.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);
	m_cfg.ModifyStyle(0, LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
	m_cfg.InsertColumn(0, LL14(L"設定", L"Setting", L"Reglage", L"Impostazione", L"Ajuste", L"설정", L"设置", L"إعداد", L"Параметр", L"Einstellung", L"Ajuste", L"Instelling", L"Ustawienie", L"Ayar"), LVCFMT_LEFT, 160, 0);
	m_cfg.InsertColumn(1, LL14(L"値", L"Value", L"Valeur", L"Valore", L"Valor", L"값", L"值", L"القيمة", L"Значение", L"Wert", L"Valor", L"Waarde", L"Wartość", L"Değer"), LVCFMT_LEFT, 260, 0);
	m_cfg.SetLiveEditColumn(1);
	SetDlgItemText(IDC_PP_INFO_L, LL14(L"情報", L"Info", L"Infos", L"Info", L"Info", L"정보", L"信息", L"معلومات", L"Сведения", L"Info", L"Info", L"Info", L"Info", L"Bilgi"));
	SetDlgItemText(IDC_PP_CFG_L, LL14(L"設定", L"Settings", L"Reglages", L"Impostazioni", L"Ajustes", L"설정", L"设置", L"إعدادات", L"Настройки", L"Einstellungen", L"Ajustes", L"Instellingen", L"Ustawienia", L"Ayarlar"));
	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		PpTip(m_tooltip, m_kpiName, LL14(L"この拡張子を受け持つプラグインのファイル名です。",
			L"File name of the plugin that handles this extension.",
			L"Nom du plugin qui prend cette extension.",
			L"Nome del plugin per questa estensione.",
			L"Nombre del complemento de esta extension.",
			L"이 확장자를 맡는 플러그인 파일 이름입니다.",
			L"负责此扩展名的插件文件名。",
			L"اسم ملف الإضافة لهذه اللاحقة.",
			L"Имя файла плагина для этого расширения.",
			L"Dateiname des Plugins fuer diese Endung.",
			L"Nome do arquivo do plugin desta extensao.",
			L"Bestandsnaam van de plug-in voor deze extensie.",
			L"Nazwa pliku wtyczki dla tego rozszerzenia.",
			L"Bu uzantiyi ustlenen eklentinin dosya adi."));
		PpTip(m_tooltip, m_infoLbl, LL14(L"プラグインが返す識別情報です。ここは変更できません。",
			L"Identity the plugin reports. This list is read-only.",
			L"Identite renvoyee par le plugin. Liste en lecture seule.",
			L"Identita restituita dal plugin. Solo lettura.",
			L"Identidad que devuelve el complemento. Solo lectura.",
			L"플러그인이 돌려주는 식별 정보입니다. 여기서는 바꿀 수 없습니다.",
			L"插件返回的标识。此处不能改。",
			L"هوية الإضافة. القائمة للقراءة فقط.",
			L"Сведения, которые сообщает плагин. Только чтение.",
			L"Kennung, die das Plugin meldet. Nur lesen.",
			L"Identidade que o plugin informa. Somente leitura.",
			L"Identiteit die de plug-in meldt. Alleen lezen.",
			L"Tozsamosc, ktora podaje wtyczka. Tylko do odczytu.",
			L"Eklentinin bildirdigi kimlik. Buradan degistirilemez."));
		PpTip(m_tooltip, m_info, LL14(L"プラグイン名や対応形式などの読み取り専用の一覧です。",
			L"Read-only list of the plugin name, formats, and similar details.",
			L"Liste en lecture seule : nom, formats, etc.",
			L"Elenco in sola lettura: nome, formati, ecc.",
			L"Lista de solo lectura: nombre, formatos, etc.",
			L"플러그인 이름과 지원 형식 등의 읽기 전용 목록입니다.",
			L"插件名、支持格式等的只读列表。",
			L"قائمة للقراءة فقط: الاسم والصيغ وغيرها.",
			L"Список только для чтения: имя, форматы и прочее.",
			L"Nur-Lese-Liste: Name, Formate und mehr.",
			L"Lista somente leitura: nome, formatos e mais.",
			L"Alleen-lezenlijst: naam, formaten en meer.",
			L"Lista tylko do odczytu: nazwa, formaty i inne.",
			L"Eklenti adi ve desteklenen bicimler. Salt okunur."));
		PpTip(m_tooltip, m_cfgLbl, LL14(L"このプラグインの設定です。チェック、値の編集、行のダブルクリックが使えます。",
			L"Settings for this plugin. Use the checkbox, edit the value, or double-click a row.",
			L"Reglages du plugin. Case, edition, ou double-clic.",
			L"Impostazioni del plugin. Casella, modifica o doppio clic.",
			L"Ajustes del complemento. Casilla, edicion o doble clic.",
			L"이 플러그인의 설정입니다. 체크, 값 편집, 행 더블 클릭을 씁니다.",
			L"此插件的设置。可勾选、编辑数值，或双击一行。",
			L"إعدادات الإضافة. مربع أو تحرير أو نقر مزدوج.",
			L"Настройки плагина. Флажок, правка или двойной щелчок.",
			L"Einstellungen des Plugins. Kontrollkaestchen, Wert oder Doppelklick.",
			L"Ajustes do plugin. Caixa, edicao ou clique duplo.",
			L"Instellingen van de plug-in. Vakje, waarde of dubbelklik.",
			L"Ustawienia wtyczki. Pole, edycja albo podwojne klikniecie.",
			L"Bu eklentinin ayarlari. Kutu, deger veya cift tik."));
		PpTip(m_tooltip, m_cfg, LL14(L"左のチェックはオン/オフです。値の列は直接編集できます。プラグイン画面の行をダブルクリックすると、その画面を開きます。",
			L"The left checkbox is on/off. Edit the value column directly. Double-click the plugin-window row to open that window.",
			L"La case est on/off. La colonne valeur se modifie. Double-clic sur la ligne fenetre ouvre le plugin.",
			L"La casella e on/off. La colonna valore si modifica. Doppio clic sulla riga finestra apre il plugin.",
			L"La casilla es on/off. La columna valor se edita. Doble clic en la fila de ventana abre el complemento.",
			L"왼쪽 체크는 켜기/끄기입니다. 값 열은 직접 고칩니다. 플러그인 창 행을 더블 클릭하면 그 창이 열립니다.",
			L"左侧勾选是开关。值列可直接改。双击插件窗口那一行会打开该窗口。",
			L"المربع تشغيل/إيقاف. عمود القيمة يُحرَّر. النقر المزدوج على صف النافذة يفتحها.",
			L"Флажок слева — вкл/выкл. Столбец значения правится. Двойной щелчок по строке окна открывает его.",
			L"Links an/aus. Die Wertspalte ist editierbar. Doppelklick auf die Fensterzeile oeffnet es.",
			L"A caixa e liga/desliga. A coluna valor se edita. Clique duplo na linha da janela abre o plugin.",
			L"Het vakje is aan/uit. De waardekolom is te bewerken. Dubbelklik op de vensterrij opent het.",
			L"Pole po lewej to wl/wyl. Kolumne wartosci mozna edytowac. Podwojne klikniecie wiersza okna otwiera je.",
			L"Soldaki kutu ac/kapa. Deger sutunu dogrudan duzenlenir. Eklenti penceresi satirina cift tik o pencereyi acar."));
		PpTip(m_tooltip, m_cfgHelp, LL14(L"選んだ設定行の説明です。",
			L"Description of the selected setting.",
			L"Description du reglage choisi.",
			L"Descrizione dell'impostazione scelta.",
			L"Descripcion del ajuste elegido.",
			L"고른 설정 행의 설명입니다.",
			L"所选设置行的说明。",
			L"وصف الإعداد المحدد.",
			L"Описание выбранной настройки.",
			L"Beschreibung der gewaehlten Einstellung.",
			L"Descricao do ajuste escolhido.",
			L"Uitleg van de gekozen instelling.",
			L"Opis wybranego ustawienia.",
			L"Secilen ayar satirinin aciklamasi."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
	}
	return TRUE;
}

BOOL CPlayProperty_tab3::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomDialog::PreTranslateMessage(pMsg);
}

void CPlayProperty_tab3::LoadPlugin(int idx)
{
	m_kpiIdx = idx;
	m_inN = 0;
	m_cfgN = 0;
	if (m_info.GetSafeHwnd()) { m_info.SetRedraw(FALSE); m_info.DeleteAllItems(); }
	if (m_cfg.GetSafeHwnd()) {
		m_bFillingDetail = TRUE;
		m_cfg.SetLiveEditHold(TRUE);
		m_cfg.SetRedraw(FALSE);
		m_cfg.DeleteAllItems();
	}
	if (m_cfgHelp.GetSafeHwnd()) m_cfgHelp.SetWindowText(L"");
	if (idx < 0 || idx >= kpicnt || idx >= 400) {
		m_bFillingDetail = FALSE;
		if (m_cfg.GetSafeHwnd()) m_cfg.SetLiveEditHold(FALSE);
		if (m_info.GetSafeHwnd()) { m_info.SetRedraw(TRUE); m_info.Invalidate(FALSE); }
		if (m_cfg.GetSafeHwnd()) { m_cfg.SetRedraw(TRUE); m_cfg.Invalidate(FALSE); }
		return;
	}

	CString name = kpif[idx];
	const int slash = (std::max)(name.ReverseFind(_T('\\')), name.ReverseFind(_T('/')));
	if (slash >= 0) name = name.Mid(slash + 1);
	m_kpiName.SetWindowText(name);

	CString blob;
	int winUi = 0;
	KpiInspectPluginBlob(idx, blob, winUi);

	int pos = 0;
	while (pos < blob.GetLength()) {
		int nl = blob.Find(L'\n', pos);
		CString line = (nl < 0) ? blob.Mid(pos) : blob.Mid(pos, nl - pos);
		pos = (nl < 0) ? blob.GetLength() : nl + 1;
		if (line.IsEmpty()) continue;
		CString f[8];
		int nf = 0;
		int a = 0;
		while (nf < 8) {
			int tab = line.Find(L'\t', a);
			CString part = (tab < 0) ? line.Mid(a) : line.Mid(a, tab - a);
			part.Replace((wchar_t)1, L'\n');
			f[nf++] = part;
			if (tab < 0) break;
			a = tab + 1;
		}
		if (f[0] == L"I" && nf >= 3 && m_inN < 32) {
			PpKpiPut(m_inName[m_inN], 48, f[1]);
			PpKpiPut(m_inVal[m_inN], 512, f[2]);
			m_inN++;
		} else if (f[0] == L"W")
			winUi = 1;
		else if (f[0] == L"K" && nf >= 8 && m_cfgN < 96) {
			int row = m_cfgN++;
			m_cfgType[row] = _wtoi(f[1]);
			m_cfgLock[row] = 0;
			PpKpiPut(m_cfgSec[row], 96, f[2]);
			PpKpiPut(m_cfgKey[row], 96, f[3]);
			PpKpiPut(m_cfgDesc[row], 192, f[4]);
			PpKpiPut(m_cfgDef[row], 256, f[5]);
			PpKpiPut(m_cfgHelpTxt[row], 768, f[6]);
			PpKpiPut(m_cfgList[row], 512, f[7]);
			m_cfgVal[row][0] = 0;
		}
	}

	std::wstring plug = KpiV5PluginNameFromPath((LPCWSTR)kpif[idx]);
	const BOOL sasami = (_wcsicmp(plug.c_str(), L"kbsasami") == 0) ? TRUE : FALSE;
	if (plugkind[idx] == PLUGKIND_KPI) {
		const std::vector<KpiV5ConfigEntry>& known = GetKpiV5KnownEntries();
		for (size_t i = 0; i < known.size() && m_cfgN < 96; ++i) {
			if (_wcsicmp(known[i].plugin, plug.c_str()) != 0) continue;
			int found = -1;
			for (int r = 0; r < m_cfgN; ++r) {
				if (_wcsicmp(m_cfgSec[r], known[i].section) == 0 && _wcsicmp(m_cfgKey[r], known[i].key) == 0) {
					found = r; break;
				}
			}
			if (found >= 0) continue;
			int row = m_cfgN++;
			m_cfgType[row] = KPI_CFG_TYPE_STR;
			m_cfgLock[row] = 0;
			wcsncpy_s(m_cfgSec[row], known[i].section, _TRUNCATE);
			wcsncpy_s(m_cfgKey[row], known[i].key, _TRUNCATE);
			wcsncpy_s(m_cfgDesc[row], known[i].key, _TRUNCATE);
			wcsncpy_s(m_cfgDef[row], known[i].defaultValue ? known[i].defaultValue : L"", _TRUNCATE);
			m_cfgHelpTxt[row][0] = 0;
			m_cfgList[row][0] = 0;
			m_cfgVal[row][0] = 0;
		}
	}
	for (int r = 0; r < m_cfgN; ++r) {
		if (sasami && _wcsicmp(m_cfgKey[r], L"raira") == 0) {
			m_cfgType[r] = KPI_CFG_TYPE_BOOL;
			m_cfgLock[r] = 1;
			wcscpy_s(m_cfgVal[r], L"1");
			KpiV5SetInt(plug, m_cfgSec[r], m_cfgKey[r], 1);
			continue;
		}
		std::wstring cur = KpiV5GetStr(plug, m_cfgSec[r], m_cfgKey[r], m_cfgDef[r]);
		wcsncpy_s(m_cfgVal[r], cur.c_str(), _TRUNCATE);
	}
	if (winUi && m_cfgN < 96) {
		int row = m_cfgN++;
		m_cfgType[row] = KPI_ROW_WINUI;
		m_cfgLock[row] = 0;
		m_cfgSec[row][0] = 0;
		m_cfgKey[row][0] = 0;
		wcsncpy_s(m_cfgDesc[row], LL14(L"プラグインの設定画面", L"Plugin setup", L"Reglages du plugin", L"Impostazioni plugin",
			L"Ajustes del plugin", L"플러그인 설정 화면", L"插件设置窗口", L"نافذة إعداد الإضافة",
			L"Окно настроек плагина", L"Plugin-Einstellungen", L"Configuracao do plugin", L"Plugin-instellingen",
			L"Ustawienia wtyczki", L"Eklenti ayarlari"), _TRUNCATE);
		m_cfgDef[row][0] = 0;
		wcsncpy_s(m_cfgHelpTxt[row], LL14(L"ダブルクリックでプラグイン自身の設定画面を開きます",
			L"Double-click to open the plugin's own setup",
			L"Double-clic pour ouvrir le reglage du plugin",
			L"Doppio clic per aprire le impostazioni del plugin",
			L"Doble clic para abrir los ajustes del plugin",
			L"더블클릭하면 플러그인 설정 화면을 엽니다",
			L"双击打开插件自己的设置窗口",
			L"انقر نقراً مزدوجاً لفتح إعداد الإضافة",
			L"Двойной щелчок откроет окно настроек плагина",
			L"Doppelklick oeffnet die eigenen Einstellungen",
			L"Duplo clique abre a configuracao do plugin",
			L"Dubbelklik opent de eigen instellingen",
			L"Podwojny klik otwiera ustawienia wtyczki",
			L"Cift tik eklentinin kendi ayarini acar"), _TRUNCATE);
		m_cfgList[row][0] = 0;
		wcsncpy_s(m_cfgVal[row], LL14(L"開く", L"Open", L"Ouvrir", L"Apri", L"Abrir", L"열기", L"打开", L"فتح", L"Открыть", L"Oeffnen", L"Abrir", L"Openen", L"Otworz", L"Ac"), _TRUNCATE);
	}

	BOOL hasPath = FALSE, hasType = FALSE, hasVer = FALSE, hasExt = FALSE, hasPlat = FALSE;
	for (int i = 0; i < m_inN; ++i) {
		if (_wcsicmp(m_inName[i], L"Path") == 0) hasPath = TRUE;
		else if (_wcsicmp(m_inName[i], L"Type") == 0) hasType = TRUE;
		else if (_wcsicmp(m_inName[i], L"Version") == 0) hasVer = TRUE;
		else if (_wcsicmp(m_inName[i], L"SupportExts") == 0) hasExt = TRUE;
		else if (_wcsicmp(m_inName[i], L"Platform") == 0) hasPlat = TRUE;
	}
	if (!hasPath && m_inN < 32) { PpKpiPut(m_inName[m_inN], 48, CString(L"Path")); PpKpiPut(m_inVal[m_inN], 512, kpif[idx]); m_inN++; }
	if (!hasType && m_inN < 32) {
		CString kind = L"KPI";
		if (plugkind[idx] == PLUGKIND_WINAMP) kind = L"Winamp";
		else if (plugkind[idx] == PLUGKIND_XMPLAY) kind = L"XMPlay";
		else if (plugkind[idx] == PLUGKIND_AIMP) kind = L"AIMP";
		PpKpiPut(m_inName[m_inN], 48, CString(L"Type")); PpKpiPut(m_inVal[m_inN], 512, kind); m_inN++;
	}
	if (!hasVer && m_inN < 32) {
		CString ver; ver.Format(L"%u", (unsigned)kvar[idx][0]);
		PpKpiPut(m_inName[m_inN], 48, CString(L"Version")); PpKpiPut(m_inVal[m_inN], 512, ver); m_inN++;
	}
	if (!hasExt && m_inN < 32) {
		CString exts;
		for (int i = 0; i < 299 && !ext[idx][i].IsEmpty(); ++i) {
			if (!exts.IsEmpty()) exts += L'/';
			exts += ext[idx][i];
		}
		if (!exts.IsEmpty()) { PpKpiPut(m_inName[m_inN], 48, CString(L"SupportExts")); PpKpiPut(m_inVal[m_inN], 512, exts); m_inN++; }
	}
	if (!hasPlat && m_inN < 32) {
		CString plat = L"?";
		if (kpiarch[idx] == 64) plat = L"x64(64bit)";
		else if (kpiarch[idx] == 32) plat = L"x86(32bit)";
		PpKpiPut(m_inName[m_inN], 48, CString(L"Platform")); PpKpiPut(m_inVal[m_inN], 512, plat); m_inN++;
	}

	if (m_info.GetSafeHwnd()) {
		for (int i = 0; i < m_inN; ++i) {
			int id = m_info.InsertItem(i, m_inName[i]);
			m_info.SetItemText(id, 1, m_inVal[i]);
		}
		m_info.SetRedraw(TRUE);
		m_info.Invalidate(FALSE);
	}
	if (m_cfg.GetSafeHwnd()) {
		for (int r = 0; r < m_cfgN; ++r) {
			int id = m_cfg.InsertItem(r, m_cfgDesc[r][0] ? m_cfgDesc[r] : m_cfgKey[r]);
			CString shown = (m_cfgType[r] == KPI_CFG_TYPE_BOOL)
				? (PpKpiBoolOn(m_cfgVal[r]) ? L"true" : L"false") : CString(m_cfgVal[r]);
			m_cfg.SetItemText(id, 1, shown);
			if (m_cfgType[r] == KPI_CFG_TYPE_BOOL)
				m_cfg.SetCheck(id, PpKpiBoolOn(m_cfgVal[r]) ? TRUE : FALSE);
			else
				m_cfg.SetItemState(id, 0, LVIS_STATEIMAGEMASK);
		}
		m_bFillingDetail = FALSE;
		if (m_cfgN > 0) {
			m_cfg.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			m_cfgHelp.SetWindowText(m_cfgHelpTxt[0]);
		}
		m_cfg.SetRedraw(TRUE);
		m_cfg.Invalidate(FALSE);
		m_cfg.SetLiveEditHold(FALSE);
	} else {
		m_bFillingDetail = FALSE;
	}
}

void CPlayProperty_tab3::SaveCfgRow(int row)
{
	if (row < 0 || row >= m_cfgN) return;
	if (m_cfgType[row] == KPI_ROW_WINUI) return;
	if (m_kpiIdx < 0 || m_kpiIdx >= kpicnt) return;
	std::wstring plug = KpiV5PluginNameFromPath((LPCWSTR)kpif[m_kpiIdx]);
	const BOOL sasami = (_wcsicmp(plug.c_str(), L"kbsasami") == 0) ? TRUE : FALSE;
	if (m_cfgLock[row]) {
		wcscpy_s(m_cfgVal[row], L"1");
		KpiV5SetInt(plug, m_cfgSec[row], m_cfgKey[row], 1);
		return;
	}
	if (m_cfgType[row] == KPI_CFG_TYPE_BOOL) {
		if (PpKpiBoolOn(m_cfgVal[row])) wcscpy_s(m_cfgVal[row], L"1");
		else wcscpy_s(m_cfgVal[row], L"0");
	}
	KpiV5SetStr(plug, m_cfgSec[row], m_cfgKey[row], m_cfgVal[row]);
	if (sasami && _wcsicmp(m_cfgKey[row], L"vst") == 0) {
		const BOOL on = PpKpiBoolOn(m_cfgVal[row]);
		if (on) savedata.midPlayPrefer = 0;
		else if (savedata.midPlayPrefer == 0) savedata.midPlayPrefer = 1;
		MpPersistSavedataQuick();
		PlRefreshMidiPlayModes();
		CEmuRequestMidiEngineReplay();
	}
}

void CPlayProperty_tab3::OnLvnItemchangedCfg(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLISTVIEW p = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	*pResult = 0;
	if (m_bFillingDetail || !p) return;
	const int row = p->iItem;
	if (row < 0 || row >= m_cfgN) return;
	if ((p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED) && m_cfgHelp.GetSafeHwnd())
		m_cfgHelp.SetWindowText(m_cfgHelpTxt[row]);
	if ((p->uChanged & LVIF_STATE) == 0) return;
	if ((p->uOldState & LVIS_STATEIMAGEMASK) == (p->uNewState & LVIS_STATEIMAGEMASK)) return;
	if (m_cfgType[row] != KPI_CFG_TYPE_BOOL) return;
	if (m_cfgLock[row]) {
		m_bFillingDetail = TRUE;
		m_cfg.SetCheck(row, TRUE);
		m_cfg.SetItemText(row, 1, L"true");
		m_bFillingDetail = FALSE;
		return;
	}
	wcscpy_s(m_cfgVal[row], m_cfg.GetCheck(row) ? L"1" : L"0");
	SaveCfgRow(row);
	m_cfg.SetItemText(row, 1, PpKpiBoolOn(m_cfgVal[row]) ? L"true" : L"false");
}

void CPlayProperty_tab3::OnCfgDblClk(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE p = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	*pResult = 0;
	if (!p || p->iItem < 0 || p->iItem >= m_cfgN) return;
	const int row = p->iItem;
	if (m_cfgLock[row] || m_cfgType[row] == KPI_CFG_TYPE_BOOL) return;
	if (m_cfgType[row] == KPI_ROW_WINUI) {
		if (m_kpiIdx < 0 || m_kpiIdx >= kpicnt) return;
#ifdef _WIN64
		const BOOL remote = (kpiarch[m_kpiIdx] == 32) ? TRUE : FALSE;
#else
		const BOOL remote = (kpiarch[m_kpiIdx] == 64) ? TRUE : FALSE;
#endif
		KpiOpenWinampUi(kpif[m_kpiIdx], m_hWnd, remote);
		return;
	}
	if (m_cfgType[row] == KPI_CFG_TYPE_FILE) {
		CFileDialog dlg(TRUE, NULL, m_cfgVal[row], OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, NULL, this);
		if (dlg.DoModal() != IDOK) return;
		wcsncpy_s(m_cfgVal[row], dlg.GetPathName(), _TRUNCATE);
		SaveCfgRow(row);
		m_cfg.SetItemText(row, 1, m_cfgVal[row]);
		return;
	}
	if (m_cfgType[row] == KPI_CFG_TYPE_FOLDER) {
		BROWSEINFO bi = {};
		bi.hwndOwner = m_hWnd;
		bi.lpszTitle = m_cfgDesc[row];
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
		LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
		if (!pidl) return;
		wchar_t path[MAX_PATH] = {};
		BOOL got = SHGetPathFromIDList(pidl, path);
		CoTaskMemFree(pidl);
		if (!got || !path[0]) return;
		wcsncpy_s(m_cfgVal[row], path, _TRUNCATE);
		size_t n = wcslen(m_cfgVal[row]);
		if (n > 0 && n + 1 < 512 && m_cfgVal[row][n - 1] != L'\\') {
			m_cfgVal[row][n] = L'\\'; m_cfgVal[row][n + 1] = 0;
		}
		SaveCfgRow(row);
		m_cfg.SetItemText(row, 1, m_cfgVal[row]);
		return;
	}
	if (m_cfgList[row][0]) {
		CCustomPopupMenu menu;
		menu.SetSkipChrome(TRUE);
		int n = 0, a = 0;
		const CString all(m_cfgList[row]);
		while (n < 24) {
			int cut = all.Find((wchar_t)2, a);
			CString part = (cut < 0) ? all.Mid(a) : all.Mid(a, cut - a);
			if (!part.IsEmpty()) menu.AddCommand((UINT)(n + 1), part, NULL, TRUE);
			n++;
			if (cut < 0) break;
			a = cut + 1;
		}
		CPoint pt; GetCursorPos(&pt);
		UINT cmd = menu.Track(pt, this);
		if (cmd >= 1 && cmd <= 24) {
			int pick = (int)cmd - 1, b = 0;
			CString chosen;
			for (int i = 0; i <= pick; ++i) {
				int cut = all.Find((wchar_t)2, b);
				chosen = (cut < 0) ? all.Mid(b) : all.Mid(b, cut - b);
				if (cut < 0) break;
				b = cut + 1;
			}
			wcsncpy_s(m_cfgVal[row], chosen, _TRUNCATE);
			SaveCfgRow(row);
			m_cfg.SetItemText(row, 1, m_cfgVal[row]);
		}
	}
}

IMPLEMENT_DYNAMIC(CPlayProperty_tab4, CCustomDialog)

CPlayProperty_tab4::CPlayProperty_tab4(CWnd* pParent)
	: CCustomDialog(CPlayProperty_tab4::IDD, pParent), m_pl(NULL)
{
}

void CPlayProperty_tab4::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PP_MORE_HINT, m_hint);
	DDX_Control(pDX, IDC_PP_MORE_EXPLORER, m_btnExplorer);
	DDX_Control(pDX, IDC_PP_MORE_OPENFOLDER, m_btnOpenFolder);
	DDX_Control(pDX, IDC_PP_MORE_COPYPATH, m_btnCopyPath);
	DDX_Control(pDX, IDC_PP_MORE_COPYNAME, m_btnCopyName);
	DDX_Control(pDX, IDC_PP_MORE_PROTOOLS, m_btnProTools);
	DDX_Control(pDX, IDC_PP_MORE_CLEARPARAM, m_btnClearParam);
	DDX_Control(pDX, IDC_PP_MORE_WAV, m_btnWav);
	DDX_Control(pDX, IDC_PP_MORE_TRANSCODE, m_btnTranscode);
	DDX_Control(pDX, IDC_PP_MORE_VIDEO, m_btnVideo);
	DDX_Control(pDX, IDC_PP_MORE_BPM, m_btnBpm);
	DDX_Control(pDX, IDC_PP_MORE_TAGBATCH, m_btnTagBatch);
	DDX_Control(pDX, IDC_PP_MORE_QUEUE, m_btnQueue);
	DDX_Control(pDX, IDC_PP_MORE_PLAYNEXT, m_btnPlayNext);
	DDX_Control(pDX, IDC_PP_MORE_ADDSAME, m_btnAddSame);
	DDX_Control(pDX, IDC_PP_MORE_MIDEXPORT, m_btnMidExport);
}

BEGIN_MESSAGE_MAP(CPlayProperty_tab4, CCustomDialog)
	ON_CONTROL_RANGE(BN_CLICKED, IDC_PP_MORE_EXPLORER, IDC_PP_MORE_OPENFOLDER, OnMoreCmd)
END_MESSAGE_MAP()

void CPlayProperty_tab4::OnOK() {}
void CPlayProperty_tab4::OnCancel() {}

BOOL CPlayProperty_tab4::OnInitDialog()
{
	CCustomDialog::OnInitDialog();
	EnableAero(FALSE);
	PpStyleStatic(m_hint);
	CCustomStandardButton* btns[] = {
		&m_btnExplorer, &m_btnOpenFolder, &m_btnCopyPath, &m_btnCopyName, &m_btnProTools, &m_btnClearParam,
		&m_btnWav, &m_btnTranscode, &m_btnVideo, &m_btnBpm, &m_btnTagBatch, &m_btnQueue, &m_btnPlayNext,
		&m_btnAddSame, &m_btnMidExport
	};
	for (int i = 0; i < (int)(sizeof(btns) / sizeof(btns[0])); ++i) PpStyleBtn(*btns[i]);
	m_hint.SetWindowText(LL14(
		L"選択曲に対する操作です。押した時点で実行されます（OK 待ちではありません）。",
		L"Actions for the selected track. They run immediately (not waiting for OK).",
		L"Actions sur la piste. Elles s'exécutent tout de suite.",
		L"Azioni sulla traccia. Si eseguono subito.",
		L"Acciones de la pista. Se ejecutan al instante.",
		L"선택 곡에 대한 조작입니다. 누르는 즉시 실행됩니다.",
		L"针对所选曲目的操作。按下即执行。",
		L"إجراءات المسار. تُنفَّذ فوراً.",
		L"Действия с треком. Выполняются сразу.",
		L"Aktionen für den Titel. Sofort ausgeführt.",
		L"Ações da faixa. Executam na hora.",
		L"Acties voor het nummer. Direct uitgevoerd.",
		L"Akcje utworu. Wykonują się od razu.",
		L"Seçili parça işlemleri. Hemen çalışır."));
	SetDlgItemText(IDC_PP_MORE_OPENFOLDER, LL14(L"フォルダを開く", L"Open folder", L"Ouvrir le dossier", L"Apri cartella", L"Abrir carpeta", L"폴더 열기", L"打开文件夹", L"فتح المجلد", L"Открыть папку", L"Ordner öffnen", L"Abrir pasta", L"Map openen", L"Otwórz folder", L"Klasörü aç"));
	SetDlgItemText(IDC_PP_MORE_WAV, LL14(L"WAV書き出し", L"WAV export", L"Export WAV", L"Esporta WAV", L"Exportar WAV", L"WAV보내기", L"导出WAV", L"تصدير WAV", L"Экспорт WAV", L"WAV exportieren", L"Exportar WAV", L"WAV exporteren", L"Eksport WAV", L"WAV dışa aktar"));
	SetDlgItemText(IDC_PP_MORE_TRANSCODE, LL14(L"変換", L"Convert", L"Convertir", L"Converti", L"Convertir", L"변환", L"转换", L"تحويل", L"Преобразовать", L"Umwandeln", L"Converter", L"Converteren", L"Konwertuj", L"Dönüştür"));
	SetDlgItemText(IDC_PP_MORE_VIDEO, LL14(L"映像抽出", L"Extract video", L"Extraire vidéo", L"Estrai video", L"Extraer vídeo", L"영상 추출", L"提取影像", L"استخراج الفيديو", L"Извлечь видео", L"Video extrahieren", L"Extrair vídeo", L"Video extraheren", L"Wyodrębnij wideo", L"Videoyu çıkar"));
	SetDlgItemText(IDC_PP_MORE_TAGBATCH, LL14(L"まとめて編集", L"Batch edit", L"Edition groupée", L"Modifica multipla", L"Edición por lotes", L"일괄 편집", L"批量编辑", L"تحرير جماعي", L"Пакетное прав.", L"Stapelbearbeitung", L"Edição em lote", L"Batchbewerking", L"Edycja zbiorcza", L"Toplu düzenleme"));
	SetDlgItemText(IDC_PP_MORE_QUEUE, LL14(L"キューに追加", L"Add to queue", L"Ajouter à la file", L"Aggiungi in coda", L"Añadir a cola", L"큐에 추가", L"加入队列", L"إضافة للطابور", L"В очередь", L"Zur Warteschlange", L"Adicionar à fila", L"Aan wachtrij", L"Dodaj do kolejki", L"Kuyruğa ekle"));
	SetDlgItemText(IDC_PP_MORE_PLAYNEXT, LL14(L"次に再生", L"Play next", L"Lire ensuite", L"Riproduci dopo", L"Reproducir siguiente", L"다음에 재생", L"下一首播放", L"تشغيل التالي", L"Играть следующим", L"Als Nächstes", L"Tocar em seguida", L"Speel hierna", L"Odtwórz następny", L"Sırada çal"));
	SetDlgItemText(IDC_PP_MORE_ADDSAME, LL14(L"同じフォルダを追加", L"Add same folder", L"Ajouter le même dossier", L"Aggiungi stessa cartella", L"Añadir misma carpeta", L"같은 폴더 추가", L"添加相同文件夹", L"إضافة نفس المجلد", L"Добавить ту же папку", L"Gleichen Ordner hinzufügen", L"Adicionar mesma pasta", L"Zelfde map toevoegen", L"Dodaj ten sam folder", L"Aynı klasörü ekle"));
	SetDlgItemText(IDC_PP_MORE_MIDEXPORT, LL14(L"MIDI書き出し", L"MIDI export", L"Export MIDI", L"Esporta MIDI", L"Exportar MIDI", L"MIDI보내기", L"导出MIDI", L"تصدير MIDI", L"Экспорт MIDI", L"MIDI exportieren", L"Exportar MIDI", L"MIDI exporteren", L"Eksport MIDI", L"MIDI dışa aktar"));
	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		PpTip(m_tooltip, m_hint, LL14(L"押せないボタンは、今の選択では動きません。",
			L"A dimmed button does nothing for the current selection.",
			L"Un bouton grise ne fait rien pour la selection actuelle.",
			L"Un pulsante spento non fa nulla per la selezione attuale.",
			L"Un boton atenuado no hace nada con la seleccion actual.",
			L"흐린 단추는 지금 선택에서는 동작하지 않습니다.",
			L"变灰的按钮对当前选择不起作用。",
			L"الزر الباهت لا يعمل للتحديد الحالي.",
			L"Неактивная кнопка для текущего выбора ничего не делает.",
			L"Eine abgeblendete Schaltflaeche gilt nicht fuer die Auswahl.",
			L"Um botao esmaecido nao vale para a selecao atual.",
			L"Een gedimde knop doet niets voor de huidige selectie.",
			L"Wygaszony przycisk nic nie robi dla biezacego wyboru.",
			L"Soluk dugme bu secim icin calismaz."));
		PpTip(m_tooltip, m_btnExplorer, LL14(L"エクスプローラーでこのファイルを選択します。ファイルが無いときは押せません。",
			L"Select this file in Explorer. Disabled when the file is missing.",
			L"Selectionne le fichier dans l'Explorateur. Desactive s'il manque.",
			L"Seleziona il file in Esplora. Disattivo se manca.",
			L"Selecciona el archivo en el Explorador. Desactivado si falta.",
			L"탐색기에서 이 파일을 선택합니다. 파일이 없으면 누를 수 없습니다.",
			L"在资源管理器中选中此文件。文件不在时不可按。",
			L"يحدد الملف في المستكشف. غير متاح إن كان مفقوداً.",
			L"Выделяет файл в Проводнике. Недоступно, если файла нет.",
			L"Waehlt die Datei im Explorer. Gesperrt, wenn sie fehlt.",
			L"Seleciona o arquivo no Explorer. Desativado se faltar.",
			L"Selecteert het bestand in Verkenner. Uit als het ontbreekt.",
			L"Zaznacza plik w Eksploratorze. Wylaczone, gdy pliku brak.",
			L"Gezginde bu dosyayi secer. Dosya yoksa basilamaz."));
		PpTip(m_tooltip, m_btnOpenFolder, LL14(L"このファイルがあるフォルダを開きます。ファイルが無いときは押せません。",
			L"Open the folder that contains this file. Disabled when the file is missing.",
			L"Ouvre le dossier du fichier. Desactive s'il manque.",
			L"Apre la cartella del file. Disattivo se manca.",
			L"Abre la carpeta del archivo. Desactivado si falta.",
			L"이 파일이 있는 폴더를 엽니다. 파일이 없으면 누를 수 없습니다.",
			L"打开此文件所在的文件夹。文件不在时不可按。",
			L"يفتح مجلد الملف. غير متاح إن كان مفقوداً.",
			L"Открывает папку файла. Недоступно, если файла нет.",
			L"Oeffnet den Ordner der Datei. Gesperrt, wenn sie fehlt.",
			L"Abre a pasta do arquivo. Desativado se faltar.",
			L"Opent de map van het bestand. Uit als het ontbreekt.",
			L"Otwiera folder pliku. Wylaczone, gdy pliku brak.",
			L"Dosyanin klasorunu acar. Dosya yoksa basilamaz."));
		PpTip(m_tooltip, m_btnCopyPath, LL14(L"パスをクリップボードへコピーします。",
			L"Copy the path to the clipboard.",
			L"Copie le chemin.",
			L"Copia il percorso.",
			L"Copia la ruta.",
			L"경로를 클립보드에 복사합니다.",
			L"把路径复制到剪贴板。",
			L"ينسخ المسار.",
			L"Копирует путь.",
			L"Pfad kopieren.",
			L"Copia o caminho.",
			L"Kopieert het pad.",
			L"Kopiuje sciezke.",
			L"Yolu kopyalar."));
		PpTip(m_tooltip, m_btnCopyName, LL14(L"ファイル名だけをクリップボードへコピーします。",
			L"Copy only the file name to the clipboard.",
			L"Copie seulement le nom de fichier.",
			L"Copia solo il nome file.",
			L"Copia solo el nombre de archivo.",
			L"파일 이름만 클립보드에 복사합니다.",
			L"只把文件名复制到剪贴板。",
			L"ينسخ اسم الملف فقط.",
			L"Копирует только имя файла.",
			L"Nur den Dateinamen kopieren.",
			L"Copia so o nome do arquivo.",
			L"Kopieert alleen de bestandsnaam.",
			L"Kopiuje sama nazwe pliku.",
			L"Yalnizca dosya adini kopyalar."));
		PpTip(m_tooltip, m_btnProTools, LL14(L"この曲の再生詳細（音量・EQ など）を開きます。",
			L"Open playback details for this track (volume, EQ, and more).",
			L"Ouvre les details de lecture (volume, EQ…).",
			L"Apre i dettagli di riproduzione (volume, EQ…).",
			L"Abre los detalles de reproduccion (volumen, EQ…).",
			L"이 곡의 재생 상세(음량·EQ 등)를 엽니다.",
			L"打开这首的播放详情（音量、EQ 等）。",
			L"يفتح تفاصيل التشغيل.",
			L"Открывает детали воспроизведения.",
			L"Oeffnet Wiedergabedetails.",
			L"Abre detalhes de reproducao.",
			L"Opent afspeeldetails.",
			L"Otwiera szczegoly odtwarzania.",
			L"Bu parcanin oynatma ayrintilarini acar."));
		PpTip(m_tooltip, m_btnClearParam, LL14(L"この曲だけの音量・EQ（[SAV]）を消します。",
			L"Delete this track's own volume and EQ ([SAV]).",
			L"Efface volume et EQ propres a cette piste ([SAV]).",
			L"Cancella volume ed EQ propri ([SAV]).",
			L"Borra volumen y EQ propios ([SAV]).",
			L"이 곡만의 음량·EQ([SAV])를 지웁니다.",
			L"删除这首单独的音量与 EQ（[SAV]）。",
			L"يحذف مستوى الصوت وEQ الخاصين ([SAV]).",
			L"Удаляет громкость и EQ только этого трека ([SAV]).",
			L"Loescht Lautstaerke und EQ nur dieses Titels ([SAV]).",
			L"Apaga volume e EQ so desta faixa ([SAV]).",
			L"Wist volume en EQ van alleen dit nummer ([SAV]).",
			L"Usuwa glosnosc i EQ tylko tego utworu ([SAV]).",
			L"Yalniz bu parcanin ses ve EQ'sunu ([SAV]) siler."));
		PpTip(m_tooltip, m_btnWav, LL14(L"選択曲を WAV などに書き出す画面を開きます。",
			L"Open the dialog that exports the selection to WAV or similar.",
			L"Ouvre l'export de la selection en WAV, etc.",
			L"Apre l'export della selezione in WAV, ecc.",
			L"Abre la exportacion de la seleccion a WAV, etc.",
			L"선택 곡을 WAV 등으로 내보내는 화면을 엽니다.",
			L"打开把所选导出为 WAV 等的画面。",
			L"يفتح تصدير التحديد إلى WAV وغيره.",
			L"Открывает экспорт выбора в WAV и т.п.",
			L"Oeffnet den Export der Auswahl als WAV usw.",
			L"Abre a exportacao da selecao para WAV etc.",
			L"Opent het exporteren van de selectie naar WAV e.d.",
			L"Otwiera eksport wyboru do WAV itd.",
			L"Secimi WAV vb. olarak aktarma penceresini acar."));
		PpTip(m_tooltip, m_btnTranscode, LL14(L"選択曲を mp3 または FLAC に変換する画面を開きます。",
			L"Open the dialog that converts the selection to mp3 or FLAC.",
			L"Ouvre la conversion de la selection en mp3 ou FLAC.",
			L"Apre la conversione della selezione in mp3 o FLAC.",
			L"Abre la conversion de la seleccion a mp3 o FLAC.",
			L"선택 곡을 mp3 또는 FLAC로 변환하는 화면을 엽니다.",
			L"打开把所选转为 mp3 或 FLAC 的画面。",
			L"يفتح تحويل التحديد إلى mp3 أو FLAC.",
			L"Открывает конвертацию выбора в mp3 или FLAC.",
			L"Oeffnet die Umwandlung der Auswahl nach mp3 oder FLAC.",
			L"Abre a conversao da selecao para mp3 ou FLAC.",
			L"Opent het omzetten van de selectie naar mp3 of FLAC.",
			L"Otwiera konwersje wyboru do mp3 lub FLAC.",
			L"Secimi mp3 veya FLAC'a donusturme penceresini acar."));
		PpTip(m_tooltip, m_btnVideo, LL14(L"動画から音声だけを取り出します。動画でないときは押せません。",
			L"Extract only the audio from a video. Disabled unless the selection is a video.",
			L"Extrait seulement l'audio d'une video. Desactive sinon.",
			L"Estrae solo l'audio da un video. Disattivo altrimenti.",
			L"Extrae solo el audio de un video. Desactivado si no es video.",
			L"동영상에서 음성만 꺼냅니다. 동영상이 아니면 누를 수 없습니다.",
			L"只从视频取出音频。不是视频时不可按。",
			L"يستخرج الصوت فقط من فيديو. غير متاح لغير الفيديو.",
			L"Извлекает только звук из видео. Недоступно, если это не видео.",
			L"Holt nur die Audiospur aus einem Video. Sonst gesperrt.",
			L"Extrai so o audio de um video. Desativado se nao for video.",
			L"Haalt alleen audio uit een video. Uit als het geen video is.",
			L"Wyciaga samo audio z wideo. Wylaczone, gdy to nie wideo.",
			L"Videodan yalniz sesi cikarir. Video degilse basilamaz."));
		PpTip(m_tooltip, m_btnBpm, LL14(L"メディアプレイヤーで再生中の曲のテンポを測ります。プレイヤーが開いていないときは押せません。",
			L"Measure the tempo of the track playing in the media player. Disabled when that window is closed.",
			L"Mesure le tempo du morceau en lecture. Desactive si le lecteur est ferme.",
			L"Misura il tempo del brano in riproduzione. Disattivo se il player e chiuso.",
			L"Mide el tempo de lo que suena. Desactivado si el reproductor esta cerrado.",
			L"미디어 플레이어에서 재생 중인 곡의 템포를 잽니다. 플레이어가 닫혀 있으면 누를 수 없습니다.",
			L"测量媒体播放器里正在播放的曲目速度。播放器未开时不可按。",
			L"يقيس إيقاع المقطع الجاري. غير متاح إن كان المشغّل مغلقاً.",
			L"Меряет темп играющего трека. Недоступно, если плеер закрыт.",
			L"Misst das Tempo des laufenden Titels. Gesperrt, wenn der Player zu ist.",
			L"Mede o tempo da faixa tocando. Desativado se o player estiver fechado.",
			L"Meet het tempo van het spelende nummer. Uit als de speler dicht is.",
			L"Mierzy tempo granego utworu. Wylaczone, gdy odtwarzacz jest zamkniety.",
			L"Oynaticida calan parcanin temposunu olcer. Oynatici kapaliysa basilamaz."));
		PpTip(m_tooltip, m_btnTagBatch, LL14(L"選んだ複数曲のタグを一度に書き換えます。1曲だけなら押せません。",
			L"Rewrite tags of several selected tracks at once. Disabled for a single track.",
			L"Reecrit les tags de plusieurs pistes. Desactive pour une seule.",
			L"Riscrive i tag di piu tracce. Disattivo per una sola.",
			L"Reescribe etiquetas de varias pistas. Desactivado si hay una sola.",
			L"고른 여러 곡의 태그를 한 번에 고칩니다. 한 곡이면 누를 수 없습니다.",
			L"一次改写多首所选的标签。只选一首时不可按。",
			L"يعيد كتابة وسوم عدة مسارات. غير متاح لمسار واحد.",
			L"Переписывает теги нескольких треков. Недоступно для одного.",
			L"Schreibt Tags mehrerer Titel auf einmal. Bei einem Titel gesperrt.",
			L"Regrava tags de varias faixas. Desativado para uma so.",
			L"Herschrijft tags van meerdere nummers. Uit bij een enkel nummer.",
			L"Zmienia tagi wielu utworow naraz. Wylaczone przy jednym.",
			L"Secilen birden cok parcanin etiketini birden yazar. Tek parcada basilamaz."));
		PpTip(m_tooltip, m_btnQueue, LL14(L"選択曲を再生キューへ足します。メディアプレイヤーが開いていないときは押せません。",
			L"Add the selection to the play queue. Disabled when the media player is closed.",
			L"Ajoute la selection a la file. Desactive si le lecteur est ferme.",
			L"Aggiunge la selezione alla coda. Disattivo se il player e chiuso.",
			L"Anade la seleccion a la cola. Desactivado si el reproductor esta cerrado.",
			L"선택 곡을 재생 큐에 넣습니다. 미디어 플레이어가 닫혀 있으면 누를 수 없습니다.",
			L"把所选加入播放队列。媒体播放器未开时不可按。",
			L"يضيف التحديد إلى الطابور. غير متاح إن كان المشغّل مغلقاً.",
			L"Добавляет выбор в очередь. Недоступно, если плеер закрыт.",
			L"Haengt die Auswahl an die Warteschlange. Gesperrt, wenn der Player zu ist.",
			L"Adiciona a selecao a fila. Desativado se o player estiver fechado.",
			L"Zet de selectie in de wachtrij. Uit als de speler dicht is.",
			L"Dodaje wybor do kolejki. Wylaczone, gdy odtwarzacz jest zamkniety.",
			L"Secimi calma kuyruguna ekler. Oynatici kapaliysa basilamaz."));
		PpTip(m_tooltip, m_btnPlayNext, LL14(L"今の曲の次に選択曲を再生します。メディアプレイヤーが開いていないときは押せません。",
			L"Play the selection right after the current track. Disabled when the media player is closed.",
			L"Lit la selection juste apres le morceau en cours. Desactive si le lecteur est ferme.",
			L"Riproduce la selezione subito dopo il brano attuale. Disattivo se il player e chiuso.",
			L"Reproduce la seleccion justo despues de la pista actual. Desactivado si el reproductor esta cerrado.",
			L"지금 곡 다음에 선택 곡을 재생합니다. 미디어 플레이어가 닫혀 있으면 누를 수 없습니다.",
			L"在当前曲之后播放所选。媒体播放器未开时不可按。",
			L"يشغّل التحديد بعد المقطع الحالي. غير متاح إن كان المشغّل مغلقاً.",
			L"Играет выбор сразу после текущего. Недоступно, если плеер закрыт.",
			L"Spielt die Auswahl direkt nach dem aktuellen Titel. Gesperrt, wenn der Player zu ist.",
			L"Toca a selecao logo apos a faixa atual. Desativado se o player estiver fechado.",
			L"Speelt de selectie direct na het huidige nummer. Uit als de speler dicht is.",
			L"Odtwarza wybor zaraz po biezacym. Wylaczone, gdy odtwarzacz jest zamkniety.",
			L"Secimi simdiki parcadan hemen sonra calar. Oynatici kapaliysa basilamaz."));
		PpTip(m_tooltip, m_btnAddSame, LL14(L"このファイルと同じフォルダの曲を一覧へ追加します。ファイルが無いときは押せません。",
			L"Add tracks from the same folder to the list. Disabled when the file is missing.",
			L"Ajoute les pistes du meme dossier. Desactive si le fichier manque.",
			L"Aggiunge i brani della stessa cartella. Disattivo se il file manca.",
			L"Anade pistas de la misma carpeta. Desactivado si falta el archivo.",
			L"이 파일과 같은 폴더의 곡을 목록에 추가합니다. 파일이 없으면 누를 수 없습니다.",
			L"把同一文件夹的曲目加入列表。文件不在时不可按。",
			L"يضيف مقاطع المجلد نفسه. غير متاح إن كان الملف مفقوداً.",
			L"Добавляет треки из той же папки. Недоступно, если файла нет.",
			L"Fuegt Titel aus demselben Ordner hinzu. Gesperrt, wenn die Datei fehlt.",
			L"Adiciona faixas da mesma pasta. Desativado se o arquivo faltar.",
			L"Voegt nummers uit dezelfde map toe. Uit als het bestand ontbreekt.",
			L"Dodaje utwory z tego samego folderu. Wylaczone, gdy pliku brak.",
			L"Ayni klasordeki parcalari listeye ekler. Dosya yoksa basilamaz."));
		PpTip(m_tooltip, m_btnMidExport, LL14(L"MIDI またはささみ☆ﾐ の曲を標準 MIDI として保存します。MIDI でないときは押せません。",
			L"Save a MIDI or Sasami track as standard MIDI. Disabled unless the selection is MIDI.",
			L"Enregistre un MIDI ou Sasami en MIDI standard. Desactive sinon.",
			L"Salva un MIDI o Sasami come MIDI standard. Disattivo altrimenti.",
			L"Guarda un MIDI o Sasami como MIDI estandar. Desactivado si no es MIDI.",
			L"MIDI 또는 사사미 곡을 표준 MIDI로 저장합니다. MIDI가 아니면 누를 수 없습니다.",
			L"把 MIDI 或ささみ☆ﾐ 曲存成标准 MIDI。不是 MIDI 时不可按。",
			L"يحفظ MIDI أو Sasami كـ MIDI قياسي. غير متاح لغير MIDI.",
			L"Сохраняет MIDI или Sasami как обычный MIDI. Недоступно, если это не MIDI.",
			L"Speichert MIDI oder Sasami als Standard-MIDI. Sonst gesperrt.",
			L"Salva MIDI ou Sasami como MIDI padrao. Desativado se nao for MIDI.",
			L"Slaat MIDI of Sasami op als standaard-MIDI. Uit als het geen MIDI is.",
			L"Zapisuje MIDI lub Sasami jako standardowe MIDI. Wylaczone, gdy to nie MIDI.",
			L"MIDI veya Sasami parcayi standart MIDI olarak kaydeder. MIDI degilse basilamaz."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
	}
	UpdateActions();
	return TRUE;
}

BOOL CPlayProperty_tab4::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomDialog::PreTranslateMessage(pMsg);
}

void CPlayProperty_tab4::UpdateActions()
{
	CPlayProperty* host = dynamic_cast<CPlayProperty*>(GetParent());
	if (!host) return;
	CString path = host->pc.fol;
	CString phys = PlPhysicalMediaPath(path);
	if (phys.IsEmpty()) phys = path;
	const BOOL hasPath = !path.IsEmpty();
	const BOOL onDisk = (!phys.IsEmpty() && PathFileExists(phys)) ? TRUE : FALSE;
	const BOOL multi = (host->m_batchIndices.size() >= 2) ? TRUE : FALSE;
	const BOOL hasMp = (mp && ::IsWindow(mp->GetSafeHwnd())) ? TRUE : FALSE;

	BOOL video = (host->pc.sub == -2 || PpLooksLikeVideo(phys.IsEmpty() ? (LPCTSTR)path : (LPCTSTR)phys)) ? TRUE : FALSE;
	BOOL midi = FALSE;
	const int nBatch = (int)host->m_batchIndices.size();
	for (int k = 0; k < nBatch && !(video && midi); ++k) {
		const int i = host->m_batchIndices[k];
		if (!m_pl || !m_pl->pc || i < 0 || i >= m_pl->playcnt) continue;
		const CString& fol = m_pl->pc[i].fol;
		if (!video && (m_pl->pc[i].sub == -2 || PpLooksLikeVideo(fol)))
			video = TRUE;
#ifndef _UNICODE
		if (!midi && (SasamiExtIsMidi(CStringW(fol)) || VstIsMidiExt(CStringW(fol))))
#else
		if (!midi && (SasamiExtIsMidi(fol) || VstIsMidiExt(fol)))
#endif
			midi = TRUE;
	}
	if (!midi && hasPath) {
#ifndef _UNICODE
		if (SasamiExtIsMidi(CStringW(path)) || VstIsMidiExt(CStringW(path)))
#else
		if (SasamiExtIsMidi(path) || VstIsMidiExt(path))
#endif
			midi = TRUE;
	}

	m_btnExplorer.EnableWindow(onDisk);
	m_btnOpenFolder.EnableWindow(onDisk);
	m_btnAddSame.EnableWindow(onDisk);
	m_btnCopyPath.EnableWindow(hasPath);
	m_btnCopyName.EnableWindow(hasPath);
	m_btnVideo.EnableWindow(video);
	m_btnTagBatch.EnableWindow(multi);
	m_btnQueue.EnableWindow(hasMp);
	m_btnPlayNext.EnableWindow(hasMp);
	m_btnBpm.EnableWindow(hasMp);
	m_btnMidExport.EnableWindow(midi);
}

void CPlayProperty_tab4::OnMoreCmd(UINT nID)
{
	if (!m_pl) return;
	int cmd = 0;
	switch (nID) {
	case IDC_PP_MORE_EXPLORER: cmd = PL_CTX_OPEN_FOLDER; break;
	case IDC_PP_MORE_OPENFOLDER: cmd = PL_CTX_OPEN_FOLDER; break;
	case IDC_PP_MORE_COPYPATH: cmd = PL_CTX_COPY_TITLEART; break;
	case IDC_PP_MORE_PROTOOLS: cmd = PL_CTX_PROTOOLS; break;
	case IDC_PP_MORE_CLEARPARAM: cmd = PL_CTX_CLEAR_SONGPARAM; break;
	case IDC_PP_MORE_WAV: cmd = PL_CTX_WAV; break;
	case IDC_PP_MORE_TRANSCODE: cmd = PL_CTX_TRANSCODE; break;
	case IDC_PP_MORE_VIDEO: cmd = PL_CTX_VIDEO_EXTRACT; break;
	case IDC_PP_MORE_BPM: cmd = PL_CTX_BPM; break;
	case IDC_PP_MORE_TAGBATCH: cmd = PL_CTX_TAG_BATCH; break;
	case IDC_PP_MORE_QUEUE: cmd = PL_CTX_QUEUE_ADD; break;
	case IDC_PP_MORE_PLAYNEXT: cmd = PL_CTX_QUEUE_PLAYNEXT; break;
	case IDC_PP_MORE_ADDSAME: cmd = PL_CTX_ADD_SAME_FOLDER; break;
	case IDC_PP_MORE_MIDEXPORT: cmd = PL_CTX_MID_EXPORT; break;
	case IDC_PP_MORE_COPYNAME:
		{
			CPlayProperty* host = (CPlayProperty*)GetParent();
			if (host) {
				CString name = host->pc.fol;
				const int slash = (std::max)(name.ReverseFind(_T('\\')), name.ReverseFind(_T('/')));
				if (slash >= 0) name = name.Mid(slash + 1);
				PpCopyText(GetSafeHwnd(), name);
			}
		}
		return;
	default: return;
	}
	if (nID == IDC_PP_MORE_COPYPATH) {
		CPlayProperty* host = (CPlayProperty*)GetParent();
		if (host) PpCopyText(GetSafeHwnd(), host->pc.fol);
		return;
	}
	m_pl->HandleTrackContextCmd(cmd);
}

IMPLEMENT_DYNAMIC(CPlayProperty, CCustomBlurDialogBase)

CPlayProperty::CPlayProperty(CWnd* pParent)
	: CCustomBlurDialogBase(CPlayProperty::IDD, pParent)
	, m_pl(NULL), m_kpiIdx(-1), m_minW(0), m_minH(0)
{
	ZeroMemory(&pc, sizeof(pc));
}

void CPlayProperty::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PP_TABS, m_tabs);
	DDX_Control(pDX, IDOK, m_ok);
	DDX_Control(pDX, IDCANCEL, m_cancel);
	DDX_Control(pDX, IDC_PP_APPLY, m_apply);
}

BEGIN_MESSAGE_MAP(CPlayProperty, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDOK, OnBnClickedOk)
	ON_BN_CLICKED(IDC_PP_APPLY, OnBnClickedApply)
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_NOTIFY(TCN_SELCHANGE, IDC_PP_TABS, OnTabSelChange)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CPlayProperty::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

BOOL CPlayProperty::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	SetWindowText(LL14(L"プロパティ", L"Properties", L"Propriétés", L"Proprietà", L"Propiedades",
		L"속성", L"属性", L"خصائص", L"Свойства", L"Eigenschaften", L"Propriedades", L"Eigenschappen", L"Właściwości", L"Özellikler"));
	SetDlgItemText(IDOK, LL14(L"OK", L"OK", L"OK", L"OK", L"OK", L"OK", L"确定", L"موافق", L"ОК", L"OK", L"OK", L"OK", L"OK", L"Tamam"));
	SetDlgItemText(IDCANCEL, LL14(L"キャンセル", L"Cancel", L"Annuler", L"Annulla", L"Cancelar", L"취소", L"取消", L"إلغاء", L"Отмена", L"Abbrechen", L"Cancelar", L"Annuleren", L"Anuluj", L"İptal"));
	SetDlgItemText(IDC_PP_APPLY, LL14(L"適用", L"Apply", L"Appliquer", L"Applica", L"Aplicar", L"적용", L"应用", L"تطبيق", L"Применить", L"Übernehmen", L"Aplicar", L"Toepassen", L"Zastosuj", L"Uygula"));
	m_tabs.SetAeroMode(FALSE);
	m_ok.SetAeroMode(FALSE); m_cancel.SetAeroMode(FALSE); m_apply.SetAeroMode(FALSE);
	PpStyleBtn(m_ok); PpStyleBtn(m_cancel); PpStyleBtn(m_apply);

	m_kpiIdx = FindKpiForPath(pc.fol);
	m_tab1.m_pc = &pc;
	m_tab1.m_batch = &m_batchIndices;
	m_tab2.m_pc = &pc;
	m_tab2.m_multi = (m_batchIndices.size() > 1);
	if (m_tab2.m_multi && pl && pl->pc) {
		m_tab2.m_pcs.reserve(m_batchIndices.size());
		for (size_t i = 0; i < m_batchIndices.size(); ++i) {
			const int idx = m_batchIndices[i];
			if (idx >= 0 && idx < pl->playcnt)
				m_tab2.m_pcs.push_back(pl->pc[idx]);
		}
	}
	m_tab3.m_kpiIdx = m_kpiIdx;
	m_tab4.m_pl = m_pl;

	/* Create 中の WS_VISIBLE 初回 WM_PAINT は fixer 未装着でガラス穴になる。
	   全ページをここで作る。後から Create するとアクリル抜けする。 */
	SetRedraw(FALSE);
	m_tab1.Create(IDD_PLAYPROP_TAB1, this);
	m_tab2.Create(IDD_PLAYPROP_TAB2, this);
	m_tab3.Create(IDD_PLAYPROP_TAB3, this);
	m_tab4.Create(IDD_PLAYPROP_TAB4, this);
	if (m_tab1.GetSafeHwnd()) m_tab1.ShowWindow(SW_HIDE);
	if (m_tab2.GetSafeHwnd()) m_tab2.ShowWindow(SW_HIDE);
	if (m_tab3.GetSafeHwnd()) m_tab3.ShowWindow(SW_HIDE);
	if (m_tab4.GetSafeHwnd()) m_tab4.ShowWindow(SW_HIDE);

	TCITEM ti = {};
	ti.mask = TCIF_TEXT;
	ti.pszText = (LPTSTR)LL14(L"ファイル", L"File", L"Fichier", L"File", L"Archivo", L"파일", L"文件", L"ملف", L"Файл", L"Datei", L"Arquivo", L"Bestand", L"Plik", L"Dosya");
	m_tabs.InsertItem(0, &ti);
	ti.pszText = (LPTSTR)LL14(L"タグ", L"Tags", L"Tags", L"Tag", L"Etiquetas", L"태그", L"标签", L"وسوم", L"Теги", L"Tags", L"Tags", L"Tags", L"Tagi", L"Etiketler");
	m_tabs.InsertItem(1, &ti);
	if (m_kpiIdx >= 0) {
		ti.pszText = (LPTSTR)LL14(L"プラグイン", L"Plugin", L"Plugin", L"Plugin", L"Complemento", L"플러그인", L"插件", L"إضافة", L"Плагин", L"Plugin", L"Plugin", L"Plug-in", L"Wtyczka", L"Eklenti");
		m_tabs.InsertItem(2, &ti);
		ti.pszText = (LPTSTR)LL14(L"その他", L"More", L"Plus", L"Altro", L"Más", L"기타", L"其他", L"المزيد", L"Ещё", L"Mehr", L"Mais", L"Meer", L"Więcej", L"Diğer");
		m_tabs.InsertItem(3, &ti);
	} else {
		ti.pszText = (LPTSTR)LL14(L"その他", L"More", L"Plus", L"Altro", L"Más", L"기타", L"其他", L"المزيد", L"Ещё", L"Mehr", L"Mais", L"Meer", L"Więcej", L"Diğer");
		m_tabs.InsertItem(2, &ti);
	}
	m_tabs.SetCurSel(0);
	m_tabs.LayoutEqualTabs(m_tabs.GetItemCount());

	CRect wr; GetWindowRect(&wr);
	m_minW = wr.Width();
	m_minH = wr.Height();
	LayoutPages();
	/* ページ HWND が揃ってから fixer を載せる。Post だと ShowPage の初回描画に間に合わない。 */
	SendMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS);
	SetRedraw(TRUE);
	ShowPage(0);
	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		PpTip(m_tooltip, m_tabs, LL14(L"ファイル情報、タグ、プラグイン、その他のページを切り替えます。",
			L"Switch among file info, tags, plugin, and more.",
			L"Bascule entre fichier, tags, plugin et plus.",
			L"Passa tra file, tag, plugin e altro.",
			L"Cambia entre archivo, etiquetas, complemento y mas.",
			L"파일 정보, 태그, 플러그인, 기타 페이지를 바꿉니다.",
			L"在文件信息、标签、插件、其他页面之间切换。",
			L"التبديل بين الملف والوسوم والإضافة والمزيد.",
			L"Переключение: файл, теги, плагин и прочее.",
			L"Wechselt zwischen Datei, Tags, Plugin und mehr.",
			L"Alterna entre arquivo, tags, plugin e mais.",
			L"Wisselt tussen bestand, tags, plug-in en meer.",
			L"Przelacza plik, tagi, wtyczke i wiecej.",
			L"Dosya, etiket, eklenti ve diger sayfalar arasinda gecer."));
		PpTip(m_tooltip, m_ok, LL14(L"プレイリスト欄とタグの変更を保存して閉じます。",
			L"Save playlist fields and tags, then close.",
			L"Enregistre playlist et tags, puis ferme.",
			L"Salva playlist e tag, poi chiude.",
			L"Guarda lista y etiquetas, y cierra.",
			L"재생 목록과 태그를 저장하고 닫습니다.",
			L"保存播放列表与标签后关闭。",
			L"يحفظ القائمة والوسوم ثم يغلق.",
			L"Сохраняет плейлист и теги и закрывает.",
			L"Speichert Playlist und Tags und schliesst.",
			L"Salva lista e tags e fecha.",
			L"Slaat playlist en tags op en sluit.",
			L"Zapisuje playliste i tagi i zamyka.",
			L"Liste ve etiketleri kaydedip kapatir."));
		PpTip(m_tooltip, m_cancel, LL14(L"プレイリスト欄とタグの変更を捨てて閉じます。その他ですでに実行した操作は戻しません。",
			L"Discard playlist and tag edits and close. Actions already run on More stay done.",
			L"Abandonne playlist et tags. Les actions deja faites dans Plus restent.",
			L"Scarta playlist e tag. Le azioni gia fatte in Altro restano.",
			L"Descarta lista y etiquetas. Lo ya hecho en Mas permanece.",
			L"재생 목록과 태그 변경을 버리고 닫습니다. 기타에서 이미 실행한 조작은 그대로입니다.",
			L"放弃播放列表和标签的修改并关闭。其他页已经执行的操作不会撤回。",
			L"يتجاهل تعديلات القائمة والوسوم. ما نُفِّذ في المزيد يبقى.",
			L"Отменяет правки плейлиста и тегов. Уже сделанное на «Ещё» остаётся.",
			L"Verwirft Playlist- und Tag-Aenderungen. Bereits ausgefuehrte Mehr-Aktionen bleiben.",
			L"Descarta lista e tags. O que ja foi feito em Mais permanece.",
			L"Gooit playlist- en tagwijzigingen weg. Al uitgevoerde Meer-acties blijven.",
			L"Odrzuca zmiany playlisty i tagow. Juz wykonane akcje z Wiecej zostaja.",
			L"Liste ve etiket degisikliklerini atar. Diger'de yapilan islemler kalir."));
		PpTip(m_tooltip, m_apply, LL14(L"プレイリスト欄とタグの変更を保存して、この窓は開いたままにします。",
			L"Save playlist fields and tags, keep this window open.",
			L"Enregistrer playlist et tags, garder la fenetre.",
			L"Salva playlist e tag, tieni aperta.",
			L"Guardar lista y etiquetas, dejar abierta.",
			L"재생 목록과 태그를 저장하고 창은 엽니다.",
			L"保存播放列表与标签，窗口保持打开。",
			L"حفظ القائمة والوسوم مع إبقاء النافذة.",
			L"Сохранить плейлист и теги, окно оставить.",
			L"Playlist und Tags speichern, Fenster offen lassen.",
			L"Salvar lista e tags, manter janela.",
			L"Playlist en tags opslaan, venster openhouden.",
			L"Zapisz playliste i tagi, zostaw okno.",
			L"Liste ve etiketleri kaydet, pencere acik kalsin."));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
	}
	return TRUE;
}

void CPlayProperty::LayoutPages()
{
	if (!m_tabs.GetSafeHwnd()) return;
	CRect rc;
	m_tabs.GetWindowRect(&rc);
	ScreenToClient(&rc);
	CRect inner = rc;
	m_tabs.AdjustRect(FALSE, &inner);
	CCustomDialog* pages[4] = { &m_tab1, &m_tab2, &m_tab3, &m_tab4 };
	for (int i = 0; i < 4; ++i) {
		if (pages[i]->GetSafeHwnd())
			pages[i]->SetWindowPos(&wndTop, inner.left, inner.top, inner.Width(), inner.Height(), SWP_NOACTIVATE);
	}
}

void CPlayProperty::EnsurePage(int page)
{
	if (page < 0 || page > 3) return;
	CCustomDialog* pages[4] = { &m_tab1, &m_tab2, &m_tab3, &m_tab4 };
	const UINT ids[4] = { IDD_PLAYPROP_TAB1, IDD_PLAYPROP_TAB2, IDD_PLAYPROP_TAB3, IDD_PLAYPROP_TAB4 };
	if (!pages[page]->GetSafeHwnd()) {
		pages[page]->Create(ids[page], this);
		LayoutPages();
		SendMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS);
	}
	if (page == 2 && m_kpiIdx >= 0 && !m_tab3.m_pluginLoaded) {
		m_tab3.m_pluginLoaded = TRUE;
		m_tab3.LoadPlugin(m_kpiIdx);
	}
}

void CPlayProperty::ShowPage(int tabSel)
{
	int page = tabSel;
	if (m_kpiIdx < 0 && tabSel >= 2) page = 3;
	EnsurePage(page);
	CCustomDialog* pages[4] = { &m_tab1, &m_tab2, &m_tab3, &m_tab4 };
	for (int i = 0; i < 4; ++i) {
		if (i == page) continue;
		if (pages[i]->GetSafeHwnd())
			pages[i]->ShowWindow(SW_HIDE);
	}
	if (pages[page]->GetSafeHwnd()) {
		pages[page]->SetWindowPos(&wndTop, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
		pages[page]->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}
}

void CPlayProperty::OnTabSelChange(NMHDR*, LRESULT* pResult)
{
	if (pResult) *pResult = 0;
	ShowPage(m_tabs.GetCurSel());
}

void CPlayProperty::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (!m_tabs.GetSafeHwnd() || !m_ok.GetSafeHwnd()) return;
	const int m = 7, bh = 22, gap = 6, bw = 70;
	m_tabs.SetWindowPos(NULL, m, m, cx - m * 2, cy - m * 2 - bh - gap, SWP_NOZORDER | SWP_NOACTIVATE);
	int x = cx - m - bw;
	m_apply.SetWindowPos(NULL, x, cy - m - bh, bw, bh, SWP_NOZORDER | SWP_NOACTIVATE);
	x -= bw + gap;
	m_cancel.SetWindowPos(NULL, x, cy - m - bh, bw, bh, SWP_NOZORDER | SWP_NOACTIVATE);
	x -= bw + gap;
	m_ok.SetWindowPos(NULL, x, cy - m - bh, bw, bh, SWP_NOZORDER | SWP_NOACTIVATE);
	m_tabs.LayoutEqualTabs(m_tabs.GetItemCount());
	LayoutPages();
}

void CPlayProperty::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CCustomBlurDialogBase::OnGetMinMaxInfo(lpMMI);
	if (m_minW > 0) lpMMI->ptMinTrackSize.x = m_minW;
	if (m_minH > 0) lpMMI->ptMinTrackSize.y = m_minH;
}

BOOL CPlayProperty::ApplyChanges()
{
	if (!m_pl || !m_pl->pc) return FALSE;
	m_tab1.CollectPlaylistFields();
	if (m_batchIndices.size() > 1) {
		for (int idx : m_batchIndices) {
			if (idx < 0 || idx >= m_pl->playcnt) continue;
			_tcscpy(m_pl->pc[idx].art, pc.art);
			_tcscpy(m_pl->pc[idx].alb, pc.alb);
		}
		if (m_tab2.m_multi && m_tab2.GetSafeHwnd()) {
			m_tab2.WriteTags();
			const size_t n = (m_batchIndices.size() < m_tab2.m_pcs.size()) ? m_batchIndices.size() : m_tab2.m_pcs.size();
			for (size_t i = 0; i < n; ++i) {
				const int idx = m_batchIndices[i];
				if (idx < 0 || idx >= m_pl->playcnt) continue;
				_tcscpy(m_pl->pc[idx].name, m_tab2.m_pcs[i].name);
				_tcscpy(m_pl->pc[idx].art, m_tab2.m_pcs[i].art);
				_tcscpy(m_pl->pc[idx].alb, m_tab2.m_pcs[i].alb);
			}
		}
	} else if (!m_batchIndices.empty()) {
		const int idx = m_batchIndices[0];
		if (idx >= 0 && idx < m_pl->playcnt) {
			_tcscpy(m_pl->pc[idx].name, pc.name);
			_tcscpy(m_pl->pc[idx].art, pc.art);
			_tcscpy(m_pl->pc[idx].alb, pc.alb);
			m_pl->pc[idx].loop1 = pc.loop1;
			m_pl->pc[idx].loop2 = pc.loop2;
			const CString folStored = PlStorePlaylistFol(pc.fol, m_pl->pc[idx].sub);
			_tcscpy(m_pl->pc[idx].fol, folStored);
		}
		if (m_tab2.GetSafeHwnd())
			m_tab2.WriteTags();
		if (idx >= 0 && idx < m_pl->playcnt) {
			_tcscpy(m_pl->pc[idx].name, pc.name);
			_tcscpy(m_pl->pc[idx].art, pc.art);
			_tcscpy(m_pl->pc[idx].alb, pc.alb);
		}
	}
	if (m_pl->pnt >= 0 && m_pl->pnt < m_pl->playcnt) {
		CString cur = PlPhysicalMediaPath(filen);
		if (cur.IsEmpty()) cur = filen;
		CString row = PlPhysicalMediaPath(m_pl->pc[m_pl->pnt].fol);
		if (row.IsEmpty()) row = m_pl->pc[m_pl->pnt].fol;
		if (!cur.IsEmpty() && !row.IsEmpty() && _tcsicmp(cur, row) == 0)
			fnn = m_pl->pc[m_pl->pnt].name;
	}
	m_pl->Save();
	if (m_pl->m_lc.GetSafeHwnd())
		m_pl->m_lc.Invalidate(FALSE);
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->RefreshList(TRUE);
	return TRUE;
}

void CPlayProperty::OnBnClickedOk()
{
	ApplyChanges();
	EndDialog(IDOK);
}

void CPlayProperty::OnBnClickedApply()
{
	ApplyChanges();
}

void CPlayProperty::OnDestroy()
{
	if (m_tab1.GetSafeHwnd()) m_tab1.DestroyWindow();
	if (m_tab2.GetSafeHwnd()) m_tab2.DestroyWindow();
	if (m_tab3.GetSafeHwnd()) m_tab3.DestroyWindow();
	if (m_tab4.GetSafeHwnd()) m_tab4.DestroyWindow();
	CCustomBlurDialogBase::OnDestroy();
}
