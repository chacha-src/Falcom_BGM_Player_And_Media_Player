#include "stdafx.h"
#include "CPromptEngine.h"
#include "CMediaPlayerDlg.h"
#include "CEqualizer.h"
#include "oggDlg.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <mutex>

extern save savedata;
extern COggDlg* og;
extern __int64 playb;
extern int loop1, loop2;
extern int plf;
extern int wavbit_sample_Hz, wavchannel;
extern int tempo, pitch;
extern std::mutex cl2;

namespace {

enum MpPromptCmd {
	CMD_NONE = 0,
	CMD_PITCH,
	CMD_TEMPO,
	CMD_DSVOL,
	CMD_EQ_BAND0,
	CMD_EQ_MASTER = CMD_EQ_BAND0 + 15,
	CMD_EQ_SENMEI,
	CMD_EQ_KOUTEI,
	CMD_EQ_MITSUDO,
	CMD_EQ_RITTAI,
	CMD_EQ_REVERB,
	CMD_EQ_CHORUS,
	CMD_EQ_DELAY,
	CMD_EQ_ENV,      // 環境番号 (eqsoundenv)
	CMD_EQ_EFFECT,   // 環境のかかり具合 (eqsoundeffect)
	CMD_PRESET_SB,
	CMD_PRESET_BR,
	CMD_PRESET_SL,
	CMD_PRESET_FA,
};

struct MpPromptEvent {
	MpPromptCmd cmd;
	double t0;
	double t1;
	int v0;
	int v1;
};

static std::vector<MpPromptEvent> g_events;
static std::vector<int> g_eventsByCmd[CMD_PRESET_FA + 1];
static int g_lastAppliedVal[CMD_PRESET_FA + 1];
static BOOL g_lastAppliedValid[CMD_PRESET_FA + 1];
static DWORD g_lastUiSyncTick = 0;
static BOOL g_uiDirty = FALSE;
static BOOL g_active = FALSE;
static BOOL g_hasBackup = FALSE;
static MpPromptBackup g_backup;
static int g_lastPresetIdx = -1;
static int g_lastPlf = 0;
static BOOL g_promptAwaitPlayStart = FALSE;
static BOOL g_promptExecuteBeforePlay = FALSE;
static double g_promptEventCutoff = -1.0;
static BOOL g_promptNewTrack = FALSE;

static void MpPromptResetPlaybackState()
{
	g_lastPlf = 0;
	g_promptAwaitPlayStart = FALSE;
	g_promptExecuteBeforePlay = FALSE;
	g_promptEventCutoff = -1.0;
}

static int DsVolFromPercent(int pct)
{
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	int v = (int)((double)pct * 5.0 - 499.0 + 0.5);
	if (v < -498) v = -498;
	if (v > 1) v = 1;
	if (v == 0) v = 1;
	return v;
}

static int DsVolToPercent(int v)
{
	return (int)(((double)v + 499.0) * 2.0 / 10.0 + 0.5);
}

static int PitchTempoSlFromPercent(int pct)
{
	if (pct < 0) pct = 0;
	if (pct > 200) pct = 200;
	return pct * 2;
}

static BOOL ParseTimeToken(const CString& s, int& pos, double& outSec)
{
	const int n = s.GetLength();
	while (pos < n && s[pos] == ' ') pos++;
	if (pos >= n) return FALSE;
	int start = pos;
	int colon = -1;
	for (int i = start; i < n; ++i) {
		if (s[i] == ':') { colon = i; break; }
		if (!_istdigit(s[i])) break;
	}
	if (colon >= 0) {
		int min = _tstoi(s.Mid(start, colon - start));
		int secStart = colon + 1;
		int secEnd = secStart;
		while (secEnd < n && _istdigit(s[secEnd])) secEnd++;
		if (secEnd == secStart) return FALSE;
		int sec = _tstoi(s.Mid(secStart, secEnd - secStart));
		if (min < 0) min = 0;
		if (sec < 0) sec = 0;
		outSec = (double)min * 60.0 + (double)sec;
		pos = secEnd;
		return TRUE;
	}
	while (pos < n && _istdigit(s[pos])) pos++;
	if (pos == start) return FALSE;
	outSec = (double)_tstoi(s.Mid(start, pos - start));
	return TRUE;
}

static BOOL ParseValueRange(const CString& s, int& pos, int& v0, int& v1)
{
	const int n = s.GetLength();
	while (pos < n && s[pos] == ' ') pos++;
	if (pos >= n || s[pos] != '[') return FALSE;
	pos++;
	int a0 = pos;
	while (pos < n && s[pos] != ']' && s[pos] != '-') pos++;
	if (pos <= a0) return FALSE;
	v0 = _tstoi(s.Mid(a0, pos - a0));
	if (pos < n && s[pos] == '-') {
		pos++;
		int a1 = pos;
		while (pos < n && s[pos] != ']') pos++;
		v1 = _tstoi(s.Mid(a1, pos - a1));
	}
	else {
		v1 = v0;
	}
	while (pos < n && s[pos] != ']') pos++;
	if (pos < n && s[pos] == ']') pos++;
	return TRUE;
}

static MpPromptCmd CmdFromLetters(TCHAR c1, TCHAR c2, BOOL& used2)
{
	used2 = FALSE;
	const TCHAR l1 = (TCHAR)towlower(c1);
	const TCHAR l2 = c2 ? (TCHAR)towlower(c2) : 0;
	if (c2) {
		if (l1 == 's' && l2 == 'b') { used2 = TRUE; return CMD_PRESET_SB; }
		if (l1 == 'b' && l2 == 'r') { used2 = TRUE; return CMD_PRESET_BR; }
		if (l1 == 's' && l2 == 'l') { used2 = TRUE; return CMD_PRESET_SL; }
		if (l1 == 'f' && l2 == 'a') { used2 = TRUE; return CMD_PRESET_FA; }
	}
	// EQ項目(大文字): 周波数帯 a-o(小文字) と衝突しない
	if (c1 == 'M') return CMD_EQ_MASTER;
	if (c1 == 'N') return CMD_EQ_SENMEI;
	if (c1 == 'K') return CMD_EQ_KOUTEI;
	if (c1 == 'I') return CMD_EQ_MITSUDO;
	if (c1 == 'S') return CMD_EQ_RITTAI;
	if (c1 == 'E') return CMD_EQ_ENV;      // 環境番号 (小文字 e=160Hz帯と区別)
	if (c1 == 'F') return CMD_EQ_EFFECT;   // かかり具合
	if (l1 == 'p') return CMD_PITCH;
	if (l1 == 't') return CMD_TEMPO;
	if (l1 == 'd') return CMD_DSVOL;
	if (l1 == 'r') return CMD_EQ_REVERB;
	if (l1 == 'c') return CMD_EQ_CHORUS;
	if (l1 == 'y') return CMD_EQ_DELAY;
	// EQ周波数帯 15帯: a=25Hz … o=16kHz (小文字)
	if (l1 >= 'a' && l1 <= 'o') return (MpPromptCmd)(CMD_EQ_BAND0 + (l1 - 'a'));
	// 互換: 小文字 s=立体 (sl/sb プリセットは上で2文字判定済み)
	if (l1 == 's') return CMD_EQ_RITTAI;
	return CMD_NONE;
}

static BOOL ParseOnePrompt(const CString& line, int startPos, CString* errMsg)
{
	const int n = line.GetLength();
	int pos = startPos;
	while (pos < n && line[pos] != '@') pos++;
	if (pos >= n) return FALSE;
	pos++;
	if (pos >= n) {
		if (errMsg) *errMsg = LL14(L"コマンド文字がありません。", L"No command letter.", L"Pas de lettre de commande.", L"Nessuna lettera di comando.", L"Sin letra de comando.", L"명령 문자가 없습니다.", L"没有命令字母。", L"لا يوجد حرف أمر.", L"Нет буквы команды.", L"Kein Befehlsbuchstabe.", L"Sem letra de comando.", L"Geen opdrachtletter.", L"Brak litery polecenia.", L"Komut harfi yok.");
		return FALSE;
	}
	TCHAR c1 = line[pos++];
	TCHAR c2 = 0;
	BOOL used2 = FALSE;
	if (pos < n && _istalpha(line[pos])) {
		c2 = line[pos];
	}
	MpPromptCmd cmd = CmdFromLetters(c1, c2, used2);
	if (used2) pos++;
	if (cmd == CMD_NONE) {
		if (errMsg) *errMsg = LL14(L"不明なコマンドです。", L"Unknown command.", L"Commande inconnue.", L"Comando sconosciuto.", L"Comando desconocido.", L"알 수 없는 명령입니다.", L"未知命令。", L"أمر غير معروف.", L"Неизвестная команда.", L"Unbekannter Befehl.", L"Comando desconhecido.", L"Onbekende opdracht.", L"Nieznane polecenie.", L"Bilinmeyen komut.");
		return FALSE;
	}
	double t0 = 0, t1 = 0;
	if (!ParseTimeToken(line, pos, t0)) {
		if (errMsg) *errMsg = LL14(L"時刻の解析に失敗しました。", L"Failed to parse time.", L"Echec analyse heure.", L"Analisi ora fallita.", L"Error al analizar tiempo.", L"시간 해석 실패.", L"时间解析失败。", L"فشل تحليل الوقت.", L"Ошибка разбора времени.", L"Zeit parsen fehlgeschlagen.", L"Falha ao analisar hora.", L"Tijd parseren mislukt.", L"Blad parsowania czasu.", L"Zaman ayrıştırılamadı.");
		return FALSE;
	}
	t1 = t0;
	while (pos < n && line[pos] == ' ') pos++;
	if (pos < n && line[pos] == '-') {
		pos++;
		if (!ParseTimeToken(line, pos, t1)) {
			if (errMsg) *errMsg = LL14(L"終了時刻の解析に失敗しました。", L"Failed to parse end time.", L"Echec heure de fin.", L"Fine ora fallita.", L"Error hora final.", L"종료 시각 해석 실패.", L"结束时间解析失败。", L"فشل وقت النهاية.", L"Ошибка конечного времени.", L"Endzeit parsen fehlgeschlagen.", L"Falha hora final.", L"Eindtijd mislukt.", L"Blad czasu końca.", L"Bitiş zamanı başarısız.");
			return FALSE;
		}
	}
	int v0 = 0, v1 = 0;
	const bool isPreset = (cmd >= CMD_PRESET_SB && cmd <= CMD_PRESET_FA);
	if (!isPreset) {
		if (!ParseValueRange(line, pos, v0, v1)) {
			if (errMsg) *errMsg = LL14(L"値の解析に失敗しました。", L"Failed to parse value.", L"Echec analyse valeur.", L"Valore non valido.", L"Error al analizar valor.", L"값 해석 실패.", L"值解析失败。", L"فشل تحليل القيمة.", L"Ошибка разбора значения.", L"Wert parsen fehlgeschlagen.", L"Falha ao analisar valor.", L"Waarde parseren mislukt.", L"Blad wartości.", L"Değer ayrıştırılamadı.");
			return FALSE;
		}
	}
	if (t1 < t0) {
		double tmp = t0; t0 = t1; t1 = tmp;
		int tv = v0; v0 = v1; v1 = tv;
	}
	MpPromptEvent ev{};
	ev.cmd = cmd;
	ev.t0 = t0;
	ev.t1 = t1;
	ev.v0 = v0;
	ev.v1 = v1;
	g_events.push_back(ev);
	return TRUE;
}

static void ApplyPitchPercent(int pct)
{
	if (!og) return;
	int pos = PitchTempoSlFromPercent(pct);
	og->m_pitch_sl.SetPos(pos);
	pitch = pos;
}

static void ApplyTempoPercent(int pct)
{
	if (!og) return;
	int pos = PitchTempoSlFromPercent(pct);
	og->m_tempo_sl.SetPos(pos);
	tempo = pos;
}

static void ApplyDsPercent(int pct)
{
	if (!og) return;
	int v = DsVolFromPercent(pct);
	og->m_dsval.SetPos(v);
	savedata.dsvol = v;
}

static void ApplyEqIndex(int idx, int val)
{
	if (idx < 0 || idx >= 20) return;
	if (val < 0) val = 0;
	if (val > 200) val = 200;
	savedata.eq[idx] = val;
}

static void ApplyFx(int which, int val)
{
	if (val < 0) val = 0;
	if (val > 200) val = 200;
	if (which == 0) savedata.eq_reverb = val;
	else if (which == 1) savedata.eq_chorus = val;
	else savedata.eq_delay = val;
}

static void ApplyEqEnv(int val)
{
	if (val < 0) val = 0;
	if (val > 100) val = 100;
	savedata.eqsoundenv = val;
}

static void ApplyEqEffect(int val)
{
	if (val < 0) val = 0;
	if (val > 100) val = 100;
	savedata.eqsoundeffect = val;
}

static void ApplyPreset(MpPromptCmd cmd)
{
	switch (cmd) {
	case CMD_PRESET_SB:
		ApplyPitchPercent(88);
		ApplyTempoPercent(92);
		ApplyEqIndex(0, 108); ApplyEqIndex(1, 105); ApplyEqIndex(2, 102);
		ApplyEqIndex(10, 96); ApplyEqIndex(11, 92); ApplyEqIndex(12, 88);
		ApplyEqIndex(13, 86); ApplyEqIndex(14, 84);
		// FX: 0=off / 1-100=通常 / 101-200=別モード。しょんぼりは通常リバーブを軽く。
		ApplyFx(0, 55);
		break;
	case CMD_PRESET_BR:
		ApplyPitchPercent(104);
		ApplyTempoPercent(102);
		ApplyEqIndex(8, 112); ApplyEqIndex(9, 115); ApplyEqIndex(10, 118);
		ApplyEqIndex(11, 116); ApplyEqIndex(12, 114);
		break;
	case CMD_PRESET_SL:
		ApplyTempoPercent(80);
		ApplyPitchPercent(100);
		break;
	case CMD_PRESET_FA:
		ApplyTempoPercent(120);
		ApplyPitchPercent(100);
		break;
	default:
		break;
	}
}

static int InterpValue(const MpPromptEvent& ev, double t)
{
	if (t <= ev.t0) return ev.v0;
	if (t >= ev.t1 || ev.t1 <= ev.t0) return ev.v1;
	double r = (t - ev.t0) / (ev.t1 - ev.t0);
	return (int)(ev.v0 + (ev.v1 - ev.v0) * r + 0.5);
}

static void ApplyEventValue(MpPromptCmd cmd, int val)
{
	switch (cmd) {
	case CMD_PITCH: ApplyPitchPercent(val); break;
	case CMD_TEMPO: ApplyTempoPercent(val); break;
	case CMD_DSVOL: ApplyDsPercent(val); break;
	case CMD_EQ_MASTER: ApplyEqIndex(15, val); break;
	case CMD_EQ_SENMEI: ApplyEqIndex(16, val); break;
	case CMD_EQ_KOUTEI: ApplyEqIndex(17, val); break;
	case CMD_EQ_MITSUDO: ApplyEqIndex(18, val); break;
	case CMD_EQ_RITTAI: ApplyEqIndex(19, val); break;
	case CMD_EQ_REVERB: ApplyFx(0, val); break;
	case CMD_EQ_CHORUS: ApplyFx(1, val); break;
	case CMD_EQ_DELAY: ApplyFx(2, val); break;
	case CMD_EQ_ENV: ApplyEqEnv(val); break;
	case CMD_EQ_EFFECT: ApplyEqEffect(val); break;
	default:
		if (cmd >= CMD_EQ_BAND0 && cmd < CMD_EQ_MASTER)
			ApplyEqIndex(cmd - CMD_EQ_BAND0, val);
		break;
	}
}

static void ApplyBackupForCmd(MpPromptCmd cmd)
{
	if (!g_hasBackup || !og) return;
	switch (cmd) {
	case CMD_PITCH:
		og->m_pitch_sl.SetPos(g_backup.pitchSl);
		pitch = g_backup.pitchSl;
		break;
	case CMD_TEMPO:
		og->m_tempo_sl.SetPos(g_backup.tempoSl);
		tempo = g_backup.tempoSl;
		break;
	case CMD_DSVOL:
		og->m_dsval.SetPos(g_backup.dsvol);
		savedata.dsvol = g_backup.dsvol;
		break;
	case CMD_EQ_MASTER: ApplyEqIndex(15, g_backup.eq[15]); break;
	case CMD_EQ_SENMEI: ApplyEqIndex(16, g_backup.eq[16]); break;
	case CMD_EQ_KOUTEI: ApplyEqIndex(17, g_backup.eq[17]); break;
	case CMD_EQ_MITSUDO: ApplyEqIndex(18, g_backup.eq[18]); break;
	case CMD_EQ_RITTAI: ApplyEqIndex(19, g_backup.eq[19]); break;
	case CMD_EQ_REVERB: ApplyFx(0, g_backup.eqReverb); break;
	case CMD_EQ_CHORUS: ApplyFx(1, g_backup.eqChorus); break;
	case CMD_EQ_DELAY: ApplyFx(2, g_backup.eqDelay); break;
	case CMD_EQ_ENV: ApplyEqEnv(g_backup.eqEnv); break;
	case CMD_EQ_EFFECT: ApplyEqEffect(g_backup.eqEffect); break;
	default:
		if (cmd >= CMD_EQ_BAND0 && cmd < CMD_EQ_MASTER)
			ApplyEqIndex(cmd - CMD_EQ_BAND0, g_backup.eq[cmd - CMD_EQ_BAND0]);
		break;
	}
}

static int FindLastEventIndex(MpPromptCmd cmd, double t)
{
	if (cmd <= CMD_NONE || cmd > CMD_PRESET_FA) return -1;
	const std::vector<int>& idxs = g_eventsByCmd[cmd];
	int best = -1;
	double bestT = -1.0;
	// 時刻順に積んであるので末尾から探す
	for (int k = (int)idxs.size() - 1; k >= 0; --k) {
		const int i = idxs[k];
		const MpPromptEvent& ev = g_events[i];
		if (g_promptEventCutoff >= 0.0 && ev.t0 < g_promptEventCutoff - 0.05) continue;
		if (ev.t0 > t + 0.001) continue;
		best = i;
		bestT = ev.t0;
		break;
	}
	(void)bestT;
	return best;
}

static void RebuildEventIndex()
{
	for (int c = 0; c <= CMD_PRESET_FA; ++c)
		g_eventsByCmd[c].clear();
	for (int i = 0; i < (int)g_events.size(); ++i) {
		const int c = (int)g_events[i].cmd;
		if (c > CMD_NONE && c <= CMD_PRESET_FA)
			g_eventsByCmd[c].push_back(i);
	}
	for (int c = 0; c <= CMD_PRESET_FA; ++c) {
		g_lastAppliedValid[c] = FALSE;
		g_lastAppliedVal[c] = INT_MIN;
	}
	g_uiDirty = FALSE;
	g_lastUiSyncTick = 0;
}

static void ApplyEventValueCached(MpPromptCmd cmd, int val)
{
	const int ci = (int)cmd;
	if (ci > 0 && ci <= CMD_PRESET_FA
		&& g_lastAppliedValid[ci] && g_lastAppliedVal[ci] == val)
		return;
	ApplyEventValue(cmd, val);
	if (ci > 0 && ci <= CMD_PRESET_FA) {
		g_lastAppliedValid[ci] = TRUE;
		g_lastAppliedVal[ci] = val;
	}
	g_uiDirty = TRUE;
}

} // namespace

