#include "stdafx.h"
#include "CPromptEngine.h"
#include "CMediaPlayerDlg.h"
#include "CEqualizer.h"
#include "oggDlg.h"
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
	CMD_EQ_ENV,      // ?????? (eqsoundenv)
	CMD_EQ_EFFECT,   // ??????????? (eqsoundeffect)
	CMD_PRESET_SB,
	CMD_PRESET_BR,
	CMD_PRESET_SL,
	CMD_PRESET_FA,
	CMD_PRESET_WM,
	CMD_PRESET_CD,
	CMD_PRESET_DP,
	CMD_PRESET_WI,
	CMD_PRESET_NR,
	CMD_PRESET_GN,
	CMD_PRESET_PW,
	CMD_PRESET_DR,
	CMD_PRESET_LAST = CMD_PRESET_DR,
};

struct MpPromptEvent {
	MpPromptCmd cmd;
	double t0;       // @: ?J?n?b / %: ???????I?t?Z?b?g?J?n
	double t1;       // @: ?I???b / %: ???????I?t?Z?b?g?I??
	int v0;
	int v1;
	double period;   // 0=@ / >0=% ????(?b)
};

const int kMaxPromptEvents = 2048;
static MpPromptEvent g_events[kMaxPromptEvents];
static int g_eventCount = 0;
static int g_eventsByCmd[CMD_PRESET_LAST + 1][kMaxPromptEvents];
static int g_eventsByCmdN[CMD_PRESET_LAST + 1];
static int g_lastAppliedVal[CMD_PRESET_LAST + 1];
static BOOL g_lastAppliedValid[CMD_PRESET_LAST + 1];
static DWORD g_lastUiSyncTick = 0;
static BOOL g_uiDirty = FALSE;
static BOOL g_active = FALSE;
static BOOL g_hasBackup = FALSE;
static MpPromptBackup g_backup;
static int g_lastPresetIdx = -1;
static int g_lastPresetCycle = -1;
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
		if (l1 == 'w' && l2 == 'm') { used2 = TRUE; return CMD_PRESET_WM; }
		if (l1 == 'c' && l2 == 'd') { used2 = TRUE; return CMD_PRESET_CD; }
		if (l1 == 'd' && l2 == 'p') { used2 = TRUE; return CMD_PRESET_DP; }
		if (l1 == 'w' && l2 == 'i') { used2 = TRUE; return CMD_PRESET_WI; }
		if (l1 == 'n' && l2 == 'r') { used2 = TRUE; return CMD_PRESET_NR; }
		if (l1 == 'g' && l2 == 'n') { used2 = TRUE; return CMD_PRESET_GN; }
		if (l1 == 'p' && l2 == 'w') { used2 = TRUE; return CMD_PRESET_PW; }
		if (l1 == 'd' && l2 == 'r') { used2 = TRUE; return CMD_PRESET_DR; }
	}
	// EQ????(????): ???g???? a-o(??????) ????????
	if (c1 == 'M') return CMD_EQ_MASTER;
	if (c1 == 'N') return CMD_EQ_SENMEI;
	if (c1 == 'K') return CMD_EQ_KOUTEI;
	if (c1 == 'I') return CMD_EQ_MITSUDO;
	if (c1 == 'S') return CMD_EQ_RITTAI;
	if (c1 == 'E') return CMD_EQ_ENV;      // ?????? (?????? e=160Hz?????)
	if (c1 == 'F') return CMD_EQ_EFFECT;   // ???????
	if (l1 == 'p') return CMD_PITCH;
	if (l1 == 't') return CMD_TEMPO;
	if (l1 == 'd') return CMD_DSVOL;
	if (l1 == 'r') return CMD_EQ_REVERB;
	if (l1 == 'c') return CMD_EQ_CHORUS;
	if (l1 == 'y') return CMD_EQ_DELAY;
	// EQ???g???? 15??: a=25Hz ?c o=16kHz (??????)
	if (l1 >= 'a' && l1 <= 'o') return (MpPromptCmd)(CMD_EQ_BAND0 + (l1 - 'a'));
	// ???: ?????? s=???? (sl/sb ?v???Z?b?g????2??????????)
	if (l1 == 's') return CMD_EQ_RITTAI;
	return CMD_NONE;
}

