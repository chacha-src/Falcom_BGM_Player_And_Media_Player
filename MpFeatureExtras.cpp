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
	menu.AddCommand(ID_MP_KEY_CAPTURE, cap,
		LL14(L"現在の解析キー(Camelot)を曲に確定保存。相性検索やDJセット編成に使います。", L"Save the analyzed Camelot key to this track for compatibility search and DJ set building.", L"Enregistre la cle Camelot analysee pour recherche de compatibilite et sets DJ.", L"Salva la chiave Camelot analizzata per ricerca compatibilita e set DJ.", L"Guarda la tonalidad Camelot analizada para busqueda de compatibilidad y sets DJ.",
			L"현재 분석 키(Camelot)를 곡에 확정 저장. 호환 검색·DJ 세트 구성에 사용.", L"将当前分析的 Camelot 调性保存到曲目，用于相容搜索和 DJ 套曲。", L"حفظ مفتاح Camelot المحلَّل للمسار لبحث التوافق وبناء مجموعات DJ.", L"Сохранить проанализированный ключ Camelot для поиска совместимости и DJ-сетов.", L"Analysierte Camelot-Tonart speichern für Kompatibilitäts-Suche und DJ-Sets.",
			L"Salvar a tonalidade Camelot analisada para busca de compatibilidade e sets DJ.", L"Sla de geanalyseerde Camelot-toonsoort op voor compatibiliteitszoeken en DJ-sets.", L"Zapisz przeanalizowana tonacje Camelot do wyszukiwania zgodnosci i setow DJ.", L"Analiz edilen Camelot tonunu parcaya kaydet; uyum arama ve DJ seti icin."));
	s_compatN = MpKeyFindCompatibleInPlaylist(32, s_compatNames, s_compatRows);
	if (s_compatN > 0) {
		CCustomPopupMenu* sub = menu.AddSubMenu(
			LL14(L"相性候補", L"Compatible tracks", L"Titres compatibles", L"Brani compatibili", L"Pistas compatibles",
				L"호환 곡", L"相容曲目", L"مقاطع متوافقة", L"Совместимые треки", L"Passende Tracks",
				L"Faixas compatíveis", L"Compatibele tracks", L"Kompatybilne utwory", L"Uyumlu parçalar"),
			LL14(L"現在キーと調和しやすい曲をプレイリストから列挙。選ぶとその曲へジャンプ再生。", L"List playlist tracks that harmonize with the current key. Pick one to jump and play it.", L"Liste les titres compatibles avec la cle actuelle. Choisir pour lire ce titre.", L"Elenca brani armonici con la chiave attuale. Seleziona per riprodurre.", L"Lista pistas que armonizan con la tonalidad actual. Elige una para reproducir.",
				L"현재 키와 어울리는 곡을 재생목록에서 나열. 선택하면 그 곡으로 점프 재생.", L"列出与当前调性和谐的播放列表曲目。选择即跳转播放。", L"سرد المقاطع المتوافقة مع المفتاح الحالي. اختر واحداً للتشغيل.", L"Список треков, гармонирующих с текущим ключом. Выбор — переход и воспроизведение.", L"Listet Tracks, die zur aktuellen Tonart passen. Auswahl springt und spielt ab.",
				L"Lista faixas que harmonizam com a tonalidade atual. Escolha uma para pular e tocar.", L"Lijst tracks die bij de huidige toonsoort passen. Kies er een om te springen en af te spelen.", L"Lista utworow zgodnych z biezaca tonacja. Wybor skacze i odtwarza.", L"Gecerli tonla uyumlu parcaylari listeler. Secince o parcaya atlayip calar."));
		if (sub) {
			for (int i = 0; i < s_compatN; ++i)
				sub->AddCommand(ID_MP_KEY_COMPAT_BASE + i, s_compatNames[i],
					LL14(L"この相性候補の曲へジャンプして再生します。", L"Jump to this compatible track and play it.", L"Aller a ce titre compatible et le lire.", L"Vai a questo brano compatibile e riproducilo.", L"Ir a esta pista compatible y reproducirla.",
						L"이 호환 후보 곡으로 점프하여 재생합니다.", L"跳到该相容曲目并播放。", L"الانتقال إلى هذا المقطع المتوافق وتشغيله.", L"Перейти к этому совместимому треку и воспроизвести.", L"Zu diesem passenden Track springen und abspielen.",
						L"Pular para esta faixa compativel e reproduzi-la.", L"Spring naar dit compatibele nummer en speel het af.", L"Skocz do tego zgodnego utworu i odtworz.", L"Bu uyumlu parcaya atlayip cal."));
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
			L"Capítulo do set", L"Set-hoofdstuk", L"Rozdział setu", L"Set bölümü"),
		LL14(L"ライブセット内の役割(導入/ピーク/締め)を曲に付けます。構成メモ用。", L"Tag this track's role in a live set (warmup/peak/cooldown). For set planning notes.", L"Marque le role dans le set (warmup/peak/cooldown). Aide a planifier.", L"Assegna il ruolo nel set (warmup/peak/cooldown). Per pianificare.", L"Marca el rol en el set (warmup/peak/cooldown). Para planificar.",
			L"라이브 세트 내 역할(도입/피크/마무리)을 곡에 붙입니다. 구성 메모용.", L"为曲目标记现场套曲角色（预热/高潮/收尾），便于编排笔记。", L"وسم دور المقطع في المجموعة (تسخين/ذروة/تهدئة) لملاحظات التخطيط.", L"Пометить роль трека в сете (разогрев/пик/спад) для планирования.", L"Rolle im Live-Set markieren (Warmup/Peak/Cooldown) als Planungshilfe.",
			L"Marcar o papel no set ao vivo (warmup/peak/cooldown) para planejamento.", L"Markeer de rol in de live-set (warmup/peak/cooldown) als planningsnotitie.", L"Oznacz role w secie (warmup/peak/cooldown) jako notatka planu.", L"Canli settteki rolu (warmup/peak/cooldown) parcaya isaretle; plan notu icin."));
	if (!sub) return;
	sub->AddCheck(ID_MP_CH_NONE, LL14(L"なし", L"None", L"Aucun", L"Nessuno", L"Ninguno", L"없음", L"无", L"لا شيء", L"Нет", L"Keine", L"Nenhum", L"Geen", L"Brak", L"Yok"), ch == 0,
		LL14(L"セット章タグを外します（未分類）。", L"Clear the set-chapter tag (uncategorized).", L"Retire le tag de chapitre (non classe).", L"Rimuove il tag capitolo (non classificato).", L"Quita la etiqueta de capitulo (sin clasificar).",
			L"세트 구간 태그를 제거합니다(미분류).", L"清除套章节标签（未分类）。", L"إزالة وسم فصل المجموعة (غير مصنَّف).", L"Снять тег главы сета (без категории).", L"Set-Kapitel-Tag entfernen (unkategorisiert).",
			L"Remover a etiqueta de capitulo do set (sem categoria).", L"Verwijder de set-hoofdstuk-tag (ongecategoriseerd).", L"Usun tag rozdzialu setu (bez kategorii).", L"Set bolumu etiketini kaldir (siniflandirilmamis)."));
	sub->AddCheck(ID_MP_CH_WARM, L"Warmup", ch == 1,
		LL14(L"導入・ウォームアップ向きとしてマークします。", L"Mark as warmup / opening section.", L"Marquer comme warmup / ouverture.", L"Segna come warmup / apertura.", L"Marcar como warmup / apertura.",
			L"도입·워밍업용으로 표시합니다.", L"标记为预热/开场段落。", L"وسم كتمهيد / افتتاح.", L"Пометить как разогрев / открытие.", L"Als Warmup / Eröffnung markieren.",
			L"Marcar como warmup / abertura.", L"Markeren als warmup / opening.", L"Oznacz jako warmup / otwarcie.", L"Warmup / acilis olarak isaretle."));
	sub->AddCheck(ID_MP_CH_PEAK, L"Peak", ch == 2,
		LL14(L"ピーク・盛り上がり向きとしてマークします。", L"Mark as peak / climax section.", L"Marquer comme peak / climax.", L"Segna come peak / climax.", L"Marcar como peak / climax.",
			L"피크·클라이맥스용으로 표시합니다.", L"标记为高潮段落。", L"وسم كذروة / climax.", L"Пометить как пик / кульминацию.", L"Als Peak / Höhepunkt markieren.",
			L"Marcar como peak / climax.", L"Markeren als peak / climax.", L"Oznacz jako peak / climax.", L"Peak / doruk olarak isaretle."));
	sub->AddCheck(ID_MP_CH_COOL, L"Cooldown", ch == 3,
		LL14(L"締め・クールダウン向きとしてマークします。", L"Mark as cooldown / closing section.", L"Marquer comme cooldown / cloture.", L"Segna come cooldown / chiusura.", L"Marcar como cooldown / cierre.",
			L"마무리·쿨다운용으로 표시합니다.", L"标记为收尾/冷却段落。", L"وسم كتهدئة / ختام.", L"Пометить как спад / завершение.", L"Als Cool-down / Abschluss markieren.",
			L"Marcar como cooldown / encerramento.", L"Markeren als cooldown / afsluiting.", L"Oznacz jako cooldown / zakonczenie.", L"Cooldown / kapanis olarak isaretle."));
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