double MpGetPerformanceTimeSec()
{
	extern double OggGetGdiPlaybackTimeSec();
	return OggGetGdiPlaybackTimeSec();
}

void MpPromptTick()
{
	MpPromptTickAtTime(MpGetPerformanceTimeSec());
}

static void MpPromptSyncUi()
{
	if (!og) return;
	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
		if (mp->m_tempo.GetSafeHwnd())
			mp->m_tempo.SetPos(og->m_tempo_sl.GetPos());
		if (mp->m_pitch.GetSafeHwnd())
			mp->m_pitch.SetPos(og->m_pitch_sl.GetPos());
	}
	if (og->m_EqualizerDlg && ::IsWindow(og->m_EqualizerDlg->GetSafeHwnd()))
		og->m_EqualizerDlg->SyncSlidersFromSavedata();
	if (og->m_pitch.GetSafeHwnd()) {
		CString s;
		float pi = (float)og->m_pitch_sl.GetPos();
		if (pi >= 200.0f) pi -= 100.0f;
		else pi = pi / 3.0f + 33.3f;
		s.Format(L"%3d%%", (int)pi);
		og->m_pitch.SetWindowText(s);
	}
	if (og->m_temp_num.GetSafeHwnd()) {
		CString s;
		float te = (float)og->m_tempo_sl.GetPos();
		if (te >= 200.0f) te -= 100.0f;
		else te = te / 3.0f + 33.3f;
		s.Format(L"%3d%%", (int)te);
		og->m_temp_num.SetWindowText(s);
	}
}

