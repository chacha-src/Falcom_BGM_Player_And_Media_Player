#include "stdafx.h"
#include "ogg.h"
#include "CSasamiVstPartMenu.h"
#include "CSasamiVstPickDlg.h"
#include "CSasamiToneMapDlg.h"
#include "CCustomPopupMenu.h"
#include "VstMidiEngine.h"

/* Local command ids — must not collide with VstHostDlg (0xe7xx). */
enum {
	SC_VST_POP_EDITOR = 0xf713,
	SC_VST_POP_CHANGE = 0xf714,
	SC_VST_POP_MEDIABAY_HINT = 0xf715,
	SC_VST_POP_CLEAR = 0xf711,
	SC_VST_POP_CH_ASIS = 0xf720,
	SC_VST_POP_CH_FIRST = 0xf721, /* +0..15 */
	SC_VST_POP_PROG_FIRST = 0xf740,
	SC_VST_PROG_MENU_MAX = 64
};

static void ScVstApplyBindPath(ScMidiVstBind* bind, int part1to32, const wchar_t* path)
{
	if (!bind || part1to32 < 1 || part1to32 > 32 || !path) return;
	wcsncpy_s(bind->vstPath[part1to32 - 1], path, _TRUNCATE);
	bind->isMpw3 = 1;
}

static void ScVstClearBind(ScMidiVstBind* bind, int part1to32)
{
	if (!bind || part1to32 < 1 || part1to32 > 32) return;
	bind->vstPath[part1to32 - 1][0] = 0;
	bind->vstProg[part1to32 - 1] = -1;
	bind->vstBankMsb[part1to32 - 1] = -1;
	bind->vstBankLsb[part1to32 - 1] = -1;
	bind->vstForceCh[part1to32 - 1] = -1;
}

static int ScVstSamePathLoaded(int part1to32, const wchar_t* path)
{
	if (!VstLivePartIsLoaded(part1to32) || !path || !path[0]) return 0;
	wchar_t cur[520];
	cur[0] = 0;
	if (!VstLivePartGetPath(part1to32, cur, 520) || !cur[0]) return 0;
	return _wcsicmp(cur, path) == 0 ? 1 : 0;
}

static int ScVstPathIsDedicated(const wchar_t* path)
{
	if (!path || !path[0]) return 0;
	return VstDetectMultiTimbral(path) ? 0 : 1;
}

static int ScVstLoadPicked(CWnd* owner, int part1to32, ScMidiVstBind* bind)
{
	wchar_t path[520];
	int is3 = 0;
	if (CSasamiVstPickDlg::PickForPart(owner, part1to32, path, 520, &is3) != IDOK)
		return 0;
	if (::GetCapture()) ::ReleaseCapture();

	/* Same path already live — no reload (avoids HALion 30s cold restart). */
	if (ScVstSamePathLoaded(part1to32, path)) {
		ScVstApplyBindPath(bind, part1to32, path);
		return 1;
	}

	/* LoadPart already replaces the slot. Do NOT call VstLiveUnloadPart first:
	   remote UnloadPart always LiveRemoteStop() → full host audio teardown →
	   HALion MediaBay cold start (~30s) and empty program list. */
	const wchar_t* waitName = path;
	if (const wchar_t* slash = wcsrchr(path, L'\\')) waitName = slash + 1;
	HWND waitOwner = owner ? owner->GetSafeHwnd() : NULL;
	if (!waitOwner) {
		if (CWnd* main = AfxGetMainWnd()) waitOwner = main->GetSafeHwnd();
	}
	/* Preview/monitor must not hold Host64 SHM while SC-VA loads. */
	VstLiveMonitorStop();
	VstWaitShowLoad(waitOwner, waitName);
	const int rc = VstLiveLoadPart(part1to32, path, is3);
	VstWaitHide();
	if (rc != 0) {
		AfxMessageBox(LL14(
			L"VSTの初期化に失敗／タイムアウトしました。\nMediaBayの警告が出ていれば先に閉じてください。",
			L"VST init failed or timed out.\nIf a MediaBay warning is open, dismiss it first.",
			L"Échec/timeout init VST. Fermez d’abord MediaBay si besoin.",
			L"Init VST fallita/timeout. Chiudi prima MediaBay se compare.",
			L"Falló/timeout init VST. Cierre MediaBay si aparece.",
			L"VST 초기화 실패/시간 초과. MediaBay 경고가 있으면 먼저 닫으세요.",
			L"VST初始化失败或超时。如有MediaBay警告请先关闭。",
			L"فشل/انتهت مهلة تهيئة VST.",
			L"Инициализация VST не удалась или таймаут.",
			L"VST-Init fehlgeschlagen/Timeout. MediaBay-Hinweis zuerst schließen.",
			L"Falha/timeout na init VST.",
			L"VST-init mislukt/time-out.",
			L"Init VST nieudana/timeout.",
			L"VST başlatma başarısız/zaman aşımı."), MB_ICONWARNING);
		return 0;
	}
	ScVstApplyBindPath(bind, part1to32, path);
	if (bind) {
		/* Dedicated (ST/GA/HALion…): plugin hears ch0. Live part index selects
		   which instance — do NOT set sendCh=part-1 (that silenced GA/HL).
		   Multi (SC-VA): keep incoming channel (-1). */
		if (ScVstPathIsDedicated(path)) {
			VstLiveSetSendChannel(part1to32, 0);
			bind->vstForceCh[part1to32 - 1] = 0;
		} else {
			VstLiveSetSendChannel(part1to32, -1);
			bind->vstForceCh[part1to32 - 1] = -1;
		}
	}
	VstLiveMonitorEnsure();
	return 1;
}