static BOOL ParseOnePrompt(const CString& line, int startPos, CString* errMsg)
{
	const int n = line.GetLength();
	int pos = startPos;
	if (pos >= n || (line[pos] != '@' && line[pos] != '%'))
		return FALSE;
	const BOOL isPct = (line[pos] == '%') ? TRUE : FALSE;
	pos++;
	if (pos >= n) {
		if (errMsg) *errMsg = LL14(L"?R?}???h??????????????B", L"No command letter.", L"Pas de lettre de commande.", L"Nessuna lettera di comando.", L"Sin letra de comando.", L"?? ??? ????.", L"?v?L???????B", L"?? ???? ??? ???.", L"?N?u?? ?q???{?r?? ?{???}?p?~?t??.", L"Kein Befehlsbuchstabe.", L"Sem letra de comando.", L"Geen opdrachtletter.", L"Brak litery polecenia.", L"Komut harfi yok.");
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
		if (errMsg) *errMsg = LL14(L"?s????R?}???h????B", L"Unknown command.", L"Commande inconnue.", L"Comando sconosciuto.", L"Comando desconocido.", L"? ? ?? ?????.", L"???m????B", L"??? ??? ?????.", L"?N?u?y?x?r?u?????~?p?? ?{???}?p?~?t?p.", L"Unbekannter Befehl.", L"Comando desconhecido.", L"Onbekende opdracht.", L"Nieznane polecenie.", L"Bilinmeyen komut.");
		return FALSE;
	}
	double t0 = 0, t1 = 0, period = 0;
	if (!ParseTimeToken(line, pos, t0)) {
		if (errMsg) *errMsg = isPct
			? LL14(L"???????????s????????B", L"Failed to parse period.", L"Echec analyse periode.", L"Analisi periodo fallita.", L"Error al analizar periodo.", L"?? ?? ??.", L"??????????B", L"??? ????? ??????.", L"?O???y?q?{?p ???p?x?q?????p ???u???y???t?p.", L"Periode parsen fehlgeschlagen.", L"Falha ao analisar periodo.", L"Periode parseren mislukt.", L"Blad parsowania okresu.", L"Donem ayr??t?r?lamad?.")
			: LL14(L"???????????s????????B", L"Failed to parse time.", L"Echec analyse heure.", L"Analisi ora fallita.", L"Error al analizar tiempo.", L"?? ?? ??.", L"????????B", L"??? ????? ?????.", L"?O???y?q?{?p ???p?x?q?????p ?r???u?}?u?~?y.", L"Zeit parsen fehlgeschlagen.", L"Falha ao analisar hora.", L"Tijd parseren mislukt.", L"Blad parsowania czasu.", L"Zaman ayr??t?r?lamad?.");
		return FALSE;
	}
	if (isPct) {
		period = t0;
		if (period < 0.001) {
			if (errMsg) *errMsg = LL14(L"??????0???????K?v?????????B", L"Period must be greater than 0.", L"La periode doit etre > 0.", L"Il periodo deve essere > 0.", L"El periodo debe ser > 0.", L"??? 0?? ?? ???.", L"?????K???0?B", L"??? ?? ???? ?????? > 0.", L"?P?u???y???t ?t???|?w?u?~ ?q?????? > 0.", L"Periode muss > 0 sein.", L"Periodo deve ser > 0.", L"Periode moet > 0 zijn.", L"Okres musi byc > 0.", L"Donem 0'dan buyuk olmali.");
			return FALSE;
		}
		while (pos < n && line[pos] == ' ') pos++;
		if (pos >= n || line[pos] != '<') {
			if (errMsg) *errMsg = LL14(L"?????I?t?Z?b?g?? '<' ??????????B", L"Missing '<' for period offset.", L"'<' manquant pour le decalage.", L"Manca '<' per l'offset.", L"Falta '<' del desplazamiento.", L"?? ??? '<' ? ????.", L"????????? '<'?B", L"????? '<' ???????.", L"?N?u?? '<' ?t?|?? ???}?u???u?~?y??.", L"'<' fuer Offset fehlt.", L"Falta '<' do deslocamento.", L"'<' voor offset ontbreekt.", L"Brak '<' dla offsetu.", L"Donem ofseti icin '<' yok.");
			return FALSE;
		}
		pos++;
		if (!ParseTimeToken(line, pos, t0)) {
			if (errMsg) *errMsg = LL14(L"?I?t?Z?b?g???????s????????B", L"Failed to parse offset.", L"Echec analyse decalage.", L"Analisi offset fallita.", L"Error al analizar desplazamiento.", L"??? ?? ??.", L"????????B", L"??? ????? ???????.", L"?O???y?q?{?p ???p?x?q?????p ???}?u???u?~?y??.", L"Offset parsen fehlgeschlagen.", L"Falha ao analisar deslocamento.", L"Offset parseren mislukt.", L"Blad parsowania offsetu.", L"Ofset ayr??t?r?lamad?.");
			return FALSE;
		}
		t1 = t0;
		while (pos < n && line[pos] == ' ') pos++;
		if (pos < n && line[pos] == '-') {
			pos++;
			if (!ParseTimeToken(line, pos, t1)) {
				if (errMsg) *errMsg = LL14(L"?I???I?t?Z?b?g???????s????????B", L"Failed to parse end offset.", L"Echec decalage de fin.", L"Fine offset fallita.", L"Error desplazamiento final.", L"?? ??? ?? ??.", L"???????????B", L"??? ????? ???????.", L"?O???y?q?{?p ?{???~?u???~???s?? ???}?u???u?~?y??.", L"End-Offset parsen fehlgeschlagen.", L"Falha deslocamento final.", L"Eind-offset mislukt.", L"Blad offsetu konca.", L"Bitis ofseti basarisiz.");
				return FALSE;
			}
		}
		while (pos < n && line[pos] == ' ') pos++;
		if (pos >= n || line[pos] != '>') {
			if (errMsg) *errMsg = LL14(L"?????I?t?Z?b?g?? '>' ??????????B", L"Missing '>' for period offset.", L"'>' manquant pour le decalage.", L"Manca '>' per l'offset.", L"Falta '>' del desplazamiento.", L"?? ??? '>' ? ????.", L"????????? '>'?B", L"????? '>' ???????.", L"?N?u?? '>' ?t?|?? ???}?u???u?~?y??.", L"'>' fuer Offset fehlt.", L"Falta '>' do deslocamento.", L"'>' voor offset ontbreekt.", L"Brak '>' dla offsetu.", L"Donem ofseti icin '>' yok.");
			return FALSE;
		}
		pos++;
		if (t0 < 0.0) t0 = 0.0;
		if (t1 < 0.0) t1 = 0.0;
		if (t0 >= period) t0 = period - 0.001;
		if (t1 >= period) t1 = period - 0.001;
	}
	else {
		t1 = t0;
		while (pos < n && line[pos] == ' ') pos++;
		if (pos < n && line[pos] == '-') {
			pos++;
			if (!ParseTimeToken(line, pos, t1)) {
				if (errMsg) *errMsg = LL14(L"?I?????????????s????????B", L"Failed to parse end time.", L"Echec heure de fin.", L"Fine ora fallita.", L"Error hora final.", L"?? ?? ?? ??.", L"???????????B", L"??? ??? ???????.", L"?O???y?q?{?p ?{???~?u???~???s?? ?r???u?}?u?~?y.", L"Endzeit parsen fehlgeschlagen.", L"Falha hora final.", L"Eindtijd mislukt.", L"Blad czasu ko?ca.", L"Biti? zaman? ba?ar?s?z.");
				return FALSE;
			}
		}
	}
	int v0 = 0, v1 = 0;
	const bool isPreset = (cmd >= CMD_PRESET_SB && cmd <= CMD_PRESET_LAST);
	if (!isPreset) {
		if (!ParseValueRange(line, pos, v0, v1)) {
			if (errMsg) *errMsg = LL14(L"?l???????s????????B", L"Failed to parse value.", L"Echec analyse valeur.", L"Valore non valido.", L"Error al analizar valor.", L"? ?? ??.", L"???????B", L"??? ????? ??????.", L"?O???y?q?{?p ???p?x?q?????p ?x?~?p???u?~?y??.", L"Wert parsen fehlgeschlagen.", L"Falha ao analisar valor.", L"Waarde parseren mislukt.", L"Blad warto?ci.", L"De?er ayr??t?r?lamad?.");
			return FALSE;
		}
	}
	if (t1 < t0) {
		double tmp = t0; t0 = t1; t1 = tmp;
		int tv = v0; v0 = v1; v1 = tv;
	}
	if (g_eventCount >= kMaxPromptEvents) {
		if (errMsg) *errMsg = LL14(L"?v?????v?g?C?x???g???????????????B", L"Prompt event limit exceeded.", L"Limite d evenements depassee.", L"Limite eventi superato.", L"Limite de eventos superado.", L"???? ??? ?? ??.", L"?????????o????B", L"?? ????? ?? ???????.", L"?P???u?r?????u?~ ?|?y?}?y?? ?????q?????y?z.", L"Ereignislimit ueberschritten.", L"Limite de eventos excedido.", L"Eventlimiet overschreden.", L"Przekroczono limit zdarzen.", L"Olay limiti asildi.");
		return FALSE;
	}
	MpPromptEvent& ev = g_events[g_eventCount++];
	ev.cmd = cmd;
	ev.t0 = t0;
	ev.t1 = t1;
	ev.v0 = v0;
	ev.v1 = v1;
	ev.period = period;
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
	// ?v?????v?g/EQ?X???C?_?[?\???? 0..200?B????????? /2 (0..100)
	if (val < 0) val = 0;
	if (val > 200) val = 200;
	savedata.eqsoundeffect = val / 2;
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
		// FX: 0=off / 1-100=??? / 101-200=????[?h?B???????????o?[?u???y???B
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
	case CMD_PRESET_WM: // warm
		ApplyEqIndex(3, 108); ApplyEqIndex(4, 110); ApplyEqIndex(5, 112);
		ApplyEqIndex(6, 110); ApplyEqIndex(7, 108);
		ApplyEqIndex(16, 104);
		ApplyFx(0, 40);
		break;
	case CMD_PRESET_CD: // cold
		ApplyEqIndex(10, 92); ApplyEqIndex(11, 90); ApplyEqIndex(12, 88);
		ApplyEqIndex(13, 86); ApplyEqIndex(14, 84);
		ApplyEqIndex(16, 108);
		ApplyFx(1, 35);
		break;
	case CMD_PRESET_DP: // deep
		ApplyEqIndex(0, 118); ApplyEqIndex(1, 115); ApplyEqIndex(2, 112);
		ApplyEqIndex(3, 108);
		ApplyEqIndex(12, 92); ApplyEqIndex(13, 90); ApplyEqIndex(14, 88);
		ApplyEqIndex(17, 92);
		break;
	case CMD_PRESET_WI: // wide
		ApplyEqIndex(19, 125);
		ApplyFx(2, 45);
		ApplyFx(0, 30);
		break;
	case CMD_PRESET_NR: // near
		ApplyEqIndex(18, 118);
		ApplyEqIndex(19, 92);
		ApplyEqIndex(16, 108);
		break;
	case CMD_PRESET_GN: // gentle
		ApplyTempoPercent(90);
		ApplyPitchPercent(96);
		ApplyEqIndex(16, 98);
		ApplyFx(0, 35);
		break;
	case CMD_PRESET_PW: // power
		ApplyEqIndex(16, 118);
		ApplyEqIndex(18, 110);
		ApplyDsPercent(108);
		ApplyEqIndex(1, 110); ApplyEqIndex(2, 108);
		break;
	case CMD_PRESET_DR: // dreamy
		ApplyFx(0, 130); // ?p?????o?[?u???
		ApplyEqIndex(19, 115);
		ApplyEqIndex(16, 102);
		ApplyTempoPercent(96);
		break;
	default:
		break;
	}
}