static void MpPromptPrepareForNextPlayback()
{
	if (!g_hasBackup && !g_active) return;

	if (g_hasBackup)
		MpPromptBackupRestore(g_backup);

	g_lastPresetIdx = -1;
	g_promptEventCutoff = -1.0;
	g_promptExecuteBeforePlay = FALSE;
	g_lastPlf = 0;
	g_promptAwaitPlayStart = g_active ? TRUE : FALSE;
	g_promptNewTrack = g_active ? TRUE : FALSE;

	OggResetRubberBandStretcher();
	MpPromptSyncUi();
}

void MpPromptOnTrackChange()
{
	MpPromptPrepareForNextPlayback();
}

void MpPromptOnPlaybackStop()
{
	MpPromptPrepareForNextPlayback();
}

void MpPromptOnAppShutdown()
{
	if (g_hasBackup)
		MpPromptBackupRestore(g_backup);
	g_active = FALSE;
	g_lastPresetIdx = -1;
	g_promptNewTrack = FALSE;
	MpPromptResetPlaybackState();
	OggResetRubberBandStretcher();
	MpPromptSyncUi();
}

void MpPromptNotifyPlayback(int plfNow, double tSec)
{
	const int on = (plfNow != 0) ? 1 : 0;
	if (!g_active) {
		g_lastPlf = on;
		return;
	}
	if (on && !g_lastPlf) {
		g_promptAwaitPlayStart = FALSE;
		if (g_promptNewTrack) {
			g_promptNewTrack = FALSE;
			g_promptEventCutoff = -1.0;
			if (g_hasBackup)
				MpPromptBackupRestore(g_backup);
			OggResetRubberBandStretcher();
			MpPromptSyncUi();
		}
		else if (g_promptExecuteBeforePlay) {
			g_promptEventCutoff = (tSec < 0.0) ? 0.0 : tSec;
			g_promptExecuteBeforePlay = FALSE;
		}
		g_lastPresetIdx = -1;
	}
	if (!on && g_lastPlf)
		g_lastPresetIdx = -1;
	g_lastPlf = on;
}