int ScVstPickLoadForPart(CWnd* owner, int part1to32, ScMidiVstBind* bind)
{
	return ScVstLoadPicked(owner, part1to32, bind);
}

static int ScVstTryLoadPath(CWnd* owner, int part1to32, ScMidiVstBind* bind, const wchar_t* path)
{
	if (!path || !path[0]) return 0;
	if (ScVstSamePathLoaded(part1to32, path)) {
		ScVstApplyBindPath(bind, part1to32, path);
		return 1;
	}
	const size_t len = wcslen(path);
	const int is3 = (len >= 5 && _wcsicmp(path + len - 5, L".vst3") == 0) ? 1 : 0;
	const wchar_t* waitName = path;
	if (const wchar_t* slash = wcsrchr(path, L'\\')) waitName = slash + 1;
	HWND waitOwner = owner ? owner->GetSafeHwnd() : NULL;
	VstLiveMonitorStop();
	VstWaitShowLoad(waitOwner, waitName);
	const int rc = VstLiveLoadPart(part1to32, path, is3);
	VstWaitHide();
	if (rc != 0) return 0;
	ScVstApplyBindPath(bind, part1to32, path);
	if (bind) {
		if (ScVstPathIsDedicated(path)) {
			VstLiveSetSendChannel(part1to32, 0);
			bind->vstForceCh[part1to32 - 1] = 0;
		} else {
			VstLiveSetSendChannel(part1to32, -1);
			bind->vstForceCh[part1to32 - 1] = -1;
		}
	}
	VstLiveMonitorEnsure();
	return 1;
}

