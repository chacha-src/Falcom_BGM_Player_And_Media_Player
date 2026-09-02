#include "stdafx.h"
#include "CSasamiScoreMidiIn.h"
#include <math.h>

#pragma comment(lib, "winmm.lib")

static void CALLBACK ScScoreMidiInProc(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR p1, DWORD_PTR)
{
	if (msg != MIM_DATA || !instance) return;
	ScScoreMidiIn* s = (ScScoreMidiIn*)instance;
	if (!s->notify || !::IsWindow(s->notify)) return;
	const DWORD shortMsg = (DWORD)p1;
	const int st = (int)(shortMsg & 0xF0);
	const int ch = (int)(shortMsg & 0x0F) + 1;
	if (s->chFilter > 0 && ch != s->chFilter) return;
	if (s->mode == SC_MIDIIN_OFF) return;
	::PostMessage(s->notify, WM_SASAMI_SCORE_MIDI, (WPARAM)shortMsg, 0);
	(void)st;
}

void ScScoreMidiInInit(ScScoreMidiIn* s)
{
	if (!s) return;
	memset(s, 0, sizeof(*s));
	s->deviceIndex = -1;
	s->chFilter = 0;
	s->mode = SC_MIDIIN_OFF;
	s->velCurve = SC_VEL_LINEAR;
	s->velFixed = 100;
	s->velSense = 100;
	s->quant = SC_QUANT_WEAK;
	s->overdub = 1;
	s->metroOn = 1;
	s->tempoBpm = 120.0;
	for (int i = 0; i < 128; i++) s->heldNote[i] = 0;
}

void ScScoreMidiInShutdown(ScScoreMidiIn* s)
{
	ScScoreMidiInClose(s);
}

void ScScoreMidiInFillDeviceCombo(HWND combo)
{
	if (!combo || !::IsWindow(combo)) return;
	const int prevData = (int)::SendMessage(combo, CB_GETITEMDATA, (WPARAM)::SendMessage(combo, CB_GETCURSEL, 0, 0), 0);
	::SendMessage(combo, CB_RESETCONTENT, 0, 0);
	int idx = (int)::SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)LL14(
		L"(なし)", L"(None)", L"(Aucun)", L"(Nessuno)", L"(Ninguno)",
		L"(없음)", L"(无)", L"(لا شيء)", L"(Нет)", L"(Keiner)",
		L"(Nenhum)", L"(Geen)", L"(Brak)", L"(Yok)"));
	::SendMessage(combo, CB_SETITEMDATA, idx, (LPARAM)-1);
	const UINT n = midiInGetNumDevs();
	for (UINT i = 0; i < n; i++) {
		MIDIINCAPS caps = {};
		if (midiInGetDevCaps(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
		idx = (int)::SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)caps.szPname);
		::SendMessage(combo, CB_SETITEMDATA, idx, (LPARAM)(INT_PTR)i);
	}
	int sel = 0;
	const int count = (int)::SendMessage(combo, CB_GETCOUNT, 0, 0);
	for (int i = 0; i < count; i++) {
		if ((int)::SendMessage(combo, CB_GETITEMDATA, i, 0) == prevData) { sel = i; break; }
	}
	::SendMessage(combo, CB_SETCURSEL, sel, 0);
}

