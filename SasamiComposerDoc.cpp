#include "stdafx.h"
#include "SasamiComposerDoc.h"
#include "DatArchive.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>

void ScMidiVstBindFreeStates(ScMidiVstBind* b)
{
	if (!b) return;
	for (int i = 0; i < 32; i++) {
		if (b->vstComp[i]) { free(b->vstComp[i]); b->vstComp[i] = NULL; }
		if (b->vstCtrl[i]) { free(b->vstCtrl[i]); b->vstCtrl[i] = NULL; }
		b->vstCompLen[i] = b->vstCtrlLen[i] = 0;
	}
}

void ScMidiVstBindClear(ScMidiVstBind* b)
{
	if (!b) return;
	ScMidiVstBindFreeStates(b);
	memset(b, 0, sizeof(*b));
	for (int i = 0; i < 32; i++) {
		b->vstProg[i] = -1;
		b->vstForceCh[i] = -1;
	}
}

void ScMidiVstBindSetState(ScMidiVstBind* b, int ch0to31,
	const uint8_t* comp, uint32_t compLen,
	const uint8_t* ctrl, uint32_t ctrlLen)
{
	if (!b || ch0to31 < 0 || ch0to31 > 31) return;
	SasamiVstBlobSet(&b->vstComp[ch0to31], &b->vstCompLen[ch0to31], comp, compLen);
	SasamiVstBlobSet(&b->vstCtrl[ch0to31], &b->vstCtrlLen[ch0to31], ctrl, ctrlLen);
}

void ScMidiFxBindFreeStates(ScMidiFxBind* b)
{
	if (!b) return;
	for (int ch = 0; ch < 32; ++ch)
		for (int sl = 0; sl < ScMidiFxBind::SC_FX_SLOTS; ++sl) {
			if (b->fxState[ch][sl]) { free(b->fxState[ch][sl]); b->fxState[ch][sl] = NULL; }
			b->fxStateLen[ch][sl] = 0;
		}
}

void ScMidiFxBindClear(ScMidiFxBind* b)
{
	if (!b) return;
	ScMidiFxBindFreeStates(b);
	memset(b, 0, sizeof(*b));
}

void ScMidiFxBindSetState(ScMidiFxBind* b, int ch0to31, int slot0to1,
	const uint8_t* state, uint32_t stateLen)
{
	if (!b || ch0to31 < 0 || ch0to31 > 31 ||
		slot0to1 < 0 || slot0to1 >= ScMidiFxBind::SC_FX_SLOTS) return;
	SasamiVstBlobSet(&b->fxState[ch0to31][slot0to1],
		&b->fxStateLen[ch0to31][slot0to1], state, stateLen);
}

static const char kB64Tab[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int ScB64Encode(const uint8_t* in, uint32_t inLen, wchar_t* out, int outCch)
{
	if (!out || outCch < 1) return 0;
	out[0] = 0;
	if (!in || !inLen) return 1;
	uint32_t need = ((inLen + 2) / 3) * 4;
	if ((int)need + 1 > outCch) return 0;
	int o = 0;
	for (uint32_t i = 0; i < inLen; i += 3) {
		uint32_t n = (uint32_t)in[i] << 16;
		if (i + 1 < inLen) n |= (uint32_t)in[i + 1] << 8;
		if (i + 2 < inLen) n |= (uint32_t)in[i + 2];
		out[o++] = (wchar_t)kB64Tab[(n >> 18) & 63];
		out[o++] = (wchar_t)kB64Tab[(n >> 12) & 63];
		out[o++] = (i + 1 < inLen) ? (wchar_t)kB64Tab[(n >> 6) & 63] : L'=';
		out[o++] = (i + 2 < inLen) ? (wchar_t)kB64Tab[n & 63] : L'=';
	}
	out[o] = 0;
	return 1;
}

static int ScB64Val(wchar_t c)
{
	if (c >= L'A' && c <= L'Z') return c - L'A';
	if (c >= L'a' && c <= L'z') return c - L'a' + 26;
	if (c >= L'0' && c <= L'9') return c - L'0' + 52;
	if (c == L'+') return 62;
	if (c == L'/') return 63;
	return -1;
}

static int ScB64Decode(const wchar_t* in, uint8_t** outBytes, uint32_t* outLen)
{
	if (outBytes) *outBytes = NULL;
	if (outLen) *outLen = 0;
	if (!in || !outBytes || !outLen) return 0;
	int nchar = 0;
	for (const wchar_t* p = in; *p; ++p) {
		if (*p == L'=' || ScB64Val(*p) >= 0) ++nchar;
	}
	if (nchar <= 0) return 1;
	uint8_t* buf = (uint8_t*)malloc((size_t)nchar);
	if (!buf) return 0;
	int o = 0;
	int val = 0, valb = -8;
	for (const wchar_t* p = in; *p; ++p) {
		if (*p == L'=') break;
		int d = ScB64Val(*p);
		if (d < 0) continue;
		val = (val << 6) + d;
		valb += 6;
		if (valb >= 0) {
			buf[o++] = (uint8_t)((val >> valb) & 0xFF);
			valb -= 8;
		}
	}
	*outBytes = buf;
	*outLen = (uint32_t)o;
	return 1;
}

void ScMidiDocClear(ScMidiDoc* d)
{
	if (!d) return;
	ScMidiVstBindFreeStates(&d->bind);
	ScMidiFxBindFreeStates(&d->fxBind);
	memset(d, 0, sizeof(*d));
	d->tempoT = 13000;
	d->numer = 4;
	d->denom = 4;
	for (int i = 0; i < 32; i++) {
		d->bind.vstProg[i] = -1;
		d->bind.vstForceCh[i] = -1;
		d->trackPart[i] = 0xFF;
	}
}


void ScFmDocClear(ScFmDoc* d)
{
	if (!d) return;
	memset(d, 0, sizeof(*d));
	d->tempoT = 13000;
	d->opna10 = 1;
}

static int ScPush(ScEvent* ev, int* n, uint32_t tick, uint8_t ch, uint8_t kind, uint8_t a, uint8_t b, uint8_t c, uint16_t dur)
{
	if (!ev || !n || *n >= SC_EV_MAX) return 0;
	ScEvent* e = &ev[*n];
	e->tick = tick;
	e->seq = (uint32_t)(*n);
	e->ch = ch;
	e->kind = kind;
	e->a = a;
	e->b = b;
	e->c = c;
	e->dur = dur;
	(*n)++;
	return 1;
}

/* Same tick+ch+kind → update (and drop duplicates). Avoids stacking |:2 |:6 at one beat. */
static int ScUpsertMark(ScEvent* ev, int* n, uint32_t tick, uint8_t ch, uint8_t kind,
	uint8_t a, uint8_t b, uint8_t c, uint16_t dur)
{
	if (!ev || !n) return 0;
	int kept = -1;
	for (int i = *n - 1; i >= 0; i--) {
		if (ev[i].tick != tick || ev[i].ch != ch || ev[i].kind != kind) continue;
		if (kept < 0) {
			kept = i;
			ev[i].a = a;
			ev[i].b = b;
			ev[i].c = c;
			ev[i].dur = dur;
		} else {
			for (int j = i; j + 1 < *n; j++)
				ev[j] = ev[j + 1];
			(*n)--;
			if (kept > i) kept--;
		}
	}
	if (kept >= 0) return 1;
	return ScPush(ev, n, tick, ch, kind, a, b, c, dur);
}

int ScMidiAddNote(ScMidiDoc* d, uint32_t tick, int ch, int note, int dur, int vel)
{
	if (!d || ch < 0 || ch >= SC_MIDI_CH) return 0;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_NOTE, (uint8_t)(note & 127), (uint8_t)(vel & 127), 0, (uint16_t)dur);
}

int ScMidiAddRest(ScMidiDoc* d, uint32_t tick, int ch, int dur)
{
	if (!d || ch < 0 || ch >= SC_MIDI_CH) return 0;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_REST, 0, 0, 0, (uint16_t)dur);
}

int ScMidiAddJumpMark(ScMidiDoc* d, uint32_t tick, int ch)
{
	if (!d || ch < 0 || ch >= SC_MIDI_CH) return 0;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_JUMP_MARK, 0, 0, 0, 0);
}

int ScMidiAddJump(ScMidiDoc* d, uint32_t tick, int ch)
{
	if (!d || ch < 0 || ch >= SC_MIDI_CH) return 0;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_JUMP, 0, 0, 0, 0);
}

int ScMidiAddLoopStart(ScMidiDoc* d, uint32_t tick, int ch, int repeatN, int stack)
{
	if (!d || ch < 0 || ch >= SC_MIDI_CH) return 0;
	if (repeatN <= 0) repeatN = 2;
	if (repeatN > 99) repeatN = 99;
	if (stack)
		return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_LOOP_START,
			(uint8_t)repeatN, 0, 0, 0);
	return ScUpsertMark(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_LOOP_START,
		(uint8_t)repeatN, 0, 0, 0);
}

int ScMidiAddLoopEnd(ScMidiDoc* d, uint32_t tick, int ch, int stack)
{
	if (!d || ch < 0 || ch >= SC_MIDI_CH) return 0;
	if (stack)
		return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_LOOP_END, 0, 0, 0, 0);
	return ScUpsertMark(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_LOOP_END, 0, 0, 0, 0);
}

int ScMidiAddPedalOn(ScMidiDoc* d, uint32_t tick, int ch, int stack)
{
	if (!d || ch < 0 || ch >= SC_MIDI_CH) return 0;
	if (stack)
		return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_PEDAL_ON, 0, 0, 0, 0);
	return ScUpsertMark(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_PEDAL_ON, 0, 0, 0, 0);
}

int ScMidiAddPedalOff(ScMidiDoc* d, uint32_t tick, int ch, int stack)
{
	if (!d || ch < 0 || ch >= SC_MIDI_CH) return 0;
	if (stack)
		return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_PEDAL_OFF, 0, 0, 0, 0);
	return ScUpsertMark(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_PEDAL_OFF, 0, 0, 0, 0);
}

int ScMidiAddRpn(ScMidiDoc* d, uint32_t tick, int ch, int msb, int lsb, int data)
{
	if (!d || ch < 0 || ch >= SC_MIDI_CH) return 0;
	if (msb < 0) msb = 0; if (msb > 127) msb = 127;
	if (lsb < 0) lsb = 0; if (lsb > 127) lsb = 127;
	if (data < 0) data = 0; if (data > 127) data = 127;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_RPN,
		(uint8_t)msb, (uint8_t)lsb, (uint8_t)data, 0);
}

int ScMidiAddNrpn(ScMidiDoc* d, uint32_t tick, int ch, int msb, int lsb, int data)
{
	if (!d || ch < 0 || ch >= SC_MIDI_CH) return 0;
	if (msb < 0) msb = 0; if (msb > 127) msb = 127;
	if (lsb < 0) lsb = 0; if (lsb > 127) lsb = 127;
	if (data < 0) data = 0; if (data > 127) data = 127;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_NRPN,
		(uint8_t)msb, (uint8_t)lsb, (uint8_t)data, 0);
}

int ScMidiAddSysex(ScMidiDoc* d, uint32_t tick, int ch, const uint8_t* bytes, int len)
{
	if (!d || ch < 0 || ch >= SC_MIDI_CH || !bytes || len <= 0) return 0;
	if (d->sysexCount >= ScMidiDoc::SC_SYSEX_MAX) return 0;
	if (len > ScMidiDoc::SC_SYSEX_BYTES) len = ScMidiDoc::SC_SYSEX_BYTES;
	const int idx = d->sysexCount++;
	memcpy(d->sysex[idx], bytes, (size_t)len);
	d->sysexLen[idx] = (uint16_t)len;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_SYSEX,
		(uint8_t)idx, 0, 0, 0);
}

int ScFmAddNote(ScFmDoc* d, uint32_t tick, int ch, uint8_t noteByte, int dur)
{
	if (!d || ch < 0 || ch >= SC_FM_TOTAL) return 0;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_NOTE, noteByte, 0, 0, (uint16_t)dur);
}

int ScFmAddRest(ScFmDoc* d, uint32_t tick, int ch, int dur)
{
	if (!d || ch < 0 || ch >= SC_FM_TOTAL) return 0;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_REST, 0, 0, 0, (uint16_t)dur);
}

int ScFmAddJumpMark(ScFmDoc* d, uint32_t tick, int ch)
{
	if (!d || ch < 0 || ch >= SC_FM_TOTAL) return 0;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_JUMP_MARK, 0, 0, 0, 0);
}

int ScFmAddJump(ScFmDoc* d, uint32_t tick, int ch)
{
	if (!d || ch < 0 || ch >= SC_FM_TOTAL) return 0;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_JUMP, 0, 0, 0, 0);
}

int ScFmAddLoopStart(ScFmDoc* d, uint32_t tick, int ch, int repeatN, int stack)
{
	if (!d || ch < 0 || ch >= SC_FM_TOTAL) return 0;
	if (repeatN <= 0) repeatN = 2;
	if (repeatN > 99) repeatN = 99;
	if (stack)
		return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_LOOP_START,
			(uint8_t)repeatN, 0, 0, 0);
	return ScUpsertMark(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_LOOP_START,
		(uint8_t)repeatN, 0, 0, 0);
}

int ScFmAddLoopEnd(ScFmDoc* d, uint32_t tick, int ch, int stack)
{
	if (!d || ch < 0 || ch >= SC_FM_TOTAL) return 0;
	if (stack)
		return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_LOOP_END, 0, 0, 0, 0);
	return ScUpsertMark(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_LOOP_END, 0, 0, 0, 0);
}

int ScFmAllocVoice(ScFmDoc* d, const uint8_t voice25[25])
{
	if (!d || !voice25 || d->voiceCount >= SC_VOICE_MAX) return -1;
	/* reuse identical */
	for (int i = 0; i < d->voiceCount; i++) {
		if (memcmp(d->voices[i], voice25, 25) == 0) return i;
	}
	memcpy(d->voices[d->voiceCount], voice25, 25);
	return d->voiceCount++;
}

int ScFmAddVoiceSelect(ScFmDoc* d, uint32_t tick, int ch, int voiceIdx, int isCustom)
{
	if (!d || ch < 0 || ch >= SC_FM_TOTAL) return 0;
	if (voiceIdx < 0) voiceIdx = 0;
	if (voiceIdx > 63) voiceIdx = 63;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_VOICE,
		(uint8_t)voiceIdx, isCustom ? 1 : 0, 0, 0);
}

int ScFmAddVolTl(ScFmDoc* d, uint32_t tick, int ch, int tl)
{
	if (!d || ch < 0 || ch >= SC_FM_TOTAL) return 0;
	if (tl < 0) tl = 0;
	if (tl > 127) tl = 127;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_FM_VOL, (uint8_t)tl, 0, 0, 0);
}

int ScFmAddPcmSample(ScFmDoc* d, uint32_t tick, int ch, int slot)
{
	if (!d || ch < SC_FM_CH || ch >= SC_FM_TOTAL) return 0;
	if (slot < 0) slot = 0;
	if (slot > 127) slot = 127;
	return ScPush(d->ev, &d->evCount, tick, (uint8_t)ch, SC_EV_PCM_SAMPLE, (uint8_t)slot, 0, 0, 0);
}


static int IsWs(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r'; }
static int IsDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }
static int IsHexDigit(wchar_t c)
{
	return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}
static int HexVal(wchar_t c)
{
	if (c >= L'0' && c <= L'9') return c - L'0';
	if (c >= L'a' && c <= L'f') return c - L'a' + 10;
	if (c >= L'A' && c <= L'F') return c - L'A' + 10;
	return -1;
}
static int ContainsIW(const wchar_t* text, const wchar_t* needle)
{
	if (!text || !needle || !needle[0]) return 0;
	const size_t n = wcslen(needle);
	for (const wchar_t* p = text; *p; ++p)
		if (_wcsnicmp(p, needle, n) == 0) return 1;
	return 0;
}

static int ParseInt(const wchar_t** pp)
{
	const wchar_t* p = *pp;
	int v = 0, any = 0;
	while (IsDigit(*p)) { v = v * 10 + (*p - L'0'); p++; any = 1; }
	*pp = p;
	return any ? v : -1;
}

static int ParseMidiTriplet(const wchar_t** pp, int* a, int* b, int* c)
{
	const wchar_t* p = *pp;
	while (IsWs(*p)) p++;
	int x = ParseInt(&p);
	while (IsWs(*p) || *p == L',' || *p == L':') p++;
	int y = ParseInt(&p);
	while (IsWs(*p) || *p == L',' || *p == L':') p++;
	int z = ParseInt(&p);
	if (x < 0 || y < 0 || z < 0) return 0;
	if (x > 127) x = 127;
	if (y > 127) y = 127;
	if (z > 127) z = 127;
	if (a) *a = x; if (b) *b = y; if (c) *c = z;
	*pp = p;
	return 1;
}