int ScVstShowPartMenu(CWnd* owner, int part1to32, CPoint screenPt, ScMidiVstBind* bind)
{
	if (!owner || part1to32 < 1 || part1to32 > 32) return 0;
	if (!VstLivePartIsLoaded(part1to32)) return 0;

	const int curCh = VstLiveSendChannel(part1to32);
	const int curProg = VstLiveProgramCurrent(part1to32);
	int progCount = VstLiveProgramCount(part1to32);
	if (progCount > SC_VST_PROG_MENU_MAX) progCount = SC_VST_PROG_MENU_MAX;

	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	menu.AddCommand(SC_VST_POP_EDITOR, LL14(
		L"設定画面を開く", L"Open editor", L"Ouvrir l'éditeur", L"Apri l'editor", L"Abrir el editor",
		L"설정 화면 열기", L"打开设置界面", L"فتح واجهة الإعدادات", L"Открыть редактор", L"Editor öffnen",
		L"Abrir o editor", L"Editor openen", L"Otwórz edytor", L"Arayüzü aç"));
	menu.AddCommand(SC_VST_POP_CHANGE, LL14(
		L"音色を変更…", L"Change instrument…", L"Changer de timbre…", L"Cambia strumento…", L"Cambiar instrumento…",
		L"음색 변경…", L"更改音色…", L"تغيير الطابع…", L"Сменить тембр…", L"Klang wechseln…",
		L"Mudar instrumento…", L"Instrument wijzigen…", L"Zmień instrument…", L"Enstrüman değiştir…"));
	menu.AddSeparator();

	CCustomPopupMenu* chMenu = menu.AddSubMenu(LL14(
		L"プラグインに渡すチャンネル", L"Channel sent to the plug-in", L"Canal envoyé au plug-in",
		L"Canale inviato al plug-in", L"Canal enviado al plug-in", L"플러그인에 보낼 채널", L"发送到插件的通道",
		L"القناة المرسلة إلى الإضافة", L"Канал, отправляемый плагину", L"An das Plug-in gesendeter Kanal",
		L"Canal enviado ao plug-in", L"Kanaal naar de plug-in", L"Kanał wysyłany do wtyczki", L"Eklentiye gönderilen kanal"));
	if (chMenu) {
		chMenu->AddCheck(SC_VST_POP_CH_ASIS, LL14(
			L"受信したチャンネルのまま", L"Keep the received channel", L"Conserver le canal reçu",
			L"Mantieni il canale ricevuto", L"Mantener el canal recibido", L"수신한 채널 그대로", L"保持接收到的通道",
			L"الإبقاء على القناة المستلمة", L"Оставить принятый канал", L"Empfangenen Kanal beibehalten",
			L"Manter o canal recebido", L"Ontvangen kanaal behouden", L"Zachowaj odebrany kanał", L"Alınan kanalı koru"),
			curCh < 0);
		for (int c = 0; c < 16; ++c) {
			CString s;
			s.Format(L"ch %d%s", c + 1, c == 9 ? L"  (drums)" : L"");
			chMenu->AddCheck(SC_VST_POP_CH_FIRST + c, s, curCh == c);
		}
	}

	CCustomPopupMenu* progMenu = progCount > 0
		? menu.AddSubMenu(LL14(
			L"プログラム / キット", L"Program / kit", L"Programme / kit", L"Programma / kit", L"Programa / kit",
			L"프로그램 / 킷", L"程序 / 音色组", L"البرنامج / الطقم", L"Программа / кит", L"Programm / Kit",
			L"Programa / kit", L"Programma / kit", L"Program / zestaw", L"Program / kit"))
		: NULL;
	if (progMenu) {
		const int stride = 64;
		wchar_t buf[SC_VST_PROG_MENU_MAX * 64];
		memset(buf, 0, sizeof(buf));
		const int got = VstLiveProgramNames(part1to32, 0, progCount, buf, stride);
		for (int i = 0; i < got; ++i) {
			CString s;
			s.Format(L"%3d  %s", i + 1, buf + (size_t)i * stride);
			progMenu->AddCheck(SC_VST_POP_PROG_FIRST + i, s, i == curProg);
		}
	} else {
		menu.AddCommand(SC_VST_POP_MEDIABAY_HINT, LL14(
			L"（一覧0件 — MediaBayは設定画面内）", L"(0 programs — use MediaBay in editor)",
			L"(0 programmes — MediaBay dans l'éditeur)", L"(0 programmi — MediaBay nell'editor)",
			L"(0 programas — MediaBay en el editor)", L"(목록 0 — 편집기 MediaBay)",
			L"(列表0 — 请在编辑器 MediaBay)", L"(0 — MediaBay in editor)",
			L"(0 программ — MediaBay в редакторе)", L"(0 Programme — MediaBay im Editor)",
			L"(0 programas — MediaBay no editor)", L"(0 programma's — MediaBay in editor)",
			L"(0 programów — MediaBay w edytorze)", L"(0 program — editörde MediaBay)"));
	}

	menu.AddSeparator();
	menu.AddCommand(SC_VST_POP_CLEAR, LL14(
		L"このパートの音色を解除", L"Clear this part's tone", L"Effacer le timbre", L"Azzera il timbro",
		L"Borrar el timbre", L"이 파트의 음색 해제", L"清除此声部音色", L"مسح طابع هذا الجزء",
		L"Сбросить тембр части", L"Klang dieser Spur leeren", L"Limpar timbre", L"Klank wissen",
		L"Wyczyść barwę", L"Bu partinin tınısını kaldır"));

	const UINT cmd = menu.Track(screenPt, owner);
	if (!cmd) return 0;
	int changed = 0;
	if (cmd == SC_VST_POP_EDITOR || cmd == SC_VST_POP_MEDIABAY_HINT) {
		VstLiveEditorOpenAsync(part1to32);
		return 1;
	} else if (cmd == SC_VST_POP_CHANGE) {
		if (ScVstLoadPicked(owner, part1to32, bind))
			changed = 1;
	} else if (cmd == SC_VST_POP_CLEAR) {
		VstLiveUnloadPart(part1to32);
		ScVstClearBind(bind, part1to32);
		changed = 1;
	} else if (cmd == SC_VST_POP_CH_ASIS) {
		VstLiveSetSendChannel(part1to32, -1);
		if (bind) bind->vstForceCh[part1to32 - 1] = -1;
		changed = 1;
	} else if (cmd >= SC_VST_POP_CH_FIRST && cmd < SC_VST_POP_CH_FIRST + 16) {
		const int ch = (int)(cmd - SC_VST_POP_CH_FIRST);
		VstLiveSetSendChannel(part1to32, ch);
		if (bind) bind->vstForceCh[part1to32 - 1] = ch;
		changed = 1;
	} else if (cmd >= SC_VST_POP_PROG_FIRST &&
		cmd < SC_VST_POP_PROG_FIRST + SC_VST_PROG_MENU_MAX) {
		const int prog = (int)(cmd - SC_VST_POP_PROG_FIRST);
		VstLiveSetProgram(part1to32, prog);
		if (bind) bind->vstProg[part1to32 - 1] = prog;
		changed = 1;
	}
	return changed;
}