static int InterpValueAbs(double t, double abs0, double abs1, int v0, int v1)
{
	if (t <= abs0) return v0;
	if (t >= abs1 || abs1 <= abs0) return v1;
	double r = (t - abs0) / (abs1 - abs0);
	return (int)(v0 + (v1 - v0) * r + 0.5);
}

static int InterpValue(const MpPromptEvent& ev, double t)
{
	return InterpValueAbs(t, ev.t0, ev.t1, ev.v0, ev.v1);
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
	case CMD_EQ_EFFECT:
		// ?o?b?N?A?b?v??????l(0..100)?BApplyEqEffect ??UI?X?P?[???????????
		if (g_backup.eqEffect < 0) savedata.eqsoundeffect = 0;
		else if (g_backup.eqEffect > 100) savedata.eqsoundeffect = 100;
		else savedata.eqsoundeffect = g_backup.eqEffect;
		break;
	default:
		if (cmd >= CMD_EQ_BAND0 && cmd < CMD_EQ_MASTER)
			ApplyEqIndex(cmd - CMD_EQ_BAND0, g_backup.eq[cmd - CMD_EQ_BAND0]);
		break;
	}
}

static int FindLastEventIndex(MpPromptCmd cmd, double t, double* outAbs0, double* outAbs1)
{
	if (cmd <= CMD_NONE || cmd > CMD_PRESET_LAST) return -1;
	const int nIdx = g_eventsByCmdN[cmd];
	int best = -1;
	double bestAbs0 = -1.0;
	double bestAbs1 = -1.0;
	for (int k = 0; k < nIdx; ++k) {
		const int i = g_eventsByCmd[cmd][k];
		const MpPromptEvent& ev = g_events[i];
		double abs0 = 0.0, abs1 = 0.0;
		if (ev.period > 0.0) {
			if (ev.period < 0.001) continue;
			double ph = fmod(t, ev.period);
			if (ph < 0.0) ph += ev.period;
			if (ph + 0.001 < ev.t0 || ph > ev.t1 + 0.001)
				continue;
			const double cycle = floor(t / ev.period);
			abs0 = cycle * ev.period + ev.t0;
			abs1 = cycle * ev.period + ev.t1;
			if (g_promptEventCutoff >= 0.0 && abs0 < g_promptEventCutoff - 0.05)
				continue;
		}
		else {
			if (g_promptEventCutoff >= 0.0 && ev.t0 < g_promptEventCutoff - 0.05)
				continue;
			if (ev.t0 > t + 0.001)
				continue;
			abs0 = ev.t0;
			abs1 = ev.t1;
		}
		if (abs0 >= bestAbs0 - 0.0001) {
			best = i;
			bestAbs0 = abs0;
			bestAbs1 = abs1;
		}
	}
	if (outAbs0) *outAbs0 = bestAbs0;
	if (outAbs1) *outAbs1 = bestAbs1;
	return best;
}