static int ScNamedSysex(const wchar_t* s, uint8_t* out, int maxOut)
{
	if (!s || !out || maxOut < 16) return 0;
	static const uint8_t gmOn[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
	static const uint8_t gm2On[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x03, 0xF7 };
	static const uint8_t gsReset[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
	static const uint8_t xgOn[] = { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00, 0xF7 };
	static const uint8_t gsMasterVol[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x04, 0x64, 0x58, 0xF7 };
	const uint8_t* src = NULL;
	int n = 0;
	if (ContainsIW(s, L"GM2")) { src = gm2On; n = (int)sizeof(gm2On); }
	else if (ContainsIW(s, L"GM")) { src = gmOn; n = (int)sizeof(gmOn); }
	else if (ContainsIW(s, L"GS") && ContainsIW(s, L"VOL")) { src = gsMasterVol; n = (int)sizeof(gsMasterVol); }
	else if (ContainsIW(s, L"GS")) { src = gsReset; n = (int)sizeof(gsReset); }
	else if (ContainsIW(s, L"XG")) { src = xgOn; n = (int)sizeof(xgOn); }
	if (!src || n <= 0 || n > maxOut) return 0;
	memcpy(out, src, (size_t)n);
	return n;
}

static int ParseSysexBody(const wchar_t** pp, uint8_t* out, int maxOut)
{
	const wchar_t* p = *pp;
	while (IsWs(*p)) p++;
	wchar_t named[80];
	int ni = 0;
	if (*p == L'"') {
		p++;
		while (*p && *p != L'"' && ni < 79) named[ni++] = *p++;
		named[ni] = 0;
		if (*p == L'"') p++;
		int n = ScNamedSysex(named, out, maxOut);
		if (n > 0) { *pp = p; return n; }
		p = named;
	} else {
		const wchar_t* q = p;
		while (*q && *q != L'\r' && *q != L'\n' && ni < 79) named[ni++] = *q++;
		named[ni] = 0;
		int n = ScNamedSysex(named, out, maxOut);
		if (n > 0) { *pp = q; return n; }
	}
	int n = 0;
	while (*p && *p != L'\r' && *p != L'\n' && n < maxOut) {
		while (IsWs(*p) || *p == L',' || *p == L':' || *p == L'{' || *p == L'}') p++;
		if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) p += 2;
		if (!IsHexDigit(*p)) break;
		int hi = HexVal(*p++);
		int lo = 0;
		if (IsHexDigit(*p)) lo = HexVal(*p++);
		out[n++] = (uint8_t)((hi << 4) | lo);
	}
	*pp = p;
	return n;
}

static int NoteIndex(wchar_t c)
{
	switch (c) {
	case L'c': case L'C': return 0;
	case L'd': case L'D': return 2;
	case L'e': case L'E': return 4;
	case L'f': case L'F': return 5;
	case L'g': case L'G': return 7;
	case L'a': case L'A': return 9;
	case L'b': case L'B': return 11;
	default: return -1;
	}
}

static int LenToTicks(int len, int defLen, int dots)
{
	if (len <= 0) len = defLen;
	if (len <= 0) len = 4;
	int ticks = (SC_PPQN * 4) / len;
	int add = ticks / 2;
	for (int i = 0; i < dots && add > 0; i++) {
		ticks += add;
		add /= 2;
	}
	if (ticks < 1) ticks = 1;
	return ticks;
}

/* SASAMI tempo: T ≈ 13000 at 120BPM → T = 13000 * 120 / bpm */
static int BpmToT(int bpm)
{
	if (bpm < 20) bpm = 20;
	if (bpm > 400) bpm = 400;
	return (int)((13000.0 * 120.0) / (double)bpm + 0.5);
}

int ScCompileMidiMml(const wchar_t* text, ScMidiDoc* out, int* errLine, wchar_t* errMsg, int errMsgCch)
{
	if (!text || !out) return 0;
	ScMidiDocClear(out);
	if (errLine) *errLine = 0;
	if (errMsg && errMsgCch > 0) errMsg[0] = 0;

	int ch = 0;
	uint32_t tick[SC_MIDI_CH];
	memset(tick, 0, sizeof(tick));
	int oct = 4, defLen = 4, vel = 105, pan = 64;
	int line = 1;
	const wchar_t* p = text;
	int tempoWritten = 0;
	int lastNoteEv = -1;
	/* {n ... } / |:n ... :| expand: fixed stack */
	enum { SC_LOOP_MAX = SC_LOOP_NEST_MAX };
	const wchar_t* loopPos[SC_LOOP_MAX];
	int loopLeft[SC_LOOP_MAX];
	int loopSp = 0;
	const wchar_t* chStart[SC_MIDI_CH];
	int chLoopLeft[SC_MIDI_CH];
	memset(chStart, 0, sizeof(chStart));
	memset(chLoopLeft, 0, sizeof(chLoopLeft));

	auto fail = [&](const wchar_t* msg) -> int {
		if (errLine) *errLine = line;
		if (errMsg && errMsgCch > 0) wcsncpy_s(errMsg, errMsgCch, msg, _TRUNCATE);
		return 0;
	};
	auto parseLenDots = [&](int* outLen, int* outDots) {
		*outLen = ParseInt(&p);
		*outDots = 0;
		while (*p == L'.') { (*outDots)++; p++; }
	};
	auto skipBalanced = [&](wchar_t openc, wchar_t closec) {
		if (*p != openc) return;
		int depth = 0;
		while (*p) {
			if (*p == openc) depth++;
			else if (*p == closec) {
				depth--;
				p++;
				if (depth <= 0) return;
				continue;
			} else if (*p == L'\n') line++;
			p++;
		}
	};

	while (*p) {
		if (*p == L'\n') { line++; p++; continue; }
		if (IsWs(*p)) { p++; continue; }
		if (*p == L';') {
			while (*p && *p != L'\n') p++;
			continue;
		}
		if (p[0] == L'/' && p[1] == L'/') {
			while (*p && *p != L'\n') p++;
			continue;
		}
		if (p[0] == L'/' && p[1] == L'*') {
			p += 2;
			while (*p && !(p[0] == L'*' && p[1] == L'/')) {
				if (*p == L'\n') line++;
				p++;
			}
			if (*p) p += 2;
			continue;
		}
		/* PMD/MICP |:n ... :| — native MPY loop cmds 23/24 (do NOT unroll; matches DO--.MPY). */
		if (p[0] == L'|' && p[1] == L':') {
			p += 2;
			int n = ParseInt(&p);
			if (n <= 0) n = 2;
			if (n > 99) n = 99;
			if (!ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_LOOP_START,
				(uint8_t)n, 0, 0, 0)) return fail(L"overflow");
			continue;
		}
		if ((p[0] == L':' && p[1] == L'|') || (p[0] == L':' && p[1] == L']')) {
			p += 2;
			if (!ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_LOOP_END,
				0, 0, 0, 0)) return fail(L"overflow");
			continue;
		}
		/* bar / visual separators in MICP dumps */
		if (*p == L'|' || *p == L'~' || *p == L'$') { p++; continue; }

		/* title TIT"..." */
		if ((p[0] == L'T' || p[0] == L't') && (p[1] == L'I' || p[1] == L'i') && (p[2] == L'T' || p[2] == L't')
			&& !IsDigit(p[3])) {
			p += 3;
			while (IsWs(*p)) p++;
			if (*p == L'"') {
				p++;
				char sj[65]; int si = 0;
				while (*p && *p != L'"' && si < 63) {
					wchar_t wc = *p++;
					char mb[8];
					int n = WideCharToMultiByte(932, 0, &wc, 1, mb, 8, NULL, NULL);
					if (n > 0 && si + n < 64) { memcpy(sj + si, mb, n); si += n; }
				}
				sj[si] = 0;
				if (*p == L'"') p++;
				strncpy_s(out->titleSjis, sj, _TRUNCATE);
			}
			continue;
		}

		/* [midiCh:dataArea] or [ch] or #ch — DO--.MPY packs by dataArea, part=midiCh-1 */
		if (*p == L'[' || *p == L'#') {
			const int br = (*p == L'[');
			p++;
			while (IsWs(*p)) p++;
			int a = ParseInt(&p);
			int b = -1;
			while (IsWs(*p)) p++;
			if (*p == L':') {
				p++;
				while (IsWs(*p)) p++;
				b = ParseInt(&p);
			}
			while (IsWs(*p)) p++;
			if (br) {
				if (*p != L']') return fail(L"[part] missing ]");
				p++;
			}
			if (a < 1) return fail(L"channel 1..32");
			if (a > 16) {
				out->bind.dualPort = 1;
			}
			if (a > SC_MIDI_CH) return fail(L"channel 1..32");
			/* part = MIDI ch (0-based). Track slot = dataArea-1 when present (DO--.MPY). */
			int part = a - 1;
			if (part > 15) part = part & 15;
			if (b >= 1) {
				if (b > SC_MIDI_CH) return fail(L"dataArea 1..32");
				ch = b - 1;
			} else {
				ch = a - 1;
			}
			out->trackPart[ch] = (uint8_t)part;
			oct = 4; defLen = 4; vel = 105; pan = 64;
			lastNoteEv = -1;
			chStart[ch] = p;
			chLoopLeft[ch] = 1;
			continue;
		}

		/* @VST / @PROG / @BANK / @n[:m] / @letter… */
		if (*p == L'@') {
			p++;
			if (_wcsnicmp(p, L"VSTFX", 5) == 0) {
				p += 5;
				while (IsWs(*p)) p++;
				int slot = ParseInt(&p);
				if (slot < 0) slot = 0;
				if (slot >= ScMidiFxBind::SC_FX_SLOTS) slot = ScMidiFxBind::SC_FX_SLOTS - 1;
				while (IsWs(*p) || *p == L',' || *p == L':') p++;
				if (*p != L'"') return fail(L"@VSTFX slot \"path\"");
				p++;
				wchar_t path[260]; int pi = 0;
				while (*p && *p != L'"' && pi < 259) path[pi++] = *p++;
				path[pi] = 0;
				if (*p == L'"') p++;
				wcsncpy_s(out->fxBind.fxPath[ch][slot], path, _TRUNCATE);
				out->bind.isMpw3 = 1;
				continue;
			}
			/* @VSTFXSTATEB64 slot  / @VSTFXSTATEB64+ slot */
			if (_wcsnicmp(p, L"VSTFXSTATEB64", 13) == 0) {
				p += 13;
				int cont = 0;
				if (*p == L'+') { cont = 1; p++; }
				while (IsWs(*p)) p++;
				int slot = ParseInt(&p);
				if (slot < 0) slot = 0;
				if (slot >= ScMidiFxBind::SC_FX_SLOTS) slot = ScMidiFxBind::SC_FX_SLOTS - 1;
				while (IsWs(*p)) p++;
				if (*p == 0x2026 || (*p == L'.' && p[1] == L'.')) {
					while (*p && *p != L'\r' && *p != L'\n') p++;
					continue;
				}
				wchar_t chunk[4096];
				int ci = 0;
				while (*p && *p != L'\r' && *p != L'\n' && ci < 4095)
					chunk[ci++] = *p++;
				chunk[ci] = 0;
				uint8_t* decoded = NULL;
				uint32_t dlen = 0;
				if (!ScB64Decode(chunk, &decoded, &dlen)) return fail(L"fx b64");
				uint8_t** dst = &out->fxBind.fxState[ch][slot];
				uint32_t* dstLen = &out->fxBind.fxStateLen[ch][slot];
				if (!cont) {
					SasamiVstBlobSet(dst, dstLen, decoded, dlen);
				} else if (decoded && dlen) {
					uint32_t nl = *dstLen + dlen;
					uint8_t* nb = (uint8_t*)realloc(*dst, nl ? nl : 1);
					if (!nb) { free(decoded); return fail(L"oom"); }
					memcpy(nb + *dstLen, decoded, dlen);
					*dst = nb;
					*dstLen = nl;
				}
				if (decoded) free(decoded);
				out->bind.isMpw3 = 1;
				continue;
			}
			if (_wcsnicmp(p, L"VST", 3) == 0 &&
				_wcsnicmp(p, L"VSTSTATEB64", 11) != 0 &&
				_wcsnicmp(p, L"VSTCTRLB64", 10) != 0 &&
				_wcsnicmp(p, L"VSTFXSTATEB64", 13) != 0) {
				p += 3;
				while (IsWs(*p)) p++;
				if (*p != L'"') return fail(L"@VST\"path\"");
				p++;
				wchar_t path[260]; int pi = 0;
				while (*p && *p != L'"' && pi < 259) path[pi++] = *p++;
				path[pi] = 0;
				if (*p == L'"') p++;
				wcsncpy_s(out->bind.vstPath[ch], path, _TRUNCATE);
				out->bind.isMpw3 = 1;
				continue;
			}
			if (_wcsnicmp(p, L"RPN", 3) == 0) {
				p += 3;
				int a = 0, b = 0, c = 0;
				if (!ParseMidiTriplet(&p, &a, &b, &c)) return fail(L"@RPN msb,lsb,data");
				if (!ScMidiAddRpn(out, tick[ch], ch, a, b, c)) return fail(L"overflow");
				continue;
			}
			if (_wcsnicmp(p, L"NRPN", 4) == 0) {
				p += 4;
				int a = 0, b = 0, c = 0;
				if (!ParseMidiTriplet(&p, &a, &b, &c)) return fail(L"@NRPN msb,lsb,data");
				if (!ScMidiAddNrpn(out, tick[ch], ch, a, b, c)) return fail(L"overflow");
				continue;
			}
			if (_wcsnicmp(p, L"EX", 2) == 0) {
				p += 2;
				uint8_t sx[ScMidiDoc::SC_SYSEX_BYTES];
				int n = ParseSysexBody(&p, sx, ScMidiDoc::SC_SYSEX_BYTES);
				if (n <= 0) return fail(L"@EX name-or-hex");
				if (!ScMidiAddSysex(out, tick[ch], ch, sx, n)) return fail(L"overflow");
				continue;
			}
			if (_wcsnicmp(p, L"PEDON", 5) == 0 || _wcsnicmp(p, L"PEDALON", 7) == 0) {
				p += (_wcsnicmp(p, L"PEDALON", 7) == 0) ? 7 : 5;
				if (!ScMidiAddPedalOn(out, tick[ch], ch, 1)) return fail(L"overflow");
				continue;
			}
			if (_wcsnicmp(p, L"PEDOFF", 6) == 0 || _wcsnicmp(p, L"PEDALOFF", 8) == 0) {
				p += (_wcsnicmp(p, L"PEDALOFF", 8) == 0) ? 8 : 6;
				if (!ScMidiAddPedalOff(out, tick[ch], ch, 1)) return fail(L"overflow");
				continue;
			}
			/* @VSTSTATEB64 / @VSTSTATEB64+ / @VSTCTRLB64 / @VSTCTRLB64+ */
			if (_wcsnicmp(p, L"VSTSTATEB64", 11) == 0 || _wcsnicmp(p, L"VSTCTRLB64", 10) == 0) {
				const int isCtrl = (_wcsnicmp(p, L"VSTCTRLB64", 10) == 0) ? 1 : 0;
				p += isCtrl ? 10 : 11;
				int cont = 0;
				if (*p == L'+') { cont = 1; p++; }
			while (IsWs(*p)) p++;
			/* Folded placeholder from score→text sync (no payload). */
			if (*p == 0x2026 || (*p == L'.' && p[1] == L'.')) {
				while (*p && *p != L'\r' && *p != L'\n') p++;
				continue;
			}
			wchar_t chunk[4096];
				int ci = 0;
				while (*p && *p != L'\r' && *p != L'\n' && ci < 4095)
					chunk[ci++] = *p++;
				chunk[ci] = 0;
				/* Accumulate into temporary grow buffer on bind slot via decode-append. */
				uint8_t* decoded = NULL;
				uint32_t dlen = 0;
				if (!ScB64Decode(chunk, &decoded, &dlen)) return fail(L"b64");
				uint8_t** slot = isCtrl ? &out->bind.vstCtrl[ch] : &out->bind.vstComp[ch];
				uint32_t* slotLen = isCtrl ? &out->bind.vstCtrlLen[ch] : &out->bind.vstCompLen[ch];
				if (!cont) {
					SasamiVstBlobSet(slot, slotLen, decoded, dlen);
				} else if (decoded && dlen) {
					uint32_t nl = *slotLen + dlen;
					uint8_t* nb = (uint8_t*)realloc(*slot, nl ? nl : 1);
					if (!nb) { free(decoded); return fail(L"oom"); }
					memcpy(nb + *slotLen, decoded, dlen);
					*slot = nb;
					*slotLen = nl;
				}
				if (decoded) free(decoded);
				out->bind.isMpw3 = 1;
				continue;
			}
			if (_wcsnicmp(p, L"PROG", 4) == 0) {
				p += 4;
				while (IsWs(*p)) p++;
				int n = ParseInt(&p);
				if (n < 0) return fail(L"@PROG n");
				out->bind.vstProg[ch] = n;
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_PROG, (uint8_t)n, (uint8_t)n, 0, 0);
				continue;
			}
			if (_wcsnicmp(p, L"BANK", 4) == 0) {
				p += 4;
				while (IsWs(*p)) p++;
				int msb = ParseInt(&p);
				while (IsWs(*p) || *p == L',') p++;
				int lsb = ParseInt(&p);
				if (msb < 0) msb = 0;
				if (lsb < 0) lsb = 0;
				out->bind.vstBankMsb[ch] = msb;
				out->bind.vstBankLsb[ch] = lsb;
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_BANK, (uint8_t)msb, (uint8_t)lsb, 0, 0);
				continue;
			}
			/* @METER 4/4 or @TS 7/8 — MPW3 time signature (measure grid). */
			if (_wcsnicmp(p, L"METER", 5) == 0) {
				p += 5;
				while (IsWs(*p)) p++;
				int numer = ParseInt(&p);
				while (IsWs(*p) || *p == L'/' || *p == L',' || *p == L':') p++;
				int denom = ParseInt(&p);
				if (numer < 1) numer = 4;
				if (numer > 32) numer = 32;
				if (denom < 1) denom = 4;
				if (denom > 32) denom = 32;
				out->numer = numer;
				out->denom = denom;
				out->bind.isMpw3 = 1;
				continue;
			}
			if (_wcsnicmp(p, L"TS", 2) == 0 && !((p[2] >= L'A' && p[2] <= L'Z') || (p[2] >= L'a' && p[2] <= L'z'))) {
				p += 2;
				while (IsWs(*p)) p++;
				int numer = ParseInt(&p);
				while (IsWs(*p) || *p == L'/' || *p == L',' || *p == L':') p++;
				int denom = ParseInt(&p);
				if (numer < 1) numer = 4;
				if (numer > 32) numer = 32;
				if (denom < 1) denom = 4;
				if (denom > 32) denom = 32;
				out->numer = numer;
				out->denom = denom;
				out->bind.isMpw3 = 1;
				continue;
			}
			/* @63:85 tone — DO--.MPY stores PROG as (n-1),(m-1) in one cmd2 (no separate BANK). */
			if (IsDigit(*p)) {
				int prog = ParseInt(&p);
				int bank = -1;
				if (*p == L':') {
					p++;
					bank = ParseInt(&p);
					if (bank < 0) bank = 0;
				}
				if (prog < 0) return fail(L"@prog");
				if (bank >= 0) {
					uint8_t p1 = (uint8_t)((prog > 0 ? prog - 1 : 0) & 127);
					uint8_t p2 = (uint8_t)((bank > 0 ? bank - 1 : 0) & 127);
					out->bind.vstProg[ch] = prog;
					out->bind.vstBankMsb[ch] = bank;
					/* a=b1 b=b2 for cmd2; c=1 marks “paired @n:m” (no cmd7) */
					ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_PROG, p1, p2, 1, 0);
				} else {
					out->bind.vstProg[ch] = prog;
					ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_PROG,
						(uint8_t)(prog & 127), (uint8_t)(prog & 127), 0, 0);
				}
				continue;
			}
			/* @L n:n:n → MPY cmd13 (4-byte). @R n:n:n → cmd14. */
			if (*p == L'L' || *p == L'l') {
				p++;
				int a1 = ParseInt(&p); if (a1 < 0) a1 = 0;
				int a2 = 0, a3 = 0;
				if (*p == L':') { p++; a2 = ParseInt(&p); if (a2 < 0) a2 = 0; }
				if (*p == L':') { p++; a3 = ParseInt(&p); if (a3 < 0) a3 = 0; }
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_COMMENT,
					(uint8_t)a1, (uint8_t)a2, (uint8_t)(0x80 | (a3 & 0x7F)), 13);
				continue;
			}
			if (*p == L'R' || *p == L'r') {
				p++;
				int a1 = ParseInt(&p); if (a1 < 0) a1 = 0;
				int a2 = 0, a3 = 0;
				if (*p == L':') { p++; a2 = ParseInt(&p); if (a2 < 0) a2 = 0; }
				if (*p == L':') { p++; a3 = ParseInt(&p); if (a3 < 0) a3 = 0; }
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_COMMENT,
					(uint8_t)a1, (uint8_t)a2, (uint8_t)(0x80 | (a3 & 0x7F)), 14);
				continue;
			}
			/* @~n:n:n MICP portamento / special; @#… etc. */
			if (*p == L'~' || *p == L'#' || *p == L'!' || *p == L'%' || *p == L'^' || *p == L'*') {
				p++;
				while (IsDigit(*p) || *p == L':' || *p == L'-' || *p == L'+' || *p == L'.') p++;
				while (IsWs(*p)) p++;
				if (*p == L'{') skipBalanced(L'{', L'}');
				continue;
			}
			/* @Vnnn → track volume (MPY cmd 5). */
			if (*p == L'V' || *p == L'v') {
				p++;
				int n = ParseInt(&p);
				if (n < 0) n = 100;
				if (n > 127) n = 127;
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_VOL, (uint8_t)n, (uint8_t)n, 0, 0);
				continue;
			}
			/* @P nnnn → pitch bend 14-bit (center 8192). @PROG already handled above. */
			if (*p == L'P' || *p == L'p') {
				if (_wcsnicmp(p, L"PROG", 4) != 0 && _wcsnicmp(p, L"PAN", 3) != 0) {
					p++;
					int n = ParseInt(&p);
					if (n < 0) n = 0x2000;
					if (n > 0x3FFF) n = 0x3FFF;
					int a = 64 + (n - 0x2000) * 64 / 0x1FFF;
					if (a < 0) a = 0;
					if (a > 127) a = 127;
					ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_PITCH, (uint8_t)a, 0, 0, 0);
					continue;
				}
			}
			/* other @letter — skip args + optional {...} */
			if ((*p >= L'A' && *p <= L'Z') || (*p >= L'a' && *p <= L'z')) {
				p++;
				while (IsDigit(*p) || *p == L':' || *p == L'-' || *p == L'+' || *p == L'.') p++;
				while (IsWs(*p)) p++;
				if (*p == L'{') skipBalanced(L'{', L'}');
				continue;
			}
			return fail(L"unknown @ command");
		}

		/* {n ... } / {:n ... }:  repeat (MICP PC98). n<=0 → 2 */
		if (*p == L'{') {
			p++;
			if (*p == L':') p++; /* {:16 ... }: style */
			int n = ParseInt(&p);
			if (n <= 0) n = 2;
			if (n > 99) n = 99;
			if (loopSp >= SC_LOOP_MAX) return fail(L"loop nest too deep");
			loopPos[loopSp] = p;
			loopLeft[loopSp] = n;
			loopSp++;
			continue;
		}
		if (*p == L'}') {
			p++;
			if (*p == L':') p++; /* closing }: */
			if (loopSp <= 0) continue;
			loopSp--;
			loopLeft[loopSp]--;
			if (loopLeft[loopSp] > 0) {
				p = loopPos[loopSp];
				loopSp++;
			}
			continue;
		}

		/* Q = soft-loop land mark (MICP); J → first |: or Q body (MPY cmd 10). */
		if (*p == L'Q') {
			p++;
			if (!ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_JUMP_MARK,
				0, 0, 0, 0)) return fail(L"overflow");
			continue;
		}
		if (*p == L'J') {
			p++;
			if (!ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_JUMP,
				0, 0, 0, 0)) return fail(L"overflow");
			continue;
		}
		if (*p == L'q') {
			p++;
			int n = ParseInt(&p);
			(void)n; /* gate ratio — applied at write time later */
			continue;
		}

		/* tempo Tnnn / tnnn */
		if (*p == L't' || *p == L'T') {
			p++;
			int bpm = ParseInt(&p);
			if (bpm < 0) return fail(L"T/t BPM");
			out->tempoT = BpmToT(bpm);
			if (!tempoWritten) {
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_TEMPO,
					(uint8_t)(out->tempoT & 0xFF), (uint8_t)((out->tempoT >> 8) & 0xFF), 0, 0);
				tempoWritten = 1;
			} else {
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_TEMPO,
					(uint8_t)(out->tempoT & 0xFF), (uint8_t)((out->tempoT >> 8) & 0xFF), 0, 0);
			}
			continue;
		}
		if (*p == L'o' || *p == L'O') {
			p++;
			int n = ParseInt(&p);
			if (n < 0 || n > 8) return fail(L"O0..8");
			oct = n;
			continue;
		}
		if (*p == L'l' || *p == L'L') {
			p++;
			int n = ParseInt(&p);
			if (n <= 0) return fail(L"LN");
			defLen = n;
			continue;
		}
		if (*p == L'v' || *p == L'V') {
			p++;
			int n = ParseInt(&p);
			if (n < 0) return fail(L"VN");
			vel = n;
			if (vel > 127) vel = 127;
			ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_VELO, (uint8_t)vel, 0, 0, 0);
			continue;
		}
		/* P pan (optional : variants skipped after first value) */
		if (*p == L'P') {
			p++;
			int n = ParseInt(&p);
			if (n < 0) return fail(L"P pan");
			pan = n;
			if (pan > 127) pan = 127;
			while (*p == L':') {
				p++;
				ParseInt(&p);
			}
			ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_PAN, (uint8_t)pan, 0, 0, 0);
			continue;
		}
		if (*p == L'<') { if (oct > 0) oct--; p++; continue; }
		if (*p == L'>') { if (oct < 8) oct++; p++; continue; }

		/* ^ / & tie / slur: extend last note duration */
		if (*p == L'^' || *p == L'&') {
			p++;
			int len, dots;
			parseLenDots(&len, &dots);
			int dur = LenToTicks(len, defLen, dots);
			if (lastNoteEv >= 0 && lastNoteEv < out->evCount) {
				uint32_t nd = (uint32_t)out->ev[lastNoteEv].dur + (uint32_t)dur;
				if (nd > 65535) nd = 65535;
				out->ev[lastNoteEv].dur = (uint16_t)nd;
				out->ev[lastNoteEv].c = 1; /* tie flag */
			}
			tick[ch] += (uint32_t)dur;
			continue;
		}

		/* rests */
		if (*p == L'r' || *p == L'R') {
			/* drum 'r' (ride) only when followed by length-like and ch is drum? MICP: lowercase r on drum part.
			   Heuristic: if ch==9 (MIDI10) and next is letter not digit/dot, treat as drum later.
			   Plain R/r + optional len = rest. */
			const wchar_t rc = *p;
			p++;
			if (rc == L'r' && ch == 9 && *p && !IsDigit(*p) && *p != L'.' && NoteIndex(*p) < 0 && *p != L'^') {
				/* fall through — actually already consumed; treat as ride drum note 51 */
				int len, dots;
				parseLenDots(&len, &dots);
				int dur = LenToTicks(len, defLen, dots);
				if (!ScMidiAddNote(out, tick[ch], ch, 51, dur, vel)) return fail(L"event overflow");
				lastNoteEv = out->evCount - 1;
				tick[ch] += (uint32_t)dur;
				continue;
			}
			int len, dots;
			parseLenDots(&len, &dots);
			int dur = LenToTicks(len, defLen, dots);
			if (!ScMidiAddRest(out, tick[ch], ch, dur)) return fail(L"event overflow");
			tick[ch] += (uint32_t)dur;
			lastNoteEv = -1;
			continue;
		}

		/* drum shortcuts (MICP drum part) */
		if (*p == L's' || *p == L'b' || *p == L'h') {
			int drum = (*p == L's') ? 38 : (*p == L'b') ? 36 : 42;
			p++;
			int len, dots;
			parseLenDots(&len, &dots);
			int dur = LenToTicks(len, defLen, dots);
			if (!tempoWritten) {
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_TEMPO,
					(uint8_t)(out->tempoT & 0xFF), (uint8_t)((out->tempoT >> 8) & 0xFF), 0, 0);
				tempoWritten = 1;
			}
			if (!ScMidiAddNote(out, tick[ch], ch, drum, dur, vel)) return fail(L"event overflow");
			lastNoteEv = out->evCount - 1;
			tick[ch] += (uint32_t)dur;
			continue;
		}

		int ni = NoteIndex(*p);
		if (ni >= 0) {
			p++;
			int sharp = 0;
			if (*p == L'#' || *p == L'+') { sharp = 1; p++; }
			else if (*p == L'-') { sharp = -1; p++; }
			int len, dots;
			parseLenDots(&len, &dots);
			int dur = LenToTicks(len, defLen, dots);
			/* SASAMI MPY note byte = O*12+scale (DO-- O3D → 0x26). Not GM (O+1)*12. */
			int note = oct * 12 + ni + sharp;
			if (note < 0) note = 0;
			if (note > 127) note = 127;
			if (!tempoWritten) {
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_TEMPO,
					(uint8_t)(out->tempoT & 0xFF), (uint8_t)((out->tempoT >> 8) & 0xFF), 0, 0);
				tempoWritten = 1;
			}
			if (!ScMidiAddNote(out, tick[ch], ch, note, dur, vel)) return fail(L"event overflow");
			lastNoteEv = out->evCount - 1;
			tick[ch] += (uint32_t)dur;
			continue;
		}

		/* skip parenthetical / underscore / MICP separators lightly.
		   Do NOT skip ':' alone before digits — that made {:N become bare digits. */
		if (*p == L'(' || *p == L')' || *p == L'_' || *p == L'!' || *p == L'=' || *p == L'"' || *p == L'`') {
			p++;
			continue;
		}
		/* stray ':' with no following digit (loop closers already handled) */
		if (*p == L':') {
			p++;
			continue;
		}
		/* bare digits: treat as length override leftover / skip (MICP dumps) */
		if (IsDigit(*p)) {
			ParseInt(&p);
			while (*p == L'.') p++;
			continue;
		}

		/* Hn → cmd16, In → cmd17 (reverb send). Matches DO-- H1 I80 before T. */
		if (*p == L'H') {
			p++;
			int n = ParseInt(&p);
			if (n < 0) n = 1;
			ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_COMMENT, (uint8_t)n, 0, 0, 16);
			continue;
		}
		if (*p == L'I') {
			p++;
			int n = ParseInt(&p);
			if (n < 0) n = 0;
			if (n > 127) n = 127;
			ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_COMMENT, (uint8_t)n, 0, 0, 17);
			continue;
		}

		/* PC98/PMD leftovers: M/N/S/W/X/Y/Z + numeric args — skip for compile */
		if ((*p >= L'A' && *p <= L'Z') && *p != L'A' && *p != L'B' && *p != L'C'
			&& *p != L'D' && *p != L'E' && *p != L'F' && *p != L'G'
			&& *p != L'R' && *p != L'T' && *p != L'O' && *p != L'L'
			&& *p != L'V' && *p != L'P' && *p != L'Q' && *p != L'J'
			&& *p != L'H' && *p != L'I') {
			p++;
			while (IsWs(*p) || *p == L',' || *p == L':') p++;
			while (IsDigit(*p) || *p == L'-' || *p == L'+' || *p == L',' || *p == L':' || *p == L'.') {
				if (IsDigit(*p) || *p == L'-' || *p == L'+') (void)ParseInt(&p);
				else p++;
			}
			continue;
		}
		wchar_t bad[48];
		_snwprintf_s(bad, _TRUNCATE, L"unexpected '%c' (U+%04X)", (char)(*p < 128 ? *p : '?'), (unsigned)*p);
		return fail(bad);
	}
	if (out->evCount <= 0) return fail(L"no events");
	{
		wchar_t lerr[160];
		if (!ScValidateLoopBalance(out->ev, out->evCount, SC_MIDI_CH, lerr, 160))
			return fail(lerr);
	}
	return 1;
}
static int IsSsgCh(int ch) { return ch >= 3 && ch <= 5; }