void MpPromptBackupCapture(MpPromptBackup& out)
{
	if (!og) return;
	out.pitchSl = og->m_pitch_sl.GetPos();
	out.tempoSl = og->m_tempo_sl.GetPos();
	out.dsvol = og->m_dsval.GetPos();
	for (int i = 0; i < 20; ++i) out.eq[i] = savedata.eq[i];
	out.eqReverb = savedata.eq_reverb;
	out.eqChorus = savedata.eq_chorus;
	out.eqDelay = savedata.eq_delay;
	out.eqEnv = savedata.eqsoundenv;
	out.eqEffect = savedata.eqsoundeffect;
	out.eqSoundEq = savedata.eqsoundeq;
}

void MpPromptBackupRestore(const MpPromptBackup& in)
{
	if (!og) return;
	og->m_pitch_sl.SetPos(in.pitchSl);
	pitch = in.pitchSl;
	og->m_tempo_sl.SetPos(in.tempoSl);
	tempo = in.tempoSl;
	og->m_dsval.SetPos(in.dsvol);
	savedata.dsvol = in.dsvol;
	for (int i = 0; i < 20; ++i) savedata.eq[i] = in.eq[i];
	savedata.eq_reverb = in.eqReverb;
	savedata.eq_chorus = in.eqChorus;
	savedata.eq_delay = in.eqDelay;
	savedata.eqsoundenv = in.eqEnv;
	savedata.eqsoundeffect = in.eqEffect;
	savedata.eqsoundeq = in.eqSoundEq;
	for (int c = 0; c <= CMD_PRESET_FA; ++c) {
		g_lastAppliedValid[c] = FALSE;
		g_lastAppliedVal[c] = INT_MIN;
	}
	g_uiDirty = TRUE;
}