static void RebuildEventIndex()
{
	for (int c = 0; c <= CMD_PRESET_LAST; ++c)
		g_eventsByCmdN[c] = 0;
	for (int i = 0; i < g_eventCount; ++i) {
		const int c = (int)g_events[i].cmd;
		if (c > CMD_NONE && c <= CMD_PRESET_LAST) {
			const int n = g_eventsByCmdN[c];
			if (n < kMaxPromptEvents) {
				g_eventsByCmd[c][n] = i;
				g_eventsByCmdN[c] = n + 1;
			}
		}
	}
	for (int c = 0; c <= CMD_PRESET_LAST; ++c) {
		g_lastAppliedValid[c] = FALSE;
		g_lastAppliedVal[c] = INT_MIN;
	}
	g_uiDirty = FALSE;
	g_lastUiSyncTick = 0;
}

static void ApplyEventValueCached(MpPromptCmd cmd, int val)
{
	const int ci = (int)cmd;
	if (ci > 0 && ci <= CMD_PRESET_LAST
		&& g_lastAppliedValid[ci] && g_lastAppliedVal[ci] == val)
		return;
	ApplyEventValue(cmd, val);
	if (ci > 0 && ci <= CMD_PRESET_LAST) {
		g_lastAppliedValid[ci] = TRUE;
		g_lastAppliedVal[ci] = val;
	}
	g_uiDirty = TRUE;
}