int ScVstAssignToneForPart(CWnd* owner, int part1to32, ScMidiVstBind* bind)
{
	if (!owner || part1to32 < 1 || part1to32 > 32) return 0;

	const wchar_t* bindPath = (bind && bind->vstPath[part1to32 - 1][0])
		? bind->vstPath[part1to32 - 1] : L"";
	const int bindDedicated = ScVstPathIsDedicated(bindPath);

	if (!VstLivePartIsLoaded(part1to32)) {
		/* Song data with dedicated VST3 on this part wins over GS/XG default. */
		int loaded = 0;
		if (bindDedicated)
			loaded = ScVstTryLoadPath(owner, part1to32, bind, bindPath);
		if (!loaded) {
			wchar_t pref[520];
			pref[0] = 0;
			if (VstPickPreferredPlugin(pref, 520) && pref[0])
				loaded = ScVstTryLoadPath(owner, part1to32, bind, pref);
		}
		if (!loaded && !ScVstLoadPicked(owner, part1to32, bind))
			return 0;
	} else if (bindDedicated && !ScVstSamePathLoaded(part1to32, bindPath)) {
		/* Data says HALion/etc. for this part — switch off shared GS/XG. */
		if (!ScVstTryLoadPath(owner, part1to32, bind, bindPath))
			return 0;
	} else if (bind && !bind->vstPath[part1to32 - 1][0]) {
		wchar_t path[520];
		if (VstLivePartGetPath(part1to32, path, 520) && path[0])
			ScVstApplyBindPath(bind, part1to32, path);
	}

	wchar_t path[520];
	path[0] = 0;
	VstLivePartGetPath(part1to32, path, 520);
	/* Heal isMulti: tone map uses DetectMultiTimbral(path), but auto-editor
	   used only IsMulti — mismatch opened SC-VA VST2 editor and froze on OK. */
	int multi = VstLivePartIsMulti(part1to32) ||
		(path[0] && VstDetectMultiTimbral(path));

	if (multi) {
		/* SC-VA / GS/XG multi → tone map; VST3… inside can switch to dedicated. */
		const int r = CSasamiToneMapDlg::PickForPart(owner, part1to32, bind);
		if (r != IDOK)
			return 0;
		path[0] = 0;
		VstLivePartGetPath(part1to32, path, 520);
		multi = VstLivePartIsMulti(part1to32) ||
			(path[0] && VstDetectMultiTimbral(path));
		if (multi)
			return 1; /* tone map applied — never open VST2 editor */
		/* Fell through to dedicated after VST3 pick inside tone map. */
		if (bind)
			bind->isMpw3 = 1;
		return 2;
	}

	/* Dedicated: load only. No MonitorEnsure here — waveOut+Render during HALion
	   Home crashes ogg (probe: Host64 alone OK; Host dialog skips MonitorEnsure). */
	if (bind)
		bind->isMpw3 = 1;
	(void)owner;
	return 2;
}
