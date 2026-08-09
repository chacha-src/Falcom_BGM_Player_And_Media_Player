#include "stdafx.h"
#include "MpFeatureExtras.h"
#include "MpKeyCamelot.h"
#include "SongParams.h"
#include "ProAudio.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "CMediaPlayerDlg.h"
#include "DeviceRecordDlg.h"
#include "ScreenCaptureDlg.h"
#include "MpSidecar.h"
#include "CCustomPopupMenu.h"
#include <mmsystem.h>

extern save savedata;
extern COggDlg* og;
extern CPlayList* pl;
extern CMediaPlayerDlg* mp;
extern int plcnt;
extern int wavbit_sample_Hz;
extern int wavchannel;
extern int wavsam_depth;
extern GUID soundguid;
extern int tempo;
extern int pitch;

static CString s_compatNames[32];
static int s_compatRows[32];
static int s_compatN = 0;
static int s_chapterMenuRow = -1;

void MpFeatInitDefaults()
{
	if (savedata.mpKeyRoot < -1 || savedata.mpKeyRoot > 11)
		savedata.mpKeyRoot = -1;
	if (savedata.mpKeyMinor != 0) savedata.mpKeyMinor = 1;
	if (savedata.mpCamelot < 0 || savedata.mpCamelot > 24)
		savedata.mpCamelot = 0;
	if (savedata.mpMirrorGain <= 0 || savedata.mpMirrorGain > 200)
		savedata.mpMirrorGain = 100;
	if (savedata.mpRemoteGain <= 0 || savedata.mpRemoteGain > 200)
		savedata.mpRemoteGain = 100;
	if (savedata.mpAacProfile < 0 || savedata.mpAacProfile > 2)
		savedata.mpAacProfile = 0;
	if (savedata.confirmDanger != 0) savedata.confirmDanger = 1;
	if (savedata.mpFocusMode != 0) savedata.mpFocusMode = 1;
	if (savedata.mpNowPlayingFile != 0) savedata.mpNowPlayingFile = 1;
	if (savedata.mpMidiMapCc[0] == 0 && savedata.mpMidiMapCc[1] == 0
		&& savedata.mpMidiMapCc[2] == 0 && savedata.mpMidiMapCc[3] == 0) {
		savedata.mpMidiMapCc[0] = savedata.mpMidiMapCc[1] =
			savedata.mpMidiMapCc[2] = savedata.mpMidiMapCc[3] = -1;
	}
}

void MpFeatWriteNowPlaying()
{
	if (!savedata.mpNowPlayingFile) return;
	CString path;
	TCHAR dir[MAX_PATH] = {};
	if (::GetModuleFileName(NULL, dir, MAX_PATH) == 0) return;
	CString base(dir);
	const int slash = base.ReverseFind(_T('\\'));
	if (slash >= 0) base = base.Left(slash + 1);
	path = base + L"nowplaying.txt";
	CString title, artist;
	if (pl && plcnt >= 0 && plcnt < pl->playcnt) {
		title = pl->pc[plcnt].name;
		artist = pl->pc[plcnt].art;
		if (title.IsEmpty()) title = pl->pc[plcnt].fol;
	}
	CString line;
	if (!artist.IsEmpty())
		line.Format(L"%s - %s\r\n", (LPCTSTR)artist, (LPCTSTR)title);
	else
		line.Format(L"%s\r\n", (LPCTSTR)title);
	CFile f;
	if (f.Open(path, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite, NULL) == TRUE) {
		const CStringA utf8 = CW2A(line, CP_UTF8);
		f.Write((LPCSTR)utf8, utf8.GetLength());
		f.Close();
	}
}