static void CmdLettersFromCmd(MpPromptCmd cmd, TCHAR& c1, TCHAR& c2, BOOL& isPreset)
{
	c1 = 0; c2 = 0; isPreset = FALSE;
	switch (cmd) {
	case CMD_PRESET_SB: c1 = 's'; c2 = 'b'; isPreset = TRUE; return;
	case CMD_PRESET_BR: c1 = 'b'; c2 = 'r'; isPreset = TRUE; return;
	case CMD_PRESET_SL: c1 = 's'; c2 = 'l'; isPreset = TRUE; return;
	case CMD_PRESET_FA: c1 = 'f'; c2 = 'a'; isPreset = TRUE; return;
	case CMD_PRESET_WM: c1 = 'w'; c2 = 'm'; isPreset = TRUE; return;
	case CMD_PRESET_CD: c1 = 'c'; c2 = 'd'; isPreset = TRUE; return;
	case CMD_PRESET_DP: c1 = 'd'; c2 = 'p'; isPreset = TRUE; return;
	case CMD_PRESET_WI: c1 = 'w'; c2 = 'i'; isPreset = TRUE; return;
	case CMD_PRESET_NR: c1 = 'n'; c2 = 'r'; isPreset = TRUE; return;
	case CMD_PRESET_GN: c1 = 'g'; c2 = 'n'; isPreset = TRUE; return;
	case CMD_PRESET_PW: c1 = 'p'; c2 = 'w'; isPreset = TRUE; return;
	case CMD_PRESET_DR: c1 = 'd'; c2 = 'r'; isPreset = TRUE; return;
	case CMD_EQ_MASTER: c1 = 'M'; return;
	case CMD_EQ_SENMEI: c1 = 'N'; return;
	case CMD_EQ_KOUTEI: c1 = 'K'; return;
	case CMD_EQ_MITSUDO: c1 = 'I'; return;
	case CMD_EQ_RITTAI: c1 = 'S'; return;
	case CMD_EQ_ENV: c1 = 'E'; return;
	case CMD_EQ_EFFECT: c1 = 'F'; return;
	case CMD_PITCH: c1 = 'p'; return;
	case CMD_TEMPO: c1 = 't'; return;
	case CMD_DSVOL: c1 = 'd'; return;
	case CMD_EQ_REVERB: c1 = 'r'; return;
	case CMD_EQ_CHORUS: c1 = 'c'; return;
	case CMD_EQ_DELAY: c1 = 'y'; return;
	default:
		if (cmd >= CMD_EQ_BAND0 && cmd < CMD_EQ_MASTER) {
			c1 = (TCHAR)(L'a' + (cmd - CMD_EQ_BAND0));
		}
		return;
	}
}