void MpPromptBackupToSavedata(const MpPromptBackup& b)
{
	savedata.mpPromptBackupValid = 1;
	savedata.mpPromptBackupPitch = b.pitchSl;
	savedata.mpPromptBackupTempo = b.tempoSl;
	savedata.mpPromptBackupDsvol = b.dsvol;
	for (int i = 0; i < 20; ++i) savedata.mpPromptBackupEq[i] = b.eq[i];
	savedata.mpPromptBackupEqReverb = b.eqReverb;
	savedata.mpPromptBackupEqChorus = b.eqChorus;
	savedata.mpPromptBackupEqDelay = b.eqDelay;
	savedata.mpPromptBackupEqEnv = b.eqEnv;
	savedata.mpPromptBackupEqEffect = b.eqEffect;
	savedata.mpPromptBackupEqSoundEq = b.eqSoundEq;
}

void MpPromptBackupFromSavedata(MpPromptBackup& b)
{
	b.pitchSl = savedata.mpPromptBackupPitch;
	b.tempoSl = savedata.mpPromptBackupTempo;
	b.dsvol = savedata.mpPromptBackupDsvol;
	for (int i = 0; i < 20; ++i) b.eq[i] = savedata.mpPromptBackupEq[i];
	b.eqReverb = savedata.mpPromptBackupEqReverb;
	b.eqChorus = savedata.mpPromptBackupEqChorus;
	b.eqDelay = savedata.mpPromptBackupEqDelay;
	b.eqEnv = savedata.mpPromptBackupEqEnv;
	b.eqEffect = savedata.mpPromptBackupEqEffect;
	b.eqSoundEq = savedata.mpPromptBackupEqSoundEq;
}