int ScScoreMidiInOpen(ScScoreMidiIn* s, HWND notify, int deviceIndex)
{
	if (!s) return 0;
	ScScoreMidiInClose(s);
	s->notify = notify;
	s->deviceIndex = deviceIndex;
	if (deviceIndex < 0 || !notify) return 1;
	if (midiInOpen(&s->hIn, (UINT)deviceIndex, (DWORD_PTR)&ScScoreMidiInProc,
		(DWORD_PTR)s, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
		s->hIn = NULL;
		s->deviceIndex = -1;
		return 0;
	}
	midiInStart(s->hIn);
	return 1;
}

void ScScoreMidiInClose(ScScoreMidiIn* s)
{
	if (!s) return;
	if (s->hIn) {
		midiInStop(s->hIn);
		midiInReset(s->hIn);
		midiInClose(s->hIn);
		s->hIn = NULL;
	}
	s->recording = 0;
	for (int i = 0; i < 128; i++) s->heldNote[i] = 0;
}

void ScScoreMidiInSetMode(ScScoreMidiIn* s, int mode)
{
	if (!s) return;
	if (mode < SC_MIDIIN_OFF) mode = SC_MIDIIN_OFF;
	if (mode > SC_MIDIIN_REALTIME) mode = SC_MIDIIN_REALTIME;
	s->mode = mode;
	if (mode != SC_MIDIIN_REALTIME)
		ScScoreMidiInStopRealtime(s);
}

int ScScoreMidiInMapVelocity(const ScScoreMidiIn* s, int velIn)
{
	if (!s) return velIn;
	if (velIn <= 0) return 0;
	if (s->velCurve == SC_VEL_FIXED) {
		int v = s->velFixed;
		if (v < 1) v = 1;
		if (v > 127) v = 127;
		return v;
	}
	double t = (double)velIn / 127.0;
	if (s->velCurve == SC_VEL_SOFT)
		t = pow(t, 0.65);
	t *= (double)s->velSense / 100.0;
	int v = (int)(t * 127.0 + 0.5);
	if (v < 1) v = 1;
	if (v > 127) v = 127;
	return v;
}

uint32_t ScScoreMidiInQuantizeTick(const ScScoreMidiIn* s, uint32_t tick, int ppqn, int swing)
{
	if (!s || s->quant == SC_QUANT_OFF || ppqn <= 0) return tick;
	int grid = ppqn / 4; /* 16th */
	if (s->quant == SC_QUANT_STRONG) grid = ppqn / 2; /* 8th */
	if (s->quant == SC_QUANT_WEAK) grid = ppqn / 4;
	if (s->quant == SC_QUANT_SWING) grid = ppqn / 2;
	if (grid < 1) grid = 1;
	uint32_t q = ((tick + (uint32_t)grid / 2) / (uint32_t)grid) * (uint32_t)grid;
	if (s->quant == SC_QUANT_SWING) {
		const uint32_t eighth = (uint32_t)(ppqn / 2);
		if (eighth > 0 && ((q / eighth) & 1))
			q += (eighth * 33) / 100;
	}
	(void)swing;
	return q;
}

uint32_t ScScoreMidiInNowTick(const ScScoreMidiIn* s, int ppqn)
{
	if (!s || !s->recording || s->tempoBpm <= 1.0 || ppqn <= 0) return s ? s->recOriginTick : 0;
	const DWORD now = timeGetTime();
	const double elapsedMs = (double)(now - s->recStartMs);
	const double ticks = elapsedMs * s->tempoBpm * (double)ppqn / 60000.0;
	uint32_t t = s->recOriginTick + (uint32_t)(ticks + 0.5);
	if (s->recLoopEnd > s->recOriginTick) {
		const uint32_t span = s->recLoopEnd - s->recOriginTick;
		if (span > 0)
			t = s->recOriginTick + ((t - s->recOriginTick) % span);
	}
	return t;
}

void ScScoreMidiInStartRealtime(ScScoreMidiIn* s, uint32_t originTick, uint32_t loopEndTick, double bpm)
{
	if (!s) return;
	s->recording = 1;
	s->recOriginTick = originTick;
	s->recLoopEnd = loopEndTick;
	s->tempoBpm = (bpm > 1.0) ? bpm : 120.0;
	s->recStartMs = timeGetTime();
	for (int i = 0; i < 128; i++) { s->heldNote[i] = 0; s->heldOnTick[i] = 0; }
	s->pedDown = 0;
}

void ScScoreMidiInStopRealtime(ScScoreMidiIn* s)
{
	if (!s) return;
	s->recording = 0;
	for (int i = 0; i < 128; i++) s->heldNote[i] = 0;
}

void ScScoreMidiInMetroTick(ScScoreMidiIn* s, int beatInMeasure)
{
	if (!s || !s->metroOn) return;
	/* Simple MessageBeep accents — no extra audio device deps. */
	if (beatInMeasure == 0)
		MessageBeep(MB_OK);
	else
		MessageBeep(MB_ICONASTERISK);
}