static CString FmtPromptTime(double sec)
{
	if (sec < 0.0) sec = 0.0;
	int total = (int)(sec + 0.5);
	if (total >= 60) {
		CString s;
		s.Format(L"%d:%02d", total / 60, total % 60);
		return s;
	}
	CString s;
	s.Format(L"%d", total);
	return s;
}

static void FillSnapshotFromEvent(const MpPromptEvent& ev, MpPromptSnapshotEvent& out)
{
	memset(&out, 0, sizeof(out));
	BOOL isPreset = FALSE;
	CmdLettersFromCmd(ev.cmd, out.c1, out.c2, isPreset);
	out.isPreset = isPreset;
	out.hasVal = isPreset ? FALSE : TRUE;
	out.t0 = ev.t0;
	out.t1 = ev.t1;
	out.v0 = ev.v0;
	out.v1 = ev.v1;
	out.period = ev.period;
	out.cmdId = (int)ev.cmd;
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
	g_lastPresetCycle = -1;
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
	g_lastPresetCycle = -1;
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
		g_lastPresetCycle = -1;
	}
	if (!on && g_lastPlf) {
		g_lastPresetIdx = -1;
		g_lastPresetCycle = -1;
	}
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
	for (int c = 0; c <= CMD_PRESET_LAST; ++c) {
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
	g_eventCount = 0;
	CString src = text;
	src.Replace(_T("\r\n"), _T("\n"));
	int lineStart = 0;
	const int n = src.GetLength();
	for (int i = 0; i <= n; ++i) {
		if (i == n || src[i] == '\n') {
			CString line = src.Mid(lineStart, i - lineStart);
			line.Trim();
			// # ????n???s??R?????g?idsBase=40% ???? % ???R?}???h???F??????j
			if (!line.IsEmpty() && line[0] == '#') {
				lineStart = i + 1;
				continue;
			}
			int p = 0;
			const int len = line.GetLength();
			while (p < len) {
				int at = -1, pct = -1;
				for (int j = p; j < len; ++j) {
					if (at < 0 && line[j] == '@') at = j;
					// % ??????R?}???h?????????????????R?}???h?J?n
					if (pct < 0 && line[j] == '%' && j + 1 < len && _istalpha(line[j + 1]))
						pct = j;
					if (at >= 0 && pct >= 0) break;
				}
				int next = -1;
				if (at >= 0 && pct >= 0) next = (at < pct) ? at : pct;
				else if (at >= 0) next = at;
				else if (pct >= 0) next = pct;
				if (next < 0) break;
				if (!ParseOnePrompt(line, next, errMsg))
					return FALSE;
				p = next + 1;
			}
			lineStart = i + 1;
		}
	}
	// ?}???\?[?g (t0, cmd)?B% ??I?t?Z?b?g??????????\?z?p?????
	for (int i = 1; i < g_eventCount; ++i) {
		MpPromptEvent key = g_events[i];
		int j = i - 1;
		while (j >= 0) {
			const BOOL less = (g_events[j].t0 > key.t0)
				|| (g_events[j].t0 == key.t0 && (int)g_events[j].cmd > (int)key.cmd);
			if (!less) break;
			g_events[j + 1] = g_events[j];
			--j;
		}
		g_events[j + 1] = key;
	}
	RebuildEventIndex();
	return TRUE;
}