static uint8_t FmNoteByte(int mmlOct, int scale)
{
	/* SASAMI FPY: MML O4 → note high-nibble 3 (see DO--.FPY vs DO--.DAT). */
	int oct = mmlOct - 1;
	if (oct < 0) oct = 0;
	if (oct > 7) oct = 7;
	if (scale < 0) scale = 0;
	if (scale > 11) scale = 11;
	return (uint8_t)(((oct & 0x0F) << 4) | (scale & 0x0F));
}

/* SSG note byte: MML O keeps high nibble (DO-- O6F → 0x65, not 0x55). */
static uint8_t SsgNoteByte(int mmlOct, int scale)
{
	int oct = mmlOct;
	if (oct < 0) oct = 0;
	if (oct > 7) oct = 7;
	if (scale < 0) scale = 0;
	if (scale > 11) scale = 11;
	return (uint8_t)(((oct & 0x0F) << 4) | (scale & 0x0F));
}

int ScCompileFmText(const wchar_t* text, ScFmDoc* out, int* errLine, wchar_t* errMsg, int errMsgCch)
{
	if (!text || !out) return 0;
	ScFmDocClear(out);
	if (errLine) *errLine = 0;
	if (errMsg && errMsgCch > 0) errMsg[0] = 0;

	int ch = 0;
	uint32_t tick[SC_FM_CH];
	memset(tick, 0, sizeof(tick));
	int oct = 4, defLen = 4;
	int gatePct = 100;
	int voiceSet[SC_FM_CH];
	int volSet[SC_FM_CH];
	int chLoopLeft[SC_FM_CH];
	const wchar_t* chStart[SC_FM_CH];
	memset(voiceSet, 0, sizeof(voiceSet));
	memset(volSet, 0, sizeof(volSet));
	memset(chLoopLeft, 0, sizeof(chLoopLeft));
	memset(chStart, 0, sizeof(chStart));
	const wchar_t* loopPos[SC_LOOP_NEST_MAX];
	int loopLeft[SC_LOOP_NEST_MAX];
	int loopSp = 0;
	int line = 1;
	const wchar_t* p = text;
	int tempoWritten = 0;

	auto fail = [&](const wchar_t* msg) -> int {
		if (errLine) *errLine = line;
		if (errMsg && errMsgCch > 0) wcsncpy_s(errMsg, errMsgCch, msg, _TRUNCATE);
		return 0;
	};

	while (*p) {
		if (*p == L'\n') { line++; p++; continue; }
		if (IsWs(*p)) { p++; continue; }
		if (*p == L';' || (p[0] == L'/' && p[1] == L'/')) {
			while (*p && *p != L'\n') p++;
			continue;
		}
		if (*p == L'[' || *p == L'#') {
			p++;
			while (IsWs(*p)) p++;
			int n = ParseInt(&p);
			/* PC98 DAT may use parts 1..16; map into FM 1..10 */
			if (n < 1) return fail(L"FM channel 1..10");
			if (n > SC_FM_CH) n = ((n - 1) % SC_FM_CH) + 1;
			ch = n - 1;
			while (IsWs(*p)) p++;
			if (*p == L']') p++;
			chStart[ch] = p;
			chLoopLeft[ch] = 1; /* J may rewind once */
			oct = 4; defLen = 4; gatePct = 100;
			continue;
		}
		if (*p == L']') { p++; continue; }
		if (_wcsnicmp(p, L"OPNA", 4) == 0) { p += 4; out->opna10 = 1; continue; }
		if (_wcsnicmp(p, L"OPN", 3) == 0) { p += 3; out->opna10 = 0; continue; }
		if (*p == L't' || *p == L'T') {
			p++;
			int bpm = ParseInt(&p);
			if (bpm < 0) return fail(L"tBPM");
			out->tempoT = BpmToT(bpm);
			if (!tempoWritten) {
				ScPush(out->ev, &out->evCount, 0, 0, SC_EV_FM_TEMPO, (uint8_t)(out->tempoT & 0xFF), (uint8_t)((out->tempoT >> 8) & 0xFF), 0, 0);
				tempoWritten = 1;
			}
			continue;
		}
		if (*p == L'o' || *p == L'O') {
			p++;
			int n = ParseInt(&p);
			if (n < 0) return fail(L"oN");
			oct = n;
			continue;
		}
		if (*p == L'l' || *p == L'L') {
			p++;
			int n = ParseInt(&p);
			if (n <= 0) return fail(L"lN");
			defLen = n;
			continue;
		}
		if (*p == L'<') { if (oct > 1) oct--; p++; continue; }
		if (*p == L'>') { if (oct < 7) oct++; p++; continue; }
		/* Volume: FM → FVOL (YM TL); SSG ch4-6 → PSGVOL 0..15 (DO-- uses cmd4). */
		if (*p == L'v' || *p == L'V') {
			p++;
			int n = ParseInt(&p);
			if (n < 0) n = 15;
			if (IsSsgCh(ch)) {
				if (n > 15) n = 15;
				/* a = PSG level, b = 1 marks PSGVOL in writer */
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_VOL, (uint8_t)n, 1, 0, 0);
			} else {
				int tl;
				if (n <= 15) tl = (15 - n) * 8; /* V15 → 0, V0 → 120 */
				else { if (n > 127) n = 127; tl = 127 - n; }
				if (tl < 0) tl = 0;
				if (tl > 127) tl = 127;
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_VOL, (uint8_t)tl, 0, 0, 0);
			}
			volSet[ch] = 1;
			continue;
		}
		/* q = gate %; Q = soft-J land mark (same as MIDI/MICP) */
		if (*p == L'q') {
			p++;
			int n = ParseInt(&p);
			if (n < 1) n = 100;
			if (n > 100) n = 100;
			gatePct = n;
			continue;
		}
		if (*p == L'Q') {
			p++;
			if (!ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_JUMP_MARK,
				0, 0, 0, 0)) return fail(L"overflow");
			continue;
		}
		/* J = FJUMP back to first loop / channel head (native cmd 3). Do not text-rewind. */
		if (*p == L'J') {
			p++;
			if (!ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_JUMP,
				0, 0, 0, 0)) return fail(L"overflow");
			continue;
		}
		/* PMD |:n ... :| — emit native FPY loop opcodes (do NOT unroll; matches DO--.FPY). */
		if (p[0] == L'|' && p[1] == L':') {
			p += 2;
			int n = ParseInt(&p);
			if (n <= 0) n = 2;
			if (n > 99) n = 99;
			if (!ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_LOOP_START,
				(uint8_t)n, 0, 0, 0)) return fail(L"overflow");
			continue;
		}
		if ((p[0] == L':' && p[1] == L'|') || (p[0] == L':' && p[1] == L']')) {
			p += 2;
			if (!ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_LOOP_END,
				0, 0, 0, 0)) return fail(L"overflow");
			continue;
		}
		if (*p == L'|' || *p == L'~' || *p == L'$' || *p == L'&') { p++; continue; }
		if (*p == L'^') {
			p++;
			(void)ParseInt(&p);
			while (*p == L'.') p++;
			continue;
		}
		/* {n ... } / {:n ... }: repeat */
		if (*p == L'{') {
			p++;
			if (*p == L':') p++;
			int n = ParseInt(&p);
			if (n <= 0) n = 2;
			if (n > 99) n = 99;
			if (loopSp >= SC_LOOP_NEST_MAX) return fail(L"loop nest too deep");
			loopPos[loopSp] = p;
			loopLeft[loopSp] = n;
			loopSp++;
			continue;
		}
		if (*p == L'}') {
			p++;
			if (*p == L':') p++;
			if (loopSp <= 0) continue;
			loopSp--;
			loopLeft[loopSp]--;
			if (loopLeft[loopSp] > 0) {
				p = loopPos[loopSp];
				loopSp++;
			}
			continue;
		}
		if (*p == L'@') {
			p++;
			if (*p == L'~' || *p == L'#' || *p == L'!' || *p == L'%' || *p == L'^' || *p == L'*') {
				p++;
				while (IsDigit(*p) || *p == L':' || *p == L'-' || *p == L'+' || *p == L'.') p++;
				continue;
			}
			if ((*p >= L'A' && *p <= L'Z') || (*p >= L'a' && *p <= L'z')) {
				p++;
				while (IsDigit(*p) || *p == L':' || *p == L'-' || *p == L'+' || *p == L'.') p++;
				continue;
			}
			/* @n → built-in neiro (FNEIRO). SSG channels ignore @ (no FM voice). */
			int n = ParseInt(&p);
			if (n < 0) n = 0;
			if (n > 31) n = 31; /* bank has ~20; clamp safe */
			if (*p == L':') { p++; (void)ParseInt(&p); }
			if (!IsSsgCh(ch)) {
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_VOICE, (uint8_t)n, 0, 0, 0);
				voiceSet[ch] = 1;
			}
			continue;
		}
		if (*p == L'(' || *p == L')' || *p == L'_' || *p == L'!' || *p == L':' || *p == L'=') {
			p++;
			continue;
		}
		if (IsDigit(*p)) {
			(void)ParseInt(&p);
			while (*p == L'.') p++;
			continue;
		}
		if (*p == L'r' || *p == L'R') {
			p++;
			int len = ParseInt(&p);
			int dots = 0;
			while (*p == L'.') { dots++; p++; }
			int dur = LenToTicks(len, defLen, dots);
			int wait = dur;
			if (wait > 255) wait = 255;
			if (!ScFmAddRest(out, tick[ch], ch, wait)) return fail(L"overflow");
			tick[ch] += (uint32_t)dur;
			continue;
		}
		int ni = NoteIndex(*p);
		if (ni >= 0) {
			p++;
			if (*p == L'#' || *p == L'+') { ni++; p++; }
			else if (*p == L'-') { ni--; p++; }
			if (ni < 0) ni = 0;
			if (ni > 11) ni = 11;
			int len = ParseInt(&p);
			int dots = 0;
			while (*p == L'.') { dots++; p++; }
			int dur = LenToTicks(len, defLen, dots);
			/* gate % shortens sounding wait; step advances by full dur */
			int wait = (dur * gatePct) / 100;
			if (wait < 2) wait = 2;
			if (wait > 255) wait = 255;
			if (!tempoWritten) {
				ScPush(out->ev, &out->evCount, 0, 0, SC_EV_FM_TEMPO, (uint8_t)(out->tempoT & 0xFF), (uint8_t)((out->tempoT >> 8) & 0xFF), 0, 0);
				tempoWritten = 1;
			}
			/* Safety: built-in neiro + audible level before first note on this ch */
			if (!IsSsgCh(ch)) {
				if (!voiceSet[ch]) {
					ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_VOICE, 1, 0, 0, 0);
					voiceSet[ch] = 1;
				}
				if (!volSet[ch]) {
					ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_VOL, 8, 0, 0, 0);
					volSet[ch] = 1;
				}
			} else if (!volSet[ch]) {
				ScPush(out->ev, &out->evCount, tick[ch], (uint8_t)ch, SC_EV_FM_VOL, 15, 1, 0, 0);
				volSet[ch] = 1;
			}
			const uint8_t nb = IsSsgCh(ch) ? SsgNoteByte(oct, ni) : FmNoteByte(oct, ni);
			if (!ScFmAddNote(out, tick[ch], ch, nb, wait)) return fail(L"overflow");
			tick[ch] += (uint32_t)dur;
			continue;
		}
		if ((*p >= L'A' && *p <= L'Z') && NoteIndex(*p) < 0
			&& *p != L'R' && *p != L'T' && *p != L'O' && *p != L'L'
			&& *p != L'V' && *p != L'Q' && *p != L'J' && *p != L'P') {
			p++;
			while (IsWs(*p) || *p == L',' || *p == L':') p++;
			while (IsDigit(*p) || *p == L'-' || *p == L'+' || *p == L',' || *p == L':' || *p == L'.') {
				if (IsDigit(*p) || *p == L'-' || *p == L'+') (void)ParseInt(&p);
				else p++;
			}
			continue;
		}
		return fail(L"FM syntax error");
	}
	if (out->evCount <= 0) return fail(L"no events");
	{
		wchar_t lerr[160];
		if (!ScValidateLoopBalance(out->ev, out->evCount, SC_FM_TOTAL, lerr, 160))
			return fail(lerr);
	}
	return 1;
}