BOOL MpPromptParse(const CString& text, CString* errMsg)
{
	g_events.clear();
	CString src = text;
	src.Replace(_T("\r\n"), _T("\n"));
	int lineStart = 0;
	const int n = src.GetLength();
	for (int i = 0; i <= n; ++i) {
		if (i == n || src[i] == '\n') {
			CString line = src.Mid(lineStart, i - lineStart);
			line.Trim();
			int p = 0;
			while (p < line.GetLength()) {
				int at = line.Find('@', p);
				if (at < 0) break;
				if (!ParseOnePrompt(line, at, errMsg))
					return FALSE;
				p = at + 1;
			}
			lineStart = i + 1;
		}
	}
	std::sort(g_events.begin(), g_events.end(), [](const MpPromptEvent& a, const MpPromptEvent& b) {
		if (a.t0 != b.t0) return a.t0 < b.t0;
		return (int)a.cmd < (int)b.cmd;
	});
	RebuildEventIndex();
	return TRUE;
}

void MpPromptClearEvents()
{
	g_events.clear();
	g_lastPresetIdx = -1;
}

BOOL MpPromptIsActive() { return g_active; }
void MpPromptSetActive(BOOL active) { g_active = active; }
BOOL MpPromptHasBackup() { return g_hasBackup; }

