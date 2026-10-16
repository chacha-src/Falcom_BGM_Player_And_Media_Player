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

int CPlayProperty::FindKpiForPath(LPCTSTR path)
{
	CString media = PlPhysicalMediaPath(path);
	if (media.IsEmpty()) media = path;
	if (media.IsEmpty()) return -1;
	const int dot = media.ReverseFind(_T('.'));
	if (dot < 0) return -1;
	CString extPart = media.Mid(dot);
	extPart.MakeLower();
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
	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
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
	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
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
	return TRUE;
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
	return TRUE;
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
	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	m_tooltip.AddTool(&m_apply, LL14(L"プレイリスト欄とタグの変更を保存して、この窓は開いたままにします。",
		L"Save playlist fields and tags, keep this window open.",
		L"Enregistrer playlist et tags, garder la fenêtre.",
		L"Salva playlist e tag, tieni aperta.",
		L"Guardar lista y etiquetas, dejar abierta.",
		L"재생 목록과 태그를 저장하고 창은 엽니다.",
		L"保存播放列表与标签，窗口保持打开。",
		L"حفظ القائمة والوسوم مع إبقاء النافذة.",
		L"Сохранить плейлист и теги, окно оставить.",
		L"Playlist und Tags speichern, Fenster offen lassen.",
		L"Salvar lista e tags, manter janela.",
		L"Playlist en tags opslaan, venster openhouden.",
		L"Zapisz playlistę i tagi, zostaw okno.",
		L"Liste ve etiketleri kaydet, pencere açık kalsın."));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);
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