/* After successful FM compile: max tick across channels (for UI / desk-check). */
uint32_t ScFmDocMaxTick(const ScFmDoc* d)
{
	if (!d || d->evCount <= 0) return 0;
	uint32_t mx = 0;
	for (int i = 0; i < d->evCount; i++) {
		uint32_t end = d->ev[i].tick + (d->ev[i].dur ? d->ev[i].dur : 0);
		if (end > mx) mx = end;
	}
	return mx;
}

int ScFmDocNoteCount(const ScFmDoc* d)
{
	if (!d) return 0;
	int n = 0;
	for (int i = 0; i < d->evCount; i++)
		if (d->ev[i].kind == SC_EV_FM_NOTE) n++;
	return n;
}

static int CmpEv(const void* a, const void* b)
{
	const ScEvent* ea = (const ScEvent*)a;
	const ScEvent* eb = (const ScEvent*)b;
	if (ea->ch != eb->ch) return (int)ea->ch - (int)eb->ch;
	if (ea->tick < eb->tick) return -1;
	if (ea->tick > eb->tick) return 1;
	/* Same tick: preserve MML emission order. Rank-sorting broke DO-- `:||:`
	   (LOOP_START ranked before LOOP_END → empty cmd13/14, stack corruption). */
	if (ea->seq < eb->seq) return -1;
	if (ea->seq > eb->seq) return 1;
	return 0;
}

/* SC_EV_MAX (~1MB) — never put on the stack. */
static ScEvent* ScSortedEvBuf(void)
{
	static ScEvent* s = NULL;
	if (!s)
		s = (ScEvent*)HeapAlloc(GetProcessHeap(), 0, sizeof(ScEvent) * (SIZE_T)SC_EV_MAX);
	return s;
}

static int ScPutMisaoPcmSample(SasamiTrackStream* s, uint8_t slot, const wchar_t* relPath)
{
	if (!s || !relPath || !relPath[0]) return 1;
	char path8[255];
	int n = WideCharToMultiByte(CP_UTF8, 0, relPath, -1, path8, (int)sizeof(path8), NULL, NULL);
	if (n <= 1)
		n = WideCharToMultiByte(932, 0, relPath, -1, path8, (int)sizeof(path8), NULL, NULL);
	if (n <= 1) return 0;
	int len = n - 1; /* command 26 stores byte count without the trailing NUL. */
	if (len > 255) len = 255;
	uint8_t hdr[3] = { 26, slot, (uint8_t)len };
	if (!SasamiStreamPut(s, hdr, 3)) return 0;
	return SasamiStreamPut(s, (const uint8_t*)path8, (uint32_t)len);
}

static uint16_t ScPitchCenter64ToMisaoRaw(uint8_t v)
{
	int d = (int)v - 64;
	int raw = 0x8000 + d * 0x0200;
	if (raw < 0) raw = 0;
	if (raw > 0xFFFF) raw = 0xFFFF;
	return (uint16_t)raw;
}

static wchar_t s_scLastWriteErr[160];

const wchar_t* ScGetLastWriteErr(void)
{
	return s_scLastWriteErr;
}

int ScValidateLoopBalance(const ScEvent* ev, int n, int chMax, wchar_t* errMsg, int errMsgCch)
{
	if (errMsg && errMsgCch > 0) errMsg[0] = 0;
	if (!ev || n <= 0) return 1;
	if (chMax <= 0) chMax = 1;
	if (chMax > 64) chMax = 64;

	ScEvent* sorted = ScSortedEvBuf();
	if (!sorted) {
		if (errMsg && errMsgCch > 0)
			wcsncpy_s(errMsg, errMsgCch, L"oom", _TRUNCATE);
		return 0;
	}
	if (n > SC_EV_MAX) n = SC_EV_MAX;
	memcpy(sorted, ev, sizeof(ScEvent) * (size_t)n);
	qsort(sorted, (size_t)n, sizeof(ScEvent), CmpEv);

	int sp[64];
	memset(sp, 0, sizeof(sp));
	for (int i = 0; i < n; i++) {
		const ScEvent& e = sorted[i];
		const int ch = (int)e.ch;
		if (ch < 0 || ch >= chMax) continue;
		if (e.kind == SC_EV_FM_LOOP_START) {
			if (sp[ch] >= SC_LOOP_NEST_MAX) {
				const wchar_t* msg = L"loop nest too deep (max 16)";
				if (errMsg && errMsgCch > 0) wcsncpy_s(errMsg, errMsgCch, msg, _TRUNCATE);
				return 0;
			}
			sp[ch]++;
		} else if (e.kind == SC_EV_FM_LOOP_END) {
			if (sp[ch] <= 0) {
				const wchar_t* msg = L"ループの不一致です (:| に対応する |: がありません)";
				if (errMsg && errMsgCch > 0) wcsncpy_s(errMsg, errMsgCch, msg, _TRUNCATE);
				return 0;
			}
			sp[ch]--;
		}
	}
	for (int ch = 0; ch < chMax; ch++) {
		if (sp[ch] > 0) {
			const wchar_t* msg = L"ループの不一致です (|: が :| で閉じられていません)";
			if (errMsg && errMsgCch > 0) wcsncpy_s(errMsg, errMsgCch, msg, _TRUNCATE);
			return 0;
		}
	}
	return 1;
}

int ScDocMaxLoopNest(const ScEvent* ev, int n, int chMax)
{
	if (!ev || n <= 0) return 0;
	if (chMax <= 0) chMax = 1;
	if (chMax > 64) chMax = 64;
	ScEvent* sorted = ScSortedEvBuf();
	if (!sorted) return 0;
	if (n > SC_EV_MAX) n = SC_EV_MAX;
	memcpy(sorted, ev, sizeof(ScEvent) * (size_t)n);
	qsort(sorted, (size_t)n, sizeof(ScEvent), CmpEv);
	int sp[64];
	memset(sp, 0, sizeof(sp));
	int maxD = 0;
	for (int i = 0; i < n; i++) {
		const ScEvent& e = sorted[i];
		const int ch = (int)e.ch;
		if (ch < 0 || ch >= chMax) continue;
		if (e.kind == SC_EV_FM_LOOP_START) {
			sp[ch]++;
			if (sp[ch] > maxD) maxD = sp[ch];
		} else if (e.kind == SC_EV_FM_LOOP_END) {
			if (sp[ch] > 0) sp[ch]--;
		}
	}
	return maxD;
}