BOOL MpPromptExecute(const CString& text, CString* errMsg)
{
	MpPromptBackupCapture(g_backup);
	g_hasBackup = TRUE;
	MpPromptBackupToSavedata(g_backup);
	if (!MpPromptParse(text, errMsg))
		return FALSE;
	g_active = TRUE;
	g_lastPresetIdx = -1;
	g_promptExecuteBeforePlay = (plf != 1);
	g_promptAwaitPlayStart = (plf != 1);
	g_promptEventCutoff = -1.0;
	g_lastPlf = (plf == 1) ? 1 : 0;
	return TRUE;
}

void MpPromptStop()
{
	g_active = FALSE;
	MpPromptResetPlaybackState();
}

void MpPromptReset()
{
	if (g_hasBackup)
		MpPromptBackupRestore(g_backup);
	g_active = FALSE;
	g_lastPresetIdx = -1;
	MpPromptResetPlaybackState();
	MpPromptSyncUi();
}

void MpPromptClearAll()
{
	g_events.clear();
	RebuildEventIndex();
	if (g_hasBackup)
		MpPromptBackupRestore(g_backup);
	g_active = FALSE;
	g_hasBackup = FALSE;
	savedata.mpPromptBackupValid = 0;
	g_lastPresetIdx = -1;
	MpPromptResetPlaybackState();
	MpPromptSyncUi();
}