void MpPromptClearEvents()
{
	g_eventCount = 0;
	g_lastPresetIdx = -1;
	g_lastPresetCycle = -1;
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
	g_lastPresetCycle = -1;
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
	g_lastPresetCycle = -1;
	MpPromptResetPlaybackState();
	MpPromptSyncUi();
}

void MpPromptClearAll()
{
	g_eventCount = 0;
	RebuildEventIndex();
	if (g_hasBackup)
		MpPromptBackupRestore(g_backup);
	g_active = FALSE;
	g_hasBackup = FALSE;
	savedata.mpPromptBackupValid = 0;
	g_lastPresetIdx = -1;
	g_lastPresetCycle = -1;
	MpPromptResetPlaybackState();
	MpPromptSyncUi();
}

void MpPromptTickAtTime(double tSec)
{
	if (!g_active || g_eventCount <= 0 || !og) return;
	if (g_promptAwaitPlayStart) return;
	if (tSec < 0.0) return;

	// tSec ?? GDI ?????????B?K?p??f?R?[?h??(playb)????s???A
	// DS ?L???[?x??(~1?b)???????????? ?? ?L??????????????v????B
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
		double abs0 = -1.0, abs1 = -1.0;
		int idx = FindLastEventIndex(cmd, t, &abs0, &abs1);
		if (idx < 0) {
			if (!(ci <= CMD_PRESET_LAST && g_lastAppliedValid[ci] && g_lastAppliedVal[ci] == INT_MIN)) {
				ApplyBackupForCmd(cmd);
				if (ci <= CMD_PRESET_LAST) {
					g_lastAppliedValid[ci] = TRUE;
					g_lastAppliedVal[ci] = INT_MIN;
				}
				g_uiDirty = TRUE;
			}
			continue;
		}
		const MpPromptEvent& ev = g_events[idx];
		int val;
		if (ev.period > 0.0) {
			if (t <= abs0) val = ev.v0;
			else if (t >= abs1 || abs1 <= abs0) val = ev.v1;
			else {
				double r = (t - abs0) / (abs1 - abs0);
				val = (int)(ev.v0 + (ev.v1 - ev.v0) * r + 0.5);
			}
		}
		else {
			if (t < ev.t0) {
				if (!(ci <= CMD_PRESET_LAST && g_lastAppliedValid[ci] && g_lastAppliedVal[ci] == INT_MIN)) {
					ApplyBackupForCmd(cmd);
					if (ci <= CMD_PRESET_LAST) {
						g_lastAppliedValid[ci] = TRUE;
						g_lastAppliedVal[ci] = INT_MIN;
					}
					g_uiDirty = TRUE;
				}
				continue;
			}
			val = InterpValue(ev, t);
		}
		ApplyEventValueCached(cmd, val);
	}

	for (int i = 0; i < g_eventCount; ++i) {
		const MpPromptEvent& ev = g_events[i];
		if (ev.cmd < CMD_PRESET_SB || ev.cmd > CMD_PRESET_LAST) continue;
		if (ev.period > 0.0) {
			if (ev.period < 0.001) continue;
			double ph = fmod(t, ev.period);
			if (ph < 0.0) ph += ev.period;
			if (ph + 0.001 < ev.t0 || ph > ev.t1 + 0.5) continue;
			const int cycle = (int)floor(t / ev.period);
			const double abs0 = (double)cycle * ev.period + ev.t0;
			if (g_promptEventCutoff >= 0.0 && abs0 < g_promptEventCutoff - 0.05) continue;
			if (i == g_lastPresetIdx && cycle == g_lastPresetCycle) continue;
			ApplyPreset(ev.cmd);
			g_lastPresetIdx = i;
			g_lastPresetCycle = cycle;
			for (int c = 0; c <= CMD_PRESET_LAST; ++c)
				g_lastAppliedValid[c] = FALSE;
			g_uiDirty = TRUE;
		}
		else {
			if (g_promptEventCutoff >= 0.0 && ev.t0 < g_promptEventCutoff - 0.05) continue;
			if (t < ev.t0) continue;
			if (t > ev.t1 + 0.5) continue;
			if (i == g_lastPresetIdx) continue;
			ApplyPreset(ev.cmd);
			g_lastPresetIdx = i;
			g_lastPresetCycle = -1;
			for (int c = 0; c <= CMD_PRESET_LAST; ++c)
				g_lastAppliedValid[c] = FALSE;
			g_uiDirty = TRUE;
		}
	}

	// EQ?? Sync ????t???[??????S????d?????B????????E??Z200ms??u?B
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