int ScMidiDocToWrite(const ScMidiDoc* d, SasamiWriteMidi* w)
{
	s_scLastWriteErr[0] = 0;
	if (!d || !w) return 0;
	if (!ScValidateLoopBalance(d->ev, d->evCount, SC_MIDI_CH, s_scLastWriteErr, 160))
		return 0;
	SasamiWriteMidiClear(w);
	w->trackCount = 32;
	w->dualPort = d->bind.dualPort;
	w->isMpw3 = d->bind.isMpw3;
	strncpy_s(w->titleSjis, d->titleSjis, _TRUNCATE);
	for (int i = 0; i < 32; i++) {
		if (d->bind.vstPath[i][0])
			wcsncpy_s(w->vstPath[i], d->bind.vstPath[i], _TRUNCATE);
		w->vstProg[i] = d->bind.vstProg[i];
		w->vstBankMsb[i] = d->bind.vstBankMsb[i];
		w->vstBankLsb[i] = d->bind.vstBankLsb[i];
		w->vstForceCh[i] = d->bind.vstForceCh[i];
		if (d->bind.vstCompLen[i] && d->bind.vstComp[i])
			SasamiVstBlobSet(&w->vstComp[i], &w->vstCompLen[i],
				d->bind.vstComp[i], d->bind.vstCompLen[i]);
		if (d->bind.vstCtrlLen[i] && d->bind.vstCtrl[i])
			SasamiVstBlobSet(&w->vstCtrl[i], &w->vstCtrlLen[i],
				d->bind.vstCtrl[i], d->bind.vstCtrlLen[i]);
	}

	ScEvent* sorted = ScSortedEvBuf();
	if (!sorted) return 0;
	int n = d->evCount;
	if (n > SC_EV_MAX) n = SC_EV_MAX;
	memcpy(sorted, d->ev, sizeof(ScEvent) * (size_t)n);
	qsort(sorted, (size_t)n, sizeof(ScEvent), CmpEv);

	uint32_t lastTick[SC_MIDI_CH];
	memset(lastTick, 0, sizeof(lastTick));
	int vel[SC_MIDI_CH];
	int preambleDone[SC_MIDI_CH];
	for (int i = 0; i < SC_MIDI_CH; i++) { vel[i] = 105; preambleDone[i] = 0; }
	uint32_t loopBodyOff[SC_MIDI_CH][SC_LOOP_NEST_MAX];
	int loopWrSp[SC_MIDI_CH];
	uint32_t chStreamJump[SC_MIDI_CH];
	int chJumpSet[SC_MIDI_CH];
	memset(loopBodyOff, 0, sizeof(loopBodyOff));
	memset(loopWrSp, 0, sizeof(loopWrSp));
	memset(chStreamJump, 0, sizeof(chStreamJump));
	memset(chJumpSet, 0, sizeof(chJumpSet));

	/* DO--.MPY track header for VST SC-VA / GS:
	   - MVOL80 + VOL117 + VELO105 (MVOL = GS master; without it SC-VA sits ~full → ~2x)
	   - Init REST waits kept at 0 so score tick 0 == first audible note (classic
	     DO-- used ~131 ticks of padding; that desynced the score playhead).
	   - Default @L/@R ONLY on first track (global GS; per-track R 003C00 killed reverb) */
	int globalFxDone = 0;
	auto putPreamble = [&](SasamiTrackStream* s, uint32_t* outWaitTicks) -> int {
		uint32_t waitSum = 0;
		auto putRest = [&](uint8_t w8) -> int {
			if (!SasamiStreamPut3(s, 8, 0, w8)) return 0;
			waitSum += w8;
			return 1;
		};
		if (!SasamiStreamPut3(s, 4, 80, 0)) return 0;           /* MVOL */
		if (!putRest(0)) return 0;
		if (!SasamiStreamPut3(s, 7, 0, 0)) return 0;            /* BANK 0 */
		if (!putRest(0)) return 0;
		if (!SasamiStreamPut3(s, 16, 1, 0)) return 0;           /* H1 */
		if (!putRest(0)) return 0;
		if (!SasamiStreamPut3(s, 17, 60, 0)) return 0;          /* I60 */
		if (!putRest(0)) return 0;
		if (!SasamiStreamPut3(s, 5, 117, 117)) return 0;        /* VOL */
		if (!putRest(0)) return 0;
		if (!SasamiStreamPut3(s, 0x13, 105, 0)) return 0;       /* VELO */
		if (!putRest(0)) return 0;
		if (!SasamiStreamPut3(s, 21, 0, 0)) return 0;           /* PEDOFF */
		if (!putRest(0)) return 0;
		if (!SasamiStreamPut3(s, 35, 1, 0)) return 0;           /* MD5588 stub */
		if (!globalFxDone) {
			uint8_t L[4] = { 13, 0, 5, 0 };
			uint8_t R[4] = { 14, 0, 60, 0 };
			if (!SasamiStreamPut(s, L, 4)) return 0;
			if (!SasamiStreamPut(s, R, 4)) return 0;
			globalFxDone = 1;
		}
		if (outWaitTicks) *outWaitTicks = waitSum;
		return 1;
	};

	for (int i = 0; i < n; i++) {
		const ScEvent* e = &sorted[i];
		int ch = e->ch;
		if (ch < 0 || ch >= SC_MIDI_CH) continue;
		SasamiTrackStream* s = &w->tr[ch];
		if (d->trackPart[ch] != 0xFF)
			s->part = d->trackPart[ch] & 15;
		else
			s->part = ch & 15;
		if (!preambleDone[ch]) {
			uint32_t preWait = 0;
			if (!putPreamble(s, &preWait)) return 0;
			/* Align stream time with score ticks (preamble waits used to skew this). */
			lastTick[ch] = preWait;
			preambleDone[ch] = 1;
			vel[ch] = 105;
		}
		/* Only notes/rests consume time gaps — loops/meta stay tight like DO--.MPY. */
		if ((e->kind == SC_EV_NOTE || e->kind == SC_EV_REST || e->kind == SC_EV_TIE)
			&& e->tick > lastTick[ch]) {
			uint32_t gap = e->tick - lastTick[ch];
			while (gap > 0) {
				uint8_t w8 = (gap > 255) ? 255 : (uint8_t)gap;
				if (!SasamiStreamPut3(s, 8, 0, w8)) return 0;
				gap -= w8;
			}
			lastTick[ch] = e->tick;
		}
		switch (e->kind) {
		case SC_EV_TEMPO:
			if (!SasamiStreamPut3(s, 9, e->a, e->b)) return 0;
			break;
		case SC_EV_VELO:
			vel[ch] = e->a;
			if (!SasamiStreamPut3(s, 0x13, e->a, 0)) return 0;
			break;
		case SC_EV_PROG:
			if (!SasamiStreamPut3(s, 2, e->a, e->b)) return 0;
			break;
		case SC_EV_BANK:
			/* Paired @n:m already encoded in PROG (c==1); skip stray bank. */
			if (!SasamiStreamPut3(s, 7, e->a, e->b)) return 0;
			break;
		case SC_EV_COMMENT: {
			/* dur holds MPY cmd id: 13/14 (@L/@R 4-byte), 16 (H), 17 (I). */
			const int cmd = (int)e->dur;
			if (cmd == 13 || cmd == 14) {
				uint8_t t[4] = { (uint8_t)cmd, e->a, e->b, (uint8_t)(e->c & 0x7F) };
				if (!SasamiStreamPut(s, t, 4)) return 0;
			} else if (cmd == 16 || cmd == 17) {
				if (!SasamiStreamPut3(s, (uint8_t)cmd, e->a, 0)) return 0;
			}
			break;
		}
		case SC_EV_VOL:
			if (!SasamiStreamPut3(s, 5, e->a, e->b)) return 0;
			break;
		case SC_EV_PAN:
			if (!SasamiStreamPut3(s, 0x0C, e->a, e->a)) return 0;
			break;
		case SC_EV_PITCH: {
			/* MIDI cmd 11 PICH: 14-bit bend, center 0x2000; a = 0..127 center 64 */
			int bent = 0x2000 + ((int)e->a - 64) * (0x1FFF / 64);
			if (bent < 0) bent = 0;
			if (bent > 0x3FFF) bent = 0x3FFF;
			if (!SasamiStreamPut3(s, 11, (uint8_t)(bent & 0x7F), (uint8_t)((bent >> 7) & 0x7F))) return 0;
			break;
		}
		case SC_EV_RPN:
			/* cmd 26 = RPN(0,0) pitch-bend sens; else cmd 41 CC stream (101/100/6). */
			if (e->a == 0 && e->b == 0) {
				if (!SasamiStreamPut3(s, 26, e->c, 0)) return 0;
			} else {
				if (!SasamiStreamPut3(s, 41, 0x65, e->a)) return 0;
				if (!SasamiStreamPut3(s, 41, 0x64, e->b)) return 0;
				if (!SasamiStreamPut3(s, 41, 0x06, e->c)) return 0;
			}
			break;
		case SC_EV_NRPN: {
			uint8_t nr[4] = { 37, e->a, e->b, e->c };
			if (!SasamiStreamPut(s, nr, 4)) return 0;
			break;
		}
		case SC_EV_SYSEX: {
			const int si = (int)e->a;
			if (si >= 0 && si < d->sysexCount && d->sysexLen[si] > 0) {
				uint8_t cmd = 36;
				if (!SasamiStreamPut(s, &cmd, 1)) return 0;
				if (!SasamiStreamPut(s, d->sysex[si], d->sysexLen[si])) return 0;
				cmd = 0xFF;
				if (!SasamiStreamPut(s, &cmd, 1)) return 0;
			}
			break;
		}
		case SC_EV_JUMP_MARK: {
			/* Q: J lands here (after R8/@V/@prog). Do not overwrite a prior |:. */
			if (!chJumpSet[ch]) {
				chStreamJump[ch] = s->size;
				chJumpSet[ch] = 1;
			}
			break;
		}
		case SC_EV_FM_LOOP_START: {
			/* MPY cmd 23: count in b1. Soft-J prefers earlier Q over this |:. */
			if (!SasamiStreamPut3(s, 23, e->a, 0)) return 0;
			if (loopWrSp[ch] < SC_LOOP_NEST_MAX) {
				loopBodyOff[ch][loopWrSp[ch]] = s->size;
				if (!chJumpSet[ch]) {
					chStreamJump[ch] = s->size - 3;
					chJumpSet[ch] = 1;
				}
				loopWrSp[ch]++;
			}
			break;
		}
		case SC_EV_FM_LOOP_END: {
			if (loopWrSp[ch] <= 0) {
				wcsncpy_s(s_scLastWriteErr, 160, L"ループの不一致です (:| に対応する |: がありません)", _TRUNCATE);
				return 0;
			}
			loopWrSp[ch]--;
			uint32_t bodyRel = loopBodyOff[ch][loopWrSp[ch]];
			/* cmd 24: track-relative body; BuildMpy patches to abs 0x1xxx */
			if (!SasamiStreamPut3(s, 24, (uint8_t)(bodyRel & 0xFF), (uint8_t)((bodyRel >> 8) & 0xFF)))
				return 0;
			break;
		}
		case SC_EV_FM_JUMP: {
			/* Local Q/|: → 0xF000|rel. Else 0xE000 = song-first |: (DO-- [5:8]). */
			uint16_t mark;
			if (chJumpSet[ch])
				mark = (uint16_t)(0xF000 | (chStreamJump[ch] & 0x0FFF));
			else
				mark = 0xE000;
			if (!SasamiStreamPut3(s, 0x0A, (uint8_t)(mark & 0xFF), (uint8_t)(mark >> 8))) return 0;
			break;
		}
		case SC_EV_PEDAL_ON:
			/* MPY cmd 20 PDLON — CC64=127 */
			if (!SasamiStreamPut3(s, 20, 0, 0)) return 0;
			break;
		case SC_EV_PEDAL_OFF:
			/* MPY cmd 21 PDLOFF — CC64=0 */
			if (!SasamiStreamPut3(s, 21, 0, 0)) return 0;
			break;
		case SC_EV_NOTE: {
			int gate = (e->c >= 1 && e->c <= 100) ? e->c : 100;
			uint32_t sounding = ((uint32_t)(e->dur ? e->dur : 1) * (uint32_t)gate) / 100u;
			if (sounding < 1) sounding = 1;
			uint8_t wait = (sounding > 255) ? 255 : (uint8_t)sounding;
			if (!SasamiStreamPut3(s, 1, e->a, wait)) return 0;
			/* advance by full step so rhythm stays on grid */
			lastTick[ch] += e->dur ? e->dur : wait;
			break;
		}
		case SC_EV_REST: {
			uint8_t wait = (e->dur > 255) ? 255 : (uint8_t)(e->dur ? e->dur : 1);
			if (!SasamiStreamPut3(s, 8, 0, wait)) return 0;
			lastTick[ch] += e->dur ? e->dur : wait;
			break;
		}
		case SC_EV_TIE: {
			uint8_t wait = (e->dur > 255) ? 255 : (uint8_t)(e->dur ? e->dur : 1);
			if (!SasamiStreamPut3(s, 3, 0, wait)) return 0;
			lastTick[ch] += e->dur ? e->dur : wait;
			break;
		}
		default: break;
		}
	}
	/* VST-bound parts with no notes still need a live stream so SMF convert
	   keeps nAlive>0 and preview can open the host. Keep rest minimal. */
	for (int ch = 0; ch < SC_MIDI_CH; ch++) {
		if (w->tr[ch].used) continue;
		if (!d->bind.vstPath[ch][0]) continue;
		if (d->trackPart[ch] != 0xFF)
			w->tr[ch].part = d->trackPart[ch] & 15;
		else
			w->tr[ch].part = ch & 15;
		uint32_t preWait = 0;
		if (!putPreamble(&w->tr[ch], &preWait)) return 0;
		if (!SasamiStreamPut3(&w->tr[ch], 8, 0, 1)) return 0;
	}
	for (int ch = 0; ch < SC_MIDI_CH; ch++) {
		if (w->tr[ch].used)
			SasamiStreamPut3(&w->tr[ch], 0x0A, 0xF0, 0x10);
	}
	/* If parts 17–32 unused and no VST bind there → 16ch (MPW2/MPY OK unless VST on 1–16) */
	int hiUsed = 0;
	for (int ch = 16; ch < SC_MIDI_CH; ch++) {
		if (w->tr[ch].used) { hiUsed = 1; break; }
		if (d->bind.vstPath[ch][0]) { hiUsed = 1; break; }
	}
	if (!hiUsed) {
		w->trackCount = 16;
		int need3 = d->bind.isMpw3 ? 1 : 0;
		for (int i = 0; i < 16; i++) {
			if (d->bind.vstPath[i][0]) { need3 = 1; break; }
		}
		w->isMpw3 = need3;
	} else {
		w->trackCount = 32;
		w->isMpw3 = 1;
	}
	return 1;
}