void MpPromptTickAtTime(double tSec)
{
	if (!g_active || g_events.empty() || !og) return;
	if (g_promptAwaitPlayStart) return;
	if (tSec < 0.0) return;

	// tSec は GDI 聴感時刻。適用はデコード先頭(playb)基準で行い、
	// DS キュー遅延(~1秒)ぶん先に書き込む → 記載時刻と聴感が一致する。
	double t = tSec;
	extern int mode;
	extern BOOL videoonly;
	if (!videoonly && mode != -2 && wavbit_sample_Hz > 0) {
		static const double wavv2[] = { 0, 2.0, 1.0, 2.0 / 3.0, 2.0 / 4.0, 2.0 / 5.0, 2.0 / 6.0 };
		int ch = wavchannel;
		if (ch < 0 || ch > 6) ch = 2;
		const double rateDiv = (double)wavbit_sample_Hz / wavv2[ch];
		if (rateDiv > 0.0) {
			__int64 pb = 0;
			{
				std::lock_guard<std::mutex> lk(cl2);
				pb = playb;
			}
			double tDec = (double)pb / rateDiv;
			if (mode == -9 && wavchannel > 2)
				tDec *= (double)wavchannel / 2.0;
			if (tDec > t)
				t = tDec;
		}
	}

	for (int ci = CMD_PITCH; ci <= CMD_EQ_EFFECT; ++ci) {
		MpPromptCmd cmd = (MpPromptCmd)ci;
		int idx = FindLastEventIndex(cmd, t);
		if (idx < 0 || t < g_events[idx].t0) {
			// INT_MIN = バックアップ適用済み（値域0..200と衝突しない）
			if (!(ci <= CMD_PRESET_FA && g_lastAppliedValid[ci] && g_lastAppliedVal[ci] == INT_MIN)) {
				ApplyBackupForCmd(cmd);
				if (ci <= CMD_PRESET_FA) {
					g_lastAppliedValid[ci] = TRUE;
					g_lastAppliedVal[ci] = INT_MIN;
				}
				g_uiDirty = TRUE;
			}
			continue;
		}
		const MpPromptEvent& ev = g_events[idx];
		int val = InterpValue(ev, t);
		ApplyEventValueCached(cmd, val);
	}

	for (int i = 0; i < (int)g_events.size(); ++i) {
		const MpPromptEvent& ev = g_events[i];
		if (ev.cmd < CMD_PRESET_SB || ev.cmd > CMD_PRESET_FA) continue;
		if (g_promptEventCutoff >= 0.0 && ev.t0 < g_promptEventCutoff - 0.05) continue;
		if (t < ev.t0) continue;
		if (t > ev.t1 + 0.5) continue;
		if (i == g_lastPresetIdx) continue;
		ApplyPreset(ev.cmd);
		g_lastPresetIdx = i;
		for (int c = 0; c <= CMD_PRESET_FA; ++c)
			g_lastAppliedValid[c] = FALSE;
		g_uiDirty = TRUE;
	}

	// EQ窓 Sync は毎フレームやると全体が重くなる。変化時のみ・最短200ms間隔。
	if (g_uiDirty) {
		const DWORD now = GetTickCount();
		if (g_lastUiSyncTick == 0 || now - g_lastUiSyncTick >= 200) {
			g_lastUiSyncTick = now;
			g_uiDirty = FALSE;
			MpPromptSyncUi();
			if (og->m_dsval.GetSafeHwnd() && og->m_dsvols.GetSafeHwnd()) {
				CString s, ss;
				s.Format(_T("%.1f%%"), (savedata.dsvol + 499) * 2.0 / 10.0);
				og->m_dsvols.GetWindowText(ss);
				if (s != ss) og->m_dsvols.SetWindowText(s);
			}
		}
	}
}

void MpPromptPushHistory(LPCTSTR text)
{
	if (!text || !*text) return;
	CString s(text);
	s.Trim();
	if (s.IsEmpty()) return;
	if (savedata.mpPromptHistCnt > 0
		&& _tcscmp(savedata.mpPromptHistText[0], s) == 0)
		return;
	const int nMove = (savedata.mpPromptHistCnt < 20) ? savedata.mpPromptHistCnt : 19;
	for (int j = nMove; j > 0; --j)
		_tcscpy(savedata.mpPromptHistText[j], savedata.mpPromptHistText[j - 1]);
	_tcsncpy(savedata.mpPromptHistText[0], s, _countof(savedata.mpPromptHistText[0]) - 1);
	savedata.mpPromptHistText[0][_countof(savedata.mpPromptHistText[0]) - 1] = 0;
	if (savedata.mpPromptHistCnt < 20)
		savedata.mpPromptHistCnt++;
	extern void MpPersistSavedataQuick();
	MpPersistSavedataQuick();
}

void MpPromptFlushHistoryOnExit()
{
	if (savedata.mpPromptText[0])
		MpPromptPushHistory(savedata.mpPromptText);
}