CString MpFeatStatusLine()
{
	CString s;
	CString aac = savedata.mpRemoteOn
		? (savedata.mpRemoteAac ? L"AAC:ON" : L"AAC:off")
		: L"Remote:off";
	CString mic = savedata.mic_device[0] ? L"Mic:set" : L"Mic:—";
	CString rate;
	if (wavbit_sample_Hz > 0)
		rate.Format(L"%dHz/%dch/%dbit", wavbit_sample_Hz, wavchannel, wavsam_depth);
	else
		rate = L"—";
	CString key;
	if (savedata.mpCamelot > 0)
		key.Format(L"Key:%s", (LPCTSTR)MpCamelotLabel(savedata.mpCamelot));
	else if (savedata.mpKeyRoot >= 0)
		key = MpKeyDisplayName(savedata.mpKeyRoot, savedata.mpKeyMinor);
	else
		key = L"Key:—";
	s.Format(L"%s · %s · %s · %s", (LPCTSTR)rate, (LPCTSTR)mic, (LPCTSTR)aac, (LPCTSTR)key);
	return s;
}

BOOL MpFeatConfirmDanger(HWND owner, LPCTSTR whatJa)
{
	if (!savedata.confirmDanger) return TRUE;
	CString msg;
	msg.Format(LL14(L"%s\nよろしいですか？", L"%s\nAre you sure?", L"%s\nConfirmer ?", L"%s\nConfermi?",
		L"%s\n¿Seguro?", L"%s\n계속할까요?", L"%s\n确定吗？", L"%s\nهل أنت متأكد؟",
		L"%s\nПродолжить?", L"%s\nWirklich?", L"%s\nTem certeza?", L"%s\nWeet u het zeker?",
		L"%s\nNa pewno?", L"%s\nEmin misiniz?"),
		whatJa ? whatJa : L"");
	const int r = ::MessageBox(owner, msg,
		LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
			L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdź", L"Onayla"),
		MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
	return r == IDYES;
}

void MpFeatApplyFocusMode(CWnd* mpDlg, BOOL on)
{
	savedata.mpFocusMode = on ? 1 : 0;
	if (!mpDlg || !::IsWindow(mpDlg->GetSafeHwnd())) return;
	CMediaPlayerDlg* d = DYNAMIC_DOWNCAST(CMediaPlayerDlg, mpDlg);
	if (!d) return;
	if (on) {
		// 下部ツールを最小化し、リスト以外を畳むヒント
		if (!savedata.mpBotToolsInited) {
			savedata.mpBotToolsInited = 1;
			savedata.mpBotToolsFlags = 0x0F;
		}
		savedata.mpBotToolsFlags = 0; // hide bot shortcuts
		savedata.mpToolsOpen = 0;
	} else {
		if (savedata.mpBotToolsFlags == 0)
			savedata.mpBotToolsFlags = 0x0F;
	}
	d->DoLayout();
	d->Invalidate(FALSE);
}

void MpFeatLiveSetRecordStart(CWnd* parent)
{
	if (!parent) return;
	savedata.cap_with_audio = 1;
	OpenScreenCaptureModeless(parent);
	ScreenCaptureApplySavedataToUi(TRUE);
	OpenDeviceRecordModeless(parent);
}

void MpFeatBumpBotUse(int idx)
{
	if (idx < 0 || idx > 7) return;
	if (savedata.mpBotToolsUse[idx] < 1000000)
		savedata.mpBotToolsUse[idx]++;
}

void MpFeatApplyBotUseOrder()
{
	// 使用回数の多いビットを下位に寄せる並べ替えヒント（flags 自体は呼び出し側が保持）
	int order[8];
	for (int i = 0; i < 8; ++i) order[i] = i;
	for (int a = 0; a < 7; ++a) {
		for (int b = a + 1; b < 8; ++b) {
			if (savedata.mpBotToolsUse[order[b]] > savedata.mpBotToolsUse[order[a]]) {
				const int t = order[a]; order[a] = order[b]; order[b] = t;
			}
		}
	}
	int flags = 0;
	int bitPos = 0;
	const int old = savedata.mpBotToolsFlags;
	for (int i = 0; i < 8; ++i) {
		const int src = order[i];
		if (old & (1 << src))
			flags |= (1 << bitPos);
		++bitPos;
	}
	// 並び替えは表示順メタのみ。flags の意味ビットは壊さないため保存はしない。
	(void)flags;
}

void MpFeatAppendKeyMenu(CCustomPopupMenu& menu)
{
	CString cap;
	if (savedata.mpCamelot > 0)
		cap.Format(LL14(L"キー確定 (%s)", L"Capture key (%s)", L"Capturer clé (%s)", L"Cattura chiave (%s)",
			L"Capturar tonalidad (%s)", L"키 확정 (%s)", L"确定调性 (%s)", L"تأكيد المفتاح (%s)",
			L"Зафиксировать тональность (%s)", L"Tonart speichern (%s)", L"Capturar tom (%s)", L"Toonsoort vastleggen (%s)",
			L"Zapisz tonację (%s)", L"Ton kaydet (%s)"),
			(LPCTSTR)MpCamelotLabel(savedata.mpCamelot));
	else
		cap = LL14(L"キー確定（解析から）", L"Capture key (from analysis)", L"Capturer clé", L"Cattura chiave",
			L"Capturar tonalidad", L"키 확정", L"确定调性", L"تأكيد المفتاح", L"Зафиксировать тональность",
			L"Tonart speichern", L"Capturar tom", L"Toonsoort vastleggen", L"Zapisz tonację", L"Ton kaydet");
	menu.AddCommand(ID_MP_KEY_CAPTURE, cap);
	s_compatN = MpKeyFindCompatibleInPlaylist(32, s_compatNames, s_compatRows);
	if (s_compatN > 0) {
		CCustomPopupMenu* sub = menu.AddSubMenu(
			LL14(L"相性候補", L"Compatible tracks", L"Titres compatibles", L"Brani compatibili", L"Pistas compatibles",
				L"호환 곡", L"相容曲目", L"مقاطع متوافقة", L"Совместимые треки", L"Passende Tracks",
				L"Faixas compatíveis", L"Compatibele tracks", L"Kompatybilne utwory", L"Uyumlu parçalar"));
		if (sub) {
			for (int i = 0; i < s_compatN; ++i)
				sub->AddCommand(ID_MP_KEY_COMPAT_BASE + i, s_compatNames[i]);
		}
	}
}

BOOL MpFeatHandleKeyMenuCmd(UINT cmd)
{
	if (cmd == ID_MP_KEY_CAPTURE) {
		MpKeyCaptureFromLiveAnalysis();
		return TRUE;
	}
	if (cmd >= ID_MP_KEY_COMPAT_BASE && cmd <= ID_MP_KEY_COMPAT_LAST) {
		const int i = (int)(cmd - ID_MP_KEY_COMPAT_BASE);
		if (i >= 0 && i < s_compatN && pl) {
			const int row = s_compatRows[i];
			if (row >= 0 && row < pl->playcnt) {
				extern int plcnt;
				extern int gameon;
				extern void RequestPlaybackRestart(HWND hwnd);
				pl->Get(row);
				plcnt = row;
				gameon = 0;
				if (og && ::IsWindow(og->GetSafeHwnd()))
					RequestPlaybackRestart(og->GetSafeHwnd());
			}
		}
		return TRUE;
	}
	return FALSE;
}

void MpFeatSetChapter(int row, int chapter)
{
	if (!pl || row < 0 || row >= pl->playcnt) return;
	if (chapter < 0 || chapter > 3) chapter = 0;
	ProSongExtra e;
	ZeroMemory(&e, sizeof(e));
	CString list = SongParams_CurrentListName();
	const playlistdata0& it = pl->pc[row];
	if (!ProAudio_GetExtra(list, it.fol, it.sub, it.ret2, e)) {
		_tcsncpy(e.listName, list, 255);
		_tcsncpy(e.path, it.fol, 1023);
		e.mode = it.sub;
		e.ret2 = it.ret2;
		e.loopIn = e.loopOut = -1;
	}
	e.setChapter = chapter;
	ProAudio_UpsertExtra(e);
	ProAudio_SaveExtras();
}

int MpFeatGetChapter(int row)
{
	if (!pl || row < 0 || row >= pl->playcnt) return 0;
	ProSongExtra e;
	if (!ProAudio_GetExtra(SongParams_CurrentListName(), pl->pc[row].fol, pl->pc[row].sub, pl->pc[row].ret2, e))
		return 0;
	return e.setChapter;
}

void MpFeatAppendChapterMenu(CCustomPopupMenu& menu, int row)
{
	s_chapterMenuRow = row;
	const int ch = MpFeatGetChapter(row);
	CCustomPopupMenu* sub = menu.AddSubMenu(
		LL14(L"セット章", L"Set chapter", L"Chapitre", L"Capitolo", L"Capítulo",
			L"세트 구간", L"套章节", L"فصل المجموعة", L"Глава сета", L"Set-Kapitel",
			L"Capítulo do set", L"Set-hoofdstuk", L"Rozdział setu", L"Set bölümü"));
	if (!sub) return;
	sub->AddCheck(ID_MP_CH_NONE, LL14(L"なし", L"None", L"Aucun", L"Nessuno", L"Ninguno", L"없음", L"无", L"لا شيء", L"Нет", L"Keine", L"Nenhum", L"Geen", L"Brak", L"Yok"), ch == 0);
	sub->AddCheck(ID_MP_CH_WARM, L"Warmup", ch == 1);
	sub->AddCheck(ID_MP_CH_PEAK, L"Peak", ch == 2);
	sub->AddCheck(ID_MP_CH_COOL, L"Cooldown", ch == 3);
}

BOOL MpFeatHandleChapterMenuCmd(UINT cmd, int row)
{
	if (row < 0) row = s_chapterMenuRow;
	if (cmd == ID_MP_CH_NONE) { MpFeatSetChapter(row, 0); return TRUE; }
	if (cmd == ID_MP_CH_WARM) { MpFeatSetChapter(row, 1); return TRUE; }
	if (cmd == ID_MP_CH_PEAK) { MpFeatSetChapter(row, 2); return TRUE; }
	if (cmd == ID_MP_CH_COOL) { MpFeatSetChapter(row, 3); return TRUE; }
	return FALSE;
}

void MpFeatOnSongStartedHooks()
{
	MpFeatWriteNowPlaying();
	// 解析キーがあれば自動スナップショット（未保存時のみ上書きしないよう Camelot 未設定時）
	if (savedata.mpCamelot <= 0)
		MpKeyCaptureFromLiveAnalysis();
}

void MpFeatEnsureRemoteOverlayHtml(CString& htmlOut)
{
	htmlOut =
		L"<!DOCTYPE html><html><head><meta charset=utf-8><title>overlay</title>"
		L"<style>html,body{margin:0;background:transparent;overflow:hidden;font-family:Segoe UI,sans-serif;color:#fff;"
		L"text-shadow:0 1px 3px #000}#box{padding:12px 16px}#t{font-size:22px;font-weight:600}#a{font-size:14px;opacity:.85}"
		L"#s{font-size:12px;margin-top:6px;opacity:.7}</style></head><body><div id=box>"
		L"<div id=t>—</div><div id=a></div><div id=s></div></div>"
		L"<script>async function tick(){try{const r=await fetch('/api/status');const d=await r.json();"
		L"document.getElementById('t').textContent=d.title||'—';"
		L"document.getElementById('a').textContent=d.artist||'';"
		L"document.getElementById('s').textContent=(d.state||'')+' '+(d.key||'');}catch(e){}"
		L"setTimeout(tick,1000)}tick()</script></body></html>";
}

int MpFeatAacBytesPerSec()
{
	// ADTS 概算: kbps/8
	int kbps = 128;
	if (savedata.mpAacProfile == 1) kbps = 192;
	else if (savedata.mpAacProfile == 2) kbps = 96;
	return (kbps * 1000) / 8;
}

float MpFeatMirrorGainLin()
{
	int g = savedata.mpMirrorGain;
	if (g <= 0) g = 100;
	if (g > 200) g = 200;
	return (float)g / 100.f;
}

float MpFeatRemoteGainLin()
{
	int g = savedata.mpRemoteGain;
	if (g <= 0) g = 100;
	if (g > 200) g = 200;
	return (float)g / 100.f;
}