int ScFmDocToWrite(const ScFmDoc* d, SasamiWriteFm* w)
{
	s_scLastWriteErr[0] = 0;
	if (!d || !w) return 0;
	if (!ScValidateLoopBalance(d->ev, d->evCount, SC_FM_TOTAL, s_scLastWriteErr, 160))
		return 0;
	SasamiWriteFmClear(w);
	w->opna10 = d->opna10;
	strncpy_s(w->titleSjis, d->titleSjis, _TRUNCATE);
	w->voiceCount = d->voiceCount;
	for (int i = 0; i < d->voiceCount && i < 64; i++)
		memcpy(w->voices[i], d->voices[i], 25);

	ScEvent* sorted = ScSortedEvBuf();
	if (!sorted) return 0;
	int n = d->evCount;
	if (n > SC_EV_MAX) n = SC_EV_MAX;
	memcpy(sorted, d->ev, sizeof(ScEvent) * (size_t)n);
	qsort(sorted, (size_t)n, sizeof(ScEvent), CmpEv);

	uint32_t lastTick[SC_FM_TOTAL];
	int chHadVoice[SC_FM_TOTAL];
	int chHadVol[SC_FM_TOTAL];
	uint32_t loopBodyOff[SC_FM_TOTAL][SC_LOOP_NEST_MAX];
	int loopWrSp[SC_FM_TOTAL];
	uint32_t chStreamJump[SC_FM_TOTAL]; /* stream offset for J target (first loop body or 0) */
	int chJumpSet[SC_FM_TOTAL];
	memset(lastTick, 0, sizeof(lastTick));
	memset(chHadVoice, 0, sizeof(chHadVoice));
	memset(chHadVol, 0, sizeof(chHadVol));
	memset(loopBodyOff, 0, sizeof(loopBodyOff));
	memset(loopWrSp, 0, sizeof(loopWrSp));
	memset(chStreamJump, 0, sizeof(chStreamJump));
	memset(chJumpSet, 0, sizeof(chJumpSet));
	int misaoMax = 0;
	int tempoWritten = 0;
	int misaoHasPcmEv[SC_FM_MISAO];
	memset(misaoHasPcmEv, 0, sizeof(misaoHasPcmEv));

	/* Inject doc tempo if score has notes but no tempo event (else default length math). */
	for (int i = 0; i < n; i++) {
		if (sorted[i].kind == SC_EV_FM_TEMPO) { tempoWritten = 1; break; }
	}
	if (!tempoWritten && d->tempoT > 0) {
		SasamiTrackStream* s0 = &w->tr[0];
		if (!SasamiStreamPut3(s0, 9, (uint8_t)(d->tempoT & 0xFF), (uint8_t)((d->tempoT >> 8) & 0xFF)))
			return 0;
		tempoWritten = 1;
	}
	for (int i = 0; i < n; i++) {
		const ScEvent* e = &sorted[i];
		if (e->kind == SC_EV_PCM_SAMPLE && e->ch >= SC_FM_CH && e->ch < SC_FM_TOTAL)
			misaoHasPcmEv[e->ch - SC_FM_CH] = 1;
	}
	for (int mi = 0; mi < SC_FM_MISAO; mi++) {
		if (!d->pcmRelPath[mi][0] || misaoHasPcmEv[mi]) continue;
		if (!ScPutMisaoPcmSample(&w->misao[mi], d->pcmSlot[mi], d->pcmRelPath[mi]))
			return 0;
		if (mi + 1 > misaoMax) misaoMax = mi + 1;
	}

	for (int i = 0; i < n; i++) {
		const ScEvent* e = &sorted[i];
		int ch = e->ch;
		if (ch < 0 || ch >= SC_FM_TOTAL) continue;
		SasamiTrackStream* s = (ch < SC_FM_CH) ? &w->tr[ch] : &w->misao[ch - SC_FM_CH];
		if (ch >= SC_FM_CH && (ch - SC_FM_CH + 1) > misaoMax)
			misaoMax = ch - SC_FM_CH + 1;

		if (e->kind == SC_EV_FM_NOTE || e->kind == SC_EV_FM_REST) {
			if (e->tick > lastTick[ch]) {
				uint32_t gap = e->tick - lastTick[ch];
				while (gap > 0) {
					uint8_t w8 = (gap > 255) ? 255 : (uint8_t)gap;
					if (!SasamiStreamPut3(s, 1, 0, w8)) return 0;
					gap -= w8;
				}
				lastTick[ch] = e->tick;
			}
		}

		switch (e->kind) {
		case SC_EV_FM_TEMPO:
			if (!SasamiStreamPut3(s, 9, e->a, e->b)) return 0;
			break;
		case SC_EV_JUMP_MARK: {
			/* Q: J lands here. Do not overwrite a prior |:. */
			if (!chJumpSet[ch]) {
				chStreamJump[ch] = s->size;
				chJumpSet[ch] = 1;
			}
			break;
		}
		case SC_EV_FM_LOOP_START: {
			/* cmd 13: count in b1. Body starts at next stream offset. */
			if (!SasamiStreamPut3(s, 13, e->a, 0)) return 0;
			if (loopWrSp[ch] < SC_LOOP_NEST_MAX) {
				loopBodyOff[ch][loopWrSp[ch]] = s->size; /* relative to track stream start */
				if (!chJumpSet[ch]) {
					/* J targets this first |: (matches DO--.FPY) */
					chStreamJump[ch] = s->size - 3;
					chJumpSet[ch] = 1;
				}
				loopWrSp[ch]++;
			}
			break;
		}
		case SC_EV_FM_LOOP_END: {
			if (loopWrSp[ch] <= 0) {
				wcsncpy_s(s_scLastWriteErr, 160, L"ループの不一致です (:| に対応する |: がありません)", _TRUNCATE);
				return 0;
			}
			loopWrSp[ch]--;
			uint32_t bodyRel = loopBodyOff[ch][loopWrSp[ch]];
			/* cmd 14: store track-relative body offset; BuildFpy patches to abs 0x1xxx */
			if (!SasamiStreamPut3(s, 14, (uint8_t)(bodyRel & 0xFF), (uint8_t)((bodyRel >> 8) & 0xFF)))
				return 0;
			break;
		}
		case SC_EV_FM_JUMP: {
			uint32_t rel = chJumpSet[ch] ? chStreamJump[ch] : 0;
			/* cmd 3 FJUMP — relative marker 0xF000|rel for BuildFpy patch */
			uint16_t mark = (uint16_t)(0xF000 | (rel & 0x0FFF));
			if (!SasamiStreamPut3(s, 3, (uint8_t)(mark & 0xFF), (uint8_t)(mark >> 8))) return 0;
			break;
		}
		case SC_EV_FM_VOICE: {
			if (IsSsgCh(ch)) break; /* SSG has no FNEIRO */
			if (ch >= SC_FM_CH) {
				if (!SasamiStreamPut3(s, 2, e->a, 0)) return 0; /* Misao PNEIRO slot */
				chHadVoice[ch] = 1;
				break;
			}
			/* b==1: custom embedded voice index (patched in SasamiBuildFpy via 0xFE00|idx) */
			uint16_t raw = (e->b == 1)
				? (uint16_t)(0xFE00 | (e->a & 0x3F))
				: (uint16_t)(0x900 + e->a * 0x30);
			if (!SasamiStreamPut3(s, 2, (uint8_t)(raw & 0xFF), (uint8_t)(raw >> 8))) return 0;
			chHadVoice[ch] = 1;
			break;
		}
		case SC_EV_FM_VOL:
			/* b==1 or SSG ch: PSGVOL cmd4 (0..15). Else FVOL cmd11 (YM TL). */
			if (IsSsgCh(ch) || e->b == 1) {
				if (!SasamiStreamPut3(s, 4, e->a, 0)) return 0;
			} else {
				if (!SasamiStreamPut3(s, 11, e->a, 0)) return 0;
			}
			chHadVol[ch] = 1;
			break;
		case SC_EV_FM_PITCH: {
			uint16_t raw = ScPitchCenter64ToMisaoRaw(e->a);
			if (!SasamiStreamPut3(s, 12, (uint8_t)(raw & 0xFF), (uint8_t)(raw >> 8))) return 0;
			break;
		}
		case SC_EV_PCM_SAMPLE:
			if (ch >= SC_FM_CH) {
				int mi = ch - SC_FM_CH;
				if (!ScPutMisaoPcmSample(s, e->a, d->pcmRelPath[mi])) return 0;
			}
			break;
		case SC_EV_FM_NOTE: {
			if (ch == 6) {
				int pad = e->a & 0x0F;
				if (pad < 0) pad = 0;
				if (pad > 5) pad = 5;
				const uint8_t mask = (uint8_t)(1 << pad);
				if (!SasamiStreamPut3(s, 8, 0x10, mask)) return 0;
				uint32_t totalDur = e->dur ? e->dur : 2;
				/* Coalesce following TIEs (same pitch) into one sounding length. */
				for (int j = i + 1; j < n; j++) {
					const ScEvent* te = &sorted[j];
					if (te->ch != e->ch) break;
					if (te->kind == SC_EV_TIE && te->a == e->a) {
						totalDur += te->dur ? te->dur : 0;
						continue;
					}
					if (te->kind == SC_EV_FM_NOTE || te->kind == SC_EV_FM_REST || te->kind == SC_EV_TIE)
						break;
				}
				uint8_t wait = (totalDur > 255) ? 255 : (uint8_t)(totalDur < 2 ? 2 : totalDur);
				if (!SasamiStreamPut3(s, 1, 0, wait)) return 0;
				if (!SasamiStreamPut3(s, 8, 0x10, (uint8_t)(0x80 | mask))) return 0;
				lastTick[ch] += totalDur;
				break;
			}
			if (!IsSsgCh(ch)) {
				if (!chHadVoice[ch]) {
					uint16_t raw;
					if (ch >= SC_FM_CH) {
						int mi = ch - SC_FM_CH;
						raw = d->pcmSlot[mi];
					} else if (d->voiceCount > 0)
						raw = (uint16_t)(0xFE00 | 0);
					else
						raw = (uint16_t)(0x900 + 1 * 0x30);
					if (!SasamiStreamPut3(s, 2, (uint8_t)(raw & 0xFF), (uint8_t)(raw >> 8))) return 0;
					chHadVoice[ch] = 1;
				}
				if (!chHadVol[ch]) {
					if (!SasamiStreamPut3(s, 11, 8, 0)) return 0;
					chHadVol[ch] = 1;
				}
			} else if (!chHadVol[ch]) {
				if (!SasamiStreamPut3(s, 4, 15, 0)) return 0; /* PSGVOL max */
				chHadVol[ch] = 1;
			}
			uint32_t totalDur = e->dur ? e->dur : 2;
			for (int j = i + 1; j < n; j++) {
				const ScEvent* te = &sorted[j];
				if (te->ch != e->ch) break;
				if (te->kind == SC_EV_TIE && te->a == e->a) {
					totalDur += te->dur ? te->dur : 0;
					continue;
				}
				if (te->kind == SC_EV_FM_NOTE || te->kind == SC_EV_FM_REST || te->kind == SC_EV_TIE)
					break;
			}
			uint8_t wait = (totalDur > 255) ? 255 : (uint8_t)(totalDur < 2 ? 2 : totalDur);
			if (!SasamiStreamPut3(s, 0, e->a, wait)) return 0;
			lastTick[ch] += totalDur;
			break;
		}
		case SC_EV_TIE: {
			/* Consumed by preceding FM_NOTE coalesce; orphan TIE → time wait only. */
			int consumed = 0;
			for (int j = i - 1; j >= 0; j--) {
				const ScEvent* pe = &sorted[j];
				if (pe->ch != e->ch) continue;
				if (pe->kind == SC_EV_FM_NOTE && pe->a == e->a) { consumed = 1; break; }
				if (pe->kind == SC_EV_TIE && pe->a == e->a) continue;
				break;
			}
			if (consumed) break;
			if (e->tick > lastTick[ch]) {
				uint32_t gap = e->tick - lastTick[ch];
				while (gap > 0) {
					uint8_t w8 = (gap > 255) ? 255 : (uint8_t)gap;
					if (!SasamiStreamPut3(s, 1, 0, w8)) return 0;
					gap -= w8;
				}
				lastTick[ch] = e->tick;
			}
			uint8_t wait = (e->dur > 255) ? 255 : (uint8_t)(e->dur < 2 ? 2 : e->dur);
			if (!SasamiStreamPut3(s, 1, 0, wait)) return 0;
			lastTick[ch] += e->dur ? e->dur : wait;
			break;
		}
		case SC_EV_FM_REST: {
			uint8_t wait = (e->dur > 255) ? 255 : (uint8_t)(e->dur < 2 ? 2 : e->dur);
			if (!SasamiStreamPut3(s, 1, 0, wait)) return 0;
			lastTick[ch] += e->dur ? e->dur : wait;
			break;
		}
		default: break;
		}
	}
	for (int ch = 0; ch < SC_FM_CH; ch++) {
		if (w->tr[ch].used)
			SasamiStreamPut3(&w->tr[ch], 3, 0xF0, 0x00);
	}
	for (int ch = 0; ch < SASAMI_MISAO_MAX_CH; ch++) {
		if (w->misao[ch].used)
			SasamiStreamPut3(&w->misao[ch], 3, 0xF0, 0x00);
	}
	w->misaoChCount = misaoMax;
	/* Nest depth ≥2 → FPY2 (classic FPY / ASM player is 1-deep only). */
	w->fpy2 = (ScDocMaxLoopNest(d->ev, d->evCount, SC_FM_TOTAL) > 1) ? 1 : 0;
	return 1;
}

int ScLoadTextFileW(const wchar_t* path, wchar_t* out, int outCch)
{
	if (!path || !out || outCch < 2) return 0;
	out[0] = 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD sz = GetFileSize(h, NULL);
	if (sz == INVALID_FILE_SIZE || sz > 4 * 1024 * 1024) { CloseHandle(h); return 0; }
	uint8_t* raw = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, sz + 2);
	if (!raw) { CloseHandle(h); return 0; }
	DWORD rd = 0;
	ReadFile(h, raw, sz, &rd, NULL);
	CloseHandle(h);
	raw[rd] = 0;
	raw[rd + 1] = 0;
	int ok = 0;
	if (rd >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
		int n = (int)((rd - 2) / 2);
		if (n >= outCch) n = outCch - 1;
		memcpy(out, raw + 2, (size_t)n * 2);
		out[n] = 0;
		ok = 1;
	} else if (rd >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
		int n = MultiByteToWideChar(CP_UTF8, 0, (char*)raw + 3, (int)rd - 3, out, outCch - 1);
		if (n > 0) { out[n] = 0; ok = 1; }
	} else {
		int n = MultiByteToWideChar(932, 0, (char*)raw, (int)rd, out, outCch - 1);
		if (n <= 0)
			n = MultiByteToWideChar(CP_UTF8, 0, (char*)raw, (int)rd, out, outCch - 1);
		if (n > 0) { out[n] = 0; ok = 1; }
	}
	HeapFree(GetProcessHeap(), 0, raw);
	if (ok) {
		/* PC98 DAT often ends with SUB (0x1A). Strip EOF marker and trailing whitespace. */
		for (int i = 0; out[i]; i++) {
			if (out[i] == 0x1A) {
				out[i] = 0;
				break;
			}
		}
		int n = (int)wcslen(out);
		while (n > 0) {
			wchar_t c = out[n - 1];
			if (c == L'\r' || c == L'\n' || c == L' ' || c == L'\t')
				n--;
			else
				break;
		}
		out[n] = 0;
	}
	return ok;
}

int ScSaveTextFileW(const wchar_t* path, const wchar_t* text)
{
	if (!path || !text) return 0;
	int need = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
	if (need <= 0) return 0;
	char* u8 = (char*)HeapAlloc(GetProcessHeap(), 0, (size_t)need + 3);
	if (!u8) return 0;
	u8[0] = (char)0xEF; u8[1] = (char)0xBB; u8[2] = (char)0xBF;
	WideCharToMultiByte(CP_UTF8, 0, text, -1, u8 + 3, need, NULL, NULL);
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) { HeapFree(GetProcessHeap(), 0, u8); return 0; }
	DWORD wr = 0;
	BOOL ok = WriteFile(h, u8, (DWORD)(need - 1 + 3), &wr, NULL);
	CloseHandle(h);
	HeapFree(GetProcessHeap(), 0, u8);
	return ok ? 1 : 0;
}

int ScExtractOldFToText(const wchar_t* path, wchar_t* out, int outCch)
{
	if (!path || !out || outCch < 2) return 0;
	out[0] = 0;
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	DWORD sz = GetFileSize(h, NULL);
	if (sz == INVALID_FILE_SIZE || sz > 2 * 1024 * 1024) { CloseHandle(h); return 0; }
	uint8_t* raw = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, sz + 1);
	if (!raw) { CloseHandle(h); return 0; }
	DWORD rd = 0;
	ReadFile(h, raw, sz, &rd, NULL);
	CloseHandle(h);
	raw[rd] = 0;
	/* magic FE 21 → extract quoted DATA-like lines / after OUTPUT */
	if (rd >= 2 && raw[0] == 0xFE && raw[1] == 0x21) {
		/* collect printable SJIS runs that look like DAT body (lines with notes) */
		char* body = (char*)HeapAlloc(GetProcessHeap(), 0, rd + 1);
		if (!body) { HeapFree(GetProcessHeap(), 0, raw); return 0; }
		int bi = 0;
		int inQ = 0;
		for (DWORD i = 0; i < rd; i++) {
			char c = (char)raw[i];
			if (c == '"') { inQ = !inQ; if (!inQ && bi > 0 && bi < (int)rd) body[bi++] = '\n'; continue; }
			if (inQ) body[bi++] = c;
		}
		body[bi] = 0;
		int n = MultiByteToWideChar(932, 0, body, bi, out, outCch - 1);
		HeapFree(GetProcessHeap(), 0, body);
		HeapFree(GetProcessHeap(), 0, raw);
		if (n > 0) { out[n] = 0; return 1; }
		return 0;
	}
	HeapFree(GetProcessHeap(), 0, raw);
	return ScLoadTextFileW(path, out, outCch);
}

#include "ogg.h"
#include "resource.h"
#include "PlayList.h"
#include "TranscodeExport.h"

extern CPlayList* pl;