int MpPromptGetParsedEventCount()
{
	return g_eventCount;
}

BOOL MpPromptGetParsedEvent(int index, MpPromptSnapshotEvent* out)
{
	if (!out || index < 0 || index >= g_eventCount) return FALSE;
	FillSnapshotFromEvent(g_events[index], *out);
	return TRUE;
}

CString MpPromptFormatToken(const MpPromptSnapshotEvent& ev)
{
	CString letters;
	if (ev.c2)
		letters.Format(L"%c%c", ev.c1, ev.c2);
	else
		letters.Format(L"%c", ev.c1);

	CString out;
	if (ev.period > 0.001) {
		out = L"%";
		out += letters;
		out += FmtPromptTime(ev.period);
		out += L"<";
		out += FmtPromptTime(ev.t0);
		if (fabs(ev.t1 - ev.t0) >= 0.5)
			out += L"-" + FmtPromptTime(ev.t1);
		out += L">";
	}
	else {
		out = L"@";
		out += letters;
		out += FmtPromptTime(ev.t0);
		if (fabs(ev.t1 - ev.t0) >= 0.5)
			out += L"-" + FmtPromptTime(ev.t1);
	}
	if (!ev.isPreset && ev.hasVal) {
		CString vs;
		if (ev.v0 == ev.v1)
			vs.Format(L"[%d]", ev.v0);
		else
			vs.Format(L"[%d-%d]", ev.v0, ev.v1);
		out += vs;
	}
	return out;
}

CString MpPromptEventsToTextKeepHeader(const CString& prevText)
{
	CString header;
	{
		int pos = 0;
		const int n = prevText.GetLength();
		while (pos < n) {
			int lineEnd = prevText.Find(L'\n', pos);
			if (lineEnd < 0) lineEnd = n;
			CString line = prevText.Mid(pos, lineEnd - pos);
			line.TrimRight(L"\r");
			CString t = line;
			t.Trim();
			if (t.IsEmpty() || t[0] == '#') {
				if (!header.IsEmpty()) header += L"\r\n";
				header += line;
				pos = (lineEnd < n) ? lineEnd + 1 : n;
				continue;
			}
			break;
		}
	}
	CString body;
	for (int i = 0; i < g_eventCount; ++i) {
		MpPromptSnapshotEvent snap;
		FillSnapshotFromEvent(g_events[i], snap);
		if (!body.IsEmpty()) body += L" ";
		body += MpPromptFormatToken(snap);
		if ((i + 1) % 6 == 0) body += L"\r\n";
	}
	if (header.IsEmpty()) return body;
	if (body.IsEmpty()) return header;
	return header + L"\r\n" + body;
}
