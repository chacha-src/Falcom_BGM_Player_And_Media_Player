#include "stdafx.h"
#include "CPromptEngine.h"
#include "oggDlg.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <mutex>

extern save savedata;
extern COggDlg* og;
extern __int64 playb;
extern int loop1, loop2;
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
static BOOL g_active = FALSE;
static BOOL g_hasBackup = FALSE;
static MpPromptBackup g_backup;
static int g_lastPresetIdx = -1;

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

static void ApplyPreset(MpPromptCmd cmd)
{
	switch (cmd) {
	case CMD_PRESET_SB:
		ApplyPitchPercent(88);
		ApplyTempoPercent(92);
		ApplyEqIndex(0, 108); ApplyEqIndex(1, 105); ApplyEqIndex(2, 102);
		ApplyEqIndex(10, 96); ApplyEqIndex(11, 92); ApplyEqIndex(12, 88);
		ApplyEqIndex(13, 86); ApplyEqIndex(14, 84);
		ApplyFx(0, 108);
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
	default:
		if (cmd >= CMD_EQ_BAND0 && cmd < CMD_EQ_MASTER)
			ApplyEqIndex(cmd - CMD_EQ_BAND0, val);
		break;
	}
}

static int FindLastEventIndex(MpPromptCmd cmd, double t)
{
	int best = -1;
	double bestT = -1.0;
	for (int i = 0; i < (int)g_events.size(); ++i) {
		const MpPromptEvent& ev = g_events[i];
		if (ev.cmd != cmd) continue;
		if (ev.t0 > t + 0.001) continue;
		if (ev.t0 > bestT) {
			bestT = ev.t0;
			best = i;
		}
	}
	return best;
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
	return TRUE;
}

void MpPromptStop()
{
	g_active = FALSE;
}

void MpPromptReset()
{
	if (g_hasBackup)
		MpPromptBackupRestore(g_backup);
	g_active = FALSE;
	g_lastPresetIdx = -1;
}

void MpPromptClearAll()
{
	g_events.clear();
	if (g_hasBackup)
		MpPromptBackupRestore(g_backup);
	g_active = FALSE;
	g_hasBackup = FALSE;
	savedata.mpPromptBackupValid = 0;
	g_lastPresetIdx = -1;
}

void MpPromptTickAtTime(double tSec)
{
	if (!g_active || g_events.empty() || !og) return;
	const double t = tSec;

	for (int ci = CMD_PITCH; ci <= CMD_EQ_DELAY; ++ci) {
		MpPromptCmd cmd = (MpPromptCmd)ci;
		int idx = FindLastEventIndex(cmd, t);
		if (idx < 0) continue;
		const MpPromptEvent& ev = g_events[idx];
		if (t < ev.t0) continue;
		int val = InterpValue(ev, t);
		ApplyEventValue(cmd, val);
	}

	for (int i = 0; i < (int)g_events.size(); ++i) {
		const MpPromptEvent& ev = g_events[i];
		if (ev.cmd < CMD_PRESET_SB || ev.cmd > CMD_PRESET_FA) continue;
		if (t < ev.t0) continue;
		if (t > ev.t1 + 0.5) continue;
		if (i == g_lastPresetIdx) continue;
		ApplyPreset(ev.cmd);
		g_lastPresetIdx = i;
	}
}