void ScOpenAudioExport(CWnd* owner, const wchar_t* builtPath)
{
	if (!builtPath || !builtPath[0]) return;
	playlistdata0 item;
	memset(&item, 0, sizeof(item));
	_tcsncpy_s(item.fol, builtPath, _TRUNCATE);
	{
		const wchar_t* slash = wcsrchr(builtPath, L'\\');
		_tcsncpy_s(item.name, slash ? slash + 1 : builtPath, _TRUNCATE);
	}
	if (pl) {
		TCHAR kpiBuf[MAX_PATH];
		kpiBuf[0] = 0;
		BYTE kv = 0;
		playlistdata tmp;
		memset(&tmp, 0, sizeof(tmp));
		_tcsncpy_s(tmp.fol, builtPath, _TRUNCATE);
		_tcsncpy_s(tmp.name, item.name, _TRUNCATE);
		pl->plugs(CString(builtPath), &tmp, kpiBuf, kv);
		_tcsncpy_s(item.fol, tmp.fol, _TRUNCATE);
		_tcsncpy_s(item.name, tmp.name, _TRUNCATE);
		item.sub = tmp.sub;
		item.loop1 = tmp.loop1;
		item.loop2 = tmp.loop2;
		item.ret2 = tmp.ret2;
	}
	CTranscodeExport* dlg = new CTranscodeExport(owner ? owner : AfxGetMainWnd());
	dlg->m_initialTab = -1;
	dlg->m_preferXfade = false;
	dlg->multiFile = false;
	memcpy(&dlg->pc, &item, sizeof(item));
	dlg->DoModal();
	delete dlg;
}

/* ---- score ↔ text MML emit + DatArc last session ---- */

static int ScDurToLen(int durTicks, int* dotsOut)
{
	if (dotsOut) *dotsOut = 0;
	if (durTicks <= 0) return 4;
	/* Prefer exact power-of-two lengths from PPQN. */
	static const int lens[] = { 1, 2, 4, 8, 16, 32, 64 };
	for (int i = 0; i < 7; i++) {
		int want = (SC_PPQN * 4) / lens[i];
		if (want == durTicks) return lens[i];
		if (want + want / 2 == durTicks) {
			if (dotsOut) *dotsOut = 1;
			return lens[i];
		}
	}
	/* Fallback: nearest len */
	int best = 4, bestDiff = 99999;
	for (int i = 0; i < 7; i++) {
		int want = (SC_PPQN * 4) / lens[i];
		int d = want > durTicks ? want - durTicks : durTicks - want;
		if (d < bestDiff) { bestDiff = d; best = lens[i]; }
	}
	return best;
}

static void ScAppend(wchar_t* out, int outCch, int* len, const wchar_t* s)
{
	if (!out || !len || outCch < 2 || !s) return;
	int n = (int)wcslen(s);
	if (*len + n >= outCch - 1) n = outCch - 1 - *len;
	if (n <= 0) return;
	wcsncpy_s(out + *len, outCch - *len, s, n);
	*len += n;
	out[*len] = 0;
}

static void ScAppendF(wchar_t* out, int outCch, int* len, const wchar_t* fmt, ...)
{
	wchar_t buf[512];
	va_list ap;
	va_start(ap, fmt);
	_vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
	va_end(ap);
	ScAppend(out, outCch, len, buf);
}

static void ScAppendB64Lines(wchar_t* out, int outCch, int* len,
	const wchar_t* tag, const wchar_t* tagCont, const uint8_t* bytes, uint32_t blen)
{
	if (!bytes || !blen) return;
	const uint32_t need = ((blen + 2) / 3) * 4 + 1;
	wchar_t* enc = (wchar_t*)malloc(need * sizeof(wchar_t));
	if (!enc) return;
	if (!ScB64Encode(bytes, blen, enc, (int)need)) { free(enc); return; }
	const int chunk = 76;
	int pos = 0;
	int first = 1;
	while (enc[pos]) {
		wchar_t line[80];
		int n = 0;
		while (enc[pos] && n < chunk) line[n++] = enc[pos++];
		line[n] = 0;
		ScAppendF(out, outCch, len, L"%s %s\r\n", first ? tag : tagCont, line);
		first = 0;
	}
	free(enc);
}

static int ScVstB64LineInfo(const wchar_t* line, int lineLen, int* isCtrl, int* isCont)
{
	if (!line || lineLen <= 0 || !isCtrl || !isCont) return 0;
	int i = 0;
	while (i < lineLen && (line[i] == L' ' || line[i] == L'\t')) i++;
	if (i >= lineLen || line[i] != L'@') return 0;
	i++;
	if (i + 11 <= lineLen && _wcsnicmp(line + i, L"VSTSTATEB64", 11) == 0) {
		*isCtrl = 0;
		i += 11;
		*isCont = (i < lineLen && line[i] == L'+') ? 1 : 0;
		if (*isCont) i++;
		return 1;
	}
	if (i + 10 <= lineLen && _wcsnicmp(line + i, L"VSTCTRLB64", 10) == 0) {
		*isCtrl = 1;
		i += 10;
		*isCont = (i < lineLen && line[i] == L'+') ? 1 : 0;
		if (*isCont) i++;
		return 1;
	}
	return 0;
}

static int ScVstB64PayloadChars(const wchar_t* line, int lineLen)
{
	int i = 0;
	while (i < lineLen && (line[i] == L' ' || line[i] == L'\t')) i++;
	if (i >= lineLen || line[i] != L'@') return 0;
	i++;
	if (i + 11 <= lineLen && _wcsnicmp(line + i, L"VSTSTATEB64", 11) == 0) i += 11;
	else if (i + 10 <= lineLen && _wcsnicmp(line + i, L"VSTCTRLB64", 10) == 0) i += 10;
	else return 0;
	if (i < lineLen && line[i] == L'+') i++;
	while (i < lineLen && (line[i] == L' ' || line[i] == L'\t')) i++;
	int n = 0;
	for (; i < lineLen; i++) {
		const wchar_t c = line[i];
		if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')
			|| c == L'+' || c == L'/' || c == L'=')
			n++;
	}
	return n;
}

int ScMmlContainsVstB64(const wchar_t* text)
{
	if (!text) return 0;
	return (wcsstr(text, L"@VSTSTATEB64") != NULL || wcsstr(text, L"@VSTCTRLB64") != NULL) ? 1 : 0;
}

int ScMmlCollapseVstB64(const wchar_t* in, wchar_t* out, int outCch)
{
	if (!in || !out || outCch < 4) return 0;
	out[0] = 0;
	int len = 0;
	const wchar_t* p = in;
	while (*p) {
		const wchar_t* lineStart = p;
		while (*p && *p != L'\r' && *p != L'\n') p++;
		const int lineLen = (int)(p - lineStart);
		/* Notes glued before @VSTSTATEB64 on one line — split them. */
		const wchar_t* inl = NULL;
		for (const wchar_t* s = lineStart; s < lineStart + lineLen; s++) {
			if (*s != L'@') continue;
			if (s + 11 <= lineStart + lineLen && _wcsnicmp(s + 1, L"VSTSTATEB64", 11) == 0) {
				inl = s;
				break;
			}
			if (s + 10 <= lineStart + lineLen && _wcsnicmp(s + 1, L"VSTCTRLB64", 10) == 0) {
				inl = s;
				break;
			}
		}
		if (inl && inl > lineStart) {
			while (inl > lineStart && (inl[-1] == L' ' || inl[-1] == L'\t')) inl--;
			const int noteLen = (int)(inl - lineStart);
			if (noteLen > 0) {
				wchar_t notes[4096];
				const int cpy = noteLen < 4095 ? noteLen : 4095;
				wcsncpy_s(notes, lineStart, cpy);
				notes[cpy] = 0;
				ScAppendF(out, outCch, &len, L"%s\r\n", notes);
			}
			const int b64Len = lineLen - (int)(inl - lineStart);
			const wchar_t* b64Start = inl;
			int isCtrl = 0, isCont = 0;
			if (ScVstB64LineInfo(b64Start, b64Len, &isCtrl, &isCont) && !isCont) {
				const wchar_t* tag = isCtrl ? L"@VSTCTRLB64" : L"@VSTSTATEB64";
				int b64Chars = ScVstB64PayloadChars(b64Start, b64Len);
				int lines = 1;
				const wchar_t* q = p;
				while (*q == L'\r' || *q == L'\n') q++;
				for (;;) {
					const wchar_t* ls = q;
					while (*q && *q != L'\r' && *q != L'\n') q++;
					const int ll = (int)(q - ls);
					int ic = 0, icont = 0;
					if (!ScVstB64LineInfo(ls, ll, &ic, &icont) || !icont || ic != isCtrl)
						break;
					b64Chars += ScVstB64PayloadChars(ls, ll);
					lines++;
					while (*q == L'\r' || *q == L'\n') q++;
				}
				const unsigned bytes = (unsigned)((b64Chars * 3u) / 4u);
				ScAppendF(out, outCch, &len, L"%s …(%u bytes, %d lines)…\r\n", tag, bytes, lines);
				p = q;
				continue;
			}
		}
		int isCtrl = 0, isCont = 0;
		if (ScVstB64LineInfo(lineStart, lineLen, &isCtrl, &isCont) && !isCont) {
			const wchar_t* tag = isCtrl ? L"@VSTCTRLB64" : L"@VSTSTATEB64";
			int b64Chars = ScVstB64PayloadChars(lineStart, lineLen);
			int lines = 1;
			const wchar_t* q = p;
			while (*q == L'\r' || *q == L'\n') q++;
			for (;;) {
				const wchar_t* ls = q;
				while (*q && *q != L'\r' && *q != L'\n') q++;
				const int ll = (int)(q - ls);
				int ic = 0, icont = 0;
				if (!ScVstB64LineInfo(ls, ll, &ic, &icont) || !icont || ic != isCtrl)
					break;
				b64Chars += ScVstB64PayloadChars(ls, ll);
				lines++;
				while (*q == L'\r' || *q == L'\n') q++;
			}
			const unsigned bytes = (unsigned)((b64Chars * 3u) / 4u);
			ScAppendF(out, outCch, &len, L"%s …(%u bytes, %d lines)…\r\n", tag, bytes, lines);
			p = q;
			continue;
		}
		if (lineLen > 0) {
			wchar_t line[4096];
			const int cpy = lineLen < 4095 ? lineLen : 4095;
			wcsncpy_s(line, lineStart, cpy);
			line[cpy] = 0;
			ScAppendF(out, outCch, &len, L"%s\r\n", line);
		} else {
			ScAppend(out, outCch, &len, L"\r\n");
		}
		while (*p == L'\r' || *p == L'\n') p++;
	}
	return 1;
}

static void ScNoteName(int midi, int* octOut, wchar_t* nameOut)
{
	static const wchar_t* nm[12] = {
		L"c", L"c+", L"d", L"d+", L"e", L"f", L"f+", L"g", L"g+", L"a", L"a+", L"b"
	};
	int n = midi;
	if (n < 0) n = 0;
	if (n > 127) n = 127;
	*octOut = n / 12 - 1;
	if (*octOut < 0) *octOut = 0;
	if (*octOut > 8) *octOut = 8;
	wcscpy_s(nameOut, 8, nm[n % 12]);
}

static void ScAppendB64Placeholder(wchar_t* out, int outCch, int* len,
	const wchar_t* tag, const uint8_t* bytes, uint32_t blen)
{
	if (!bytes || !blen || !tag) return;
	const unsigned b64Chars = (unsigned)(((blen + 2u) / 3u) * 4u);
	const int lines = (int)((b64Chars + 75u) / 76u);
	ScAppendF(out, outCch, len, L"%s …(%u bytes, %d lines)…\r\n", tag, (unsigned)blen, lines);
}

static int ScMidiDocToMmlImpl(const ScMidiDoc* d, wchar_t* out, int outCch, int embedB64Full)
{
	if (!d || !out || outCch < 64) return 0;
	out[0] = 0;
	int len = 0;
	const int cap = outCch - 1;
	ScAppend(out, outCch, &len, L"; SASAMI score↔text\r\n");
	if (d->titleSjis[0]) {
		wchar_t tit[80];
		MultiByteToWideChar(932, 0, d->titleSjis, -1, tit, 80);
		ScAppendF(out, outCch, &len, L"TIT\"%s\"\r\n", tit);
	}
	ScAppendF(out, outCch, &len, L"@METER %d/%d\r\n",
		d->numer > 0 ? d->numer : 4, d->denom > 0 ? d->denom : 4);
	int bpm = 120;
	if (d->tempoT > 0)
		bpm = (int)((13000.0 * 120.0) / (double)d->tempoT + 0.5);
	ScAppendF(out, outCch, &len, L"t%d\r\n", bpm);

	int used[SC_MIDI_CH];
	memset(used, 0, sizeof(used));
	for (int i = 0; i < d->evCount; i++)
		if (d->ev[i].ch < SC_MIDI_CH) used[d->ev[i].ch] = 1;
	for (int ch = 0; ch < SC_MIDI_CH; ch++) {
		if (d->bind.vstPath[ch][0] || d->bind.vstProg[ch] >= 0) used[ch] = 1;
		for (int sl = 0; sl < ScMidiFxBind::SC_FX_SLOTS; ++sl)
			if (d->fxBind.fxPath[ch][sl][0]) used[ch] = 1;
		if (!used[ch]) continue;
		if (len >= cap - 64) return 0;
		int part = d->trackPart[ch];
		if (part == 0xFF) part = (uint8_t)ch;
		ScAppendF(out, outCch, &len, L"\r\n[%d:%d]\r\n", (int)part + 1, ch + 1);
		if (d->bind.vstPath[ch][0])
			ScAppendF(out, outCch, &len, L"@VST\"%s\"\r\n", d->bind.vstPath[ch]);
		for (int sl = 0; sl < ScMidiFxBind::SC_FX_SLOTS; ++sl) {
			if (d->fxBind.fxPath[ch][sl][0])
				ScAppendF(out, outCch, &len, L"@VSTFX %d \"%s\"\r\n", sl, d->fxBind.fxPath[ch][sl]);
			if (d->fxBind.fxStateLen[ch][sl] && d->fxBind.fxState[ch][sl]) {
				wchar_t tag0[48], tag1[48];
				_snwprintf_s(tag0, _TRUNCATE, L"@VSTFXSTATEB64 %d", sl);
				_snwprintf_s(tag1, _TRUNCATE, L"@VSTFXSTATEB64+ %d", sl);
				if (embedB64Full)
					ScAppendB64Lines(out, outCch, &len, tag0, tag1,
						d->fxBind.fxState[ch][sl], d->fxBind.fxStateLen[ch][sl]);
				else
					ScAppendB64Placeholder(out, outCch, &len, tag0,
						d->fxBind.fxState[ch][sl], d->fxBind.fxStateLen[ch][sl]);
			}
		}
		if (d->bind.vstProg[ch] >= 0)
			ScAppendF(out, outCch, &len, L"@PROG %d\r\n", d->bind.vstProg[ch]);
		if (d->bind.vstBankMsb[ch] >= 0 || d->bind.vstBankLsb[ch] >= 0)
			ScAppendF(out, outCch, &len, L"@BANK %d,%d\r\n",
				d->bind.vstBankMsb[ch] >= 0 ? d->bind.vstBankMsb[ch] : 0,
				d->bind.vstBankLsb[ch] >= 0 ? d->bind.vstBankLsb[ch] : 0);

		int curOct = -1;
		uint32_t expect = 0;

		/* Own buffer — do not reuse ScSortedEvBuf (writer may share). */
		static ScEvent* chEv = NULL;
		static uint8_t* consumed = NULL;
		if (!chEv)
			chEv = (ScEvent*)HeapAlloc(GetProcessHeap(), 0, sizeof(ScEvent) * (SIZE_T)SC_EV_MAX);
		if (!consumed)
			consumed = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)SC_EV_MAX);
		if (!chEv || !consumed) return 0;

		int nCh = 0;
		for (int i = 0; i < d->evCount; i++)
			if (d->ev[i].ch == (uint8_t)ch) chEv[nCh++] = d->ev[i];
		qsort(chEv, (size_t)nCh, sizeof(ScEvent), CmpEv);
		memset(consumed, 0, (size_t)nCh);

		auto fitPiece = [](int gap, int* lnOut, int* dotsOut) -> int {
			static const int lens[] = { 1, 2, 4, 8, 16, 32, 64 };
			int bestLn = 64, bestDots = 0, bestGot = 0;
			for (int i = 0; i < 7; i++) {
				int want = (SC_PPQN * 4) / lens[i];
				if (want <= gap && want >= bestGot) {
					bestGot = want; bestLn = lens[i]; bestDots = 0;
				}
				int dotted = want + want / 2;
				if (dotted <= gap && dotted >= bestGot) {
					bestGot = dotted; bestLn = lens[i]; bestDots = 1;
				}
			}
			if (bestGot <= 0) {
				bestGot = 1; bestLn = 64; bestDots = 0;
			}
			if (lnOut) *lnOut = bestLn;
			if (dotsOut) *dotsOut = bestDots;
			return bestGot;
		};

		auto emitGap = [&](uint32_t toTick) {
			if (toTick <= expect) return;
			uint32_t gap = toTick - expect;
			while (gap > 0) {
				int ln = 4, dots = 0;
				int got = fitPiece((int)gap, &ln, &dots);
				if (got > (int)gap) got = (int)gap;
				ScAppendF(out, outCch, &len, L"r%d", ln);
				for (int dti = 0; dti < dots; dti++) ScAppend(out, outCch, &len, L".");
				ScAppend(out, outCch, &len, L" ");
				expect += (uint32_t)got;
				gap -= (uint32_t)got;
				if (len >= cap - 64) return;
			}
		};

		auto emitCc = [&](const ScEvent& e) {
			if (e.kind == SC_EV_VELO)
				ScAppendF(out, outCch, &len, L"v%d ", (int)e.a);
			else if (e.kind == SC_EV_VOL)
				ScAppendF(out, outCch, &len, L"@V%d ", (int)e.a);
			else if (e.kind == SC_EV_PAN)
				ScAppendF(out, outCch, &len, L"P%d ", (int)e.a);
			else if (e.kind == SC_EV_PITCH) {
				int bent = 0x2000 + ((int)e.a - 64) * (0x1FFF / 64);
				if (bent < 0) bent = 0;
				if (bent > 0x3FFF) bent = 0x3FFF;
				ScAppendF(out, outCch, &len, L"@P%d ", bent);
			}
		};

		auto isCc = [](uint8_t k) -> int {
			return (k == SC_EV_VELO || k == SC_EV_VOL || k == SC_EV_PAN || k == SC_EV_PITCH) ? 1 : 0;
		};

		auto emitLen = [&](int durTicks, int asTie) {
			int left = durTicks;
			int first = 1;
			while (left > 0) {
				int ln = 4, dots = 0;
				int got = fitPiece(left, &ln, &dots);
				if (got > left) got = left;
				if (asTie || !first) ScAppendF(out, outCch, &len, L"^%d", ln);
				else ScAppendF(out, outCch, &len, L"%d", ln);
				for (int dti = 0; dti < dots; dti++) ScAppend(out, outCch, &len, L".");
				left -= got;
				first = 0;
				asTie = 1;
			}
		};

		for (int i = 0; i < nCh; i++) {
			if (len >= cap - 128) return 0;
			if (consumed[i]) continue;
			const ScEvent& e = chEv[i];

			if (e.kind == SC_EV_TEMPO) {
				emitGap(e.tick);
				int t = e.a | (e.b << 8);
				int b = t > 0 ? (int)((13000.0 * 120.0) / (double)t + 0.5) : 120;
				ScAppendF(out, outCch, &len, L"t%d ", b);
				continue;
			}
			if (e.kind == SC_EV_PROG) {
				emitGap(e.tick);
				ScAppendF(out, outCch, &len, L"@PROG %d ", (int)e.a);
				continue;
			}
			if (e.kind == SC_EV_BANK) {
				emitGap(e.tick);
				ScAppendF(out, outCch, &len, L"@BANK %d,%d ", (int)e.a, (int)e.b);
				continue;
			}
			if (e.kind == SC_EV_RPN) {
				emitGap(e.tick);
				ScAppendF(out, outCch, &len, L"@RPN %u,%u,%u ", (unsigned)e.a, (unsigned)e.b, (unsigned)e.c);
				continue;
			}
			if (e.kind == SC_EV_NRPN) {
				emitGap(e.tick);
				ScAppendF(out, outCch, &len, L"@NRPN %u,%u,%u ", (unsigned)e.a, (unsigned)e.b, (unsigned)e.c);
				continue;
			}
			if (e.kind == SC_EV_SYSEX) {
				emitGap(e.tick);
				const int si = (int)e.a;
				if (si >= 0 && si < d->sysexCount && d->sysexLen[si] > 0) {
					ScAppend(out, outCch, &len, L"@EX ");
					for (int bi = 0; bi < d->sysexLen[si]; ++bi)
						ScAppendF(out, outCch, &len, bi ? L" %02X" : L"%02X", (unsigned)d->sysex[si][bi]);
					ScAppend(out, outCch, &len, L" ");
				}
				continue;
			}
			if (e.kind == SC_EV_JUMP_MARK || e.kind == SC_EV_FM_JUMP ||
				e.kind == SC_EV_FM_LOOP_START || e.kind == SC_EV_FM_LOOP_END ||
				e.kind == SC_EV_PEDAL_ON || e.kind == SC_EV_PEDAL_OFF) {
				emitGap(e.tick);
				if (e.kind == SC_EV_JUMP_MARK) ScAppend(out, outCch, &len, L"Q ");
				else if (e.kind == SC_EV_FM_JUMP) ScAppend(out, outCch, &len, L"J ");
				else if (e.kind == SC_EV_FM_LOOP_START)
					ScAppendF(out, outCch, &len, L"|:%d ", e.a ? (int)e.a : 2);
				else if (e.kind == SC_EV_FM_LOOP_END) ScAppend(out, outCch, &len, L":| ");
				else if (e.kind == SC_EV_PEDAL_ON) ScAppend(out, outCch, &len, L"@PEDON ");
				else ScAppend(out, outCch, &len, L"@PEDOFF ");
				continue;
			}

			if (isCc(e.kind)) {
				emitGap(e.tick);
				emitCc(e);
				continue;
			}

			if (e.kind != SC_EV_NOTE && e.kind != SC_EV_REST) continue;

			const uint32_t noteTick = e.tick;
			const uint32_t noteDur = e.dur ? e.dur : (uint32_t)SC_PPQN;
			const uint32_t noteEnd = noteTick + noteDur;

			int ccIdx[256];
			int ccN = 0;
			for (int j = 0; j < nCh && ccN < 256; j++) {
				if (consumed[j]) continue;
				const ScEvent& ccev = chEv[j];
				if (!isCc(ccev.kind)) continue;
				if (ccev.tick < noteTick) continue;
				if (ccev.tick >= noteEnd) continue;
				ccIdx[ccN++] = j;
			}

			emitGap(noteTick);
			for (int c = 0; c < ccN; c++) {
				if (chEv[ccIdx[c]].tick == noteTick) {
					emitCc(chEv[ccIdx[c]]);
					consumed[ccIdx[c]] = 1;
				}
			}

			if (e.kind == SC_EV_REST) {
				expect = noteTick;
				for (int c = 0; c < ccN; c++) {
					if (consumed[ccIdx[c]]) continue;
					uint32_t ct = chEv[ccIdx[c]].tick;
					emitGap(ct);
					emitCc(chEv[ccIdx[c]]);
					consumed[ccIdx[c]] = 1;
				}
				emitGap(noteEnd);
				continue;
			}

			/* NOTE: a+8 v29 ^8 v45 … (timed CC inside the sounding note) */
			int oct = 4;
			wchar_t nm[8];
			ScNoteName((int)e.a, &oct, nm);
			if (oct != curOct) {
				ScAppendF(out, outCch, &len, L"o%d ", oct);
				curOct = oct;
			}

			uint32_t brk[260];
			int brN = 0;
			brk[brN++] = noteTick;
			for (int c = 0; c < ccN; c++) {
				uint32_t ct = chEv[ccIdx[c]].tick;
				if (ct > noteTick && ct < noteEnd) brk[brN++] = ct;
			}
			brk[brN++] = noteEnd;
			int wbr = 1;
			for (int b = 1; b < brN; b++)
				if (brk[b] != brk[wbr - 1]) brk[wbr++] = brk[b];
			brN = wbr;

			for (int b = 0; b + 1 < brN; b++) {
				uint32_t t0 = brk[b];
				uint32_t t1 = brk[b + 1];
				int seg = (int)(t1 - t0);
				if (seg <= 0) continue;
				if (b == 0) {
					ScAppendF(out, outCch, &len, L"%s", nm);
					emitLen(seg, 0);
				} else {
					for (int c = 0; c < ccN; c++) {
						if (consumed[ccIdx[c]]) continue;
						if (chEv[ccIdx[c]].tick == t0) {
							emitCc(chEv[ccIdx[c]]);
							consumed[ccIdx[c]] = 1;
						}
					}
					emitLen(seg, 1);
				}
			}
			ScAppend(out, outCch, &len, L" ");
			expect = noteEnd;
			for (int c = 0; c < ccN; c++)
				if (chEv[ccIdx[c]].tick < noteEnd) consumed[ccIdx[c]] = 1;
		}
		/* VST blobs after notes — always on their own line(s). */
		if (d->bind.vstCompLen[ch] && d->bind.vstComp[ch]) {
			ScAppend(out, outCch, &len, L"\r\n");
			if (embedB64Full)
				ScAppendB64Lines(out, outCch, &len, L"@VSTSTATEB64", L"@VSTSTATEB64+",
					d->bind.vstComp[ch], d->bind.vstCompLen[ch]);
			else
				ScAppendB64Placeholder(out, outCch, &len, L"@VSTSTATEB64",
					d->bind.vstComp[ch], d->bind.vstCompLen[ch]);
		}
		if (d->bind.vstCtrlLen[ch] && d->bind.vstCtrl[ch]) {
			ScAppend(out, outCch, &len, L"\r\n");
			if (embedB64Full)
				ScAppendB64Lines(out, outCch, &len, L"@VSTCTRLB64", L"@VSTCTRLB64+",
					d->bind.vstCtrl[ch], d->bind.vstCtrlLen[ch]);
			else
				ScAppendB64Placeholder(out, outCch, &len, L"@VSTCTRLB64",
					d->bind.vstCtrl[ch], d->bind.vstCtrlLen[ch]);
		}
		ScAppend(out, outCch, &len, L"\r\n");
	}
	return len > 0 ? 1 : 0;
}

int ScMidiDocToMml(const ScMidiDoc* d, wchar_t* out, int outCch)
{
	return ScMidiDocToMmlImpl(d, out, outCch, 0);
}

int ScMidiDocToMmlFull(const ScMidiDoc* d, wchar_t* out, int outCch)
{
	return ScMidiDocToMmlImpl(d, out, outCch, 1);
}

void ScMidiDocMergeVstBind(ScMidiDoc* dst, const ScMidiDoc* src)
{
	if (!dst || !src) return;
	for (int ch = 0; ch < SC_MIDI_CH; ch++) {
		if (!dst->bind.vstPath[ch][0] && src->bind.vstPath[ch][0])
			wcsncpy_s(dst->bind.vstPath[ch], src->bind.vstPath[ch], _TRUNCATE);
		if (dst->bind.vstProg[ch] < 0 && src->bind.vstProg[ch] >= 0)
			dst->bind.vstProg[ch] = src->bind.vstProg[ch];
		if (dst->bind.vstBankMsb[ch] < 0 && src->bind.vstBankMsb[ch] >= 0)
			dst->bind.vstBankMsb[ch] = src->bind.vstBankMsb[ch];
		if (dst->bind.vstBankLsb[ch] < 0 && src->bind.vstBankLsb[ch] >= 0)
			dst->bind.vstBankLsb[ch] = src->bind.vstBankLsb[ch];
		if (!dst->bind.vstCompLen[ch] && src->bind.vstCompLen[ch] && src->bind.vstComp[ch])
			SasamiVstBlobSet(&dst->bind.vstComp[ch], &dst->bind.vstCompLen[ch],
				src->bind.vstComp[ch], src->bind.vstCompLen[ch]);
		if (!dst->bind.vstCtrlLen[ch] && src->bind.vstCtrlLen[ch] && src->bind.vstCtrl[ch])
			SasamiVstBlobSet(&dst->bind.vstCtrl[ch], &dst->bind.vstCtrlLen[ch],
				src->bind.vstCtrl[ch], src->bind.vstCtrlLen[ch]);
		for (int sl = 0; sl < ScMidiFxBind::SC_FX_SLOTS; ++sl) {
			if (!dst->fxBind.fxPath[ch][sl][0] && src->fxBind.fxPath[ch][sl][0])
				wcsncpy_s(dst->fxBind.fxPath[ch][sl], src->fxBind.fxPath[ch][sl], _TRUNCATE);
			if (!dst->fxBind.fxStateLen[ch][sl] && src->fxBind.fxStateLen[ch][sl] && src->fxBind.fxState[ch][sl])
				SasamiVstBlobSet(&dst->fxBind.fxState[ch][sl], &dst->fxBind.fxStateLen[ch][sl],
					src->fxBind.fxState[ch][sl], src->fxBind.fxStateLen[ch][sl]);
			if (src->fxBind.fxBypass[ch][sl])
				dst->fxBind.fxBypass[ch][sl] = src->fxBind.fxBypass[ch][sl];
		}
	}
	if (src->bind.isMpw3) dst->bind.isMpw3 = 1;
}

#pragma pack(push, 1)
struct ScSessionHdr {
	char magic[8];      /* "SASAMI2\0" */
	uint32_t ver;       /* 1 */
	uint32_t flags;
	uint32_t mmlBytes;  /* UTF-16 bytes following hdr+pad */
	uint32_t reserved[32]; /* future expansion (128 bytes) */
};
#pragma pack(pop)

static const wchar_t* kScSessionLeaf = L"sasami_composer_last.dat";

int ScSessionSaveLastMidi(const ScMidiDoc* d, const wchar_t* mmlOpt)
{
	if (!d) return 0;
	const int mmlCch = (int)(SC_TEXT_MAX / sizeof(wchar_t));
	wchar_t* mmlBuf = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
		(SIZE_T)mmlCch * sizeof(wchar_t));
	if (!mmlBuf) return 0;
	if (mmlOpt && mmlOpt[0])
		wcsncpy_s(mmlBuf, mmlCch, mmlOpt, _TRUNCATE);
	else if (!ScMidiDocToMml(d, mmlBuf, mmlCch)) {
		HeapFree(GetProcessHeap(), 0, mmlBuf);
		return 0;
	}

	DatArc_Chdir();
	CString path = DatArc_Path(kScSessionLeaf);
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		HeapFree(GetProcessHeap(), 0, mmlBuf);
		return 0;
	}

	ScSessionHdr hdr;
	memset(&hdr, 0, sizeof(hdr));
	memcpy(hdr.magic, "SASAMI2", 7);
	hdr.ver = 1;
	hdr.flags = d->bind.isMpw3 ? 1u : 0u;
	hdr.mmlBytes = (uint32_t)((wcslen(mmlBuf) + 1) * sizeof(wchar_t));
	/* Pack a few live fields into reserved for quick restore without full recompile. */
	hdr.reserved[0] = (uint32_t)d->tempoT;
	hdr.reserved[1] = (uint32_t)d->numer;
	hdr.reserved[2] = (uint32_t)d->denom;
	hdr.reserved[3] = (uint32_t)d->evCount;
	DWORD wr = 0;
	BOOL ok = WriteFile(h, &hdr, sizeof(hdr), &wr, NULL) && wr == sizeof(hdr);
	if (ok) ok = WriteFile(h, mmlBuf, hdr.mmlBytes, &wr, NULL) && wr == hdr.mmlBytes;
	/* Pad so future fields can grow without shifting consumers much. */
	if (ok) {
		BYTE pad[4096];
		memset(pad, 0, sizeof(pad));
		ok = WriteFile(h, pad, sizeof(pad), &wr, NULL) != 0;
	}
	HeapFree(GetProcessHeap(), 0, mmlBuf);
	CloseHandle(h);
	if (ok) DatArc_Commit(kScSessionLeaf);
	return ok ? 1 : 0;
}

int ScSessionLoadLastMidi(ScMidiDoc* d, wchar_t* mmlOut, int mmlCch)
{
	if (!d) return 0;
	DatArc_Chdir();
	CString path = DatArc_Path(kScSessionLeaf);
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	ScSessionHdr hdr;
	DWORD rd = 0;
	if (!ReadFile(h, &hdr, sizeof(hdr), &rd, NULL) || rd != sizeof(hdr) ||
		memcmp(hdr.magic, "SASAMI2", 7) != 0 || hdr.ver < 1) {
		CloseHandle(h);
		return 0;
	}
	if (hdr.mmlBytes < sizeof(wchar_t) || hdr.mmlBytes > SC_TEXT_MAX) {
		CloseHandle(h);
		return 0;
	}
	wchar_t* buf = (wchar_t*)malloc(hdr.mmlBytes + sizeof(wchar_t));
	if (!buf) { CloseHandle(h); return 0; }
	if (!ReadFile(h, buf, hdr.mmlBytes, &rd, NULL) || rd != hdr.mmlBytes) {
		free(buf); CloseHandle(h); return 0;
	}
	CloseHandle(h);
	buf[hdr.mmlBytes / sizeof(wchar_t) - 1] = 0;
	if (mmlOut && mmlCch > 0)
		wcsncpy_s(mmlOut, mmlCch, buf, _TRUNCATE);
	wchar_t err[128];
	int errLine = 0;
	int ok = ScCompileMidiMml(buf, d, &errLine, err, 128);
	free(buf);
	if (!ok) return 0;
	if (hdr.reserved[1] > 0) d->numer = (int)hdr.reserved[1];
	if (hdr.reserved[2] > 0) d->denom = (int)hdr.reserved[2];
	return 1;
}

void ScSessionClearLast(void)
{
	DatArc_Chdir();
	DatArc_Delete(kScSessionLeaf);
}
