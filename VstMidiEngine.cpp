// One engine source for both hosts. KpiHost64.exe compiles this same file
// through VstMidiEngine_k64.cpp; it used to keep a private copy, which quietly
// meant every VST2 hosting fix landed only in ogg.exe while the x64 plug-ins
// that actually run inside KpiHost64 kept the old code. stdafx.h routes itself
// to the MFC-free header when KPIHOST64_BUILD is set.
#include "stdafx.h"
#include "VstMidiEngine.h"
#include "Vst3Host.h"
#include "third_party/vst2/aeffect.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <stdarg.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <process.h>

#ifndef KPIHOST64_BUILD
#include "kpi_host_ipc.h"
#include "KpiHostClient.h"
#include "resource.h"
#else
#include "KpiHost64VstLive.h"
#endif

#pragma comment(lib, "winmm.lib")
static int ProbeLoadedEffectAudible(AEffect* effect);
static int ProbeLoadedVst3Audible(Vst3Inst* vst3, int drums);

#pragma comment(lib, "advapi32.lib")

static void MmCompactKey(const wchar_t* s, wchar_t* out, int outN)
{
	int j = 0;
	if (!out || outN <= 0) return;
	out[0] = 0;
	if (!s) return;
	for (; *s && j < outN - 1; ++s) {
		wchar_t c = *s;
		if (c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
		if (c == L'-' || c == L'_' || c == L' ' || c == L'\t' || c == L'.')
			continue;
		out[j++] = c;
	}
	out[j] = 0;
}

static int MmIsolated55(const wchar_t* s)
{
	if (!s || !s[0]) return 0;
	for (int i = 0; s[i]; ++i) {
		if (s[i] != L'5' || s[i + 1] != L'5') continue;
		const wchar_t prev = (i > 0) ? s[i - 1] : 0;
		const wchar_t next = s[i + 2];
		const int prevDig = (prev >= L'0' && prev <= L'9');
		const int nextDig = (next >= L'0' && next <= L'9');
		if (!prevDig && !nextDig) return 1;
		++i;
	}
	return 0;
}

static int MmIsAlnumW(wchar_t c)
{
	return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

static int MmHasToken(const wchar_t* s, const wchar_t* tok)
{
	if (!s || !tok || !tok[0]) return 0;
	const int tn = (int)wcslen(tok);
	for (int i = 0; s[i]; ++i) {
		int j = 0;
		for (; tok[j] && s[i + j]; ++j) {
			wchar_t a = s[i + j];
			wchar_t b = tok[j];
			if (a >= L'a' && a <= L'z') a = (wchar_t)(a - 32);
			if (b >= L'a' && b <= L'z') b = (wchar_t)(b - 32);
			if (a != b) break;
		}
		if (tok[j]) continue;
		const wchar_t prev = (i > 0) ? s[i - 1] : 0;
		const wchar_t next = s[i + tn];
		if (!MmIsAlnumW(prev) && !MmIsAlnumW(next)) return 1;
		i += tn - 1;
	}
	return 0;
}

static int MmKindFromCompact(const wchar_t* k)
{
	if (!k || !k[0]) return 0;
	if (wcsstr(k, L"8850") || wcsstr(k, L"SC8850") ||
		wcsstr(k, L"8820") || wcsstr(k, L"SC8820"))
		return 4;
	if (wcsstr(k, L"88PRO") || wcsstr(k, L"SC88PRO") || wcsstr(k, L"88PMAP"))
		return 3;
	if (wcsstr(k, L"88P"))
		return 3;
	if (wcsstr(k, L"88VL") || wcsstr(k, L"SC88VL") || wcsstr(k, L"88VALUE"))
		return 2;
	if (wcsstr(k, L"SC88") || wcsstr(k, L"88MAP"))
		return 2;
	if (wcsstr(k, L"LAMAP") || wcsstr(k, L"MT32") || wcsstr(k, L"CM32L") ||
		wcsstr(k, L"CM64") || wcsstr(k, L"LAPC") || wcsstr(k, L"LASYNTH") ||
		wcsstr(k, L"ROLANDLA") || wcsstr(k, L"CM32"))
		return 8;
	if (wcsstr(k, L"GM2") || wcsstr(k, L"GENERALMIDI2") || wcsstr(k, L"GMLEVEL2"))
		return 9;
	if (wcsstr(k, L"NS5R") || wcsstr(k, L"NX5R") || wcsstr(k, L"X5DR") ||
		wcsstr(k, L"X5D") || wcsstr(k, L"05RW") || wcsstr(k, L"AG10"))
		return 10;
	if (wcsstr(k, L"GMEGA") || wcsstr(k, L"GMEGALX") || wcsstr(k, L"KAWAIK11"))
		return 11;
	if (wcsstr(k, L"SG01") || wcsstr(k, L"SG01V") || wcsstr(k, L"SG01K"))
		return 12;
	if (wcsstr(k, L"KROSS"))
		return 13;
	if (wcsstr(k, L"KORGPA") || wcsstr(k, L"PA80") || wcsstr(k, L"PA50") ||
		wcsstr(k, L"PA60") || wcsstr(k, L"PA1X"))
		return 14;
	if (wcsstr(k, L"CS2X") || wcsstr(k, L"CASIOCS"))
		return 15;
	if (wcsstr(k, L"GEMGMX") || wcsstr(k, L"GENERALMUSIC"))
		return 16;
	if (wcsstr(k, L"CASIOLK") || wcsstr(k, L"LK93") || wcsstr(k, L"LK50"))
		return 17;
	if (wcsstr(k, L"PEAVEYDPM") || wcsstr(k, L"DPMV3"))
		return 18;
	if (wcsstr(k, L"SC55") || wcsstr(k, L"55MAP") || wcsstr(k, L"SC55MK") ||
		wcsstr(k, L"GS55"))
		return 1;
	if (wcsstr(k, L"SD90") || wcsstr(k, L"SD80") || wcsstr(k, L"SD20") ||
		wcsstr(k, L"SDMAP") || wcsstr(k, L"STUDIOCANVAS"))
		return 6;
	if (wcsstr(k, L"GENERALMIDI") || wcsstr(k, L"GMMAP") || wcsstr(k, L"GM1"))
		return 5;
	if (wcsstr(k, L"XGMAP") || wcsstr(k, L"SOFTXG") || wcsstr(k, L"SYXG") ||
		wcsstr(k, L"MU50") || wcsstr(k, L"MU10") || wcsstr(k, L"MU15") ||
		wcsstr(k, L"MU80") || wcsstr(k, L"MU90") || wcsstr(k, L"MU100") ||
		wcsstr(k, L"MU128") || wcsstr(k, L"MU500") || wcsstr(k, L"MU1000") ||
		wcsstr(k, L"MU2000") || wcsstr(k, L"YAMAHAXG") || wcsstr(k, L"XG50"))
		return 7;
	return 0;
}

static int MmKindFromText(const wchar_t* s)
{
	if (!s || !s[0]) return 0;
	wchar_t k[280];
	MmCompactKey(s, k, 280);
	int kind = MmKindFromCompact(k);
	if (!kind && MmHasToken(s, L"VL") && !wcsstr(k, L"VL1") && !wcsstr(k, L"VL70"))
		kind = 2;
	if (!kind && MmIsolated55(s)) kind = 1;
	if (!kind && MmHasToken(s, L"GM2")) kind = 9;
	if (!kind && MmHasToken(s, L"GM")) kind = 5;
	if (!kind && MmHasToken(s, L"XG")) kind = 7;
	if (!kind && (MmHasToken(s, L"LA") || MmHasToken(s, L"MT32") || MmHasToken(s, L"MT-32")))
		kind = 8;
	return kind;
}

extern "C" int VstMidiFoldGsMapHint(int cur, int kind)
{
	if (kind == 4) return 4;
	if (kind == 3 && cur != 4) return 3;
	if (kind == 2 && cur != 4 && cur != 3) return 2;
	if (kind == 8 && cur != 4 && cur != 3 && cur != 2) return 8;
	if (kind >= 9 && kind <= 18 && cur == 0) return kind;
	if (kind == 1 && cur == 0) return 1;
	if ((kind == 5 || kind == 6 || kind == 7) && cur == 0) return kind;
	return cur;
}

extern "C" int VstMidiGuessGsMapKind(const wchar_t* title, const wchar_t* path)
{
	int kind = MmKindFromText(title);
	const wchar_t* base = path;
	if (base && base[0]) {
		const wchar_t* sl = wcsrchr(base, L'\\');
		if (sl) base = sl + 1;
		const wchar_t* sl2 = wcsrchr(base, L'/');
		if (sl2) base = sl2 + 1;
	}
	if (!kind)
		kind = MmKindFromText(base);
	return kind;
}

extern "C" int VstMidiSysexIsGmOn(const unsigned char* d, int n)
{
	if (!d || n < 6 || d[0] != 0xf0 || d[1] != 0x7e || d[3] != 0x09) return 0;
	return (d[4] == 0x01 || d[4] == 0x03) ? 1 : 0;
}

extern "C" int VstMidiSysexIsGsReset(const unsigned char* d, int n)
{
	if (!d || n < 11 || d[0] != 0xf0 || d[1] != 0x41) return 0;
	if (d[3] != 0x42 || d[4] != 0x12) return 0;
	if (d[5] == 0x40 && d[6] == 0x00 && d[7] == 0x7f) return 1;
	if (d[5] == 0x00 && d[6] == 0x00 && d[7] == 0x7f) return 1;
	return 0;
}

extern "C" int VstMidiSysexIsXgOn(const unsigned char* d, int n)
{
	if (!d || n < 9 || d[0] != 0xf0 || d[1] != 0x43) return 0;
	return (d[3] == 0x4c && d[4] == 0x00 && d[5] == 0x00 && d[6] == 0x7e) ? 1 : 0;
}

extern "C" int VstMidiSysexMarksGs32(const unsigned char* d, int n)
{
	if (!d || n < 8 || d[0] != 0xf0) return 0;
	if (d[1] == 0x41 && n >= 11 && d[3] == 0x42 && d[4] == 0x12) {
		const BYTE a0 = d[5], a1 = d[6], a2 = d[7];
		// Port B / Port C part & system DT1 (40=A, 50=B, 60=C).
		if (a0 == 0x50 || a0 == 0x60) return 1;
		// Voice Reserve starts at 00 01 10. 16 bytes = 16 parts (very common
		// in ordinary GS). 32 bytes = parts 1–32. Treating any 00 01 10–1F
		// as 32ch used to mirror every 16ch song onto B01–B16.
		if (a0 == 0x00 && a1 == 0x01 && a2 == 0x10 && (n - 10) >= 32) return 1;
		return 0;
	}
	// XG Multi Part 17–32 (high address 09). 08 is parts 1–16.
	if (d[1] == 0x43 && n >= 8 && d[3] == 0x4c && d[4] == 0x09) return 1;
	return 0;
}

extern "C" int VstMidiBankMsbIsSdNative(int msb)
{
	return (msb == 80 || msb == 81 ||
		msb == 96 || msb == 97 || msb == 98 || msb == 99 ||
		msb == 104 || msb == 105 || msb == 106 || msb == 107) ? 1 : 0;
}

static BYTE g_gsBits[5][2048];
static int g_gsBitsReady;

static int GsBitsHas(int map, int bank, int pc)
{
	if (map < 1 || map > 4 || bank < 0 || bank > 127 || pc < 0 || pc > 127) return 0;
	const int bit = bank * 128 + pc;
	return (g_gsBits[map][bit >> 3] >> (bit & 7)) & 1;
}

static void GsBitsAddBuf(const BYTE* d, int n)
{
	if (!d || n < 20) return;
	const int rec = n / 20;
	for (int i = 0; i < rec; ++i) {
		const BYTE* r = d + i * 20;
		const int map = r[0], bank = r[1], pc = r[2];
		if (map < 1 || map > 4 || bank > 127 || pc > 127) continue;
		const int bit = bank * 128 + pc;
		g_gsBits[map][bit >> 3] = (BYTE)(g_gsBits[map][bit >> 3] | (BYTE)(1 << (bit & 7)));
	}
}

static int GsBitsLoadFile(const wchar_t* path)
{
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD sz = GetFileSize(f, NULL), got = 0;
	if (sz < 20 || sz > 8 * 1024 * 1024) { CloseHandle(f); return 0; }
	BYTE* buf = new BYTE[sz];
	if (!ReadFile(f, buf, sz, &got, NULL) || got != sz) {
		CloseHandle(f); delete[] buf; return 0;
	}
	CloseHandle(f);
	GsBitsAddBuf(buf, (int)sz);
	delete[] buf;
	return 1;
}

static int GsBitsEnsure()
{
	if (g_gsBitsReady) return g_gsBitsReady > 0;
	g_gsBitsReady = -1;
#ifndef KPIHOST64_BUILD
	{
		HINSTANCE hi = GetModuleHandleW(NULL);
		HRSRC hr = FindResourceW(hi, MAKEINTRESOURCEW(IDR_SASAMI_GS), RT_RCDATA);
		if (hr) {
			HGLOBAL hg = LoadResource(hi, hr);
			DWORD sz = SizeofResource(hi, hr);
			const BYTE* p = (const BYTE*)LockResource(hg);
			if (p && sz >= 20) {
				GsBitsAddBuf(p, (int)sz);
				g_gsBitsReady = 1;
				return 1;
			}
		}
	}
#endif
	if (GsBitsLoadFile(L"C:\\Windows\\SASAMI_GS.DAT")) {
		g_gsBitsReady = 1;
		return 1;
	}
	wchar_t exe[MAX_PATH];
	exe[0] = 0;
	GetModuleFileNameW(NULL, exe, MAX_PATH);
	for (int up = 0; up < 8; ++up) {
		wchar_t dir[MAX_PATH];
		wcsncpy_s(dir, exe, _TRUNCATE);
		wchar_t* sl = wcsrchr(dir, L'\\');
		if (sl) *sl = 0;
		else break;
		wchar_t cand[MAX_PATH];
		_snwprintf_s(cand, _TRUNCATE, L"%s\\SASAMI_GS.DAT", dir);
		if (GsBitsLoadFile(cand)) { g_gsBitsReady = 1; return 1; }
		_snwprintf_s(cand, _TRUNCATE, L"%s\\res\\SASAMI_GS.DAT", dir);
		if (GsBitsLoadFile(cand)) { g_gsBitsReady = 1; return 1; }
		wcsncpy_s(exe, dir, _TRUNCATE);
	}
	return 0;
}

extern "C" int VstMidiGsMapDropFromUsed(const unsigned short* pairs, int nPairs)
{
	if (!pairs || nPairs <= 0) return 0;
	if (!GsBitsEnsure()) return 0;
	int all[5] = { 1, 1, 1, 1, 1 };
	int any = 0;
	for (int i = 0; i < nPairs; ++i) {
		const int bank = (pairs[i] >> 8) & 0x7f;
		const int pc = pairs[i] & 0x7f;
		any = 1;
		for (int m = 1; m <= 4; ++m) {
			if (!GsBitsHas(m, bank, pc)) all[m] = 0;
		}
	}
	if (!any) return 0;
	int kind = 4;
	if (all[3]) kind = 3;
	if (all[2]) kind = 2;
	if (all[1]) kind = 1;
	return kind;
}

namespace {

static void GsFixChecksum(BYTE* d, int n)
{
	if (!d || n < 10 || d[0] != 0xf0 || d[n - 1] != 0xf7) return;
	unsigned sum = 0;
	for (int i = 5; i < n - 2; ++i)
		sum += d[i];
	d[n - 2] = (BYTE)((0x80 - (sum & 0x7f)) & 0x7f);
}

static void GsFillRhythmDt1(BYTE* d, BYTE aa)
{
	if (!d) return;
	d[0] = 0xf0; d[1] = 0x41; d[2] = 0x10; d[3] = 0x42; d[4] = 0x12;
	d[5] = aa; d[6] = 0x10; d[7] = 0x15; d[8] = 0x01; d[9] = 0; d[10] = 0xf7;
	GsFixChecksum(d, 11);
}

static int GsDt1IsPortReset(const BYTE* d, int n)
{
	if (!d || n < 11 || d[0] != 0xf0 || d[1] != 0x41) return 0;
	if (d[3] != 0x42 || d[4] != 0x12) return 0;
	if (d[6] != 0x00 || d[7] != 0x7f) return 0;
	if (d[5] == 0x50 || d[5] == 0x60) return 1;
	return 0;
}

enum { SAMPLE_RATE = 44100, BLOCK_FRAMES = 512, MAX_MIDI_EVENTS = 500000 };
// Loading is not proof of anything: an unauthorised or half-installed
// instrument opens cleanly, reports its name, accepts every note and returns
// digital silence. The only honest test is to play something and look at the
// output. Peak is in the same units as the render log, and 0.001 sits well
// above an idle noise floor (~0.00002 observed) and far below a real note
// (~0.06 observed).
enum { PROBE_AUDIBLE_MILLI = 1 }; // peak x1000
enum { CACHE_MAGIC = 0x43545356, CACHE_VERSION = 10 }; // first droppable same-name wins

struct MidiItem {
	unsigned __int64 tick;
	__int64 sample;
	DWORD msg; // 0xff tempo, 0xf0 sysex, else channel short
	DWORD aux; // tempo usec/qn, or sysex byte length
	int port; // SMF MIDI Port meta (FF 21): 0=ch1-16, 1=17-32, 2=33-48
	int sysexOff; // offset into EngineState::sysexData; -1 if not sysex
	int seq; // parse order; same-tick tie-break so qsort stays stable
};

struct Voice {
	double phase;
	double step;
	float env;
	float velocity;
	BYTE note;
	BYTE channel;
	BYTE stage; // 0 free, 1 attack, 2 sustain, 3 release
};

struct DrumV {
	int stage;
	int kind;
	int note;
	float env;
	float vel;
	double phase;
	double step;
	unsigned rng;
};

enum { LIVE_PEND_EVENTS = 512, LIVE_PEND_SYSEX_BYTES = 4096 };

struct LivePendEv {
	DWORD msg;
	int sysexOff; // <0 for a plain short message
	int sysexLen;
};

// VST2 wants effProcessEvents once per block, carrying every event for that
// block. Sending one event per call makes the plug-in keep only the last one,
// which silently eats notes and, when the lost one is a note-off, leaves the
// voice ringing forever. So live input is collected here and handed over in a
// single call right before the part is rendered.
struct LivePending {
	int count;
	int sysexUsed;
	int lostEvents;
	int lostSysex;
	LivePendEv ev[LIVE_PEND_EVENTS];
	BYTE sysex[LIVE_PEND_SYSEX_BYTES];
};

struct LivePart {
	HMODULE module;
	AEffect* effect;
	Vst3Inst* vst3;
	int isMulti;
	int remote; // 1=hosted by KpiHost64 (wrong-arch plug-in)
	int sendCh; // -1 = channel as received, 0..15 = forced
	int prog;   // program last picked from the slot menu
	HWND edWnd;
	LivePending pend;
};

enum { MIX_SLOTS = 16 };
enum {
	FAM_DRUM = 0, FAM_PIANO, FAM_CHROME, FAM_ORGAN, FAM_GUITAR, FAM_BASS,
	FAM_STRINGS, FAM_ENSEMBLE, FAM_BRASS, FAM_REED, FAM_PIPE, FAM_LEAD,
	FAM_PAD, FAM_FX, FAM_ETHNIC, FAM_PERC, FAM_SFX, FAM_GENERIC, FAM_COUNT
};

struct MixSlot {
	HMODULE module;
	AEffect* effect;
	Vst3Inst* vst3;
	int family;
	int vstProg;
	int keepMidiCh;
	wchar_t path[VST_PATH_CHARS];
};

struct EngineState {
	CRITICAL_SECTION cs;
	int csReady;
	BYTE* fileData;
	DWORD fileBytes;
	MidiItem* events;
	int eventCount;
	int eventPos;
	__int64 playSample;
	__int64 lengthSamples;
	HMODULE module;
	AEffect* effect;
	Vst3Inst* vst3;
	// Extra multi-timbral instances for SMF ports (SC-88 style 32/48ch).
	// Port0 = effect/vst3, port1 = effectB (VST2), port2+ = vst3C or effectC.
	HMODULE moduleB;
	AEffect* effectB;
	HMODULE moduleC;
	AEffect* effectC;
	Vst3Inst* vst3C;
	BYTE* sysexData;
	int sysexBytes;
	int maxMidiPort; // highest FF 21 port seen (0=16ch only)
	int mirrorToB;   // GS/XG 32-part with no FF 21: copy ch MIDI to unit B
	int usingBuiltin;
	int useEnsemble;
	int useDrums;
	int useMapper;
	HMIDIOUT midiOut;
	MixSlot mix[MIX_SLOTS];
	int mixCount;
	int chSlot[16]; // -1=silent, else mix index
	Voice voices[32];
	DrumV drums[16];
	BYTE noteState[16][128];
	short ring[16384];
	int ringRead;
	int ringCount;
	__declspec(align(32)) float outL[BLOCK_FRAMES];
	__declspec(align(32)) float outR[BLOCK_FRAMES];
	__declspec(align(32)) float zero[BLOCK_FRAMES];
	__declspec(align(32)) float mixL[BLOCK_FRAMES];
	__declspec(align(32)) float mixR[BLOCK_FRAMES];
	LivePart live[32];
	int gmResetMode; // 0=GM+GS, 1=GS, 2=XG（シーク巻き戻し時に再送）
	int gsMapLsb;    // 0=なし 1=SC-55 2=SC-88 3=88Pro 4=8820/50（CC#32）
	int songGm;      // 1=GM  2=GM2（GM On のみ。GM2 は ch10 に MSB120）

	EngineState() : csReady(0), fileData(NULL), fileBytes(0), events(NULL),
		eventCount(0), eventPos(0), playSample(0), lengthSamples(0),
		module(NULL), effect(NULL), vst3(NULL),
		moduleB(NULL), effectB(NULL), moduleC(NULL), effectC(NULL), vst3C(NULL),
		sysexData(NULL), sysexBytes(0), maxMidiPort(0), mirrorToB(0), usingBuiltin(1),
		useEnsemble(0), useDrums(0), useMapper(0), midiOut(NULL), mixCount(0),
		ringRead(0), ringCount(0), gmResetMode(0), gsMapLsb(0), songGm(0)
	{
		InitializeCriticalSection(&cs);
		csReady = 1;
		ZeroMemory(voices, sizeof(voices));
		ZeroMemory(drums, sizeof(drums));
		ZeroMemory(noteState, sizeof(noteState));
		ZeroMemory(live, sizeof(live));
		ZeroMemory(zero, sizeof(zero));
		ZeroMemory(mix, sizeof(mix));
		for (int i = 0; i < 16; ++i) chSlot[i] = -1;
	}
	~EngineState() {
		if (csReady) DeleteCriticalSection(&cs);
	}
};

static EngineState g_engs[2];
static __declspec(thread) int t_vstIoSlot = 0;

static int VstIoSlot()
{
	int s = t_vstIoSlot;
	if (s < 0 || s >= 2) s = 0;
	return s;
}

extern "C" void VstMidiSetIoSlot(int slot)
{
	if (slot < 0 || slot >= 2) slot = 0;
	t_vstIoSlot = slot;
}

extern "C" int VstMidiGetIoSlot(void)
{
	return VstIoSlot();
}

#define g_eng (g_engs[VstIoSlot()])

static VstPluginInfo g_plugins[VST_MAX_PLUGINS];
static int g_pluginCount = 0;
static int g_scanReady = 0;
static int g_scanInvalid = 0;
static int g_ensLogBlocks = 0;

static void EnsLog(const wchar_t* fmt, ...)
{
	(void)fmt;
}


static void SafeCopy(wchar_t* dst, int chars, const wchar_t* src)
{
	if (!dst || chars <= 0) return;
	if (!src) src = L"";
	wcsncpy_s(dst, chars, src, _TRUNCATE);
}

static const wchar_t* ExtOf(const wchar_t* path)
{
	const wchar_t* dot = path ? wcsrchr(path, L'.') : NULL;
	const wchar_t* slash = path ? wcsrchr(path, L'\\') : NULL;
	return (dot && (!slash || dot > slash)) ? dot : L"";
}

static int EqExt(const wchar_t* path, const wchar_t* ext)
{
	return _wcsicmp(ExtOf(path), ext) == 0;
}

static void ExeDir(wchar_t out[VST_PATH_CHARS])
{
	GetModuleFileNameW(NULL, out, VST_PATH_CHARS);
	wchar_t* p = wcsrchr(out, L'\\');
	if (p) *p = 0;
}

static void JoinPath(wchar_t out[VST_PATH_CHARS], const wchar_t* a, const wchar_t* b)
{
	SafeCopy(out, VST_PATH_CHARS, a);
	size_t n = wcslen(out);
	if (n && out[n - 1] != L'\\') wcscat_s(out, VST_PATH_CHARS, L"\\");
	wcscat_s(out, VST_PATH_CHARS, b);
}

static void BaseNameNoExt(const wchar_t* path, wchar_t out[VST_NAME_CHARS])
{
	const wchar_t* p = wcsrchr(path, L'\\');
	p = p ? p + 1 : path;
	SafeCopy(out, VST_NAME_CHARS, p);
	wchar_t* dot = wcsrchr(out, L'.');
	if (dot) *dot = 0;
}

static HWND g_waitWnd = NULL;
static HFONT g_waitFont = NULL;
static int g_scanIndex = 0;
static int g_scanTotal = 0;

static UINT WaitDpi(HWND h)
{
	typedef UINT (WINAPI* PFN)(HWND);
	static PFN fn = NULL;
	static int got = 0;
	if (!got) {
		HMODULE u = GetModuleHandleW(L"user32.dll");
		if (u) fn = (PFN)GetProcAddress(u, "GetDpiForWindow");
		got = 1;
	}
	if (h && fn) {
		const UINT d = fn(h);
		if (d) return d;
	}
	HDC dc = GetDC(h);
	UINT d = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
	if (dc) ReleaseDC(h, dc);
	return d ? d : 96;
}

static void DestroyWait(HWND wnd)
{
	if (wnd) DestroyWindow(wnd);
	if (g_waitWnd == wnd) g_waitWnd = NULL;
	if (g_waitFont) {
		DeleteObject(g_waitFont);
		g_waitFont = NULL;
	}
}

static void SetWaitStatus(HWND wnd, const wchar_t* msg)
{
	if (!wnd || !msg) return;
	SetWindowTextW(wnd, msg);
	MSG m;
	while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&m);
		DispatchMessage(&m);
	}
	UpdateWindow(wnd);
}

static void SetScanWait(HWND wnd, int cur, int total, int found, const wchar_t* name)
{
	wchar_t msg[384];
	_snwprintf_s(msg, _TRUNCATE, LL14(
		L"VSTスキャン %d / %d（発見 %d）\n%s",
		L"Scanning VST %d / %d (%d found)\n%s",
		L"Analyse VST %d / %d (%d trouvés)\n%s",
		L"Scansione VST %d / %d (%d trovati)\n%s",
		L"Explorando VST %d / %d (%d hallados)\n%s",
		L"VST 검색 %d / %d (발견 %d)\n%s",
		L"正在扫描 VST %d / %d（发现 %d）\n%s",
		L"مسح VST %d / %d (%d عُثر عليها)\n%s",
		L"Сканирование VST %d / %d (найдено %d)\n%s",
		L"VST-Scan %d / %d (%d gefunden)\n%s",
		L"A varrer VST %d / %d (%d achados)\n%s",
		L"VST scannen %d / %d (%d gevonden)\n%s",
		L"Skan VST %d / %d (znaleziono %d)\n%s",
		L"VST tarama %d / %d (%d bulundu)\n%s"),
		cur, total, found, name ? name : L"");
	SetWaitStatus(wnd, msg);
}

static void SetVerifyWait(HWND wnd, int done, int todo, const wchar_t* name, const wchar_t* step)
{
	wchar_t msg[512];
	_snwprintf_s(msg, _TRUNCATE, LL14(
		L"D&D・発音確認 %d / %d\n%s\n%s",
		L"Drop & sound check %d / %d\n%s\n%s",
		L"Glisser-déposer / son %d / %d\n%s\n%s",
		L"Trascina e suono %d / %d\n%s\n%s",
		L"Arrastre y sonido %d / %d\n%s\n%s",
		L"D&D·발음 확인 %d / %d\n%s\n%s",
		L"拖放·发音确认 %d / %d\n%s\n%s",
		L"التحقق من الإفلات والصوت %d / %d\n%s\n%s",
		L"Проверка D&D / звука %d / %d\n%s\n%s",
		L"D&D- und Klangprüfung %d / %d\n%s\n%s",
		L"D&D e som %d / %d\n%s\n%s",
		L"D&D- en geluidscheck %d / %d\n%s\n%s",
		L"D&D i dźwięk %d / %d\n%s\n%s",
		L"D&D / ses kontrolü %d / %d\n%s\n%s"),
		done, todo, name ? name : L"", step ? step : L"");
	SetWaitStatus(wnd, msg);
}

static HWND MakeWait(HWND owner)
{
	if (owner && !IsWindow(owner)) owner = NULL;
	RECT r = {};
	if (owner) GetWindowRect(owner, &r);
	else {
		r.left = 0; r.top = 0;
		r.right = GetSystemMetrics(SM_CXSCREEN);
		r.bottom = GetSystemMetrics(SM_CYSCREEN);
	}
	const UINT dpi = WaitDpi(owner ? owner : NULL);
	LOGFONTW lf = {};
	HFONT stock = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
	if (stock) GetObjectW(stock, sizeof(lf), &lf);
	lf.lfHeight = -MulDiv(9, (int)dpi, 96);
	if (lf.lfHeight == 0) lf.lfHeight = -12;
	if (g_waitFont) { DeleteObject(g_waitFont); g_waitFont = NULL; }
	g_waitFont = CreateFontIndirectW(&lf);
	int lineH = MulDiv(16, (int)dpi, 96);
	int textW = MulDiv(360, (int)dpi, 96);
	HDC dc = GetDC(owner);
	if (dc) {
		HGDIOBJ old = SelectObject(dc, g_waitFont ? g_waitFont : stock);
		TEXTMETRICW tm = {};
		if (GetTextMetricsW(dc, &tm) && tm.tmHeight > 0)
			lineH = tm.tmHeight + tm.tmExternalLeading;
		SelectObject(dc, old);
		ReleaseDC(owner, dc);
	}
	const int padX = MulDiv(12, (int)dpi, 96);
	const int padY = MulDiv(8, (int)dpi, 96);
	const int w = textW + padX * 2;
	const int h = padY * 2 + lineH * 4;
	int x = r.left + ((r.right - r.left) - w) / 2;
	int y = r.top + ((r.bottom - r.top) - h) / 2;
	if (owner) {
		// Palette is the left ~38% of the host; keep it readable while
		// drop / sound check fills the list one plug-in at a time.
		const int split = ((r.right - r.left) * 38) / 100;
		x = r.left + split + MulDiv(8, (int)dpi, 96);
		if (x + w > r.right - 4) x = r.right - w - 4;
		if (x < r.left) x = r.left;
		y = r.top + MulDiv(48, (int)dpi, 96);
		if (y + h > r.bottom) y = r.bottom - h;
		if (y < r.top) y = r.top;
	}
	HWND wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"STATIC",
		LL14(L"VSTプラグインを検索しています…", L"Searching VST plug-ins…",
			L"Recherche des plug-ins VST…", L"Ricerca dei plug-in VST…",
			L"Buscando plug-ins VST…", L"VST 플러그인을 찾는 중…", L"正在搜索 VST 插件…",
			L"جارٍ البحث عن إضافات VST…", L"Поиск VST-плагинов…", L"VST-Plug-ins werden gesucht…",
			L"A procurar plug-ins VST…", L"VST-plug-ins zoeken…", L"Szukanie wtyczek VST…",
			L"VST eklentileri aranıyor…"),
		WS_POPUP | WS_BORDER | SS_CENTER | SS_NOPREFIX,
		x, y, w, h,
		owner, NULL, GetModuleHandleW(NULL), NULL);
	if (wnd) {
		SendMessage(wnd, WM_SETFONT, (WPARAM)(g_waitFont ? g_waitFont : stock), TRUE);
		ShowWindow(wnd, SW_SHOWNOACTIVATE);
		UpdateWindow(wnd);
	}
	g_waitWnd = wnd;
	return wnd;
}

static void PumpWait(HWND wnd)
{
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	if (wnd) UpdateWindow(wnd);
}

static int PeArch(const wchar_t* path)
{
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	IMAGE_DOS_HEADER dos = {};
	DWORD got = 0;
	if (!ReadFile(f, &dos, sizeof(dos), &got, NULL) || got != sizeof(dos) ||
		dos.e_magic != IMAGE_DOS_SIGNATURE) {
		CloseHandle(f); return 0;
	}
	SetFilePointer(f, dos.e_lfanew, NULL, FILE_BEGIN);
	DWORD sig = 0;
	IMAGE_FILE_HEADER fh = {};
	if (!ReadFile(f, &sig, sizeof(sig), &got, NULL) || sig != IMAGE_NT_SIGNATURE ||
		!ReadFile(f, &fh, sizeof(fh), &got, NULL)) {
		CloseHandle(f); return 0;
	}
	CloseHandle(f);
	if (fh.Machine == IMAGE_FILE_MACHINE_I386) return 32;
	if (fh.Machine == IMAGE_FILE_MACHINE_AMD64) return 64;
	return 0;
}

static int HostArch()
{
#ifdef _WIN64
	return 64;
#else
	return 32;
#endif
}

static int PathLooksLikeScVa(const wchar_t* path);

static VstTimeInfo g_vstTime = {};
static char g_vstHostDirA[VST_PATH_CHARS] = {};

static void InitHostDir() {
	if (!g_vstHostDirA[0]) {
		wchar_t path[MAX_PATH];
		GetModuleFileNameW(NULL, path, MAX_PATH);
		wchar_t* slash = wcsrchr(path, L'\\');
		if (slash) *slash = 0;
		WideCharToMultiByte(CP_ACP, 0, path, -1, g_vstHostDirA, VST_PATH_CHARS, NULL, NULL);
	}
}

// audioMasterGetDirectory has to hand back the *plug-in's* folder, not ours:
// that reply is how a VST2 instrument locates the tone data sitting next to its
// own DLL. Answering with the host's folder makes such a plug-in load cleanly,
// accept MIDI and then render pure silence, and makes the whole thing depend on
// where the host executable happens to be installed.
enum { VST_PLUG_DIR_SLOTS = 64 };

struct PlugDirEntry {
	AEffect* effect;
	char dir[VST_PATH_CHARS];
};

static PlugDirEntry g_plugDirs[VST_PLUG_DIR_SLOTS];
// Plug-ins ask during VSTPluginMain, before we hold an AEffect to key on.
static char g_plugDirLoading[VST_PATH_CHARS] = {};

static void VstPlugDirSet(const wchar_t* pluginPath)
{
	g_plugDirLoading[0] = 0;
	if (!pluginPath) return;
	wchar_t dir[VST_PATH_CHARS];
	SafeCopy(dir, VST_PATH_CHARS, pluginPath);
	wchar_t* slash = wcsrchr(dir, L'\\');
	if (!slash) return;
	*slash = 0;
	WideCharToMultiByte(CP_ACP, 0, dir, -1, g_plugDirLoading,
		VST_PATH_CHARS, NULL, NULL);
}

static void VstPlugDirBind(AEffect* e)
{
	if (!e || !g_plugDirLoading[0]) return;
	for (int i = 0; i < VST_PLUG_DIR_SLOTS; ++i) {
		if (g_plugDirs[i].effect && g_plugDirs[i].effect != e) continue;
		strcpy_s(g_plugDirs[i].dir, VST_PATH_CHARS, g_plugDirLoading);
		// Publish the key only once the string is complete: the audio thread
		// may read this table while a later part is still loading.
		InterlockedExchangePointer((void* volatile*)&g_plugDirs[i].effect, e);
		return;
	}
}

static void VstPlugDirUnbind(AEffect* e)
{
	if (!e) return;
	for (int i = 0; i < VST_PLUG_DIR_SLOTS; ++i) {
		if (g_plugDirs[i].effect != e) continue;
		InterlockedExchangePointer((void* volatile*)&g_plugDirs[i].effect, NULL);
		return;
	}
}

static const char* VstPlugDirFor(AEffect* e)
{
	if (e) {
		for (int i = 0; i < VST_PLUG_DIR_SLOTS; ++i)
			if (g_plugDirs[i].effect == e) return g_plugDirs[i].dir;
	}
	return g_plugDirLoading[0] ? g_plugDirLoading : NULL;
}

static VstIntPtr VSTCALLBACK HostCallback(AEffect* effect, VstInt32 opcode,
	VstInt32, VstIntPtr value, void* ptr, float)
{
	switch (opcode) {
	case audioMasterVersion: return 2400;
	case audioMasterWantMidi: return 1; // SC-VA / older GS plugs still query this
	case audioMasterGetSampleRate: return SAMPLE_RATE;
	case audioMasterGetBlockSize: return BLOCK_FRAMES;
	case audioMasterGetCurrentProcessLevel: return 2; // realtime
	case audioMasterGetVendorVersion: return 1;
	case audioMasterGetLanguage: return 1; // English
	case audioMasterGetVendorString:
		if (ptr) strcpy_s((char*)ptr, 64, "ogg");
		return 1;
	case audioMasterGetProductString:
		if (ptr) strcpy_s((char*)ptr, 64, "ogg VST MIDI Host");
		return 1;
	case audioMasterGetTime: {
		const double tempo = 120.0;
		g_vstTime.samplePos = (double)g_eng.playSample;
		g_vstTime.sampleRate = (double)SAMPLE_RATE;
		g_vstTime.nanoSeconds = g_vstTime.samplePos * (1.0e9 / (double)SAMPLE_RATE);
		g_vstTime.tempo = tempo;
		g_vstTime.ppqPos = (g_vstTime.samplePos / (double)SAMPLE_RATE) * (tempo / 60.0);
		g_vstTime.barStartPos = 0;
		g_vstTime.cycleStartPos = 0;
		g_vstTime.cycleEndPos = 0;
		g_vstTime.timeSigNumerator = 4;
		g_vstTime.timeSigDenominator = 4;
		g_vstTime.smpteOffset = 0;
		g_vstTime.smpteFrameRate = 1;
		g_vstTime.samplesToNextClock = 0;
		g_vstTime.flags = kVstTransportPlaying | kVstNanosValid | kVstPpqPosValid |
			kVstTempoValid | kVstTimeSigValid | kVstClockValid;
		(void)value; // filter mask from plug; we always fill common fields
		return (VstIntPtr)&g_vstTime;
	}
	case audioMasterGetDirectory: {
		const char* dir = VstPlugDirFor(effect);
		if (dir && dir[0]) return (VstIntPtr)dir;
		InitHostDir();
		if (g_vstHostDirA[0]) return (VstIntPtr)g_vstHostDirA;
		return 0;
	}
	case audioMasterNeedIdle:
	case audioMasterUpdateDisplay:
	case audioMasterIdle:
		return 1;
	case audioMasterCanDo:
		if (ptr && (
			!strcmp((char*)ptr, "sendVstMidiEvent") ||
			!strcmp((char*)ptr, "sendVstEvents") ||
			!strcmp((char*)ptr, "receiveVstMidiEvent") ||
			!strcmp((char*)ptr, "midiProgramNames") ||
			!strcmp((char*)ptr, "supplyIdle") ||
			!strcmp((char*)ptr, "timeInfo") ||
			!strcmp((char*)ptr, "tempo")))
			return 1;
		return 0;
	default: return 0;
	}
}

static int HasPluginPath(const wchar_t* path)
{
	for (int i = 0; i < g_pluginCount; ++i)
		if (_wcsicmp(g_plugins[i].path, path) == 0) return 1;
	return 0;
}

static void AddPlugin(const wchar_t* path, const wchar_t* name,
	int arch, int vst3, int instrument)
{
	if (g_pluginCount >= VST_MAX_PLUGINS || HasPluginPath(path)) return;
	VstPluginInfo& p = g_plugins[g_pluginCount++];
	ZeroMemory(&p, sizeof(p));
	SafeCopy(p.path, VST_PATH_CHARS, path);
	SafeCopy(p.name, VST_NAME_CHARS, name);
	p.arch = arch;
	p.isVst3 = vst3;
	p.isInstrument = instrument;
	p.isMultiTimbral = 0; // filled after ContainsI exists via RescoreMultiFlags
	p.isLiveOk = 0;
	p.isAudible = 0;
	p.probePeakMilli = 0;
}

static int SkipCompanionDll(const wchar_t* path)
{
	if (!path || !*path) return 1;
	const wchar_t* n = wcsrchr(path, L'\\');
	n = n ? n + 1 : path;
	return !_wcsicmp(n, L"SCCore.dll") || !_wcsicmp(n, L"Wrapper.dll");
}

static void ProbeVst2(const wchar_t* path)
{
	if (SkipCompanionDll(path)) return;
	int arch = PeArch(path);
	wchar_t base[VST_NAME_CHARS];
	BaseNameNoExt(path, base);
	if (PathLooksLikeScVa(path) || PathLooksLikeScVa(base))
		wcsncpy_s(base, VST_NAME_CHARS, L"SOUND Canvas VA", _TRUNCATE);
	if (!arch) return;
	if (arch != HostArch()) {
		AddPlugin(path, base, arch, 0, 0);
		return; // Never LoadLibrary a wrong-machine image.
	}
	HMODULE mod = NULL;
	{
		wchar_t plugDir[VST_PATH_CHARS];
		SafeCopy(plugDir, VST_PATH_CHARS, path);
		wchar_t* slash = wcsrchr(plugDir, L'\\');
		if (slash) *slash = 0; else plugDir[0] = 0;
		wchar_t savedDllDir[MAX_PATH] = {};
		GetDllDirectoryW(MAX_PATH, savedDllDir);
		if (plugDir[0]) SetDllDirectoryW(plugDir);
		mod = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		SetDllDirectoryW(savedDllDir[0] ? savedDllDir : NULL);
	}
	if (!mod) return;
	VSTPluginMainProc mainProc = (VSTPluginMainProc)GetProcAddress(mod, "VSTPluginMain");
	if (!mainProc) mainProc = (VSTPluginMainProc)GetProcAddress(mod, "main");
	if (!mainProc) { FreeLibrary(mod); return; }
	AEffect* e = NULL;
	VstPlugDirSet(path);
	__try { e = mainProc(HostCallback); }
	__except (EXCEPTION_EXECUTE_HANDLER) { e = NULL; }
	if (!e || e->magic != kEffectMagic || !e->dispatcher) {
		FreeLibrary(mod); return;
	}
	VstPlugDirBind(e);
	char nm[128] = {};
	int instrument = 0;
	__try {
		e->dispatcher(e, effOpen, 0, 0, NULL, 0);
		e->dispatcher(e, effGetEffectName, 0, 0, nm, 0);
		const VstIntPtr cat = e->dispatcher(e, effGetPlugCategory, 0, 0, NULL, 0);
		const VstIntPtr can = e->dispatcher(e, effCanDo, 0, 0,
			(void*)"receiveVstMidiEvent", 0);
		instrument = (cat == kPlugCategSynth || can > 0 ||
			(e->flags & effFlagsIsSynth)) ? 1 : 0;
		e->dispatcher(e, effClose, 0, 0, NULL, 0);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { instrument = 0; }
	VstPlugDirUnbind(e);
	wchar_t wide[VST_NAME_CHARS] = {};
	if (nm[0]) MultiByteToWideChar(CP_ACP, 0, nm, -1, wide, VST_NAME_CHARS);
	if (PathLooksLikeScVa(path))
		wcsncpy_s(wide, VST_NAME_CHARS, L"SOUND Canvas VA", _TRUNCATE);
	AddPlugin(path, wide[0] ? wide : base, arch, 0, instrument);
	FreeLibrary(mod);
}

static int DirExists(const wchar_t* path)
{
	DWORD a = GetFileAttributesW(path);
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static void ProbeVst3Bundle(const wchar_t* bundle)
{
	wchar_t base[VST_NAME_CHARS];
	BaseNameNoExt(bundle, base);
	if (PathLooksLikeScVa(bundle) || PathLooksLikeScVa(base))
		wcsncpy_s(base, VST_NAME_CHARS, L"SOUND Canvas VA", _TRUNCATE);
	wchar_t p32[VST_PATH_CHARS], p64[VST_PATH_CHARS];
	JoinPath(p32, bundle, L"Contents\\x86-win");
	JoinPath(p64, bundle, L"Contents\\x86_64-win");
	int arch = DirExists(p64) ? 64 : (DirExists(p32) ? 32 : 0);
	if (!arch) arch = HostArch();
	// Registration is deliberately factory-free: some VST3 bundles perform
	// expensive initialization in GetPluginFactory. Playback falls through.
	AddPlugin(bundle, base, arch, 1, 1);
}

static int CountDir(const wchar_t* dir, int depth)
{
	if (!dir || !*dir || depth > 4 || !DirExists(dir)) return 0;
	wchar_t pat[VST_PATH_CHARS];
	JoinPath(pat, dir, L"*");
	WIN32_FIND_DATAW fd = {};
	HANDLE h = FindFirstFileW(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	int n = 0;
	do {
		if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
		wchar_t full[VST_PATH_CHARS];
		JoinPath(full, dir, fd.cFileName);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (EqExt(fd.cFileName, L".vst3")) ++n;
			else n += CountDir(full, depth + 1);
		} else if (EqExt(fd.cFileName, L".dll")) {
			++n;
		}
	} while (FindNextFileW(h, &fd) && n < VST_MAX_PLUGINS);
	FindClose(h);
	return n;
}

static void ScanDir(const wchar_t* dir, int depth, HWND wait)
{
	if (!dir || !*dir || depth > 4 || !DirExists(dir)) return;
	wchar_t pat[VST_PATH_CHARS];
	JoinPath(pat, dir, L"*");
	WIN32_FIND_DATAW fd = {};
	HANDLE h = FindFirstFileW(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
		wchar_t full[VST_PATH_CHARS];
		JoinPath(full, dir, fd.cFileName);
		int probed = 0;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (EqExt(fd.cFileName, L".vst3")) {
				ProbeVst3Bundle(full);
				probed = 1;
			} else ScanDir(full, depth + 1, wait);
		} else if (EqExt(fd.cFileName, L".dll")) {
			ProbeVst2(full);
			probed = 1;
		}
		if (probed) {
			++g_scanIndex;
			SetScanWait(wait, g_scanIndex, g_scanTotal, g_pluginCount, fd.cFileName);
		}
	} while (FindNextFileW(h, &fd) && g_pluginCount < VST_MAX_PLUGINS);
	FindClose(h);
}

static void ProbeUserSpecified(const wchar_t* src, HWND wait)
{
	if (!src || !src[0]) return;
	DWORD a = GetFileAttributesW(src);
	if (a == INVALID_FILE_ATTRIBUTES) {
		wchar_t dir[VST_PATH_CHARS];
		SafeCopy(dir, VST_PATH_CHARS, src);
		wchar_t* slash = wcsrchr(dir, L'\\');
		if (slash) { *slash = 0; if (DirExists(dir)) ScanDir(dir, 0, wait); }
		return;
	}
	if (a & FILE_ATTRIBUTE_DIRECTORY) {
		if (EqExt(src, L".vst3")) ProbeVst3Bundle(src);
		else ScanDir(src, 0, wait);
		return;
	}
	if (EqExt(src, L".vst3")) ProbeVst3Bundle(src);
	else ProbeVst2(src);
}

static void AddEnvRoot(const wchar_t* env, const wchar_t* suffix,
	wchar_t roots[][VST_PATH_CHARS], int& count, int max)
{
	if (count >= max) return;
	wchar_t val[VST_PATH_CHARS] = {};
	DWORD n = GetEnvironmentVariableW(env, val, VST_PATH_CHARS);
	if (!n || n >= VST_PATH_CHARS) return;
	if (suffix) JoinPath(roots[count], val, suffix);
	else SafeCopy(roots[count], VST_PATH_CHARS, val);
	++count;
}

static void AddRegRoot(HKEY hive, wchar_t roots[][VST_PATH_CHARS],
	int& count, int max)
{
	if (count >= max) return;
	HKEY key = NULL;
	if (RegOpenKeyExW(hive, L"SOFTWARE\\VST", 0,
		KEY_QUERY_VALUE | KEY_WOW64_32KEY, &key) != ERROR_SUCCESS) return;
	DWORD type = 0, bytes = VST_PATH_CHARS * sizeof(wchar_t);
	if (RegQueryValueExW(key, L"VSTPluginsPath", NULL, &type,
		(BYTE*)roots[count], &bytes) == ERROR_SUCCESS &&
		(type == REG_SZ || type == REG_EXPAND_SZ) && roots[count][0]) ++count;
	RegCloseKey(key);
}

static void CachePath(wchar_t path[VST_PATH_CHARS])
{
	wchar_t dir[VST_PATH_CHARS];
	ExeDir(dir);
	JoinPath(path, dir, L"vstscan.cache");
}

static int ReadCacheFile()
{
	wchar_t path[VST_PATH_CHARS];
	CachePath(path);
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD hdr[3] = {}, got = 0;
	int ok = ReadFile(f, hdr, sizeof(hdr), &got, NULL) &&
		got == sizeof(hdr) && hdr[0] == CACHE_MAGIC &&
		hdr[1] == CACHE_VERSION && hdr[2] <= VST_MAX_PLUGINS;
	if (ok && hdr[2]) {
		DWORD bytes = hdr[2] * sizeof(VstPluginInfo);
		ok = ReadFile(f, g_plugins, bytes, &got, NULL) && got == bytes;
	}
	CloseHandle(f);
	if (!ok) return 0;
	g_pluginCount = (int)hdr[2];
	return 1;
}

static int LoadCache()
{
	wchar_t path[VST_PATH_CHARS];
	CachePath(path);
	WIN32_FILE_ATTRIBUTE_DATA ad = {};
	if (!GetFileAttributesExW(path, GetFileExInfoStandard, &ad)) return 0;
	FILETIME now;
	GetSystemTimeAsFileTime(&now);
	ULARGE_INTEGER a, b;
	a.LowPart = ad.ftLastWriteTime.dwLowDateTime;
	a.HighPart = ad.ftLastWriteTime.dwHighDateTime;
	b.LowPart = now.dwLowDateTime; b.HighPart = now.dwHighDateTime;
	if (b.QuadPart < a.QuadPart ||
		b.QuadPart - a.QuadPart > 24ULL * 60 * 60 * 10000000ULL) return 0;
	return ReadCacheFile();
}

static void SaveCache()
{
	wchar_t path[VST_PATH_CHARS];
	CachePath(path);
	HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_HIDDEN, NULL);
	if (f == INVALID_HANDLE_VALUE) return;
	DWORD hdr[3] = { CACHE_MAGIC, CACHE_VERSION, (DWORD)g_pluginCount }, put = 0;
	WriteFile(f, hdr, sizeof(hdr), &put, NULL);
	if (g_pluginCount)
		WriteFile(f, g_plugins, g_pluginCount * sizeof(VstPluginInfo), &put, NULL);
	CloseHandle(f);
}

static unsigned ReadBE(const BYTE* p, int n)
{
	unsigned v = 0;
	while (n--) v = (v << 8) | *p++;
	return v;
}

static int ReadVar(const BYTE*& p, const BYTE* end, unsigned& v)
{
	v = 0;
	for (int i = 0; i < 4 && p < end; ++i) {
		BYTE c = *p++;
		v = (v << 7) | (c & 0x7f);
		if (!(c & 0x80)) return 1;
	}
	return 0;
}

static int PathLooksLikePlugin(const wchar_t* p)
{
	if (!p || !p[0]) return 0;
	DWORD a = GetFileAttributesW(p);
	if (a == INVALID_FILE_ATTRIBUTES) return 0;
	if (a & FILE_ATTRIBUTE_DIRECTORY)
		return EqExt(p, L".vst3") ? 1 : 0;
	return 1;
}

static int SysexIsXgReset(const BYTE* d, int n)
{
	if (!d || n < 7 || d[0] != 0xf0) return 0;
	if (d[1] != 0x43) return 0;
	if ((d[2] & 0xf0) != 0x10) return 0;
	if (d[3] != 0x4c || d[4] != 0x00 || d[5] != 0x00 || d[6] != 0x7e)
		return 0;
	return 1;
}

static int SmfBytesHasXgReset(const BYTE* data, DWORD size)
{
	if (!data || size < 14) return 0;
	const BYTE* smf = data;
	DWORD smfSize = size;
	if (size >= 20 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "RMID", 4) == 0) {
		DWORD off = 12;
		while (off + 8 <= size) {
			const DWORD cksz = (DWORD)data[off + 4] | ((DWORD)data[off + 5] << 8)
				| ((DWORD)data[off + 6] << 16) | ((DWORD)data[off + 7] << 24);
			if (memcmp(data + off, "data", 4) == 0) {
				if (off + 8 > size) break;
				smf = data + off + 8;
				smfSize = cksz;
				if (smf + smfSize > data + size)
					smfSize = (DWORD)(data + size - smf);
				break;
			}
			const DWORD step = 8 + ((cksz + 1) & ~1u);
			if (step < 8 || off + step < off) break;
			off += step;
		}
	}
	if (smfSize < 14 || memcmp(smf, "MThd", 4) || ReadBE(smf + 4, 4) < 6)
		return 0;
	const int tracks = (int)ReadBE(smf + 10, 2);
	const BYTE* p = smf + 8 + ReadBE(smf + 4, 4);
	const BYTE* fileEnd = smf + smfSize;
	for (int tr = 0; tr < tracks && p + 8 <= fileEnd; ++tr) {
		if (memcmp(p, "MTrk", 4)) break;
		DWORD len = ReadBE(p + 4, 4);
		const BYTE* q = p + 8;
		const BYTE* end = (q + len <= fileEnd) ? q + len : fileEnd;
		BYTE running = 0;
		while (q < end) {
			unsigned delta = 0;
			if (!ReadVar(q, end, delta)) break;
			if (q >= end) break;
			BYTE st = *q;
			if (st & 0x80) { ++q; if (st < 0xf0) running = st; }
			else if (running) st = running;
			else break;
			if (st == 0xff) {
				if (q >= end) break;
				++q;
				unsigned ml = 0;
				if (!ReadVar(q, end, ml) || q + ml > end) break;
				q += ml;
			} else if (st == 0xf0 || st == 0xf7) {
				unsigned sl = 0;
				if (!ReadVar(q, end, sl) || q + sl > end) break;
				if (st == 0xf0 && sl >= 6 && sl + 1 <= 24) {
					BYTE tmp[24];
					tmp[0] = 0xf0;
					memcpy(tmp + 1, q, sl);
					if (SysexIsXgReset(tmp, 1 + (int)sl))
						return 1;
				}
				q += sl;
			} else {
				const int kind = st & 0xf0;
				const int need = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
				if (q + need > end) break;
				q += need;
			}
		}
		p = end;
	}
	return 0;
}

static int SmfFileHasXgReset(const wchar_t* path)
{
	if (!path || !path[0]) return 0;
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD size = GetFileSize(f, NULL), got = 0;
	if (size < 14 || size > 64 * 1024 * 1024) { CloseHandle(f); return 0; }
	BYTE* data = new BYTE[size];
	if (!ReadFile(f, data, size, &got, NULL) || got != size) {
		CloseHandle(f); delete[] data; return 0;
	}
	CloseHandle(f);
	const int hit = SmfBytesHasXgReset(data, size);
	delete[] data;
	return hit;
}

static int PickGsXgDll(const wchar_t* midPath, wchar_t* out, int outN)
{
	if (!out || outN <= 0) return 0;
	out[0] = 0;
	const int wantXg = (midPath && midPath[0] && SmfFileHasXgReset(midPath)) ? 1 : 0;
	const wchar_t* gs = savedata.vstMultiDll;
	const wchar_t* xg = savedata.vstExtraPath;
	const wchar_t* pick = NULL;
	if (wantXg) {
		if (PathLooksLikePlugin(xg)) pick = xg;
		else if (PathLooksLikePlugin(gs)) pick = gs;
	} else {
		if (PathLooksLikePlugin(gs)) pick = gs;
		else if (PathLooksLikePlugin(xg)) pick = xg;
	}
	if (!pick) return 0;
	SafeCopy(out, outN, pick);
	return 1;
}

static int MidiStatusRank(DWORD msg)
{
	const BYTE st = (BYTE)(msg & 0xff);
	if (st == 0xff) return 0;
	if (st == 0xf0 || st == 0xf7) return 5; // SysEx before channel voice
	const BYTE type = st & 0xf0;
	if (type == 0xb0) {
		const BYTE cc = (BYTE)((msg >> 8) & 0x7f);
		if (cc == 0) return 10;
		if (cc == 32) return 11;
		return 20;
	}
	if (type == 0xc0) return 30;
	if (type == 0xe0) return 40;
	if (type == 0xd0) return 45;
	if (type == 0x80) return 50;
	if (type == 0x90) {
		const BYTE vel = (BYTE)((msg >> 16) & 0x7f);
		return vel ? 60 : 50;
	}
	return 70;
}

static int MidiCmp(const void* aa, const void* bb)
{
	const MidiItem* a = (const MidiItem*)aa;
	const MidiItem* b = (const MidiItem*)bb;
	if (a->tick < b->tick) return -1;
	if (a->tick > b->tick) return 1;
	/* Setup (tempo / sysex / CC / PC / bend) before notes at the same tick.
	 * Do not pull note-off ahead of note-on: a zero-length 9x/9x-vel0 pair
	 * then becomes off-then-on, the off kills the previous key, and the new
	 * on never sees its off — voices hang one by one on busy SMFs. */
	const int ga = (MidiStatusRank(a->msg) < 50) ? 0 : 1;
	const int gb = (MidiStatusRank(b->msg) < 50) ? 0 : 1;
	if (ga < gb) return -1;
	if (ga > gb) return 1;
	if (a->seq < b->seq) return -1;
	if (a->seq > b->seq) return 1;
	return 0;
}

static void RxListenInit();
static void RxChReset();

static int LoadSmf(const wchar_t* path)
{
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (f == INVALID_HANDLE_VALUE) return -1;
	DWORD size = GetFileSize(f, NULL), got = 0;
	if (size < 14 || size > 256 * 1024 * 1024) { CloseHandle(f); return -2; }
	BYTE* data = new BYTE[size];
	if (!ReadFile(f, data, size, &got, NULL) || got != size) {
		CloseHandle(f); delete[] data; return -3;
	}
	CloseHandle(f);
	const BYTE* smf = data;
	DWORD smfSize = size;
	if (size >= 20 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "RMID", 4) == 0) {
		DWORD off = 12;
		while (off + 8 <= size) {
			const DWORD cksz = (DWORD)data[off + 4] | ((DWORD)data[off + 5] << 8)
				| ((DWORD)data[off + 6] << 16) | ((DWORD)data[off + 7] << 24);
			if (memcmp(data + off, "data", 4) == 0) {
				if (off + 8 > size) break;
				smf = data + off + 8;
				smfSize = cksz;
				if (smf + smfSize > data + size)
					smfSize = (DWORD)(data + size - smf);
				break;
			}
			const DWORD step = 8 + ((cksz + 1) & ~1u);
			if (step < 8 || off + step < off) break;
			off += step;
		}
	}
	if (smfSize < 14 || memcmp(smf, "MThd", 4) || ReadBE(smf + 4, 4) < 6) {
		delete[] data; return -4;
	}
	const int tracks = (int)ReadBE(smf + 10, 2);
	const int division = (int)ReadBE(smf + 12, 2);
	if (division <= 0 || (division & 0x8000)) { delete[] data; return -5; }
	MidiItem* ev = new MidiItem[MAX_MIDI_EVENTS];
	BYTE* sxData = new BYTE[size + 8 + 128];
	int sxUsed = 0;
	const int sxCap = (int)size + 8 + 128;
	int count = 0;
	int maxPort = 0;
	int sawFf21 = 0;
	int gs32 = 0;
	unsigned __int64 maxTick = 0;
	wchar_t metaTitle[280];
	metaTitle[0] = 0;
	int hasXg = 0;
	int mapHint = 0;
	const BYTE* p = smf + 8 + ReadBE(smf + 4, 4);
	const BYTE* fileEnd = smf + smfSize;
	for (int tr = 0; tr < tracks && p + 8 <= fileEnd; ++tr) {
		if (memcmp(p, "MTrk", 4)) break;
		DWORD len = ReadBE(p + 4, 4);
		const BYTE* q = p + 8;
		const BYTE* end = q + len <= fileEnd ? q + len : fileEnd;
		unsigned __int64 tick = 0;
		BYTE running = 0;
		int curPort = 0;
		while (q < end && count < MAX_MIDI_EVENTS) {
			unsigned delta = 0;
			if (!ReadVar(q, end, delta)) break;
			tick += delta;
			if (q >= end) break;
			BYTE st = *q;
			if (st & 0x80) { ++q; if (st < 0xf0) running = st; }
			else if (running) st = running;
			else break;
			if (st == 0xff) {
				if (q >= end) break;
				BYTE type = *q++;
				unsigned ml = 0;
				if (!ReadVar(q, end, ml) || q + ml > end) break;
				if (type == 0x51 && ml == 3) {
					ev[count].tick = tick;
					ev[count].sample = 0;
					ev[count].msg = 0xff;
					ev[count].aux = ReadBE(q, 3);
					ev[count].port = curPort;
					ev[count].sysexOff = -1;
					ev[count].seq = count;
					++count;
				} else if (type == 0x21 && ml >= 1) {
					// RP-019 MIDI Port Prefix — port A/B/C for 16/32/48ch modules.
					curPort = (int)q[0];
					if (curPort < 0) curPort = 0;
					if (curPort > 2) curPort = 2;
					sawFf21 = 1;
					if (curPort > maxPort) maxPort = curPort;
				} else if ((type == 0x01 || type == 0x02 || type == 0x03) && ml > 0) {
					char tmp[256];
					unsigned n = ml;
					if (n > 255) n = 255;
					memcpy(tmp, q, n);
					tmp[n] = 0;
					wchar_t w[256];
					w[0] = 0;
					if (!MultiByteToWideChar(932, 0, tmp, -1, w, 256))
						MultiByteToWideChar(CP_ACP, 0, tmp, -1, w, 256);
					w[255] = 0;
					if (w[0]) {
						mapHint = VstMidiFoldGsMapHint(mapHint, VstMidiGuessGsMapKind(w, NULL));
						if (type == 0x03 || !metaTitle[0]) {
							if (!metaTitle[0] || type == 0x03)
								wcsncpy_s(metaTitle, w, _TRUNCATE);
						}
					}
				}
				q += ml;
			} else if (st == 0xf0 || st == 0xf7) {
				unsigned sl = 0;
				if (!ReadVar(q, end, sl) || q + sl > end) break;
				// Rebuild a full dump (F0 + payload). F7 escape is raw payload.
				const int need = (st == 0xf0) ? (1 + (int)sl) : (int)sl;
				if (need > 0 && sxUsed + need <= sxCap) {
					const int off = sxUsed;
					if (st == 0xf0) sxData[sxUsed++] = 0xf0;
					memcpy(sxData + sxUsed, q, sl);
					sxUsed += (int)sl;
					ev[count].tick = tick;
					ev[count].sample = 0;
					ev[count].msg = 0xf0;
					ev[count].aux = (DWORD)need;
					ev[count].port = curPort;
					ev[count].sysexOff = off;
					ev[count].seq = count;
					++count;
					if (need >= 6) {
						const BYTE* sx = sxData + off;
						if (VstMidiSysexIsXgOn(sx, need)) hasXg = 1;
						if (VstMidiSysexMarksGs32(sx, need)) gs32 = 1;
					}
				}
				q += sl;
			} else {
				const int kind = st & 0xf0;
				const int need = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
				if (q + need > end) break;
				BYTE d1 = q[0], d2 = need == 2 ? q[1] : 0;
				q += need;
				if (kind >= 0x80 && kind <= 0xe0) {
					ev[count].tick = tick;
					ev[count].sample = 0;
					ev[count].msg = st | ((DWORD)d1 << 8) | ((DWORD)d2 << 16);
					ev[count].aux = 0;
					ev[count].port = curPort;
					ev[count].sysexOff = -1;
					ev[count].seq = count;
					++count;
				}
			}
		}
		if (tick > maxTick) maxTick = tick;
		p = end;
	}
	if (!count) {
		delete[] ev; delete[] data; delete[] sxData; return -6;
	}
	qsort(ev, count, sizeof(MidiItem), MidiCmp);
	unsigned __int64 lastTick = 0;
	unsigned tempo = 500000;
	__int64 sample = 0;
	unsigned __int64 rem = 0;
	const unsigned __int64 den = (unsigned __int64)division * 1000000ULL;
	for (int i = 0; i < count; ++i) {
		const unsigned __int64 dt = ev[i].tick - lastTick;
		rem += dt * (unsigned __int64)tempo * (unsigned __int64)SAMPLE_RATE;
		sample += (__int64)(rem / den);
		rem %= den;
		ev[i].sample = sample;
		lastTick = ev[i].tick;
		if ((ev[i].msg & 0xff) == 0xff && ev[i].aux) tempo = ev[i].aux;
	}
	if (maxTick > lastTick) {
		rem += (maxTick - lastTick) * (unsigned __int64)tempo * (unsigned __int64)SAMPLE_RATE;
		sample += (__int64)(rem / den);
	}
	if (!mapHint)
		mapHint = VstMidiGuessGsMapKind(metaTitle, path);
	else
		mapHint = VstMidiFoldGsMapHint(mapHint, VstMidiGuessGsMapKind(NULL, path));
	if (mapHint == 7) hasXg = 1;
	{
		BYTE msb[32];
		BYTE have[2048];
		unsigned short pairs[256];
		int nPairs = 0, hasGm = 0, hasGs = 0, hasSd = 0, hasGm2 = 0, cc32Max = 0;
		memset(msb, 0, sizeof(msb));
		memset(have, 0, sizeof(have));
		for (int i = 0; i < count; ++i) {
			if ((ev[i].msg & 0xff) == 0xf0 && ev[i].sysexOff >= 0) {
				const int n = (int)ev[i].aux;
				if (ev[i].sysexOff + n <= sxUsed) {
					const BYTE* d = sxData + ev[i].sysexOff;
					if (VstMidiSysexIsXgOn(d, n)) hasXg = 1;
					if (VstMidiSysexIsGmOn(d, n)) {
						hasGm = 1;
						if (n >= 5 && d[4] == 0x03) hasGm2 = 1;
					}
					if (VstMidiSysexIsGsReset(d, n)) hasGs = 1;
				}
				continue;
			}
			const int st = (int)(ev[i].msg & 0xf0);
			const int ch = (int)(ev[i].msg & 0x0f);
			int idx = ev[i].port * 16 + ch;
			if (idx < 0) idx = ch;
			if (idx > 31) idx = 31;
			const int d1 = (int)((ev[i].msg >> 8) & 0x7f);
			const int d2 = (int)((ev[i].msg >> 16) & 0x7f);
			const int drum = (ch == 9) ? 1 : 0;
			if (st == 0xb0 && d1 == 0) {
				msb[idx] = (BYTE)d2;
				if (VstMidiBankMsbIsSdNative(d2)) hasSd = 1;
				if (d2 == 121) hasGm2 = 1;
			} else if (st == 0xb0 && d1 == 32) {
				if (!drum && d2 >= 1 && d2 <= 4 && d2 > cc32Max) cc32Max = d2;
			} else if (st == 0xc0 && !drum) {
				const int bank = (int)msb[idx];
				const int bit = bank * 128 + d1;
				if (bit >= 0 && bit < 16384) {
					const int bi = bit >> 3;
					const BYTE mask = (BYTE)(1 << (bit & 7));
					if (!(have[bi] & mask)) {
						have[bi] = (BYTE)(have[bi] | mask);
						if (nPairs < 256)
							pairs[nPairs++] = (unsigned short)((bank << 8) | d1);
					}
				}
			}
		}
		int resolved = 0;
		if (hasXg) resolved = 0;
		else if (mapHint == 8) resolved = 1;
		else if (mapHint >= 9 && mapHint <= 18) resolved = mapHint;
		else if (mapHint >= 1 && mapHint <= 4) resolved = mapHint;
		else if (hasGm2 && !hasGs) resolved = 9;
		else if ((mapHint == 5 || hasGm) && !hasGs) resolved = 5;
		else if (mapHint == 6 || hasSd) resolved = 6;
		else if (cc32Max >= 1 && cc32Max <= 4) resolved = cc32Max;
		else resolved = VstMidiGsMapDropFromUsed(pairs, nPairs);
		g_eng.songGm = (resolved == 9) ? 2 : ((resolved == 5) ? 1 : 0);
		g_eng.gsMapLsb = (resolved >= 1 && resolved <= 4) ? resolved : 0;
	}
	if (g_eng.gsMapLsb && count + 64 < MAX_MIDI_EVENTS) {
		auto isGsReset = [&](const MidiItem& e) -> int {
			if ((e.msg & 0xff) != 0xf0 || e.sysexOff < 0) return 0;
			const int n = (int)e.aux;
			if (e.sysexOff + n > sxUsed || n < 11) return 0;
			const BYTE* d = sxData + e.sysexOff;
			return VstMidiSysexIsGsReset(d, n);
		};
		auto appendMap = [&](MidiItem* dst, int w, unsigned __int64 tick, __int64 samp) -> int {
			const int lsb = g_eng.gsMapLsb;
			const int nPort = (gs32 || maxPort >= 1) ? 2 : 1;
			for (int port = 0; port < nPort && w < MAX_MIDI_EVENTS; ++port) {
				for (int ch = 0; ch < 16 && w < MAX_MIDI_EVENTS; ++ch) {
					if (ch == 9) continue;
					dst[w].tick = tick;
					dst[w].sample = samp;
					dst[w].msg = (0xb0 | ch) | (32u << 8) | ((DWORD)lsb << 16);
					dst[w].aux = 0;
					dst[w].port = port;
					dst[w].sysexOff = -1;
					dst[w].seq = w;
					++w;
				}
			}
			return w;
		};
		int anyReset = 0;
		for (int i = 0; i < count; ++i)
			if (isGsReset(ev[i])) { anyReset = 1; break; }
		MidiItem* tmp = new MidiItem[MAX_MIDI_EVENTS];
		int w = 0;
		if (!anyReset)
			w = appendMap(tmp, w, 0, 0);
		for (int i = 0; i < count && w < MAX_MIDI_EVENTS; ++i) {
			tmp[w++] = ev[i];
			if (isGsReset(ev[i]))
				w = appendMap(tmp, w, ev[i].tick, ev[i].sample);
		}
		delete[] ev;
		ev = tmp;
		count = w;
	}
	if (!hasXg && g_eng.songGm == 0 && count + 8 < MAX_MIDI_EVENTS && sxUsed + 44 <= sxCap) {
		BYTE rhyA[11], rhyB[11];
		GsFillRhythmDt1(rhyA, 0x40);
		GsFillRhythmDt1(rhyB, 0x50);
		auto isRst = [&](const MidiItem& e) -> int {
			if ((e.msg & 0xff) != 0xf0 || e.sysexOff < 0) return 0;
			const int n = (int)e.aux;
			if (e.sysexOff + n > sxUsed || n < 11) return 0;
			const BYTE* d = sxData + e.sysexOff;
			if (VstMidiSysexIsGsReset(d, n)) return 1;
			if (GsDt1IsPortReset(d, n)) return (d[5] == 0x50) ? 2 : 3;
			return 0;
		};
		auto putRhy = [&](MidiItem* dst, int w, unsigned __int64 tick, __int64 samp, int which) -> int {
			if (w >= MAX_MIDI_EVENTS) return w;
			const BYTE* src = (which == 2) ? rhyB : rhyA;
			if (sxUsed + 11 > sxCap) return w;
			const int off = sxUsed;
			memcpy(sxData + sxUsed, src, 11);
			sxUsed += 11;
			dst[w].tick = tick;
			dst[w].sample = samp;
			dst[w].msg = 0xf0;
			dst[w].aux = 11;
			dst[w].port = (which == 2) ? 1 : 0;
			dst[w].sysexOff = off;
			dst[w].seq = w;
			return w + 1;
		};
		int any = 0;
		for (int i = 0; i < count; ++i)
			if (isRst(ev[i])) { any = 1; break; }
		const int wantB = (gs32 || maxPort >= 1) ? 1 : 0;
		MidiItem* tmp = new MidiItem[MAX_MIDI_EVENTS];
		int w = 0;
		if (!any) {
			w = putRhy(tmp, w, 0, 0, 1);
			if (wantB) w = putRhy(tmp, w, 0, 0, 2);
		}
		for (int i = 0; i < count && w < MAX_MIDI_EVENTS; ++i) {
			tmp[w++] = ev[i];
			const int k = isRst(ev[i]);
			if (k == 1) {
				w = putRhy(tmp, w, ev[i].tick, ev[i].sample, 1);
				if (wantB) w = putRhy(tmp, w, ev[i].tick, ev[i].sample, 2);
			} else if (k == 2)
				w = putRhy(tmp, w, ev[i].tick, ev[i].sample, 2);
			else if (k == 3)
				w = putRhy(tmp, w, ev[i].tick, ev[i].sample, 1);
		}
		delete[] ev;
		ev = tmp;
		count = w;
	}
	if (hasXg && count + 8 < MAX_MIDI_EVENTS) {
		auto isXg = [&](const MidiItem& e) -> int {
			if ((e.msg & 0xff) != 0xf0 || e.sysexOff < 0) return 0;
			const int n = (int)e.aux;
			if (e.sysexOff + n > sxUsed) return 0;
			return VstMidiSysexIsXgOn(sxData + e.sysexOff, n);
		};
		auto putXgDrum = [&](MidiItem* dst, int w, unsigned __int64 tick, __int64 samp, int port) -> int {
			if (w + 2 > MAX_MIDI_EVENTS) return w;
			dst[w].tick = tick; dst[w].sample = samp;
			dst[w].msg = 0xb9 | (0u << 8) | (127u << 16);
			dst[w].aux = 0; dst[w].port = port; dst[w].sysexOff = -1; dst[w].seq = w;
			++w;
			dst[w].tick = tick; dst[w].sample = samp;
			dst[w].msg = 0xb9 | (32u << 8);
			dst[w].aux = 0; dst[w].port = port; dst[w].sysexOff = -1; dst[w].seq = w;
			return w + 1;
		};
		int any = 0;
		for (int i = 0; i < count; ++i)
			if (isXg(ev[i])) { any = 1; break; }
		const int wantB = (gs32 || maxPort >= 1) ? 1 : 0;
		MidiItem* tmp = new MidiItem[MAX_MIDI_EVENTS];
		int w = 0;
		if (!any) {
			w = putXgDrum(tmp, w, 0, 0, 0);
			if (wantB) w = putXgDrum(tmp, w, 0, 0, 1);
		}
		for (int i = 0; i < count && w < MAX_MIDI_EVENTS; ++i) {
			tmp[w++] = ev[i];
			if (isXg(ev[i])) {
				w = putXgDrum(tmp, w, ev[i].tick, ev[i].sample, ev[i].port);
				if (wantB && ev[i].port == 0)
					w = putXgDrum(tmp, w, ev[i].tick, ev[i].sample, 1);
			}
		}
		delete[] ev;
		ev = tmp;
		count = w;
	}
	EnsLog(L"LoadSmf GS map lsb=%d xg=%d gm=%d title=[%s]",
		g_eng.gsMapLsb, hasXg, g_eng.songGm, metaTitle[0] ? metaTitle : L"");
	g_eng.fileData = data;
	g_eng.fileBytes = size;
	g_eng.sysexData = sxData;
	g_eng.sysexBytes = sxUsed;
	g_eng.events = ev;
	g_eng.eventCount = count;
	if (gs32 && maxPort < 1) maxPort = 1;
	g_eng.maxMidiPort = maxPort;
	g_eng.mirrorToB = (gs32 && !sawFf21) ? 1 : 0;
	RxListenInit();
	/* 最後のノートの残響を鳴らし切るための余白。クロスフェードは
	 * この余白を除いた「音の終わり」を基準にする（XfTailPadBytes）。 */
	g_eng.lengthSamples = sample + SAMPLE_RATE * VST_TAIL_PAD_SEC;
	EnsLog(L"LoadSmf events=%d maxPort=%d gs32=%d ff21=%d mirrorB=%d sysexBytes=%d (ports: %dch)",
		count, maxPort, gs32, sawFf21, g_eng.mirrorToB, sxUsed, (maxPort + 1) * 16);
	return 0;
}

enum { kVstMidiEventIsRealtime = 1 };
enum { VST_PEND_N = 1024, VST_PEND_SX = 256, VST_SLICE = 64 };

struct VstPendBuf {
	struct {
		VstInt32 numEvents;
		VstIntPtr reserved;
		VstEvent* events[VST_PEND_N];
	} block;
	VstMidiEvent me[VST_PEND_N];
	VstMidiSysexEvent sx[VST_PEND_SX];
};
/* Must outlive processReplacing: SC-VA (and many VST2s) keep the VstEvent*
 * pointers from processEvents and read them in process. Stack arrays were
 * already gone, so note-offs vanished. A second MIDI player → virtual MIDI →
 * VST host keeps a heap buffer; we do the same here. */
static VstPendBuf g_vstPend[2][3];
static VstPendBuf g_livePend[32];
static MidiItem g_songEv[2][3][VST_PEND_N];
static BYTE g_songSx[2][3][8192];
static int g_songN[2][3];
static __int64 g_songT0[2];

static void SendVstEvents(AEffect* e, const MidiItem* ev, int count,
	__int64 blockStart, const BYTE* sxStore = 0, int frames = BLOCK_FRAMES,
	int unit = 0)
{
	if (!e || !e->dispatcher || count <= 0) return;
	if (unit < 0) unit = 0;
	if (unit > 2) unit = 2;
	VstPendBuf& p = g_vstPend[VstIoSlot()][unit];
	if (count > VST_PEND_N) count = VST_PEND_N;
	int n = 0, nsx = 0;
	const int last = frames > 0 ? frames - 1 : 0;
	for (int i = 0; i < count; ++i) {
		__int64 d = ev[i].sample - blockStart;
		const int delta = d < 0 ? 0 : (d > last ? last : (int)d);
		if ((ev[i].msg & 0xff) == 0xf0 && ev[i].sysexOff >= 0 && sxStore) {
			if (nsx >= VST_PEND_SX || n >= VST_PEND_N) continue;
			VstMidiSysexEvent& s = p.sx[nsx++];
			ZeroMemory(&s, sizeof(s));
			s.type = kVstSysExType;
			s.byteSize = sizeof(VstMidiSysexEvent);
			s.deltaFrames = delta;
			s.dumpBytes = (VstInt32)ev[i].aux;
			s.sysexDump = (char*)(sxStore + ev[i].sysexOff);
			p.block.events[n++] = (VstEvent*)&s;
			continue;
		}
		if (n >= VST_PEND_N) break;
		DWORD msg = ev[i].msg;
		if ((msg & 0xf0) == 0x90 && ((msg >> 16) & 0x7f) == 0)
			msg = (msg & ~0xf0u) | 0x80u;
		if ((msg & 0xf0) == 0x80 && ((msg >> 16) & 0x7f) == 0)
			msg |= (64u << 16);
		VstMidiEvent& m = p.me[n];
		ZeroMemory(&m, sizeof(m));
		m.type = kVstMidiType;
		m.byteSize = sizeof(VstMidiEvent);
		m.deltaFrames = delta;
		m.flags = kVstMidiEventIsRealtime;
		m.midiData[0] = (char)(msg & 0xff);
		m.midiData[1] = (char)((msg >> 8) & 0x7f);
		m.midiData[2] = (char)((msg >> 16) & 0x7f);
		p.block.events[n] = (VstEvent*)&m;
		++n;
	}
	p.block.numEvents = n;
	p.block.reserved = 0;
	if (n)
		e->dispatcher(e, effProcessEvents, 0, 0, &p.block, 0);
}

static void MapperSysex(HMIDIOUT h, const BYTE* data, DWORD bytes);
static void RenderEffect(AEffect* e, float* l, float* r, int frames);

static void SendVstSysex(AEffect* e, const BYTE* data, int len, int deltaFrames)
{
	if (!e || !e->dispatcher || !data || len < 2) return;
	VstMidiSysexEvent sx = {};
	sx.type = kVstSysExType;
	sx.byteSize = sizeof(VstMidiSysexEvent);
	sx.deltaFrames = deltaFrames;
	sx.dumpBytes = (VstInt32)len;
	sx.sysexDump = (char*)data;
	struct EventBlock {
		VstInt32 numEvents;
		VstIntPtr reserved;
		VstEvent* events[1];
	} block = {};
	block.numEvents = 1;
	block.events[0] = (VstEvent*)&sx;
	__try { e->dispatcher(e, effProcessEvents, 0, 0, &block, 0); }
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void SendUnitSysex(int unit, const BYTE* data, int len, int deltaFrames)
{
	if (!data || len < 2) return;
	if (unit == 0) {
		if (g_eng.effect) SendVstSysex(g_eng.effect, data, len, deltaFrames);
		if (g_eng.vst3) Vst3MidiSysex(g_eng.vst3, data, len, deltaFrames);
	} else if (unit == 1) {
		if (g_eng.effectB) SendVstSysex(g_eng.effectB, data, len, deltaFrames);
	} else if (unit == 2) {
		if (g_eng.effectC) SendVstSysex(g_eng.effectC, data, len, deltaFrames);
		if (g_eng.vst3C) Vst3MidiSysex(g_eng.vst3C, data, len, deltaFrames);
	}
}

// SC-88 32/48ch: block 40 = Port A (parts 1-16), 50 = Port B (17-32), 60 = Port C.
// Each VST instance is a 16-part module, so 50/60 must be rewritten to 40 and
// sent only to that instance. Broadcasting 40-block to B retunes Rx/rhythm and
// leaves notes hanging. Hardware SC-88 still gets the original dump.
// GS 40/50 1x 15 is USE FOR RHYTHM (0=OFF, 1=MAP1, 2=MAP2), not Rx CHANNEL
// (that is 1x 02). 1x 40 size 0C is SCALE TUNING; a fill of identical bytes is
// legal on SC-88 but some 16-part VSTs treat it as bank/PC and hang notes.
static int GsDt1UniformFill(const BYTE* d, int n)
{
	const int nd = n - 10;
	if (!d || nd < 8) return 0;
	const BYTE v = d[8];
	for (int i = 1; i < nd; ++i)
		if (d[8 + i] != v) return 0;
	return 1;
}

static int GsDt1SkipVstScaleFill(const BYTE* d, int n)
{
	if (!d || n < 18) return 0;
	if (d[0] != 0xf0 || d[1] != 0x41 || d[3] != 0x42 || d[4] != 0x12) return 0;
	const BYTE aa = d[5], bb = d[6], cc = d[7];
	if ((aa & 0xf0) != 0x40 && (aa & 0xf0) != 0x50 && (aa & 0xf0) != 0x60) return 0;
	if (bb < 0x10 || bb > 0x1f || cc != 0x40) return 0;
	return GsDt1UniformFill(d, n);
}

// Two 16-part VSTs do not share Rx CHANNEL the way one SC-88 does. Host keeps
// the listen map. 1x 02 is sent (16-part remap: 11h–1Fh minus 10h). After 1x 15
// (USE FOR RHYTHM) the same block sends 1x 02 from that map, not a forced
// identity: extra drum parts often listen on a channel other than 10.
static BYTE g_rxCh[2][3][16];
static BYTE g_rxPort[2][3][16];
static BYTE g_vstOn[2][3][16][128];
static BYTE g_vstHeldFlushed[2];

static int GsPartXToCh(int bb)
{
	const int x = bb & 0x0f;
	if (x == 0) return 9;
	if (x <= 9) return x - 1;
	return x;
}

static void RxListenInit()
{
	const int s = VstIoSlot();
	g_vstHeldFlushed[s] = 0;
	for (int u = 0; u < 3; ++u) {
		for (int p = 0; p < 16; ++p) {
			g_rxCh[s][u][p] = (BYTE)p;
			g_rxPort[s][u][p] = (BYTE)u;
		}
		ZeroMemory(g_vstOn[s][u], sizeof(g_vstOn[s][u]));
	}
}

static void RxChReset()
{
	const int s = VstIoSlot();
	g_vstHeldFlushed[s] = 0;
	for (int u = 0; u < 3; ++u) {
		for (int p = 0; p < 16; ++p) {
			g_rxCh[s][u][p] = (BYTE)p;
			g_rxPort[s][u][p] = (BYTE)u;
		}
		ZeroMemory(g_vstOn[s][u], sizeof(g_vstOn[s][u]));
	}
}

static void RxListenSet(int unit, int bb, BYTE v)
{
	const int s = VstIoSlot();
	const int p = GsPartXToCh(bb);
	if (unit < 0 || unit > 2 || p < 0 || p > 15) return;
	if (v == 0x10) {
		g_rxCh[s][unit][p] = 16;
		return;
	}
	if (v <= 0x0f) {
		g_rxCh[s][unit][p] = v;
		g_rxPort[s][unit][p] = 0;
	} else if (v <= 0x1f) {
		g_rxCh[s][unit][p] = (BYTE)(v - 0x10);
		g_rxPort[s][unit][p] = 1;
	} else if (v <= 0x2f) {
		g_rxCh[s][unit][p] = (BYTE)(v - 0x20);
		g_rxPort[s][unit][p] = 2;
	}
}

static void RxBlockPortSet(int blk, BYTE portAB)
{
	const int unit = (blk >= 0x10) ? 1 : 0;
	const int p = GsPartXToCh(0x10 | (blk & 0x0f));
	const int s = VstIoSlot();
	if (p < 0 || p > 15) return;
	g_rxPort[s][unit][p] = portAB ? 1 : 0;
}

static int UnitHasPlug(int unit)
{
	if (unit == 0) return (g_eng.effect || g_eng.vst3) ? 1 : 0;
	if (unit == 1) return g_eng.effectB ? 1 : 0;
	return (g_eng.effectC || g_eng.vst3C) ? 1 : 0;
}

static int PushFanout(MidiItem* batch, int n, int cap, MidiItem e, int unit)
{
	const int st = (int)(e.msg & 0xf0);
	if (st < 0x80 || st > 0xe0) {
		if (n < cap) batch[n++] = e;
		return n;
	}
	const int src = (int)(e.msg & 0x0f);
	int sport = e.port;
	if (sport < 0) sport = 0;
	const int s = VstIoSlot();
	for (int p = 0; p < 16; ++p) {
		if (g_rxCh[s][unit][p] != (BYTE)src) continue;
		if (g_rxPort[s][unit][p] != (BYTE)sport) continue;
		if (n >= cap) break;
		MidiItem m = e;
		m.msg = (e.msg & ~0x0fu) | (DWORD)p;
		batch[n++] = m;
	}
	return n;
}

static int GsBuildRxDt1(BYTE* d, int unit, int bb)
{
	const int p = GsPartXToCh(bb);
	const int s = VstIoSlot();
	BYTE rv = (BYTE)p;
	if (unit >= 0 && unit <= 2 && p >= 0 && p <= 15) {
		const BYTE listen = g_rxCh[s][unit][p];
		rv = (listen >= 16) ? (BYTE)0x10 : listen;
	}
	d[0] = 0xf0; d[1] = 0x41; d[2] = 0x10; d[3] = 0x42; d[4] = 0x12;
	d[5] = 0x40; d[6] = (BYTE)bb; d[7] = 0x02;
	d[8] = rv;
	d[9] = 0; d[10] = 0xf7;
	GsFixChecksum(d, 11);
	return 11;
}

static int VstTrackPush(MidiItem* batch, int n, int cap, int unit, MidiItem e)
{
	const int s = VstIoSlot();
	const int st = (int)(e.msg & 0xf0);
	const int ch = (int)(e.msg & 0x0f);
	const int note = (int)((e.msg >> 8) & 0x7f);
	const int vel = (int)((e.msg >> 16) & 0x7f);
	if (unit < 0 || unit > 2) {
		if (n < cap) batch[n++] = e;
		return n;
	}
	const int retrig = (st == 0x90 && vel && g_vstOn[s][unit][ch][note]) ? 1 : 0;
	if (n + 1 + retrig > cap) return n;
	if (retrig) {
		MidiItem off = e;
		off.msg = (DWORD)(0x80 | ch) | ((DWORD)note << 8);
		batch[n++] = off;
	}
	if (st == 0x90 && vel)
		g_vstOn[s][unit][ch][note] = 1;
	else if (st == 0x80 || (st == 0x90 && vel == 0))
		g_vstOn[s][unit][ch][note] = 0;
	else if (st == 0xb0 && (note == 120 || note == 123))
		ZeroMemory(g_vstOn[s][unit][ch], 128);
	batch[n++] = e;
	return n;
}

static int VstFlushHeld(MidiItem* batch, int n, int cap, int unit, __int64 sample)
{
	const int s = VstIoSlot();
	if (unit < 0 || unit > 2) return n;
	for (int ch = 0; ch < 16; ++ch) {
		for (int note = 0; note < 128; ++note) {
			if (!g_vstOn[s][unit][ch][note]) continue;
			if (n >= cap) return n;
			MidiItem it = {};
			it.msg = (DWORD)(0x80 | ch) | ((DWORD)note << 8);
			it.sample = sample;
			it.sysexOff = -1;
			it.port = unit;
			batch[n++] = it;
			g_vstOn[s][unit][ch][note] = 0;
		}
	}
	return n;
}

static int VstPushCc123(MidiItem* batch, int n, int cap, int unit, __int64 sample)
{
	const int s = VstIoSlot();
	if (unit < 0 || unit > 2) return n;
	for (int ch = 0; ch < 16; ++ch) {
		if (n >= cap) break;
		MidiItem it = {};
		it.msg = (DWORD)(0xb0 | ch) | (123u << 8);
		it.sample = sample;
		it.sysexOff = -1;
		it.port = unit;
		batch[n++] = it;
		ZeroMemory(g_vstOn[s][unit][ch], 128);
	}
	return n;
}

static int PrepareSongSysex(const BYTE* data, int len, int smfPort,
	BYTE* out, int outMax, int* unitsOut, int* rhyBbOut)
{
	if (unitsOut) *unitsOut = 0;
	if (rhyBbOut) *rhyBbOut = -1;
	if (!data || len < 2 || !out || outMax < len) return 0;
	int units = 0;
	const BYTE* vstData = data;
	BYTE tmp[2048];
	int skipVst = GsDt1SkipVstScaleFill(data, len);
	int rhyBb = -1;

	const int gsDt1 = (len >= 11 && data[0] == 0xf0 && data[1] == 0x41 &&
		data[3] == 0x42 && data[4] == 0x12) ? 1 : 0;
	if (gsDt1) {
		const BYTE aa = data[5];
		const BYTE bb = data[6];
		const BYTE cc = data[7];
		const int partRx = (bb >= 0x10 && bb <= 0x1f && cc == 0x02 && len >= 11) ? 1 : 0;
		const int partRhy = (bb >= 0x10 && bb <= 0x1f && cc == 0x15 && len >= 11) ? 1 : 0;
		if (aa == 0x00 && bb == 0x01 && cc <= 0x1f && len >= 11) {
			RxBlockPortSet((int)cc, data[8]);
			skipVst = 1;
		} else if (aa == 0x40 && bb <= 0x01)
			units = 1 | 2 | 4;
		else if ((aa & 0xf0) == 0x40) {
			units = 1;
			if (partRx) {
				RxListenSet(0, bb, data[8]);
				BYTE rv = data[8];
				if (rv > 0x10 && rv <= 0x1f) rv = (BYTE)(rv - 0x10);
				if (rv != data[8] && len <= (int)sizeof(tmp)) {
					memcpy(tmp, data, (size_t)len);
					tmp[8] = rv;
					GsFixChecksum(tmp, len);
					vstData = tmp;
				}
			} else if (partRhy)
				rhyBb = (int)bb;
		} else if ((aa & 0xf0) == 0x50 || (aa & 0xf0) == 0x60) {
			const int u = ((aa & 0xf0) == 0x60) ? 2 : 1;
			units = 1 << u;
			if (partRhy)
				rhyBb = (int)bb;
			if (len <= (int)sizeof(tmp)) {
				memcpy(tmp, data, (size_t)len);
				tmp[5] = (BYTE)(0x40 | (aa & 0x0f));
				if (partRx && tmp[8] > 0x10 && tmp[8] <= 0x1f)
					tmp[8] = (BYTE)(tmp[8] - 0x10);
				GsFixChecksum(tmp, len);
				vstData = tmp;
			}
			if (partRx)
				RxListenSet(u, bb, data[8]);
		} else
			units = 1 | 2 | 4;
	} else if (VstMidiSysexIsGmOn(data, len) || VstMidiSysexIsGsReset(data, len) ||
		VstMidiSysexIsXgOn(data, len)) {
		units = 1 | 2 | 4;
		RxChReset();
	} else if (len >= 8 && data[1] == 0x43 && data[3] == 0x4c && data[4] == 0x09) {
		units = 2;
		if (len <= (int)sizeof(tmp)) {
			memcpy(tmp, data, (size_t)len);
			tmp[4] = 0x08;
			vstData = tmp;
		}
	} else if (len >= 8 && data[1] == 0x43 && data[3] == 0x4c && data[4] == 0x08) {
		units = 1;
	} else {
		int p = smfPort;
		if (p < 0) p = 0;
		if (p > 2) p = 2;
		units = 1 << p;
	}

	if (skipVst || !units) return 0;
	if (unitsOut) *unitsOut = units;
	if (rhyBbOut) *rhyBbOut = rhyBb;
	memcpy(out, vstData, (size_t)len);
	return len;
}

static void BroadcastSongSysex(const BYTE* data, int len, int deltaFrames, int smfPort)
{
	if (!data || len < 2) return;
	BYTE buf[2048];
	int units = 0, rhyBb = -1;
	const int n = PrepareSongSysex(data, len, smfPort, buf, (int)sizeof(buf), &units, &rhyBb);
	if (n > 0) {
		if (units & 1) SendUnitSysex(0, buf, n, deltaFrames);
		if (units & 2) SendUnitSysex(1, buf, n, deltaFrames);
		if (units & 4) SendUnitSysex(2, buf, n, deltaFrames);
		if (rhyBb >= 0) {
			BYTE rx[11];
			if (units & 1) {
				GsBuildRxDt1(rx, 0, rhyBb);
				SendUnitSysex(0, rx, 11, deltaFrames);
			}
			if (units & 2) {
				GsBuildRxDt1(rx, 1, rhyBb);
				SendUnitSysex(1, rx, 11, deltaFrames);
			}
			if (units & 4) {
				GsBuildRxDt1(rx, 2, rhyBb);
				SendUnitSysex(2, rx, 11, deltaFrames);
			}
		}
		if (VstMidiSysexIsGsReset(data, len) || GsDt1IsPortReset(data, len)) {
			BYTE rhy[11];
			GsFillRhythmDt1(rhy, 0x40);
			if (units & 1) SendUnitSysex(0, rhy, 11, deltaFrames);
			if (units & 2) SendUnitSysex(1, rhy, 11, deltaFrames);
			if (units & 4) SendUnitSysex(2, rhy, 11, deltaFrames);
		}
		if (VstMidiSysexIsXgOn(data, len)) {
			const DWORD msb = 0xb9 | (0u << 8) | (127u << 16);
			const DWORD lsb = 0xb9 | (32u << 8);
			MidiItem cc[2] = {};
			cc[0].msg = msb; cc[1].msg = lsb;
			if (units & 1) {
				if (g_eng.effect) SendVstEvents(g_eng.effect, cc, 2, 0);
				if (g_eng.vst3) {
					Vst3MidiShort(g_eng.vst3, msb, deltaFrames);
					Vst3MidiShort(g_eng.vst3, lsb, deltaFrames);
				}
			}
			if (units & 2 && g_eng.effectB) SendVstEvents(g_eng.effectB, cc, 2, 0);
			if (units & 4) {
				if (g_eng.effectC) SendVstEvents(g_eng.effectC, cc, 2, 0);
				if (g_eng.vst3C) {
					Vst3MidiShort(g_eng.vst3C, msb, deltaFrames);
					Vst3MidiShort(g_eng.vst3C, lsb, deltaFrames);
				}
			}
		}
	}
	if (g_eng.midiOut)
		MapperSysex(g_eng.midiOut, data, (DWORD)len);
}

static int MidiIsNoteMsg(DWORD msg)
{
	const int t = (int)(msg & 0xf0);
	return (t == 0x80 || t == 0x90) ? 1 : 0;
}

/* Only the same key needs a 1-sample gap. Spreading every note in the block
 * delayed an off past the next on of that key and made hangs worse. */
static void VstPrepareBatch(MidiItem* batch, int n, __int64 start, int frames)
{
	if (!batch || n <= 0 || frames < 1) return;
	const __int64 last = start + (frames - 1);
	__int64 hold[16][128];
	for (int ch = 0; ch < 16; ++ch)
		for (int note = 0; note < 128; ++note)
			hold[ch][note] = start - 1;
	for (int i = 0; i < n; ++i) {
		if (!MidiIsNoteMsg(batch[i].msg)) continue;
		const int ch = (int)(batch[i].msg & 15);
		const int note = (int)((batch[i].msg >> 8) & 0x7f);
		__int64 s = batch[i].sample;
		if (s <= hold[ch][note]) s = hold[ch][note] + 1;
		if (s < start) s = start;
		if (s > last) s = last;
		batch[i].sample = s;
		hold[ch][note] = s;
	}
	for (int i = 1; i < n; ++i) {
		MidiItem x = batch[i];
		int j = i;
		while (j > 0 && batch[j - 1].sample > x.sample) {
			batch[j] = batch[j - 1];
			--j;
		}
		batch[j] = x;
	}
}

static void FlushUnitShorts(AEffect* effect, Vst3Inst* vst3, MidiItem* batch,
	int& n, __int64 start, int frames, const BYTE* sxStore = 0, int unit = 0)
{
	if (!n) return;
	VstPrepareBatch(batch, n, start, frames);
	if (effect) SendVstEvents(effect, batch, n, start, sxStore, frames, unit);
	if (vst3) {
		for (int i = 0; i < n; ++i) {
			__int64 d = batch[i].sample - start;
			int offset = d < 0 ? 0 : (d >= frames ? frames - 1 : (int)d);
			if ((batch[i].msg & 0xff) == 0xf0 && batch[i].sysexOff >= 0 && sxStore) {
				Vst3MidiSysex(vst3, sxStore + batch[i].sysexOff, (int)batch[i].aux, offset);
				continue;
			}
			if ((batch[i].msg & 0xff) == 0xf0) continue;
			Vst3MidiShort(vst3, batch[i].msg, offset);
		}
	}
	n = 0;
}

enum { SONG_INJ_CAP = 128, SONG_OV_PC = 6, SONG_OV_N = 7 };
static volatile LONG g_injW = 0;
static volatile LONG g_injR = 0;
static DWORD g_injMsg[SONG_INJ_CAP];
static BYTE g_injPort[SONG_INJ_CAP];
static int g_injOfs[SONG_INJ_CAP];
static LONGLONG g_injQpc[SONG_INJ_CAP];
enum { INJ_SX_N = 8, INJ_SX_B = 128 };
static BYTE g_injSx[INJ_SX_N][INJ_SX_B];
static int g_injSxLen[INJ_SX_N];
static BYTE g_injSxPort[INJ_SX_N];
static volatile LONG g_injSxW = 0;
static volatile LONG g_injSxR = 0;
static int g_liveTailFrames = 0;
static BYTE g_ovOn[3][16][SONG_OV_N];
static BYTE g_ovVal[3][16][SONG_OV_N];
static DWORD g_ovTick[3][16];

static int SongOvSlot(int cc)
{
	if (cc == 7) return 0;
	if (cc == 10) return 1;
	if (cc == 11) return 2;
	if (cc == 91) return 3;
	if (cc == 93) return 4;
	if (cc == 94) return 5;
	return -1;
}

static void SongOvClear()
{
	g_injR = g_injW;
	g_injSxR = g_injSxW;
	g_liveTailFrames = 0;
	ZeroMemory(g_ovOn, sizeof(g_ovOn));
	ZeroMemory(g_ovTick, sizeof(g_ovTick));
}

static void SongOvSet(int port, DWORD msg)
{
	if (port < 0) port = 0;
	if (port > 2) port = 2;
	const int st = (int)(msg & 0xf0);
	const int ch = (int)(msg & 0x0f);
	if (st == 0xb0) {
		const int slot = SongOvSlot((int)((msg >> 8) & 0x7f));
		if (slot < 0) return;
		g_ovOn[port][ch][slot] = 1;
		g_ovVal[port][ch][slot] = (BYTE)((msg >> 16) & 0x7f);
		g_ovTick[port][ch] = GetTickCount();
	} else if (st == 0xc0) {
		g_ovOn[port][ch][SONG_OV_PC] = 1;
		g_ovVal[port][ch][SONG_OV_PC] = (BYTE)((msg >> 8) & 0x7f);
		g_ovTick[port][ch] = GetTickCount();
	}
}

static void SongOvExpire()
{
	const DWORD now = GetTickCount();
	for (int p = 0; p < 3; ++p) {
		for (int c = 0; c < 16; ++c) {
			if (!g_ovTick[p][c]) continue;
			if (now - g_ovTick[p][c] < 2500) continue;
			memset(g_ovOn[p][c], 0, SONG_OV_N);
			g_ovTick[p][c] = 0;
		}
	}
}

static DWORD SongOverrideMsg(int port, DWORD msg)
{
	if (port < 0) port = 0;
	if (port > 2) port = 2;
	const int st = (int)(msg & 0xf0);
	const int ch = (int)(msg & 0x0f);
	if (st == 0xb0) {
		const int slot = SongOvSlot((int)((msg >> 8) & 0x7f));
		if (slot >= 0 && g_ovOn[port][ch][slot])
			return (msg & 0xFFFFu) | ((DWORD)g_ovVal[port][ch][slot] << 16);
	} else if (st == 0xc0) {
		if (g_ovOn[port][ch][SONG_OV_PC])
			return (msg & 0xFFu) | ((DWORD)g_ovVal[port][ch][SONG_OV_PC] << 8);
	}
	return msg;
}

} // namespace（無名名前空間の中で extern "C" を書いても内部リンケージのまま
  // シンボルが出ない。この2本は他モジュールから呼ばれるので外に出す）

extern "C" void VstMidiInjectShort(int portIndex0to2, DWORD shortMsg, int sampleOfs)
{
	int port = portIndex0to2;
	if (port < 0) port = 0;
	if (port > 2) port = 2;
	SongOvSet(port, shortMsg);
	/* マッパーは PCM ブロックを待たず、クリックした瞬間に短文を出す。
	 * 同じフラッシュで note on/off が潰れるのを避ける。VST へはキューする。
	 * プラグインが載っているときはマッパーへ出さない（別音源の PC が混ざる）。 */
	const int hasPlug = (g_eng.effect || g_eng.vst3 || g_eng.useEnsemble) ? 1 : 0;
	if (!hasPlug && g_eng.midiOut && (port <= 0 || !g_eng.effectB)) {
		EnterCriticalSection(&g_eng.cs);
		if (g_eng.midiOut)
			midiOutShortMsg(g_eng.midiOut, shortMsg);
		LeaveCriticalSection(&g_eng.cs);
	}
	const LONG w = g_injW;
	if ((w - g_injR) >= (SONG_INJ_CAP - 1)) return;
	const int i = (int)(w & (SONG_INJ_CAP - 1));
	g_injMsg[i] = shortMsg;
	g_injPort[i] = (BYTE)port;
	g_injOfs[i] = sampleOfs;
	LARGE_INTEGER qpc;
	QueryPerformanceCounter(&qpc);
	g_injQpc[i] = qpc.QuadPart;
	MemoryBarrier();
	g_injW = w + 1;
	g_liveTailFrames = SAMPLE_RATE * 4;
}

extern "C" void VstMidiInjectSysex(int portIndex0to2, const unsigned char* data, int bytes)
{
	if (!data || bytes < 2 || bytes > INJ_SX_B) return;
	int port = portIndex0to2;
	if (port < 0) port = 0;
	if (port > 2) port = 2;
	const LONG w = g_injSxW;
	if ((w - g_injSxR) >= (INJ_SX_N - 1)) return;
	const int i = (int)(w & (INJ_SX_N - 1));
	memcpy(g_injSx[i], data, (size_t)bytes);
	g_injSxLen[i] = bytes;
	g_injSxPort[i] = (BYTE)port;
	MemoryBarrier();
	g_injSxW = w + 1;
	g_liveTailFrames = SAMPLE_RATE * 4;
}

extern "C" int VstMidiStealInjects(BYTE* ports, DWORD* msgs, int* sampleOfs, int maxCount)
{
	if (!ports || !msgs || maxCount < 1) return 0;
	int n = 0;
	LONG r = g_injR;
	const LONG w = g_injW;
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	LONGLONG q0 = 0;
	int haveQ = 0;
	while (n < maxCount && r != w) {
		const int i = (int)(r & (SONG_INJ_CAP - 1));
		ports[n] = g_injPort[i];
		msgs[n] = g_injMsg[i];
		int ofs = g_injOfs[i];
		if (ofs < 0) {
			if (!haveQ) { q0 = g_injQpc[i]; haveQ = 1; }
			if (freq.QuadPart > 0)
				ofs = (int)((g_injQpc[i] - q0) * (LONGLONG)SAMPLE_RATE / freq.QuadPart);
			else
				ofs = 0;
		}
		if (ofs < 0) ofs = 0;
		if (sampleOfs) sampleOfs[n] = ofs;
		++n;
		++r;
	}
	g_injR = r;
	return n;
}

namespace {

static void EmitSongShort(int port, DWORD msg, __int64 start, int frames, int ofs)
{
	if (ofs < 0) ofs = 0;
	if (frames > 0 && ofs >= frames) ofs = frames - 1;
	MidiItem it = {};
	it.msg = msg;
	it.sample = start + ofs;
	it.port = port;
	it.sysexOff = -1;
	if (g_eng.useEnsemble) {
		const int ch = (int)(msg & 15);
		const int slot = g_eng.chSlot[ch];
		if (slot < 0 || slot >= g_eng.mixCount) return;
		MixSlot& ms = g_eng.mix[slot];
		MidiItem m = it;
		const int type = (int)(msg & 0xf0);
		if (!ms.keepMidiCh) {
			if (type == 0xc0) return;
			m.msg = (m.msg & ~0x0fu) | 0u;
		}
		if (ms.effect) SendVstEvents(ms.effect, &m, 1, start);
		if (ms.vst3) Vst3MidiShort(ms.vst3, m.msg, ofs);
		return;
	}
	int n = 1;
	if (port <= 0) {
		if (g_eng.effect || g_eng.vst3)
			FlushUnitShorts(g_eng.effect, g_eng.vst3, &it, n, start, frames, 0, 0);
		if (g_eng.mirrorToB && g_eng.effectB) {
			int n1 = 1;
			FlushUnitShorts(g_eng.effectB, NULL, &it, n1, start, frames, 0, 1);
		}
	} else if (port == 1) {
		if (g_eng.effectB)
			FlushUnitShorts(g_eng.effectB, NULL, &it, n, start, frames, 0, 1);
	} else if (g_eng.effectC || g_eng.vst3C) {
		FlushUnitShorts(g_eng.effectC, g_eng.vst3C, &it, n, start, frames, 0, 2);
	}
}

static void FlushInjectQueue(__int64 start, int frames)
{
	SongOvExpire();
	{
		LONG r = g_injSxR;
		const LONG w = g_injSxW;
		while (r != w) {
			const int i = (int)(r & (INJ_SX_N - 1));
			BroadcastSongSysex(g_injSx[i], g_injSxLen[i], 0, (int)g_injSxPort[i]);
			++r;
		}
		g_injSxR = r;
	}
	LONG r = g_injR;
	const LONG w = g_injW;
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	LONGLONG q0 = 0;
	int haveQ = 0;
	while (r != w) {
		const int i = (int)(r & (SONG_INJ_CAP - 1));
		int ofs = g_injOfs[i];
		if (ofs < 0) {
			if (!haveQ) { q0 = g_injQpc[i]; haveQ = 1; }
			if (freq.QuadPart > 0)
				ofs = (int)((g_injQpc[i] - q0) * (LONGLONG)SAMPLE_RATE / freq.QuadPart);
			else
				ofs = 0;
		}
		if (ofs < 0) ofs = 0;
		if (frames > 0 && ofs >= frames) ofs = frames - 1;
		EmitSongShort((int)g_injPort[i], g_injMsg[i], start, frames, ofs);
		++r;
	}
	g_injR = r;
}

static void DispatchDueEvents(__int64 start, int frames)
{
	/* VST2 keeps only the last processEvents in a processReplacing, so this
	 * callback may call it once per unit. Mid-block flush dropped note-offs. */
	enum { SONG_BATCH = 1024, SONG_SX = 8192 };
	MidiItem batch0[SONG_BATCH], batch1[SONG_BATCH], batch2[SONG_BATCH];
	BYTE sx0[SONG_SX], sx1[SONG_SX], sx2[SONG_SX];
	int n0 = 0, n1 = 0, n2 = 0;
	int u0 = 0, u1 = 0, u2 = 0;
	const __int64 end = start + frames;
	auto pushSx = [&](int unit, const BYTE* src, int srcLen, __int64 sample, int port) {
		if (!src || srcLen <= 0) return;
		MidiItem* batch = (unit == 0) ? batch0 : (unit == 1 ? batch1 : batch2);
		int* n = (unit == 0) ? &n0 : (unit == 1 ? &n1 : &n2);
		BYTE* store = (unit == 0) ? sx0 : (unit == 1 ? sx1 : sx2);
		int* used = (unit == 0) ? &u0 : (unit == 1 ? &u1 : &u2);
		if (*n >= SONG_BATCH || *used + srcLen > SONG_SX) return;
		memcpy(store + *used, src, (size_t)srcLen);
		MidiItem it = {};
		it.msg = 0xf0;
		it.aux = (DWORD)srcLen;
		it.sample = sample;
		it.port = port;
		it.sysexOff = *used;
		*used += srcLen;
		batch[(*n)++] = it;
	};
	auto routeShort = [&](MidiItem e) {
		if ((e.msg & 0xff) == 0xff) return;
		if (g_eng.midiOut && (e.port <= 0 || !g_eng.effectB))
			midiOutShortMsg(g_eng.midiOut, e.msg);
		const int port = e.port < 0 ? 0 : e.port;
		if (port <= 0) {
			if (UnitHasPlug(0)) n0 = VstTrackPush(batch0, n0, SONG_BATCH, 0, e);
			if (g_eng.mirrorToB && UnitHasPlug(1))
				n1 = VstTrackPush(batch1, n1, SONG_BATCH, 1, e);
		} else if (port == 1) {
			if (UnitHasPlug(1)) n1 = VstTrackPush(batch1, n1, SONG_BATCH, 1, e);
		} else {
			if (UnitHasPlug(2)) n2 = VstTrackPush(batch2, n2, SONG_BATCH, 2, e);
		}
	};
	SongOvExpire();
	{
		LONG r = g_injSxR;
		const LONG w = g_injSxW;
		while (r != w) {
			const int i = (int)(r & (INJ_SX_N - 1));
			const BYTE* raw = g_injSx[i];
			const int rawLen = g_injSxLen[i];
			BYTE buf[2048];
			int units = 0, rhyBb = -1;
			const int n = PrepareSongSysex(raw, rawLen, (int)g_injSxPort[i], buf,
				(int)sizeof(buf), &units, &rhyBb);
			if (n > 0) {
				if (units & 1) pushSx(0, buf, n, start, (int)g_injSxPort[i]);
				if (units & 2) pushSx(1, buf, n, start, (int)g_injSxPort[i]);
				if (units & 4) pushSx(2, buf, n, start, (int)g_injSxPort[i]);
			}
			++r;
		}
		g_injSxR = r;
	}
	{
		LONG r = g_injR;
		const LONG w = g_injW;
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		LONGLONG q0 = 0;
		int haveQ = 0;
		while (r != w) {
			const int i = (int)(r & (SONG_INJ_CAP - 1));
			int ofs = g_injOfs[i];
			if (ofs < 0) {
				if (!haveQ) { q0 = g_injQpc[i]; haveQ = 1; }
				if (freq.QuadPart > 0)
					ofs = (int)((g_injQpc[i] - q0) * (LONGLONG)SAMPLE_RATE / freq.QuadPart);
				else
					ofs = 0;
			}
			if (ofs < 0) ofs = 0;
			if (frames > 0 && ofs >= frames) ofs = frames - 1;
			MidiItem it = {};
			it.msg = g_injMsg[i];
			it.sample = start + ofs;
			it.port = (int)g_injPort[i];
			it.sysexOff = -1;
			routeShort(it);
			++r;
		}
		g_injR = r;
	}
	while (g_eng.eventPos < g_eng.eventCount &&
		g_eng.events[g_eng.eventPos].sample < end) {
		MidiItem e = g_eng.events[g_eng.eventPos++];
		e.msg = SongOverrideMsg(e.port, e.msg);
		if ((e.msg & 0xff) == 0xff) continue;

		if ((e.msg & 0xff) == 0xf0 && e.sysexOff >= 0 && g_eng.sysexData) {
			const int rawLen = (int)e.aux;
			if (e.sysexOff + rawLen > g_eng.sysexBytes) continue;
			const BYTE* raw = g_eng.sysexData + e.sysexOff;
			BYTE buf[2048];
			int units = 0, rhyBb = -1;
			const int n = PrepareSongSysex(raw, rawLen, e.port, buf, (int)sizeof(buf),
				&units, &rhyBb);
			if (g_eng.midiOut)
				MapperSysex(g_eng.midiOut, raw, (DWORD)rawLen);
			if (n > 0) {
				if (units & 1) pushSx(0, buf, n, e.sample, e.port);
				if (units & 2) pushSx(1, buf, n, e.sample, e.port);
				if (units & 4) pushSx(2, buf, n, e.sample, e.port);
				if (rhyBb >= 0) {
					BYTE rx[11];
					if (units & 1) {
						GsBuildRxDt1(rx, 0, rhyBb);
						pushSx(0, rx, 11, e.sample, e.port);
					}
					if (units & 2) {
						GsBuildRxDt1(rx, 1, rhyBb);
						pushSx(1, rx, 11, e.sample, e.port);
					}
					if (units & 4) {
						GsBuildRxDt1(rx, 2, rhyBb);
						pushSx(2, rx, 11, e.sample, e.port);
					}
				}
				if (VstMidiSysexIsGsReset(raw, rawLen) || GsDt1IsPortReset(raw, rawLen)) {
					BYTE rhy[11];
					GsFillRhythmDt1(rhy, 0x40);
					if (units & 1) pushSx(0, rhy, 11, e.sample, e.port);
					if (units & 2) pushSx(1, rhy, 11, e.sample, e.port);
					if (units & 4) pushSx(2, rhy, 11, e.sample, e.port);
				}
			}
			if (VstMidiSysexIsXgOn(raw, rawLen)) {
				MidiItem cc0 = e, cc32 = e;
				cc0.msg = 0xb9 | (0u << 8) | (127u << 16);
				cc32.msg = 0xb9 | (32u << 8);
				routeShort(cc0);
				routeShort(cc32);
			}
			if (VstMidiSysexIsGmOn(raw, rawLen) || VstMidiSysexIsGsReset(raw, rawLen) ||
				VstMidiSysexIsXgOn(raw, rawLen)) {
				if (units & 1) n0 = VstPushCc123(batch0, n0, SONG_BATCH, 0, e.sample);
				if (units & 2) n1 = VstPushCc123(batch1, n1, SONG_BATCH, 1, e.sample);
				if (units & 4) n2 = VstPushCc123(batch2, n2, SONG_BATCH, 2, e.sample);
			}
			continue;
		}

		routeShort(e);
	}
	if (g_eng.events && g_eng.eventCount > 0 && g_eng.eventPos >= g_eng.eventCount &&
		!g_vstHeldFlushed[VstIoSlot()]) {
		g_vstHeldFlushed[VstIoSlot()] = 1;
		const __int64 offAt = (end > start) ? (end - 1) : start;
		n0 = VstFlushHeld(batch0, n0, SONG_BATCH, 0, offAt);
		n1 = VstFlushHeld(batch1, n1, SONG_BATCH, 1, offAt);
		n2 = VstFlushHeld(batch2, n2, SONG_BATCH, 2, offAt);
		n0 = VstPushCc123(batch0, n0, SONG_BATCH, 0, offAt);
		n1 = VstPushCc123(batch1, n1, SONG_BATCH, 1, offAt);
		n2 = VstPushCc123(batch2, n2, SONG_BATCH, 2, offAt);
	}
	const int sl = VstIoSlot();
	g_songT0[sl] = start;
	auto stash = [&](int u, MidiItem* b, int n, BYTE* sx, int used) {
		if (n > VST_PEND_N) n = VST_PEND_N;
		g_songN[sl][u] = n;
		if (n) memcpy(g_songEv[sl][u], b, (size_t)n * sizeof(MidiItem));
		int su = used;
		if (su > 8192) su = 8192;
		if (su > 0) memcpy(g_songSx[sl][u], sx, (size_t)su);
	};
	stash(0, batch0, n0, sx0, u0);
	stash(1, batch1, n1, sx1, u1);
	stash(2, batch2, n2, sx2, u2);
}

static void VstFeedSlice(int unit, AEffect* effect, Vst3Inst* vst3,
	__int64 t0, int done, int nfr)
{
	const int sl = VstIoSlot();
	const MidiItem* src = g_songEv[sl][unit];
	const int n = g_songN[sl][unit];
	const BYTE* sx = g_songSx[sl][unit];
	const __int64 a = t0 + done;
	const __int64 b = a + nfr;
	MidiItem tmp[VST_PEND_N];
	int nt = 0;
	for (int i = 0; i < n && nt < VST_PEND_N; ++i)
		if (src[i].sample >= a && src[i].sample < b)
			tmp[nt++] = src[i];
	if (effect && nt)
		SendVstEvents(effect, tmp, nt, a, sx, nfr, unit);
	if (vst3) {
		for (int i = 0; i < nt; ++i) {
			int off = (int)(tmp[i].sample - a);
			if (off < 0) off = 0;
			if (off >= nfr) off = nfr - 1;
			if ((tmp[i].msg & 0xff) == 0xf0 && tmp[i].sysexOff >= 0) {
				Vst3MidiSysex(vst3, sx + tmp[i].sysexOff, (int)tmp[i].aux, off);
				continue;
			}
			if ((tmp[i].msg & 0xff) == 0xf0) continue;
			Vst3MidiShort(vst3, tmp[i].msg, off);
		}
	}
}

static void RenderSongUnits(int frames)
{
	ZeroMemory(g_eng.outL, frames * sizeof(float));
	ZeroMemory(g_eng.outR, frames * sizeof(float));
	const int sl = VstIoSlot();
	const __int64 t0 = g_songT0[sl];
	for (int u = 0; u < 3; ++u)
		VstPrepareBatch(g_songEv[sl][u], g_songN[sl][u], t0, frames);
	for (int done = 0; done < frames; ) {
		int nfr = frames - done;
		if (nfr > VST_SLICE) nfr = VST_SLICE;
		VstFeedSlice(0, g_eng.effect, g_eng.vst3, t0, done, nfr);
		VstFeedSlice(1, g_eng.effectB, NULL, t0, done, nfr);
		VstFeedSlice(2, g_eng.effectC, g_eng.vst3C, t0, done, nfr);
		if (g_eng.vst3)
			Vst3Process(g_eng.vst3, g_eng.outL + done, g_eng.outR + done, nfr);
		else if (g_eng.effect)
			RenderEffect(g_eng.effect, g_eng.outL + done, g_eng.outR + done, nfr);
		if (g_eng.effectB) {
			RenderEffect(g_eng.effectB, g_eng.mixL, g_eng.mixR, nfr);
			for (int i = 0; i < nfr; ++i) {
				g_eng.outL[done + i] += g_eng.mixL[i];
				g_eng.outR[done + i] += g_eng.mixR[i];
			}
		}
		if (g_eng.effectC) {
			RenderEffect(g_eng.effectC, g_eng.mixL, g_eng.mixR, nfr);
			for (int i = 0; i < nfr; ++i) {
				g_eng.outL[done + i] += g_eng.mixL[i];
				g_eng.outR[done + i] += g_eng.mixR[i];
			}
		} else if (g_eng.vst3C) {
			ZeroMemory(g_eng.mixL, nfr * sizeof(float));
			ZeroMemory(g_eng.mixR, nfr * sizeof(float));
			Vst3Process(g_eng.vst3C, g_eng.mixL, g_eng.mixR, nfr);
			for (int i = 0; i < nfr; ++i) {
				g_eng.outL[done + i] += g_eng.mixL[i];
				g_eng.outR[done + i] += g_eng.mixR[i];
			}
		}
		done += nfr;
	}
	g_songN[sl][0] = g_songN[sl][1] = g_songN[sl][2] = 0;
}

static void VoiceMidi(DWORD msg)
{
	BYTE st = (BYTE)(msg & 0xff), type = st & 0xf0, ch = st & 15;
	BYTE note = (BYTE)((msg >> 8) & 0x7f);
	BYTE vel = (BYTE)((msg >> 16) & 0x7f);
	if (type == 0x90 && vel) {
		int pick = -1;
		float quiet = 2.0f;
		for (int i = 0; i < 32; ++i) {
			if (!g_eng.voices[i].stage) { pick = i; break; }
			if (g_eng.voices[i].env < quiet) { quiet = g_eng.voices[i].env; pick = i; }
		}
		Voice& v = g_eng.voices[pick];
		v.phase = 0;
		v.step = 6.283185307179586 * 440.0 *
			pow(2.0, ((int)note - 69) / 12.0) / SAMPLE_RATE;
		v.env = 0.001f;
		v.velocity = vel / 127.0f;
		v.note = note; v.channel = ch; v.stage = 1;
		g_eng.noteState[ch][note] = 1;
	} else if (type == 0x80 || (type == 0x90 && !vel)) {
		for (int i = 0; i < 32; ++i)
			if (g_eng.voices[i].stage && g_eng.voices[i].note == note &&
				g_eng.voices[i].channel == ch) g_eng.voices[i].stage = 3;
		g_eng.noteState[ch][note] = 0;
	} else if (type == 0xb0 && (note == 120 || note == 123)) {
		for (int i = 0; i < 32; ++i)
			if (g_eng.voices[i].channel == ch) g_eng.voices[i].stage = 3;
	}
}

// 内蔵簡易シンセ: 32 voice sine oscillator with attack/release envelope.
static void RenderBuiltin(float* l, float* r, int frames)
{
	for (int n = 0; n < frames; ++n) {
		double s = 0;
		for (int i = 0; i < 32; ++i) {
			Voice& v = g_eng.voices[i];
			if (!v.stage) continue;
			if (v.stage == 1) {
				v.env += 1.0f / 220.0f;
				if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; }
			} else if (v.stage == 3) {
				v.env *= 0.9972f;
				if (v.env < 0.0005f) { v.stage = 0; continue; }
			}
			s += sin(v.phase) * v.env * v.velocity;
			v.phase += v.step;
			if (v.phase > 6.283185307179586) v.phase -= 6.283185307179586;
		}
		float x = (float)(s * 0.10);
		// Gentle saturation prevents integer wrap with dense MIDI.
		x = x / (1.0f + (float)fabs(x));
		l[n] = r[n] = x;
	}
}

static int DrumKind(int note)
{
	if (note == 35 || note == 36) return 0;
	if (note == 37 || note == 38 || note == 39 || note == 40) return 1;
	if (note == 42 || note == 44 || note == 46) return 2;
	if (note >= 41 && note <= 50) return 3;
	if (note == 49 || note == 51 || note == 52 || note == 55 || note == 57 || note == 59)
		return 4;
	return 5;
}

static void DrumMidi(DWORD msg)
{
	BYTE st = (BYTE)(msg & 0xff), type = st & 0xf0;
	BYTE note = (BYTE)((msg >> 8) & 0x7f);
	BYTE vel = (BYTE)((msg >> 16) & 0x7f);
	if (type == 0x90 && vel) {
		if (note == 42) {
			for (int i = 0; i < 16; ++i)
				if (g_eng.drums[i].stage && g_eng.drums[i].kind == 2)
					g_eng.drums[i].env *= 0.15f;
		}
		int pick = 0;
		float quiet = 2.0f;
		for (int i = 0; i < 16; ++i) {
			if (!g_eng.drums[i].stage) { pick = i; break; }
			if (g_eng.drums[i].env < quiet) { quiet = g_eng.drums[i].env; pick = i; }
		}
		DrumV& d = g_eng.drums[pick];
		d.stage = 1;
		d.kind = DrumKind(note);
		d.note = note;
		d.env = 1.0f;
		d.vel = vel / 127.0f;
		d.phase = 0;
		d.rng = 0x1234u + (unsigned)note * 17u + (unsigned)vel;
		if (d.kind == 0) d.step = 6.283185307179586 * 58.0 / SAMPLE_RATE;
		else if (d.kind == 1) d.step = 6.283185307179586 * 185.0 / SAMPLE_RATE;
		else if (d.kind == 3) {
			double hz = 90.0 + (note - 41) * 18.0;
			d.step = 6.283185307179586 * hz / SAMPLE_RATE;
		} else d.step = 6.283185307179586 * 800.0 / SAMPLE_RATE;
	} else if (type == 0xb0 && (note == 120 || note == 123)) {
		ZeroMemory(g_eng.drums, sizeof(g_eng.drums));
	}
}

static float DrumNoise(DrumV& d)
{
	d.rng = d.rng * 1103515245u + 12345u;
	return ((int)((d.rng >> 16) & 0x7fff) / 16384.0f) - 1.0f;
}

static void RenderDrums(float* l, float* r, int frames)
{
	for (int n = 0; n < frames; ++n) {
		double s = 0;
		for (int i = 0; i < 16; ++i) {
			DrumV& d = g_eng.drums[i];
			if (!d.stage) continue;
			float nse = DrumNoise(d);
			float tone = (float)sin(d.phase);
			d.phase += d.step;
			if (d.phase > 6.283185307179586) d.phase -= 6.283185307179586;
			float x = 0;
			if (d.kind == 0) {
				x = tone * d.env + nse * 0.18f * d.env;
				d.env *= 0.9992f;
			} else if (d.kind == 1) {
				x = nse * d.env * 0.85f + tone * d.env * 0.25f;
				d.env *= 0.9984f;
			} else if (d.kind == 2) {
				x = nse * d.env * ((d.note == 46) ? 0.55f : 0.40f);
				d.env *= (d.note == 46) ? 0.9978f : 0.9920f;
			} else if (d.kind == 3) {
				x = tone * d.env * 0.85f + nse * 0.08f * d.env;
				d.env *= 0.9988f;
			} else if (d.kind == 4) {
				x = nse * d.env * 0.70f;
				d.env *= 0.9994f;
			} else {
				x = nse * d.env * 0.50f;
				d.env *= 0.9965f;
			}
			s += x * d.vel;
			if (d.env < 0.0015f) d.stage = 0;
		}
		float y = (float)(s * 0.22);
		y = y / (1.0f + fabsf(y));
		l[n] = r[n] = y;
	}
}

static void LiveDllDirFromEffect(AEffect* e)
{
	const char* dirA = VstPlugDirFor(e);
	if (!dirA || !dirA[0]) return;
	wchar_t dirW[MAX_PATH];
	if (MultiByteToWideChar(CP_ACP, 0, dirA, -1, dirW, MAX_PATH) > 0 && dirW[0])
		SetDllDirectoryW(dirW);
}

static void RenderEffect(AEffect* e, float* l, float* r, int frames)
{
	LiveDllDirFromEffect(e);
	float* in[32], *out[32];
	for (int i = 0; i < 32; ++i) {
		in[i] = g_eng.zero;
		out[i] = g_eng.zero;
	}
	out[0] = l; out[1] = r;
	ZeroMemory(l, frames * sizeof(float));
	ZeroMemory(r, frames * sizeof(float));
	ZeroMemory(g_eng.zero, frames * sizeof(float));
	if (!e || !e->processReplacing) return;
	__try { e->processReplacing(e, in, out, frames); }
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ZeroMemory(l, frames * sizeof(float));
		ZeroMemory(r, frames * sizeof(float));
	}
	if (e->numOutputs == 1) memcpy(r, l, frames * sizeof(float));
}

static int LoadVst2(const wchar_t* path, HMODULE& module, AEffect*& effect)
{
	// SC-VA.dll is a tiny stub that LoadLibrary("SCCore.dll") at runtime.
	// Without the plugin directory on the DLL search path, SCCore fails and
	// the effect stays silent while still returning a valid AEffect*.
	wchar_t plugDir[VST_PATH_CHARS];
	SafeCopy(plugDir, VST_PATH_CHARS, path);
	wchar_t* slash = wcsrchr(plugDir, L'\\');
	if (slash) *slash = 0; else plugDir[0] = 0;
	wchar_t oldCurDir[MAX_PATH] = {};
	GetCurrentDirectoryW(MAX_PATH, oldCurDir);
	// Two different lookups have to succeed: LoadLibrary("SCCore.dll") from
	// inside the stub, which follows the DLL search path, and any relative
	// file the plug-in opens during init, which follows the current directory.
	wchar_t savedDllDir[MAX_PATH] = {};
	GetDllDirectoryW(MAX_PATH, savedDllDir);
	if (plugDir[0]) {
		SetDllDirectoryW(plugDir);
		SetCurrentDirectoryW(plugDir);
	}

	module = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!module) {
		EnsLog(L"LoadVst2 FAIL LoadLibrary err=%lu path=%s",
			GetLastError(), path);
		if (oldCurDir[0]) SetCurrentDirectoryW(oldCurDir);
		SetDllDirectoryW(savedDllDir[0] ? savedDllDir : NULL);
		return 0;
	}
	VSTPluginMainProc proc = (VSTPluginMainProc)GetProcAddress(module, "VSTPluginMain");
	if (!proc) proc = (VSTPluginMainProc)GetProcAddress(module, "main");
	if (!proc) {
		EnsLog(L"LoadVst2 FAIL no VSTPluginMain path=%s", path);
		FreeLibrary(module); module = NULL;
		if (oldCurDir[0]) SetCurrentDirectoryW(oldCurDir);
		SetDllDirectoryW(savedDllDir[0] ? savedDllDir : NULL);
		return 0;
	}
	DWORD seh = 0;
	VstPlugDirSet(path);
	__try { effect = proc(HostCallback); }
	__except (seh = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) { effect = NULL; }
	if (!effect || effect->magic != kEffectMagic || !effect->dispatcher ||
		!effect->processReplacing) {
		EnsLog(L"LoadVst2 FAIL entry effect=%p seh=0x%08X path=%s",
			(void*)effect, seh, path);
		if (effect && effect->dispatcher)
			__try { effect->dispatcher(effect, effClose, 0, 0, NULL, 0); }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		FreeLibrary(module); module = NULL; effect = NULL;
		if (oldCurDir[0]) SetCurrentDirectoryW(oldCurDir);
		SetDllDirectoryW(savedDllDir[0] ? savedDllDir : NULL);
		return 0;
	}
	// Bound before effOpen: that is where an instrument reads its tone data.
	VstPlugDirBind(effect);
	__try {
		effect->dispatcher(effect, effOpen, 0, 0, NULL, 0);
		effect->dispatcher(effect, effSetSampleRate, 0, 0, NULL, (float)SAMPLE_RATE);
		effect->dispatcher(effect, effSetBlockSize, 0, BLOCK_FRAMES, NULL, 0);
		if (effect->numOutputs > 0)
			effect->dispatcher(effect, effConnectOutput, 0, 1, NULL, 0);
		if (effect->numOutputs > 1)
			effect->dispatcher(effect, effConnectOutput, 1, 1, NULL, 0);
		effect->dispatcher(effect, effMainsChanged, 0, 1, NULL, 0);
		effect->dispatcher(effect, effStartProcess, 0, 0, NULL, 0);
	}
	__except (seh = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
		EnsLog(L"LoadVst2 FAIL init seh=0x%08X path=%s", seh, path);
		VstPlugDirUnbind(effect);
		FreeLibrary(module); module = NULL; effect = NULL;
		if (oldCurDir[0]) SetCurrentDirectoryW(oldCurDir);
		SetDllDirectoryW(savedDllDir[0] ? savedDllDir : NULL);
		return 0;
	}
	// Keep this plug's folder on the DLL search path. SC-VA delay-loads
	// SCCore; CloseEffect must not clear it while any live instance remains.
	EnsLog(L"LoadVst2 OK path=%s ins=%d outs=%d flags=0x%X uid=0x%08X",
		path, effect->numInputs, effect->numOutputs, (unsigned)effect->flags,
		(unsigned)effect->uniqueID);
	if (oldCurDir[0]) SetCurrentDirectoryW(oldCurDir);
	return 1;
}

#ifdef KPIHOST64_BUILD
static volatile LONG g_liveAbandonPlugins = 0;
extern "C" void VstLiveAbandonHostPlugins(int on)
{
	InterlockedExchange(&g_liveAbandonPlugins, on ? 1 : 0);
}
#else
extern "C" void VstLiveAbandonHostPlugins(int) {}
#endif

static void CloseEffect(HMODULE& module, AEffect*& effect)
{
#ifdef KPIHOST64_BUILD
	// SOUND Canvas VA's effClose / FreeLibrary can never return. Dropping the
	// pointers leaks the module until this process exits, which is how the
	// pipe stays able to open a KPI file after the live host window closes.
	if (InterlockedCompareExchange(&g_liveAbandonPlugins, 0, 0)) {
		effect = NULL;
		module = NULL;
		return;
	}
#endif
	if (effect && effect->dispatcher) {
		__try {
			effect->dispatcher(effect, effStopProcess, 0, 0, NULL, 0);
			effect->dispatcher(effect, effMainsChanged, 0, 0, NULL, 0);
			effect->dispatcher(effect, effClose, 0, 0, NULL, 0);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	VstPlugDirUnbind(effect);
	effect = NULL;
	if (module) FreeLibrary(module);
	module = NULL;
	// Do not SetDllDirectoryW(NULL) here. SC-VA delay-loads SCCore from its
	// folder; clearing the search path while another live instance is still
	// open leaves that instance silent until the host is closed and reopened.
}

static int ContainsI(const wchar_t* text, const wchar_t* needle)
{
	if (!text || !needle || !*needle) return 0;
	const size_t n = wcslen(needle);
	for (; *text; ++text)
		if (_wcsnicmp(text, needle, n) == 0) return 1;
	return 0;
}

static int PathLooksLikeScVa(const wchar_t* path)
{
	return ContainsI(path, L"SOUND Canvas") || ContainsI(path, L"SC-VA") ||
		ContainsI(path, L"SCVA") || ContainsI(path, L"SOUNDCanvas");
}

static int DetectMultiTimbralName(const wchar_t* text)
{
	if (!text || !*text) return 0;
	static const wchar_t* keys[] = {
		L"SOUND Canvas VA", L"SOUNDCanvas VA", L"SoundCanvas VA",
		L"SC-VA", L"SCVA", L"SC8820", L"SC-8820", L"SC-88", L"SC88",
		L"SGP2", L"MidRadio", L"S-YXG50", L"SYXG50", L"S-YXG", L"YXG50",
		L"VSTSynthFont", L"SynthFont", L"VirtualMIDISynth",
		L"MultiTimbral", L"Multi-Timbral", L"multitimbral",
		L"GS SoftSynth", L"Roland SC", L"Canvas VA"
	};
	for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); ++i)
		if (ContainsI(text, keys[i])) return 1;
	return 0;
}

static int LiveIsMultiPath(const wchar_t* pluginPath)
{
	if (DetectMultiTimbralName(pluginPath)) return 1;
	for (int i = 0; i < g_pluginCount; ++i)
		if (g_plugins[i].isMultiTimbral && pluginPath &&
			!_wcsicmp(g_plugins[i].path, pluginPath))
			return 1;
	return 0;
}

static void RescoreMultiFlags(void)
{
	for (int i = 0; i < g_pluginCount; ++i) {
		VstPluginInfo& p = g_plugins[i];
		p.isMultiTimbral =
			(DetectMultiTimbralName(p.name) || DetectMultiTimbralName(p.path)) ? 1 : 0;
		// Cross-arch multi (x86 app + x64 SC-VA) stays pickable for KpiHost64.
		// Do not revive a copy that verify already hid (failed drop / duplicate).
		if (p.isMultiTimbral && p.arch != HostArch() &&
			!(p.isLiveOk && p.isAudible == 0))
			p.isInstrument = 1;
	}
}

static int PluginScore(const VstPluginInfo& p,
	const wchar_t hints[][128], int hintCount)
{
	if (!p.isInstrument || p.arch != HostArch()) return -100000;
	int score = p.isVst3 ? -100 : 0;
	if (p.isMultiTimbral) score += 2000;
	if (savedata.vstMultiDll[0] &&
		(_wcsicmp(p.path, savedata.vstMultiDll) == 0 ||
		 ContainsI(p.path, savedata.vstMultiDll) ||
		 ContainsI(savedata.vstMultiDll, p.path)))
		score += 5000;
	if (savedata.vstExtraPath[0] &&
		(_wcsicmp(p.path, savedata.vstExtraPath) == 0 ||
		 ContainsI(p.path, savedata.vstExtraPath) ||
		 ContainsI(savedata.vstExtraPath, p.path)))
		score += 5000;
	if (savedata.vstMultiName[0] && ContainsI(p.name, savedata.vstMultiName))
		score += 1500;
	static const wchar_t* pref[] = {
		L"SOUND Canvas VA", L"SC-VA", L"SGP2", L"SC8820", L"SC-88",
		L"S-YXG50", L"SynthFont", L"YXG50", L"General MIDI",
		L"MT-32", L"S-YXG", L"Timidity", L"Bassmidi", L"Canvas VA"
	};
	for (int i = 0; i < (int)(sizeof(pref) / sizeof(pref[0])); ++i)
		if (ContainsI(p.name, pref[i]) || ContainsI(p.path, pref[i]))
			score += 200 - i;
	for (int i = 0; i < hintCount; ++i) {
		if (ContainsI(p.name, hints[i]) || ContainsI(hints[i], p.name))
			score += 500;
		else {
			const wchar_t* q = hints[i];
			while (*q) {
				wchar_t word[32] = {};
				int n = 0;
				while (*q && !iswalnum(*q)) ++q;
				while (*q && iswalnum(*q) && n < 31) word[n++] = *q++;
				if (n >= 4 && ContainsI(p.name, word)) score += 25;
			}
		}
	}
	return score;
}


static int GmProgramFamily(int pc)
{
	if (pc < 0) pc = 0;
	pc &= 127;
	if (pc <= 7) return FAM_PIANO;
	if (pc <= 15) return FAM_CHROME;
	if (pc <= 23) return FAM_ORGAN;
	if (pc <= 31) return FAM_GUITAR;
	if (pc <= 39) return FAM_BASS;
	if (pc <= 47) return FAM_STRINGS;
	if (pc <= 55) return FAM_ENSEMBLE;
	if (pc <= 63) return FAM_BRASS;
	if (pc <= 71) return FAM_REED;
	if (pc <= 79) return FAM_PIPE;
	if (pc <= 87) return FAM_LEAD;
	if (pc <= 95) return FAM_PAD;
	if (pc <= 103) return FAM_FX;
	if (pc <= 111) return FAM_ETHNIC;
	if (pc <= 119) return FAM_PERC;
	return FAM_SFX;
}

static int ClassifyPlugFamily(const wchar_t* name, const wchar_t* path)
{
	wchar_t t[VST_PATH_CHARS * 2];
	t[0] = 0;
	if (name) wcsncpy_s(t, name, _TRUNCATE);
	if (path) {
		wcscat_s(t, L" ");
		wcscat_s(t, path);
	}
	struct KeyFam { const wchar_t* key; int fam; };
	static const KeyFam keys[] = {
		{ L"drum", FAM_DRUM }, { L"kit", FAM_DRUM }, { L"percussion", FAM_DRUM },
		{ L"battery", FAM_DRUM }, { L"groove", FAM_DRUM }, { L"addictive", FAM_DRUM },
		{ L"powerdrum", FAM_DRUM }, { L"mt-power", FAM_DRUM },
		{ L"bfd", FAM_DRUM }, { L"superior", FAM_DRUM }, { L"ezdrummer", FAM_DRUM },
		{ L"strik", FAM_DRUM }, { L"steven slate", FAM_DRUM },
		{ L"piano", FAM_PIANO }, { L"grandeur", FAM_PIANO }, { L"the gentleman", FAM_PIANO },
		{ L"the realist", FAM_PIANO }, { L"keyscape", FAM_PIANO }, { L"electric piano", FAM_PIANO },
		{ L"rhodes", FAM_PIANO }, { L"wurli", FAM_PIANO }, { L"epiano", FAM_PIANO },
		{ L"marimba", FAM_CHROME }, { L"vibraphone", FAM_CHROME }, { L"xylophone", FAM_CHROME },
		{ L"glockenspiel", FAM_CHROME }, { L"celesta", FAM_CHROME }, { L"music box", FAM_CHROME },
		{ L"organ", FAM_ORGAN }, { L"hammond", FAM_ORGAN }, { L"b3", FAM_ORGAN },
		{ L"guitar", FAM_GUITAR }, { L"strat", FAM_GUITAR }, { L"les paul", FAM_GUITAR },
		{ L"bass", FAM_BASS }, { L"trilian", FAM_BASS },
		{ L"string", FAM_STRINGS }, { L"violin", FAM_STRINGS }, { L"cello", FAM_STRINGS },
		{ L"viola", FAM_STRINGS }, { L"orchestra", FAM_ENSEMBLE }, { L"choir", FAM_ENSEMBLE },
		{ L"ensemble", FAM_ENSEMBLE }, { L"padshop", FAM_PAD },
		{ L"brass", FAM_BRASS }, { L"trumpet", FAM_BRASS }, { L"trombone", FAM_BRASS },
		{ L"horn", FAM_BRASS }, { L"sax", FAM_REED }, { L"clarinet", FAM_REED },
		{ L"oboe", FAM_REED }, { L"flute", FAM_PIPE }, { L"recorder", FAM_PIPE },
		{ L"lead", FAM_LEAD }, { L"retrologue", FAM_LEAD }, { L"synth", FAM_LEAD },
		{ L"pad", FAM_PAD }, { L"ambient", FAM_PAD },
		{ L"fx", FAM_FX }, { L"effect", FAM_FX },
		{ L"ethnic", FAM_ETHNIC }, { L"world", FAM_ETHNIC }, { L"sitar", FAM_ETHNIC },
		{ L"halion", FAM_GENERIC }, { L"kontakt", FAM_GENERIC }, { L"sampler", FAM_GENERIC },
		{ L"play", FAM_GENERIC }, { L"omnisphere", FAM_GENERIC },
	};
	for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); ++i)
		if (ContainsI(t, keys[i].key)) return keys[i].fam;
	return FAM_GENERIC;
}

static int NameOrPathHas(const wchar_t* name, const wchar_t* path, const wchar_t* key)
{
	return ContainsI(name, key) || ContainsI(path, key);
}

static int IsBundledToySynth(const wchar_t* name, const wchar_t* path)
{
	static const wchar_t* toys[] = {
		L"Padshop", L"Retrologue", L"Prologue", L"LoopMash", L"Trip"
	};
	for (int i = 0; i < (int)(sizeof(toys) / sizeof(toys[0])); ++i)
		if (NameOrPathHas(name, path, toys[i])) return 1;
	return 0;
}

static int IsRomplerName(const wchar_t* name, const wchar_t* path)
{
	static const wchar_t* keys[] = {
		L"HALion", L"SampleTank", L"Kontakt", L"Omnisphere", L"Sforzando",
		L"Independence", L"Falcon", L"Engine 2", L"HALion Sonic"
	};
	for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); ++i)
		if (NameOrPathHas(name, path, keys[i])) return 1;
	return 0;
}

static int IsDrumPlugName(const wchar_t* name, const wchar_t* path)
{
	static const wchar_t* keys[] = {
		L"Groove Agent", L"Battery", L"Addictive Drums", L"Addictive",
		L"BFD", L"Superior Drummer", L"EZdrummer", L"Steven Slate",
		L"MT-Power", L"PowerDrum"
	};
	for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); ++i)
		if (NameOrPathHas(name, path, keys[i])) return 1;
	return 0;
}

// Romplers and kit players come up with an empty slot and expose no usable
// factory program, so they stay silent until the user picks a patch in the
// plug-in's own browser. Forcing program 0 or waiting a second of digital
// silence during D&D / scan only burns time (HALion 7, Kontakt, …).
static int NeedsUserPatch(const wchar_t* name, const wchar_t* path)
{
	static const wchar_t* keys[] = {
		L"Groove Agent", L"Battery", L"BFD", L"Addictive Drums",
		L"Superior Drummer", L"EZdrummer", L"MT-PowerDrumKit"
	};
	if (IsRomplerName(name, path)) return 1;
	for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); ++i)
		if (NameOrPathHas(name, path, keys[i])) return 1;
	return 0;
}

static int IsFactoryRompler(const wchar_t* name, const wchar_t* path)
{
	return NameOrPathHas(name, path, L"HALion Sonic") ||
		NameOrPathHas(name, path, L"HALionSonic") ||
		NameOrPathHas(name, path, L"SampleTank");
}

// Program lists bigger than a GM bank are a library browser, not a factory
// patch the host can safely load on drop.
enum { LIVE_FACTORY_PROG_MAX = 256 };

static int IsFxNotInstrument(const wchar_t* name, const wchar_t* path)
{
	static const wchar_t* fx[] = {
		L"Graillon", L"SpectraLayers", L"Replika", L"Supercharger",
		L"Pro-Q", L"Ozone", L"Imager", L"Vinyl", L"Raum", L"Trash",
		L"Guitar Rig", L"qt", L"qgif", L"qjpeg", L"qsvg", L"qico",
		L"Cubase Plug-in Set", L"Omnivocal", L"SINE Player",
		L"VST Renderer", L"vintage-plugins", L"ModScripter",
		L"vstambiconverter"
	};
	for (int i = 0; i < (int)(sizeof(fx) / sizeof(fx[0])); ++i)
		if (ContainsI(name, fx[i]) || ContainsI(path, fx[i])) return 1;
	return 0;
}

static int ScorePlugForFamily(const VstPluginInfo& p, int fam)
{
	if (!p.isInstrument || p.arch != HostArch()) return -100000;
	if (p.isMultiTimbral) return -100000; // multi handled separately
	if (IsFxNotInstrument(p.name, p.path)) return -100000;
	const int got = ClassifyPlugFamily(p.name, p.path);
	int s = 10;
	if (p.isVst3) s -= 20;
	if (got == fam) s += 500;
	else if (fam == FAM_DRUM && got != FAM_DRUM)
		return -100000;
	else if (got == FAM_GENERIC) s += 40;
	else if (fam == FAM_PAD && got == FAM_LEAD) s += 80;
	else if (fam == FAM_LEAD && got == FAM_PAD) s += 60;
	else if (fam == FAM_ENSEMBLE && (got == FAM_STRINGS || got == FAM_PAD)) s += 120;
	else if (fam == FAM_STRINGS && got == FAM_ENSEMBLE) s += 120;
	else if (fam == FAM_SFX && got == FAM_FX) s += 100;
	else if (got != fam) s -= 50;
	if (ContainsI(p.name, L"HALion") || ContainsI(p.path, L"HALion")) s += 80;
	if (ContainsI(p.name, L"SampleTank") || ContainsI(p.path, L"SampleTank")) s += 70;
	if (ContainsI(p.name, L"Kontakt")) s += 30;
	if (IsBundledToySynth(p.name, p.path)) s -= 400;
	if (IsDrumPlugName(p.name, p.path) && fam == FAM_DRUM) s += 200;
	return s;
}

static const wchar_t* GmProgramName(int pc)
{
	static const wchar_t* n[128] = {
		L"Acoustic Grand Piano", L"Bright Acoustic Piano", L"Electric Grand Piano",
		L"Honky-tonk Piano", L"Electric Piano 1", L"Electric Piano 2", L"Harpsichord", L"Clavinet",
		L"Celesta", L"Glockenspiel", L"Music Box", L"Vibraphone", L"Marimba", L"Xylophone",
		L"Tubular Bells", L"Dulcimer",
		L"Drawbar Organ", L"Percussive Organ", L"Rock Organ", L"Church Organ", L"Reed Organ",
		L"Accordion", L"Harmonica", L"Tango Accordion",
		L"Nylon Guitar", L"Steel Guitar", L"Jazz Guitar", L"Clean Guitar", L"Muted Guitar",
		L"Overdriven Guitar", L"Distortion Guitar", L"Guitar Harmonics",
		L"Acoustic Bass", L"Finger Bass", L"Pick Bass", L"Fretless Bass", L"Slap Bass 1",
		L"Slap Bass 2", L"Synth Bass 1", L"Synth Bass 2",
		L"Violin", L"Viola", L"Cello", L"Contrabass", L"Tremolo Strings", L"Pizzicato Strings",
		L"Orchestral Harp", L"Timpani",
		L"String Ensemble 1", L"String Ensemble 2", L"Synth Strings 1", L"Synth Strings 2",
		L"Choir Aahs", L"Voice Oohs", L"Synth Choir", L"Orchestra Hit",
		L"Trumpet", L"Trombone", L"Tuba", L"Muted Trumpet", L"French Horn", L"Brass Section",
		L"Synth Brass 1", L"Synth Brass 2",
		L"Soprano Sax", L"Alto Sax", L"Tenor Sax", L"Baritone Sax", L"Oboe", L"English Horn",
		L"Bassoon", L"Clarinet",
		L"Piccolo", L"Flute", L"Recorder", L"Pan Flute", L"Blown Bottle", L"Shakuhachi",
		L"Whistle", L"Ocarina",
		L"Square Lead", L"Saw Lead", L"Calliope", L"Chiff Lead", L"Charang", L"Voice Lead",
		L"Fifths Lead", L"Bass Lead",
		L"New Age Pad", L"Warm Pad", L"Polysynth Pad", L"Choir Pad", L"Bowed Pad",
		L"Metallic Pad", L"Halo Pad", L"Sweep Pad",
		L"Rain FX", L"Soundtrack", L"Crystal", L"Atmosphere", L"Brightness", L"Goblins",
		L"Echoes", L"Sci-Fi",
		L"Sitar", L"Banjo", L"Shamisen", L"Koto", L"Kalimba", L"Bag pipe", L"Fiddle", L"Shanai",
		L"Tinkle Bell", L"Agogo", L"Steel Drums", L"Woodblock", L"Taiko", L"Melodic Tom",
		L"Synth Drum", L"Reverse Cymbal",
		L"Fret Noise", L"Breath Noise", L"Seashore", L"Bird Tweet", L"Telephone", L"Helicopter",
		L"Applause", L"Gunshot"
	};
	return n[pc & 127];
}

static int IsDummyProgName(const wchar_t* name)
{
	if (!name || !*name) return 1;
	if (ContainsI(name, L"MIDI Channel")) return 1;
	if (_wcsnicmp(name, L"Program ", 8) == 0) {
		const wchar_t* p = name + 8;
		if (*p) {
			int digits = 1;
			for (; *p; ++p) {
				if (*p < L'0' || *p > L'9') { digits = 0; break; }
			}
			if (digits) return 1;
		}
	}
	if (_wcsicmp(name, L"Default") == 0 || _wcsicmp(name, L"Init") == 0)
		return 1;
	return 0;
}

static int FamilyCompat(int plugFam, int wantFam, int isDrum, int rompler, int drumPlug, int toy)
{
	if (toy) return 5;
	if (isDrum) {
		if (drumPlug) return 900;
		if (rompler) return 420;
		if (plugFam == FAM_DRUM || plugFam == FAM_PERC) return 700;
		return 20;
	}
	if (rompler) {
		if (plugFam == wantFam) return 500;
		return 380;
	}
	if (plugFam == wantFam) return 220;
	if (plugFam == FAM_GENERIC) return 90;
	return 40;
}

static int ScoreNameForTarget(const wchar_t* name, int gmPc, int isDrum)
{
	if (!name || !*name) return 0;
	int s = 0;
	if (isDrum) {
		if (ContainsI(name, L"kit") || ContainsI(name, L"drum") ||
			ContainsI(name, L"perc") || ContainsI(name, L"groove") ||
			ContainsI(name, L"battery")) s += 500;
		if (ContainsI(name, L"standard") || ContainsI(name, L"acoustic") ||
			ContainsI(name, L"gm")) s += 80;
		if (ContainsI(name, L"piano") || ContainsI(name, L"pad") ||
			ContainsI(name, L"lead") || ContainsI(name, L"organ")) s -= 250;
		return s;
	}
	const wchar_t* gm = GmProgramName(gmPc);
	if (ContainsI(name, gm)) s += 1000;
	const int fam = GmProgramFamily(gmPc);
	static const wchar_t* famKey[FAM_COUNT] = {
		L"kit", L"piano", L"vibe", L"organ", L"guitar", L"bass",
		L"string", L"choir", L"brass", L"sax", L"flute", L"lead",
		L"pad", L"fx", L"sitar", L"perc", L"noise", L""
	};
	if (famKey[fam][0] && ContainsI(name, famKey[fam])) s += 220;
	if (ContainsI(name, L"kit") || ContainsI(name, L"drum")) s -= 280;
	return s;
}

static int PickProgramForTarget(Vst3Inst* v, int gmPc, int isDrum)
{
	if (!v) return 0;
	const int n = Vst3ProgramCount(v);
	if (n <= 0) return 0;
	int best = isDrum ? 0 : ((gmPc < n) ? gmPc : (gmPc % n));
	int bestS = (n >= 128 && !isDrum) ? 250 : 20;
	const int lim = n > 512 ? 512 : n;
	for (int i = 0; i < lim; ++i) {
		wchar_t nm[128];
		if (!Vst3ProgramName(v, i, nm, 128)) continue;
		if (IsDummyProgName(nm)) continue;
		int s = ScoreNameForTarget(nm, gmPc, isDrum);
		if (s > bestS) { bestS = s; best = i; }
	}
	return best;
}

static int MakeVst3Audible(Vst3Inst* v, int drums, int gmPc)
{
	if (!Vst3IsOk(v)) return 0;
	if (ProbeLoadedVst3Audible(v, drums)) return 1;
	const int n = Vst3ProgramCount(v);
	int cand[40];
	int nc = 0;
	auto add = [&](int x) {
		if (x < 0 || (n > 0 && x >= n)) return;
		for (int i = 0; i < nc; ++i) if (cand[i] == x) return;
		if (nc < 40) cand[nc++] = x;
	};
	add(PickProgramForTarget(v, gmPc, drums));
	add(0);
	if (n > 1) add(1);
	if (!drums && gmPc < n) add(gmPc);
	if (n > 0) {
		const int lim = n > 256 ? 256 : n;
		for (int i = 0; i < lim && nc < 24; ++i) {
			wchar_t nm[128];
			if (!Vst3ProgramName(v, i, nm, 128)) continue;
			if (ScoreNameForTarget(nm, gmPc, drums) >= 400) add(i);
		}
	}
	for (int k = 0; k < nc; ++k) {
		Vst3SetProgram(v, cand[k]);
		Vst3Process(v, g_eng.outL, g_eng.outR, BLOCK_FRAMES);
		if (ProbeLoadedVst3Audible(v, drums)) return 1;
	}
	return 0;
}

static void LogVst3Programs(Vst3Inst* v, const wchar_t* tag)
{
	if (!v) return;
	const int n = Vst3ProgramCount(v);
	const int lim = n > 16 ? 16 : n;
	EnsLog(L"%s programs=%d midiCh=%d", tag, n, Vst3MidiChannels(v));
	for (int i = 0; i < lim; ++i) {
		wchar_t nm[128] = {};
		Vst3ProgramName(v, i, nm, 128);
		EnsLog(L"  prog[%d] %s", i, nm);
	}
}

static void SettleVst3(Vst3Inst* v)
{
	if (!Vst3IsOk(v)) return;
	for (int i = 0; i < 16; ++i)
		Vst3Process(v, g_eng.outL, g_eng.outR, BLOCK_FRAMES);
	Sleep(100);
	MSG m;
	while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&m);
		DispatchMessage(&m);
	}
}

static void CloseMixSlots()
{
	for (int i = 0; i < MIX_SLOTS; ++i) {
		CloseEffect(g_eng.mix[i].module, g_eng.mix[i].effect);
		Vst3Close(g_eng.mix[i].vst3);
		g_eng.mix[i].vst3 = NULL;
		g_eng.mix[i].family = FAM_GENERIC;
		g_eng.mix[i].vstProg = 0;
		g_eng.mix[i].keepMidiCh = 0;
		g_eng.mix[i].path[0] = 0;
	}
	g_eng.mixCount = 0;
	g_eng.useEnsemble = 0;
	g_eng.useDrums = 0;
	g_eng.usingBuiltin = 0;
	for (int c = 0; c < 16; ++c) g_eng.chSlot[c] = -1;
}

static int TryOpenMixPath(MixSlot& slot, const wchar_t* path, int isVst3,
	int drums, int gmPc, int vstProg)
{
	CloseEffect(slot.module, slot.effect);
	Vst3Close(slot.vst3); slot.vst3 = NULL;
	slot.vstProg = 0;
	slot.keepMidiCh = 0;
	slot.path[0] = 0;
	if (!path || !*path) return 0;
	if (isVst3 || EqExt(path, L".vst3")) {
		slot.vst3 = Vst3Open(path);
		if (!Vst3IsOk(slot.vst3)) {
			EnsLog(L"ensemble Vst3Open FAIL %s (%s)", path,
				Vst3LastError() ? Vst3LastError() : L"?");
			Vst3Close(slot.vst3); slot.vst3 = NULL; return 0;
		}
		int useProg = vstProg;
		if (useProg < 0)
			useProg = PickProgramForTarget(slot.vst3, gmPc, drums);
		Vst3SetProgram(slot.vst3, useProg);
		Vst3Process(slot.vst3, g_eng.outL, g_eng.outR, BLOCK_FRAMES);
		const int keepSilent = IsRomplerName(path, path) || IsDrumPlugName(path, path);
		if (keepSilent) SettleVst3(slot.vst3);
		if (!MakeVst3Audible(slot.vst3, drums, gmPc)) {
			if (!keepSilent) {
				EnsLog(L"ensemble silent VST3 drop %s prog=%d", path, useProg);
				Vst3Close(slot.vst3); slot.vst3 = NULL;
				return 0;
			}
			EnsLog(L"ensemble silent VST3 keep (rompler/kit) %s prog=%d", path, useProg);
		}
		slot.vstProg = useProg;
		slot.keepMidiCh = (Vst3MidiChannels(slot.vst3) >= 16) ? 1 : 0;
		wcsncpy_s(slot.path, path, _TRUNCATE);
		return 1;
	}
	if (PeArch(path) != HostArch()) return 0;
	if (!LoadVst2(path, slot.module, slot.effect)) return 0;
	if (!ProbeLoadedEffectAudible(slot.effect)) {
		EnsLog(L"ensemble silent VST2 drop %s", path);
		CloseEffect(slot.module, slot.effect);
		return 0;
	}
	wcsncpy_s(slot.path, path, _TRUNCATE);
	return 1;
}

static int RomplerPickScore(const VstPluginInfo& p)
{
	if (!p.isInstrument || p.arch != HostArch()) return -1;
	if (IsFxNotInstrument(p.name, p.path)) return -1;
	if (IsBundledToySynth(p.name, p.path)) return -1;
	if (IsDrumPlugName(p.name, p.path)) return -1;
	if (ContainsI(p.name, L"HALion Sonic")) return 900;
	if (ContainsI(p.name, L"SampleTank")) return 850;
	if (ContainsI(p.name, L"HALion")) return 800;
	if (ContainsI(p.name, L"Kontakt")) return 700;
	if (ContainsI(p.name, L"Omnisphere")) return 680;
	if (ContainsI(p.name, L"Sforzando")) return 650;
	if (IsRomplerName(p.name, p.path)) return 500;
	return -1;
}

static int DrumPickScore(const VstPluginInfo& p)
{
	if (!p.isInstrument || p.arch != HostArch()) return -1;
	if (IsFxNotInstrument(p.name, p.path)) return -1;
	if (IsBundledToySynth(p.name, p.path)) return -1;
	if (ContainsI(p.name, L"Groove Agent") && !ContainsI(p.name, L"SE"))
		return 950;
	if (ContainsI(p.name, L"Groove Agent")) return 900;
	if (IsDrumPlugName(p.name, p.path)) return 800;
	if (IsRomplerName(p.name, p.path)) return 250;
	return -1;
}

static void ApplyGmPrograms(MixSlot& slot, const int* prog, const int* usedCh, int skipDrum)
{
	if (!slot.vst3) return;
	for (int ch = 0; ch < 16; ++ch) {
		if (!usedCh[ch]) continue;
		if (skipDrum && ch == 9) continue;
		const int drums = (ch == 9);
		int pick = PickProgramForTarget(slot.vst3, drums ? 0 : prog[ch], drums);
		if (drums && pick == 0) {
			wchar_t nm[128] = {};
			if (Vst3ProgramName(slot.vst3, 0, nm, 128) && IsDummyProgName(nm))
				continue;
		}
		Vst3SetChannelProgram(slot.vst3, ch, pick);
		wchar_t nm[128] = {};
		Vst3ProgramName(slot.vst3, pick, nm, 128);
		EnsLog(L"  map ch%d gm=%d -> vstProg=%d [%s]", ch + 1, prog[ch], pick, nm);
	}
	SettleVst3(slot.vst3);
}

static int FinishEnsembleOk(const int* usedCh, const int* prog)
{
	if (g_eng.mixCount <= 0) return 0;
	g_eng.useEnsemble = 1;
	g_eng.usingBuiltin = 0;
	g_eng.useDrums = 0;
	g_ensLogBlocks = 0;
	EnsLog(L"BuildEnsemble OK mixCount=%d", g_eng.mixCount);
	for (int ch = 0; ch < 16; ++ch) {
		if (!usedCh[ch]) continue;
		EnsLog(L"  ch%d prog=%d -> slot=%d", ch + 1, prog[ch], g_eng.chSlot[ch]);
	}
	return 1;
}

static int BuildEnsembleFromSong()
{
	CloseMixSlots();
	if (!g_eng.events || g_eng.eventCount <= 0) return 0;

	int usedCh[16] = {};
	int prog[16];
	for (int i = 0; i < 16; ++i) prog[i] = 0;
	for (int i = 0; i < g_eng.eventCount; ++i) {
		const DWORD m = g_eng.events[i].msg;
		const BYTE st = (BYTE)(m & 0xff);
		if (st == 0xff) continue;
		const int ch = st & 15;
		const int type = st & 0xf0;
		if (type == 0x90 || type == 0x80 || type == 0xb0 || type == 0xc0 || type == 0xe0)
			usedCh[ch] = 1;
		if (type == 0xc0) prog[ch] = (int)((m >> 8) & 0x7f);
	}

	int bestR = -1, bestRS = -1, bestD = -1, bestDS = -1;
	for (int i = 0; i < g_pluginCount; ++i) {
		const VstPluginInfo& p = g_plugins[i];
		const int rs = RomplerPickScore(p);
		if (rs > bestRS) { bestRS = rs; bestR = i; }
		const int ds = DrumPickScore(p);
		if (ds > bestDS) { bestDS = ds; bestD = i; }
	}

	if (bestR >= 0) {
		SetWaitStatus(g_waitWnd, LL14(L"ロムプラーを開いています…", L"Opening rompler…",
			L"Ouverture du rompler…", L"Apertura del rompler…", L"Abriendo el rompler…",
			L"롬플러를 여는 중…", L"正在打开采样器…", L"جارٍ فتح العيّنة…",
			L"Открытие ромплера…", L"Rompler wird geöffnet…", L"A abrir o rompler…",
			L"Rompler openen…", L"Otwieranie romplera…", L"Rompler açılıyor…"));
		MixSlot& slot = g_eng.mix[0];
		int opened = TryOpenMixPath(slot, g_plugins[bestR].path, g_plugins[bestR].isVst3,
			0, 0, 0);
		if (!opened)
			opened = TryOpenMixPath(slot, g_plugins[bestR].path, g_plugins[bestR].isVst3,
				1, 0, 0);
		if (opened) {
			LogVst3Programs(slot.vst3, g_plugins[bestR].name);
			slot.family = FAM_GENERIC;
			slot.keepMidiCh = (slot.vst3 && Vst3MidiChannels(slot.vst3) >= 16) ? 1 : 0;
			g_eng.mixCount = 1;
			int drumSep = 0;
			if (usedCh[9] && bestD >= 0 &&
				_wcsicmp(g_plugins[bestD].path, g_plugins[bestR].path) != 0) {
				SetWaitStatus(g_waitWnd, LL14(L"ドラム音源を開いています…", L"Opening drum plug-in…",
					L"Ouverture de la batterie…", L"Apertura del kit…", L"Abriendo el kit…",
					L"드럼 음원을 여는 중…", L"正在打开鼓音源…", L"جارٍ فتح طقم الطبول…",
					L"Открытие ударных…", L"Drum-Plug-in wird geöffnet…", L"A abrir o kit…",
					L"Drumplug-in openen…", L"Otwieranie zestawu…", L"Davul eklentisi açılıyor…"));
				MixSlot& ds = g_eng.mix[1];
				int dok = TryOpenMixPath(ds, g_plugins[bestD].path, g_plugins[bestD].isVst3,
					1, 0, 0);
				if (!dok)
					dok = TryOpenMixPath(ds, g_plugins[bestD].path, g_plugins[bestD].isVst3,
						0, 0, 0);
				if (dok) {
					LogVst3Programs(ds.vst3, g_plugins[bestD].name);
					ds.family = FAM_DRUM;
					ds.keepMidiCh = (ds.vst3 && Vst3MidiChannels(ds.vst3) >= 16 &&
						IsDrumPlugName(g_plugins[bestD].name, g_plugins[bestD].path)) ? 1 : 0;
					if (!ds.keepMidiCh && ds.vst3) {
						int kit = PickProgramForTarget(ds.vst3, 0, 1);
						Vst3SetChannelProgram(ds.vst3, 0, kit);
						SettleVst3(ds.vst3);
					}
					g_eng.chSlot[9] = 1;
					g_eng.mixCount = 2;
					drumSep = 1;
					EnsLog(L"drum slot=%d %s keepMidi=%d", 1, g_plugins[bestD].name, ds.keepMidiCh);
				}
			}
			ApplyGmPrograms(slot, prog, usedCh, drumSep);
			if (slot.keepMidiCh) {
				for (int ch = 0; ch < 16; ++ch) {
					if (!usedCh[ch]) continue;
					if (drumSep && ch == 9) continue;
					g_eng.chSlot[ch] = 0;
				}
			} else {
				int firstMel = -1;
				for (int ch = 0; ch < 16; ++ch) {
					if (!usedCh[ch] || (drumSep && ch == 9)) continue;
					if (firstMel < 0) {
						firstMel = ch;
						int pick = PickProgramForTarget(slot.vst3, prog[ch], 0);
						Vst3SetProgram(slot.vst3, pick);
						slot.vstProg = pick;
						slot.family = GmProgramFamily(prog[ch]);
						g_eng.chSlot[ch] = 0;
						continue;
					}
					if (g_eng.mixCount >= MIX_SLOTS) break;
					if (prog[ch] == prog[firstMel]) {
						g_eng.chSlot[ch] = 0;
						continue;
					}
					int reuse = -1;
					for (int s = 0; s < g_eng.mixCount; ++s) {
						if (g_eng.mix[s].family == FAM_DRUM) continue;
						if (_wcsicmp(g_eng.mix[s].path, g_plugins[bestR].path) == 0 &&
							g_eng.mix[s].vstProg == prog[ch]) {
							reuse = s; break;
						}
					}
					if (reuse >= 0) { g_eng.chSlot[ch] = reuse; continue; }
					MixSlot& extra = g_eng.mix[g_eng.mixCount];
					int pick = slot.vst3 ? PickProgramForTarget(slot.vst3, prog[ch], 0) : prog[ch];
					if (!TryOpenMixPath(extra, g_plugins[bestR].path, g_plugins[bestR].isVst3,
						0, prog[ch], pick))
						continue;
					extra.family = GmProgramFamily(prog[ch]);
					extra.keepMidiCh = 0;
					g_eng.chSlot[ch] = g_eng.mixCount;
					EnsLog(L"rompler extra slot=%d ch=%d gm=%d vstProg=%d",
						g_eng.mixCount, ch + 1, prog[ch], extra.vstProg);
					g_eng.mixCount++;
				}
			}
			if (!drumSep && usedCh[9])
				g_eng.chSlot[9] = 0;
			EnsLog(L"rompler ensemble %s slots=%d keepMidi=%d",
				g_plugins[bestR].name, g_eng.mixCount, slot.keepMidiCh);
			return FinishEnsembleOk(usedCh, prog);
		}
	}

	int bestPlug[16];
	int bestProg[16];
	int bestScore[16];
	for (int ch = 0; ch < 16; ++ch) {
		bestPlug[ch] = -1;
		bestProg[ch] = 0;
		bestScore[ch] = -1;
	}

	for (int allowToy = 0; allowToy <= 1; ++allowToy) {
		int lastAudible = -1;
		int nCat = 0;
		for (int i = 0; i < g_pluginCount; ++i) {
			const VstPluginInfo& p = g_plugins[i];
			if (!p.isInstrument || p.arch != HostArch()) continue;
			if (IsFxNotInstrument(p.name, p.path)) continue;
			if (!allowToy && IsBundledToySynth(p.name, p.path)) continue;
			++nCat;
		}
		int nCatDone = 0;
		for (int i = 0; i < g_pluginCount; ++i) {
			const VstPluginInfo& p = g_plugins[i];
			if (!p.isInstrument || p.arch != HostArch()) continue;
			if (IsFxNotInstrument(p.name, p.path)) continue;
			if (!allowToy && IsBundledToySynth(p.name, p.path)) continue;
			++nCatDone;
			{
				wchar_t msg[384];
				_snwprintf_s(msg, _TRUNCATE, LL14(
					L"類似確認 %d / %d\n%s",
					L"Matching timbre %d / %d\n%s",
					L"Correspondance %d / %d\n%s",
					L"Corrispondenza %d / %d\n%s",
					L"Coincidencia %d / %d\n%s",
					L"유사 확인 %d / %d\n%s",
					L"音色匹配 %d / %d\n%s",
					L"مطابقة الجرس %d / %d\n%s",
					L"Подбор тембра %d / %d\n%s",
					L"Klangabgleich %d / %d\n%s",
					L"Correspondência %d / %d\n%s",
					L"Klankmatch %d / %d\n%s",
					L"Dobór barwy %d / %d\n%s",
					L"Tını eşleme %d / %d\n%s"),
					nCatDone, nCat, p.name);
				SetWaitStatus(g_waitWnd, msg);
			}
			MixSlot tmp = {};
			int opened = TryOpenMixPath(tmp, p.path, p.isVst3, 0, 0, 0);
			if (!opened)
				opened = TryOpenMixPath(tmp, p.path, p.isVst3, 1, 0, 0);
			if (!opened)
				continue;
			lastAudible = i;
			const int nprog = tmp.vst3 ? Vst3ProgramCount(tmp.vst3) : 0;
			const int plugFam = ClassifyPlugFamily(p.name, p.path);
			const int rompler = IsRomplerName(p.name, p.path);
			const int drumPlug = IsDrumPlugName(p.name, p.path);
			const int toy = IsBundledToySynth(p.name, p.path);
			LogVst3Programs(tmp.vst3, p.name);
			EnsLog(L"catalog %s programs=%d midiCh=%d fam=%d rompler=%d kit=%d",
				p.name, nprog, tmp.vst3 ? Vst3MidiChannels(tmp.vst3) : 0,
				plugFam, rompler, drumPlug);
			for (int ch = 0; ch < 16; ++ch) {
				if (!usedCh[ch]) continue;
				const int drums = (ch == 9);
				const int wantFam = drums ? FAM_DRUM : GmProgramFamily(prog[ch]);
				int pick = tmp.vst3 ? PickProgramForTarget(tmp.vst3, drums ? 0 : prog[ch], drums) : 0;
				int sc = FamilyCompat(plugFam, wantFam, drums, rompler, drumPlug, toy);
				if (tmp.vst3) {
					wchar_t nm[128];
					if (Vst3ProgramName(tmp.vst3, pick, nm, 128) && !IsDummyProgName(nm))
						sc += ScoreNameForTarget(nm, drums ? 0 : prog[ch], drums);
				}
				if (!drums && nprog >= 128) sc += 120;
				if (sc > bestScore[ch]) {
					bestScore[ch] = sc;
					bestPlug[ch] = i;
					bestProg[ch] = pick;
				}
			}
			CloseEffect(tmp.module, tmp.effect);
			Vst3Close(tmp.vst3);
		}

		int chOrder[16];
		int nOrd = 0;
		if (usedCh[9]) chOrder[nOrd++] = 9;
		for (int ch = 0; ch < 16; ++ch)
			if (usedCh[ch] && ch != 9) chOrder[nOrd++] = ch;

		for (int oi = 0; oi < nOrd; ++oi) {
			const int ch = chOrder[oi];
			if (g_eng.mixCount >= MIX_SLOTS) break;
			int pi = bestPlug[ch];
			int vstProg = bestProg[ch];
			if (pi < 0 && ch == 9 && lastAudible >= 0) {
				pi = lastAudible;
				vstProg = 0;
				EnsLog(L"drum fallback audible plug %s", g_plugins[pi].name);
			}
			if (pi < 0) continue;
			int reuse = -1;
			if (ch != 9) {
				for (int s = 0; s < g_eng.mixCount; ++s) {
					if (g_eng.mix[s].family == FAM_DRUM) continue;
					if (_wcsicmp(g_eng.mix[s].path, g_plugins[pi].path) == 0 &&
						g_eng.mix[s].vstProg == vstProg) {
						reuse = s; break;
					}
				}
			}
			if (reuse >= 0) {
				g_eng.chSlot[ch] = reuse;
				continue;
			}
			MixSlot& slot = g_eng.mix[g_eng.mixCount];
			const int drums = (ch == 9);
			if (!TryOpenMixPath(slot, g_plugins[pi].path, g_plugins[pi].isVst3,
				drums, drums ? 0 : prog[ch], vstProg)) {
				EnsLog(L"ensemble assign FAIL ch=%d %s", ch + 1, g_plugins[pi].path);
				continue;
			}
			const int plugFam = ClassifyPlugFamily(g_plugins[pi].name, g_plugins[pi].path);
			slot.family = drums ? FAM_DRUM : GmProgramFamily(prog[ch]);
			if (drums && !IsDrumPlugName(g_plugins[pi].name, g_plugins[pi].path) &&
				plugFam != FAM_DRUM && plugFam != FAM_PERC)
				slot.keepMidiCh = 0;
			g_eng.chSlot[ch] = g_eng.mixCount;
			wchar_t pnm[128] = {};
			if (slot.vst3) Vst3ProgramName(slot.vst3, slot.vstProg, pnm, 128);
			EnsLog(L"ensemble slot=%d ch=%d gm=%d vstProg=%d %s [%s]",
				g_eng.mixCount, ch + 1, prog[ch], slot.vstProg, g_plugins[pi].name, pnm);
			g_eng.mixCount++;
		}
		if (g_eng.mixCount > 0)
			return FinishEnsembleOk(usedCh, prog);
	}

	EnsLog(L"BuildEnsemble: no audible VST (plugins=%d hostArch=%d)",
		g_pluginCount, HostArch());
	return 0;
}

static void DispatchEnsemble(__int64 start, int frames)
{
	const __int64 end = start + frames;
	FlushInjectQueue(start, frames);
	while (g_eng.eventPos < g_eng.eventCount &&
		g_eng.events[g_eng.eventPos].sample < end) {
		MidiItem e = g_eng.events[g_eng.eventPos++];
		e.msg = SongOverrideMsg(e.port, e.msg);
		if ((e.msg & 0xff) == 0xff) continue;
		if ((e.msg & 0xff) == 0xf0 && e.sysexOff >= 0 && g_eng.sysexData) {
			const int len = (int)e.aux;
			if (e.sysexOff + len <= g_eng.sysexBytes) {
				__int64 d = e.sample - start;
				int offset = d < 0 ? 0 : (d >= frames ? frames - 1 : (int)d);
				BroadcastSongSysex(g_eng.sysexData + e.sysexOff, len, offset, e.port);
			}
			continue;
		}
		const int ch = (int)(e.msg & 15);
		const int slot = g_eng.chSlot[ch];
		if (slot < 0) continue;
		MixSlot& ms = g_eng.mix[slot];
		MidiItem m = e;
		const int type = (int)(e.msg & 0xf0);
		if (!ms.keepMidiCh) {
			if (type == 0xc0) continue;
			m.msg = (m.msg & ~0x0fu) | 0u;
		}
		if (ms.effect) SendVstEvents(ms.effect, &m, 1, start);
		if (ms.vst3) {
			__int64 d = m.sample - start;
			int offset = d < 0 ? 0 : (d >= frames ? frames - 1 : (int)d);
			Vst3MidiShort(ms.vst3, m.msg, offset);
		}
	}
}

static void RenderEnsemble(float* l, float* r, int frames)
{
	ZeroMemory(l, frames * sizeof(float));
	ZeroMemory(r, frames * sizeof(float));
	for (int s = 0; s < g_eng.mixCount; ++s) {
		MixSlot& ms = g_eng.mix[s];
		if (!ms.effect && !ms.vst3) continue;
		if (ms.vst3) {
			__try { Vst3Process(ms.vst3, g_eng.mixL, g_eng.mixR, frames); }
			__except (EXCEPTION_EXECUTE_HANDLER) {
				ZeroMemory(g_eng.mixL, frames * sizeof(float));
				ZeroMemory(g_eng.mixR, frames * sizeof(float));
			}
		} else {
			__try { RenderEffect(ms.effect, g_eng.mixL, g_eng.mixR, frames); }
			__except (EXCEPTION_EXECUTE_HANDLER) {
				ZeroMemory(g_eng.mixL, frames * sizeof(float));
				ZeroMemory(g_eng.mixR, frames * sizeof(float));
			}
		}
		for (int i = 0; i < frames; ++i) {
			l[i] += g_eng.mixL[i];
			r[i] += g_eng.mixR[i];
		}
	}
	// Soft clip after multi-plug sum
	const float gain = (g_eng.mixCount > 1) ? (0.85f / sqrtf((float)g_eng.mixCount)) : 0.85f;
	for (int i = 0; i < frames; ++i) {
		float x = l[i] * gain, y = r[i] * gain;
		l[i] = x / (1.0f + fabsf(x));
		r[i] = y / (1.0f + fabsf(y));
	}
}

static void MapperSysex(HMIDIOUT h, const BYTE* data, DWORD bytes)
{
	if (!h || !data || bytes < 2) return;
	BYTE stack[256];
	BYTE* tmp = stack;
	BYTE* heap = NULL;
	if (bytes > sizeof(stack)) {
		heap = new BYTE[bytes];
		tmp = heap;
	}
	memcpy(tmp, data, bytes);
	MIDIHDR hdr = {};
	hdr.lpData = (LPSTR)tmp;
	hdr.dwBufferLength = bytes;
	if (midiOutPrepareHeader(h, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) {
		delete[] heap;
		return;
	}
	midiOutLongMsg(h, &hdr, sizeof(hdr));
	for (int i = 0; i < 50 && !(hdr.dwFlags & MHDR_DONE); ++i)
		Sleep(1);
	midiOutUnprepareHeader(h, &hdr, sizeof(hdr));
	delete[] heap;
}

static void MapperClose()
{
	if (g_eng.midiOut) {
		midiOutReset(g_eng.midiOut);
		midiOutClose(g_eng.midiOut);
		g_eng.midiOut = NULL;
	}
	g_eng.useMapper = 0;
}

static UINT ResolveMidiOutDev()
{
	if (!savedata.midiOutName[0]) return MIDI_MAPPER;
	const UINT n = midiOutGetNumDevs();
	for (UINT i = 0; i < n; ++i) {
		MIDIOUTCAPS c = {};
		if (midiOutGetDevCaps(i, &c, sizeof(c)) != MMSYSERR_NOERROR) continue;
		if (_wcsicmp(c.szPname, savedata.midiOutName) == 0) return i;
	}
	return MIDI_MAPPER;
}

static int MapperOpen()
{
	MapperClose();
	HMIDIOUT h = NULL;
	UINT id = ResolveMidiOutDev();
	if (midiOutOpen(&h, id, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
		if (id == MIDI_MAPPER) return 0;
		if (midiOutOpen(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
			return 0;
	}
	g_eng.midiOut = h;
	g_eng.useMapper = 1;
	static const BYTE gmOn[] = { 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 };
	static const BYTE gsReset[] = {
		0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7
	};
	MapperSysex(h, gmOn, sizeof(gmOn));
	MapperSysex(h, gsReset, sizeof(gsReset));
	return 1;
}

static void FreeSong()
{
	MapperClose();
	CloseEffect(g_eng.module, g_eng.effect);
	Vst3Close(g_eng.vst3); g_eng.vst3 = NULL;
	CloseEffect(g_eng.moduleB, g_eng.effectB);
	CloseEffect(g_eng.moduleC, g_eng.effectC);
	Vst3Close(g_eng.vst3C); g_eng.vst3C = NULL;
	CloseMixSlots();
	delete[] g_eng.events; g_eng.events = NULL;
	delete[] g_eng.fileData; g_eng.fileData = NULL;
	delete[] g_eng.sysexData; g_eng.sysexData = NULL;
	g_eng.fileBytes = 0; g_eng.sysexBytes = 0;
	g_eng.maxMidiPort = 0;
	g_eng.mirrorToB = 0;
	RxListenInit();
	g_eng.eventCount = g_eng.eventPos = 0;
	g_eng.playSample = g_eng.lengthSamples = 0;
	g_eng.ringRead = g_eng.ringCount = 0;
	ZeroMemory(g_eng.voices, sizeof(g_eng.voices));
	ZeroMemory(g_eng.drums, sizeof(g_eng.drums));
	ZeroMemory(g_eng.noteState, sizeof(g_eng.noteState));
	g_eng.usingBuiltin = 1;
	g_eng.useDrums = 0;
	g_eng.gsMapLsb = 0;
	g_eng.songGm = 0;
}

static void ResetSequence()
{
	g_eng.eventPos = 0;
	g_eng.playSample = 0;
	g_eng.ringRead = g_eng.ringCount = 0;
	ZeroMemory(g_eng.voices, sizeof(g_eng.voices));
	ZeroMemory(g_eng.drums, sizeof(g_eng.drums));
	ZeroMemory(g_eng.noteState, sizeof(g_eng.noteState));
	SongOvClear();
	RxChReset();
	auto allOff = [](AEffect* effect, Vst3Inst* vst3) {
		if (effect) {
			MidiItem alloff[16] = {};
			for (int ch = 0; ch < 16; ++ch) {
				alloff[ch].msg = (0xb0 | ch) | (123 << 8);
				alloff[ch].sample = 0;
				alloff[ch].sysexOff = -1;
			}
			SendVstEvents(effect, alloff, 16, 0);
		}
		if (vst3) {
			for (int ch = 0; ch < 16; ++ch)
				Vst3MidiShort(vst3, (0xb0 | ch) | (123 << 8), 0);
		}
	};
	allOff(g_eng.effect, g_eng.vst3);
	allOff(g_eng.effectB, NULL);
	allOff(g_eng.effectC, g_eng.vst3C);
	if (g_eng.midiOut) {
		midiOutReset(g_eng.midiOut);
		for (int ch = 0; ch < 16; ++ch) {
			midiOutShortMsg(g_eng.midiOut, (0xb0 | ch) | (123 << 8));
			midiOutShortMsg(g_eng.midiOut, (0xb0 | ch) | (120 << 8));
		}
	}
	// Do not send CC123 to ensemble VST3s here: several hosts hang on
	// kLegacyMIDICCOutEvent (SampleTank / LABS). Voices are already cleared.
}

static int FindSidecar(const wchar_t* in, wchar_t* out, int chars)
{
	wchar_t direct[VST_PATH_CHARS];
	SafeCopy(direct, VST_PATH_CHARS, in);
	wchar_t* dot = wcsrchr(direct, L'.');
	if (dot) wcscpy_s(dot, VST_PATH_CHARS - (int)(dot - direct), L".mid");
	if (GetFileAttributesW(direct) != INVALID_FILE_ATTRIBUTES) {
		SafeCopy(out, chars, direct); return 1;
	}
	wchar_t dir[VST_PATH_CHARS];
	SafeCopy(dir, VST_PATH_CHARS, in);
	wchar_t* slash = wcsrchr(dir, L'\\');
	if (!slash) return 0;
	*slash = 0;
	wchar_t pat[VST_PATH_CHARS];
	JoinPath(pat, dir, L"*.mid");
	WIN32_FIND_DATAW fd = {};
	HANDLE h = FindFirstFileW(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	wchar_t found[VST_PATH_CHARS];
	JoinPath(found, dir, fd.cFileName);
	FindClose(h);
	SafeCopy(out, chars, found);
	return 1;
}

static void AddHint(wchar_t hints[][128], int maxHints, int& count,
	const char* s, int n)
{
	if (count >= maxHints || n < 4) return;
	if (n > 127) n = 127;
	char tmp[128] = {};
	memcpy(tmp, s, n);
	static const char* keys[] = {
		"VST", "VSTi", "HALion", "SampleTank", "Kontakt", "Canvas",
		"Synth", "SC-VA", "S-YXG", "BassMidi", "Timidity"
	};
	int useful = 0;
	for (int k = 0; k < (int)(sizeof(keys) / sizeof(keys[0])); ++k) {
		for (int i = 0; i + (int)strlen(keys[k]) <= n; ++i)
			if (_strnicmp(tmp + i, keys[k], strlen(keys[k])) == 0) useful = 1;
	}
	if (!useful) return;
	wchar_t w[128] = {};
	MultiByteToWideChar(CP_ACP, 0, tmp, -1, w, 128);
	for (int i = 0; i < count; ++i)
		if (_wcsicmp(hints[i], w) == 0) return;
	SafeCopy(hints[count++], 128, w);
}

static void ExtractHints(const wchar_t* path, wchar_t hints[][128],
	int maxHints, int& count)
{
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return;
	DWORD size = GetFileSize(f, NULL);
	if (size > 32 * 1024 * 1024) size = 32 * 1024 * 1024;
	BYTE* data = new BYTE[size + 1];
	DWORD got = 0;
	if (!ReadFile(f, data, size, &got, NULL)) got = 0;
	CloseHandle(f);
	data[got] = 0;
	for (DWORD i = 0; i < got && count < maxHints;) {
		while (i < got && (data[i] < 32 || data[i] > 126)) ++i;
		DWORD start = i;
		while (i < got && data[i] >= 32 && data[i] <= 126 && i - start < 256) ++i;
		DWORD n = i - start;
		if (n >= 4) AddHint(hints, maxHints, count, (char*)data + start, (int)n);
		if (i == start) ++i;
	}
	delete[] data;
}

static int FindReferencedMid(const wchar_t* path, wchar_t* out, int chars)
{
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD size = GetFileSize(f, NULL);
	if (size > 16 * 1024 * 1024) size = 16 * 1024 * 1024;
	char* data = new char[size + 1];
	DWORD got = 0;
	ReadFile(f, data, size, &got, NULL);
	CloseHandle(f); data[got] = 0;
	int result = 0;
	for (DWORD i = 0; i + 4 < got && !result; ++i) {
		if (_strnicmp(data + i, ".mid", 4)) continue;
		DWORD a = i;
		while (a && data[a - 1] != '"' && data[a - 1] != '\'' &&
			data[a - 1] >= 32) --a;
		DWORD b = i + 4;
		if (b - a >= VST_PATH_CHARS) continue;
		char ref[VST_PATH_CHARS] = {};
		memcpy(ref, data + a, b - a);
		wchar_t wr[VST_PATH_CHARS] = {};
		MultiByteToWideChar(CP_UTF8, 0, ref, -1, wr, VST_PATH_CHARS);
		if (!wcschr(wr, L'\\') && !wcschr(wr, L'/')) {
			wchar_t dir[VST_PATH_CHARS];
			wchar_t leaf[VST_PATH_CHARS];
			SafeCopy(leaf, VST_PATH_CHARS, wr);
			SafeCopy(dir, VST_PATH_CHARS, path);
			wchar_t* s = wcsrchr(dir, L'\\');
			if (s) { *s = 0; JoinPath(wr, dir, leaf); }
		}
		for (wchar_t* x = wr; *x; ++x) if (*x == L'/') *x = L'\\';
		if (GetFileAttributesW(wr) != INVALID_FILE_ATTRIBUTES) {
			SafeCopy(out, chars, wr); result = 1;
		}
	}
	delete[] data;
	return result;
}

static int XmlTagInt(const char* begin, const char* end,
	const char* open, int fallback)
{
	const char* p = strstr(begin, open);
	if (!p || p >= end) return fallback;
	p += strlen(open);
	return atoi(p);
}

static int PutVlq(BYTE* dst, int cap, unsigned value)
{
	BYTE tmp[5];
	int n = 0;
	tmp[n++] = (BYTE)(value & 0x7f);
	while ((value >>= 7) != 0 && n < 5) tmp[n++] = (BYTE)((value & 0x7f) | 0x80);
	if (n > cap) return 0;
	for (int i = 0; i < n; ++i) dst[i] = tmp[n - i - 1];
	return n;
}

static void PutBE32(BYTE* p, DWORD v)
{
	p[0] = (BYTE)(v >> 24); p[1] = (BYTE)(v >> 16);
	p[2] = (BYTE)(v >> 8); p[3] = (BYTE)v;
}

static int ConvertMusicXml(const wchar_t* inPath, wchar_t* out, int outChars)
{
	HANDLE f = CreateFileW(inPath, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	DWORD size = GetFileSize(f, NULL), got = 0;
	if (!size || size > 32 * 1024 * 1024) { CloseHandle(f); return 0; }
	char* xml = new char[size + 1];
	if (!ReadFile(f, xml, size, &got, NULL) || got != size) {
		CloseHandle(f); delete[] xml; return 0;
	}
	CloseHandle(f); xml[size] = 0;
	BYTE* track = new BYTE[size * 2 + 64];
	const int cap = (int)(size * 2 + 64);
	int pos = 0, notes = 0;
	// 120 BPM tempo.
	const BYTE tempo[] = { 0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20 };
	memcpy(track + pos, tempo, sizeof(tempo)); pos += sizeof(tempo);
	int divisions = XmlTagInt(xml, xml + size, "<divisions>", 1);
	if (divisions < 1) divisions = 1;
	const char* p = xml;
	while ((p = strstr(p, "<note")) != NULL && pos + 16 < cap) {
		const char* end = strstr(p, "</note>");
		if (!end) break;
		if (!strstr(p, "<rest") || strstr(p, "<rest") >= end) {
			const char* sp = strstr(p, "<step>");
			const char* op = strstr(p, "<octave>");
			if (sp && sp < end && op && op < end) {
				static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 }; // A..G
				char step = sp[6];
				int base = (step >= 'A' && step <= 'G') ? semis[step - 'A'] : 0;
				int octave = atoi(op + 8);
				int alter = XmlTagInt(p, end, "<alter>", 0);
				int duration = XmlTagInt(p, end, "<duration>", divisions);
				int note = (octave + 1) * 12 + base + alter;
				if (note < 0) note = 0; if (note > 127) note = 127;
				unsigned ticks = (unsigned)((duration * 480) / divisions);
				if (!ticks) ticks = 1;
				track[pos++] = 0; track[pos++] = 0x90;
				track[pos++] = (BYTE)note; track[pos++] = 96;
				int vn = PutVlq(track + pos, cap - pos, ticks);
				if (!vn) break;
				pos += vn; track[pos++] = 0x80;
				track[pos++] = (BYTE)note; track[pos++] = 64;
				++notes;
			}
		}
		p = end + 7;
	}
	track[pos++] = 0; track[pos++] = 0xff; track[pos++] = 0x2f; track[pos++] = 0;
	delete[] xml;
	if (!notes) { delete[] track; return 0; }
	wchar_t temp[VST_PATH_CHARS] = {};
	DWORD tn = GetTempPathW(VST_PATH_CHARS, temp);
	if (!tn || tn >= VST_PATH_CHARS) { delete[] track; return 0; }
	wchar_t path[VST_PATH_CHARS];
	JoinPath(path, temp, L"ogg_vst_tmp.mid");
	HANDLE o = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
	if (o == INVALID_HANDLE_VALUE) { delete[] track; return 0; }
	BYTE hdr[22] = {
		'M','T','h','d', 0,0,0,6, 0,0, 0,1, 1,0xe0,
		'M','T','r','k', 0,0,0,0
	};
	PutBE32(hdr + 18, (DWORD)pos);
	DWORD put = 0;
	int ok = WriteFile(o, hdr, sizeof(hdr), &put, NULL) && put == sizeof(hdr) &&
		WriteFile(o, track, pos, &put, NULL) && put == (DWORD)pos;
	CloseHandle(o); delete[] track;
	if (!ok) { DeleteFileW(path); return 0; }
	SafeCopy(out, outChars, path);
	return 1;
}

} // namespace

static int CollapseLiveDuplicates(void);

extern "C" int VstScanEnsure(HWND parentForWait)
{
	if (g_scanReady && !g_scanInvalid) return 0;
	if (!g_scanInvalid && LoadCache()) {
		RescoreMultiFlags();
		if (CollapseLiveDuplicates()) SaveCache();
		g_scanReady = 1;
		return 0;
	}
	HWND wait = MakeWait(parentForWait);
	g_pluginCount = 0;
	wchar_t roots[16][VST_PATH_CHARS] = {};
	int count = 0;
	AddEnvRoot(L"ProgramFiles", L"VstPlugins", roots, count, 16);
	AddEnvRoot(L"ProgramFiles(x86)", L"VstPlugins", roots, count, 16);
	AddEnvRoot(L"CommonProgramFiles", L"VST3", roots, count, 16);
	AddEnvRoot(L"CommonProgramFiles(x86)", L"VST3", roots, count, 16);
	static const wchar_t* fixedVstRoots[] = {
		L"C:\\Program Files\\Common Files\\VST3",
		L"C:\\Program Files (x86)\\Common Files\\VST3",
		L"C:\\Program Files\\Steinberg",
		L"C:\\Program Files\\Common Files\\Steinberg\\VST2",
		L"C:\\Program Files\\Common Files\\Steinberg\\VST3",
	};
	for (int i = 0; i < (int)(sizeof(fixedVstRoots) / sizeof(fixedVstRoots[0])) && count < 16; ++i) {
		if (!DirExists(fixedVstRoots[i])) continue;
		int dup = 0;
		for (int r = 0; r < count; ++r)
			if (_wcsicmp(roots[r], fixedVstRoots[i]) == 0) { dup = 1; break; }
		if (!dup) SafeCopy(roots[count++], VST_PATH_CHARS, fixedVstRoots[i]);
	}
	if (count < 16) {
		wchar_t exe[VST_PATH_CHARS]; ExeDir(exe);
		JoinPath(roots[count++], exe, L"VST");
	}
	AddRegRoot(HKEY_LOCAL_MACHINE, roots, count, 16);
	AddRegRoot(HKEY_CURRENT_USER, roots, count, 16);
	static const wchar_t* knownMulti[] = {
		L"C:\\Program Files\\Roland\\SOUND Canvas VA",
		L"C:\\Program Files (x86)\\Roland\\SOUND Canvas VA",
		L"C:\\Yamaha\\S-YXG50",
	};
	wchar_t cloudRoot[VST_PATH_CHARS]; cloudRoot[0] = 0;
	{
		wchar_t pd[MAX_PATH] = {};
		if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, pd))) {
			_snwprintf_s(cloudRoot, _TRUNCATE, L"%s\\Roland Cloud\\SOUND Canvas VA", pd);
			if (DirExists(cloudRoot) && count < 16) {
				int dup = 0;
				for (int r = 0; r < count; ++r)
					if (_wcsicmp(roots[r], cloudRoot) == 0) { dup = 1; break; }
				if (!dup) SafeCopy(roots[count++], VST_PATH_CHARS, cloudRoot);
			}
		}
	}
	for (int k = 0; k < (int)(sizeof(knownMulti) / sizeof(knownMulti[0])) && count < 16; ++k) {
		if (!DirExists(knownMulti[k])) continue;
		int dup = 0;
		for (int r = 0; r < count; ++r)
			if (_wcsicmp(roots[r], knownMulti[k]) == 0) { dup = 1; break; }
		if (!dup) SafeCopy(roots[count++], VST_PATH_CHARS, knownMulti[k]);
	}
	g_scanIndex = 0;
	g_scanTotal = 0;
	for (int i = 0; i < count; ++i)
		g_scanTotal += CountDir(roots[i], 0);
	if (savedata.vstMultiDll[0]) {
		DWORD a = GetFileAttributesW(savedata.vstMultiDll);
		if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))
			g_scanTotal += CountDir(savedata.vstMultiDll, 0);
		else
			++g_scanTotal;
	}
	if (savedata.vstExtraPath[0]) {
		DWORD a = GetFileAttributesW(savedata.vstExtraPath);
		if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))
			g_scanTotal += CountDir(savedata.vstExtraPath, 0);
		else
			++g_scanTotal;
	}
	{
		SetScanWait(wait, 0, g_scanTotal, 0, L"");
	}
	ProbeUserSpecified(savedata.vstMultiDll, wait);
	ProbeUserSpecified(savedata.vstExtraPath, wait);
	for (int i = 0; i < count; ++i) ScanDir(roots[i], 0, wait);
	RescoreMultiFlags();
	if (wait) DestroyWait(wait);
	SaveCache();
	g_scanInvalid = 0; g_scanReady = 1;
	return 0;
}

extern "C" int VstScanGetCount(void) { return g_pluginCount; }

extern "C" const VstPluginInfo* VstScanGet(int i)
{
	return (i >= 0 && i < g_pluginCount) ? &g_plugins[i] : NULL;
}

extern "C" void VstScanInvalidate(void)
{
	g_scanInvalid = 1; g_scanReady = 0;
	wchar_t p[VST_PATH_CHARS]; CachePath(p);
	DeleteFileW(p);
}

extern "C" int VstDetectMultiTimbral(const wchar_t* nameOrPath)
{
	return DetectMultiTimbralName(nameOrPath);
}

extern "C" int VstScanGetMultiCount(void)
{
	int n = 0;
	for (int i = 0; i < g_pluginCount; ++i)
		if (g_plugins[i].isInstrument && g_plugins[i].isMultiTimbral &&
			g_plugins[i].arch == HostArch())
			++n;
	return n;
}

extern "C" const VstPluginInfo* VstScanGetMulti(int multiIndex)
{
	int n = 0;
	for (int i = 0; i < g_pluginCount; ++i) {
		if (!(g_plugins[i].isInstrument && g_plugins[i].isMultiTimbral &&
			g_plugins[i].arch == HostArch()))
			continue;
		if (n == multiIndex) return &g_plugins[i];
		++n;
	}
	return NULL;
}

static int ResolveRolandScVaPath(const wchar_t* in, wchar_t* out, int outChars, int wantArch);
extern "C" int VstPluginPeArch(const wchar_t* path)
{
	return PeArch(path);
}

static int ResolvePickedPluginArch(const wchar_t* src, wchar_t* outPath, int outChars)
{
	if (!outPath || outChars <= 0) return 0;
	outPath[0] = 0;
	if (!src || !src[0]) return 0;
	DWORD a = GetFileAttributesW(src);
	if (a == INVALID_FILE_ATTRIBUTES) return 0;
	const int isDir = (a & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
	if (isDir && !EqExt(src, L".vst3")) return 0;
	SafeCopy(outPath, outChars, src);
	if (EqExt(outPath, L".vst3")) {
		wchar_t p64[VST_PATH_CHARS], p32[VST_PATH_CHARS];
		JoinPath(p64, outPath, L"Contents\\x86_64-win");
		JoinPath(p32, outPath, L"Contents\\x86-win");
		const int has64 = DirExists(p64);
		const int has32 = DirExists(p32);
		if (has64 && !has32) return 64;
		if (has32 && !has64) return 32;
		if (has64 && has32) return 64;
		return HostArch();
	}
	wchar_t resolved[VST_PATH_CHARS];
	int want = PeArch(src);
	if (!want) want = HostArch();
	if (ResolveRolandScVaPath(src, resolved, VST_PATH_CHARS, want))
		SafeCopy(outPath, outChars, resolved);
	int arch = PeArch(outPath);
	return arch ? arch : want;
}

extern "C" int VstPickPreferredPlugin(wchar_t* outPath, int outChars)
{
	wchar_t pick[VST_PATH_CHARS];
	pick[0] = 0;
	if (!PickGsXgDll(NULL, pick, VST_PATH_CHARS)) {
		if (outPath && outChars > 0) outPath[0] = 0;
		return 0;
	}
	return ResolvePickedPluginArch(pick, outPath, outChars);
}

extern "C" int VstShouldOpenRemote64(const wchar_t* midPath, wchar_t* outDll, int outChars)
{
	if (!outDll || outChars <= 0) return 0;
	outDll[0] = 0;
	wchar_t pick[VST_PATH_CHARS];
	pick[0] = 0;
	if (!PickGsXgDll(midPath, pick, VST_PATH_CHARS)) return 0;
	const int march = ResolvePickedPluginArch(pick, outDll, outChars);
	if (march == 64 && outDll[0])
		return 1;
	outDll[0] = 0;
	return 0;
}

extern "C" int VstHasX64Instruments(void)
{
	VstScanEnsure(NULL);
	for (int i = 0; i < g_pluginCount; ++i)
		if (g_plugins[i].isInstrument && g_plugins[i].arch == 64)
			return 1;
	return 0;
}

extern "C" int VstIsMidiExt(const wchar_t* path)
{
	return EqExt(path, L".mid") || EqExt(path, L".midi") || EqExt(path, L".kar") || EqExt(path, L".rmi");
}

extern "C" int VstIsProjectExt(const wchar_t* path)
{
	static const wchar_t* exts[] = {
		L".cpr", L".lt10", L".ss10", L".ssw", L".lt9", L".rpp",
		L".als", L".musicxml", L".xml", L".kar", L".mxl"
	};
	for (int i = 0; i < (int)(sizeof(exts) / sizeof(exts[0])); ++i)
		if (EqExt(path, exts[i])) return 1;
	return 0;
}

extern "C" int VstResolvePlayPath(const wchar_t* inPath, wchar_t* outMid,
	int outMidChars, wchar_t hints[][128], int maxHints, int* outHintCount)
{
	if (!inPath || !outMid || outMidChars <= 0) return 0;
	outMid[0] = 0;
	int hc = 0;
	if (outHintCount) *outHintCount = 0;
	if (VstIsMidiExt(inPath)) {
		SafeCopy(outMid, outMidChars, inPath);
		return 1;
	}
	if (!VstIsProjectExt(inPath)) return 0;
	if (hints && maxHints > 0) ExtractHints(inPath, hints, maxHints, hc);
	if (outHintCount) *outHintCount = hc;
	if (EqExt(inPath, L".rpp") &&
		FindReferencedMid(inPath, outMid, outMidChars)) return 1;
	if ((EqExt(inPath, L".musicxml") || EqExt(inPath, L".xml")) &&
		ConvertMusicXml(inPath, outMid, outMidChars)) return 1;
	// Compressed MXL and ALS need archive extraction; use a sidecar when no
	// plain MusicXML/MIDI reference is available.
	return FindSidecar(inPath, outMid, outMidChars);
}

static void PumpSilent(AEffect* effect, Vst3Inst* vst3, int blocks)
{
	__declspec(align(32)) float z[BLOCK_FRAMES];
	__declspec(align(32)) float l[BLOCK_FRAMES];
	__declspec(align(32)) float r[BLOCK_FRAMES];
	ZeroMemory(z, sizeof(z));
	for (int b = 0; b < blocks; ++b) {
		ZeroMemory(l, sizeof(l));
		ZeroMemory(r, sizeof(r));
		if (vst3) Vst3Process(vst3, l, r, BLOCK_FRAMES);
		else if (effect) RenderEffect(effect, l, r, BLOCK_FRAMES);
	}
}

static void SendGmGsReset(AEffect* effect, Vst3Inst* vst3, int preferGs)
{
	// Falcom / SC style: GS Reset only (GM On first can leave some plugs in GM
	// and ignore GS drum kits / variation banks).
	static const BYTE gmOn[] = { 0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7 };
	static const BYTE gsReset[] = {
		0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7
	};
	static const BYTE xgOn[] = {
		0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7
	};
	if (effect && effect->dispatcher) {
		VstMidiSysexEvent sx[2] = {};
		struct EventBlock {
			VstInt32 numEvents;
			VstIntPtr reserved;
			VstEvent* events[2];
		} block = {};
		int n = 0;
		if (preferGs == 2) {
			sx[0].type = kVstSysExType;
			sx[0].byteSize = sizeof(VstMidiSysexEvent);
			sx[0].dumpBytes = (VstInt32)sizeof(xgOn);
			sx[0].sysexDump = (char*)xgOn;
			block.events[n++] = (VstEvent*)&sx[0];
		} else if (g_eng.songGm) {
			sx[0].type = kVstSysExType;
			sx[0].byteSize = sizeof(VstMidiSysexEvent);
			sx[0].dumpBytes = (VstInt32)sizeof(gmOn);
			sx[0].sysexDump = (char*)gmOn;
			block.events[n++] = (VstEvent*)&sx[0];
		} else if (preferGs == 1) {
			sx[0].type = kVstSysExType;
			sx[0].byteSize = sizeof(VstMidiSysexEvent);
			sx[0].dumpBytes = (VstInt32)sizeof(gsReset);
			sx[0].sysexDump = (char*)gsReset;
			block.events[n++] = (VstEvent*)&sx[0];
		} else {
			sx[0].type = kVstSysExType;
			sx[0].byteSize = sizeof(VstMidiSysexEvent);
			sx[0].dumpBytes = (VstInt32)sizeof(gmOn);
			sx[0].sysexDump = (char*)gmOn;
			sx[1].type = kVstSysExType;
			sx[1].byteSize = sizeof(VstMidiSysexEvent);
			sx[1].dumpBytes = (VstInt32)sizeof(gsReset);
			sx[1].sysexDump = (char*)gsReset;
			block.events[n++] = (VstEvent*)&sx[0];
			block.events[n++] = (VstEvent*)&sx[1];
		}
		block.numEvents = n;
		__try { effect->dispatcher(effect, effProcessEvents, 0, 0, &block, 0); }
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		PumpSilent(effect, NULL, 2);
	}
	if (vst3) {
		for (int ch = 0; ch < 16; ++ch) {
			Vst3MidiShort(vst3, (0xb0 | ch) | (121 << 8), 0);
			Vst3MidiShort(vst3, (0xb0 | ch) | (123 << 8), 0);
		}
		if (preferGs == 2)
			Vst3MidiSysex(vst3, xgOn, (int)sizeof(xgOn), 0);
		else if (g_eng.songGm)
			Vst3MidiSysex(vst3, gmOn, (int)sizeof(gmOn), 0);
		else if (preferGs == 1)
			Vst3MidiSysex(vst3, gsReset, (int)sizeof(gsReset), 0);
		else {
			Vst3MidiSysex(vst3, gmOn, (int)sizeof(gmOn), 0);
			Vst3MidiSysex(vst3, gsReset, (int)sizeof(gsReset), 0);
		}
		PumpSilent(NULL, vst3, 2);
	}
	if (preferGs != 2 && !g_eng.songGm) {
		BYTE rhy[11];
		GsFillRhythmDt1(rhy, 0x40);
		if (effect) SendVstSysex(effect, rhy, 11, 0);
		if (vst3) Vst3MidiSysex(vst3, rhy, 11, 0);
		if (g_eng.midiOut && effect == g_eng.effect)
			MapperSysex(g_eng.midiOut, rhy, 11);
		PumpSilent(effect, vst3, 1);
	}
	if (preferGs == 2) {
		const DWORD msb = 0xb9 | (0u << 8) | (127u << 16);
		const DWORD lsb = 0xb9 | (32u << 8);
		if (effect) {
			MidiItem cc[2] = {};
			cc[0].msg = msb; cc[1].msg = lsb;
			SendVstEvents(effect, cc, 2, 0);
		}
		if (vst3) {
			Vst3MidiShort(vst3, msb, 0);
			Vst3MidiShort(vst3, lsb, 0);
		}
		if (g_eng.midiOut && effect == g_eng.effect) {
			midiOutShortMsg(g_eng.midiOut, msb);
			midiOutShortMsg(g_eng.midiOut, lsb);
		}
	} else if (g_eng.songGm == 2) {
		const DWORD msb = 0xb9 | (0u << 8) | (120u << 16);
		const DWORD lsb = 0xb9 | (32u << 8);
		if (effect) {
			MidiItem cc[2] = {};
			cc[0].msg = msb; cc[1].msg = lsb;
			SendVstEvents(effect, cc, 2, 0);
		}
		if (vst3) {
			Vst3MidiShort(vst3, msb, 0);
			Vst3MidiShort(vst3, lsb, 0);
		}
	}
	if (preferGs == 1 && g_eng.gsMapLsb >= 1 && g_eng.gsMapLsb <= 4) {
		MidiItem mapcc[15] = {};
		int nmap = 0;
		for (int ch = 0; ch < 16; ++ch) {
			if (ch == 9) continue;
			mapcc[nmap++].msg = (0xb0 | ch) | (32 << 8) | ((DWORD)g_eng.gsMapLsb << 16);
		}
		if (effect) SendVstEvents(effect, mapcc, nmap, 0);
		if (vst3) {
			for (int i = 0; i < nmap; ++i)
				Vst3MidiShort(vst3, mapcc[i].msg, 0);
		}
		if (g_eng.midiOut && effect == g_eng.effect) {
			for (int i = 0; i < nmap; ++i)
				midiOutShortMsg(g_eng.midiOut, mapcc[i].msg);
		}
	}
}


static int ProbeLoadedEffectAudible(AEffect* effect)
{
	if (!effect) return 0;
	MidiItem on = {}, off = {};
	on.msg = 0x90 | (60 << 8) | (100 << 16);
	off.msg = 0x80 | (60 << 8) | (0 << 16);
	SendVstEvents(effect, &on, 1, 0);
	float peak = 0;
	for (int b = 0; b < 8; ++b) {
		RenderEffect(effect, g_eng.outL, g_eng.outR, BLOCK_FRAMES);
		for (int i = 0; i < BLOCK_FRAMES; ++i) {
			float a = fabsf(g_eng.outL[i]); if (a > peak) peak = a;
			a = fabsf(g_eng.outR[i]); if (a > peak) peak = a;
		}
	}
	SendVstEvents(effect, &off, 1, 0);
	PumpSilent(effect, NULL, 2);
	EnsLog(L"probe peak=%.6f", peak);
	return peak >= 1.0e-5f ? 1 : 0;
}

static int ProbeLoadedVst3Audible(Vst3Inst* vst3, int drums)
{
	if (!Vst3IsOk(vst3)) return 0;
	if (drums) {
		Vst3MidiShort(vst3, 0x90 | (36 << 8) | (100 << 16), 0);
		Vst3MidiShort(vst3, 0x90 | (38 << 8) | (100 << 16), 0);
		Vst3MidiShort(vst3, 0x90 | (42 << 8) | (100 << 16), 0);
		Vst3MidiShort(vst3, 0x99 | (36 << 8) | (100 << 16), 0);
		Vst3MidiShort(vst3, 0x99 | (38 << 8) | (100 << 16), 0);
		Vst3MidiShort(vst3, 0x99 | (42 << 8) | (100 << 16), 0);
	} else {
		Vst3MidiShort(vst3, 0x90 | (60 << 8) | (100 << 16), 0);
		Vst3MidiShort(vst3, 0x90 | (48 << 8) | (100 << 16), 0);
	}
	float peak = 0;
	for (int b = 0; b < 8; ++b) {
		Vst3Process(vst3, g_eng.outL, g_eng.outR, BLOCK_FRAMES);
		for (int i = 0; i < BLOCK_FRAMES; ++i) {
			float a = fabsf(g_eng.outL[i]); if (a > peak) peak = a;
			a = fabsf(g_eng.outR[i]); if (a > peak) peak = a;
		}
	}
	Vst3MidiShort(vst3, 0x80 | (60 << 8), 0);
	Vst3MidiShort(vst3, 0x80 | (48 << 8), 0);
	Vst3MidiShort(vst3, 0x80 | (36 << 8), 0);
	Vst3MidiShort(vst3, 0x80 | (38 << 8), 0);
	Vst3MidiShort(vst3, 0x80 | (42 << 8), 0);
	Vst3MidiShort(vst3, 0x89 | (36 << 8), 0);
	Vst3MidiShort(vst3, 0x89 | (38 << 8), 0);
	Vst3MidiShort(vst3, 0x89 | (42 << 8), 0);
	PumpSilent(NULL, vst3, 2);
	EnsLog(L"probe vst3 peak=%.6f", peak);
	return peak >= 1.0e-5f ? 1 : 0;
}

static int PathFileExistsW2(const wchar_t* path)
{
	DWORD a = GetFileAttributesW(path);
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// SOUND Canvas VA.dll is the entry the user specifies (stub/wrapper).
// LoadVst2 sets the DLL directory to its folder so SCCore.dll loads beside it.
// Do not rewrite to ProgramData and do not require a separate Wrapper.dll.
static int ResolveRolandScVaPath(const wchar_t* in, wchar_t* out, int outChars, int wantArch)
{
	(void)wantArch;
	if (!out || outChars <= 0) return 0;
	out[0] = 0;
	if (!in || !*in) return 0;
	SafeCopy(out, outChars, in);
	return PathFileExistsW2(out);
}

static int FindSiblingVst3(const wchar_t* anyPath, wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return 0;
	out[0] = 0;
	if (!anyPath || !*anyPath) return 0;
	wchar_t dir[VST_PATH_CHARS], base[VST_NAME_CHARS], cand[VST_PATH_CHARS];
	SafeCopy(dir, VST_PATH_CHARS, anyPath);
	wchar_t* slash = wcsrchr(dir, L'\\');
	if (!slash) return 0;
	*slash = 0;
	BaseNameNoExt(anyPath, base);
	swprintf_s(cand, L"%s\\%s.vst3", dir, base);
	if (PathFileExistsW2(cand)) {
		SafeCopy(out, outChars, cand);
		return 1;
	}
	// Same folder: any .vst3 whose name shares SC/Canvas/multi cues with primary.
	WIN32_FIND_DATAW fd = {};
	wchar_t pat[VST_PATH_CHARS];
	swprintf_s(pat, L"%s\\*.vst3", dir);
	HANDLE h = FindFirstFileW(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return 0;
	int best = 0;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		swprintf_s(cand, L"%s\\%s", dir, fd.cFileName);
		if (DetectMultiTimbralName(cand) || PathLooksLikeScVa(cand) ||
			ContainsI(fd.cFileName, base)) {
			SafeCopy(out, outChars, cand);
			best = 1;
			break;
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	return best;
}

static int ResolveVst2PathForPortB(const wchar_t* primary, wchar_t* out, int outChars)
{
	if (!out || outChars <= 0) return 0;
	out[0] = 0;
	if (primary && *primary && !EqExt(primary, L".vst3")) {
		wchar_t resolved[VST_PATH_CHARS];
		SafeCopy(resolved, VST_PATH_CHARS, primary);
		ResolveRolandScVaPath(primary, resolved, VST_PATH_CHARS, HostArch());
		if (PathFileExistsW2(resolved) && PeArch(resolved) == HostArch()) {
			SafeCopy(out, outChars, resolved);
			return 1;
		}
	}
	if (savedata.vstMultiDll[0] && !EqExt(savedata.vstMultiDll, L".vst3")) {
		wchar_t resolved[VST_PATH_CHARS];
		SafeCopy(resolved, VST_PATH_CHARS, savedata.vstMultiDll);
		ResolveRolandScVaPath(savedata.vstMultiDll, resolved, VST_PATH_CHARS, HostArch());
		if (PathFileExistsW2(resolved) && PeArch(resolved) == HostArch()) {
			SafeCopy(out, outChars, resolved);
			return 1;
		}
	}
	if (primary && *primary && EqExt(primary, L".vst3")) {
		wchar_t dll[VST_PATH_CHARS];
		SafeCopy(dll, VST_PATH_CHARS, primary);
		wchar_t* dot = wcsrchr(dll, L'.');
		if (dot) wcscpy_s(dot, VST_PATH_CHARS - (int)(dot - dll), L".dll");
		wchar_t resolved[VST_PATH_CHARS];
		SafeCopy(resolved, VST_PATH_CHARS, dll);
		ResolveRolandScVaPath(dll, resolved, VST_PATH_CHARS, HostArch());
		if (PathFileExistsW2(resolved) && PeArch(resolved) == HostArch()) {
			SafeCopy(out, outChars, resolved);
			return 1;
		}
		wchar_t dir[VST_PATH_CHARS], base[VST_NAME_CHARS], cand[VST_PATH_CHARS];
		SafeCopy(dir, VST_PATH_CHARS, primary);
		wchar_t* slash = wcsrchr(dir, L'\\');
		if (slash) {
			*slash = 0;
			BaseNameNoExt(primary, base);
			WIN32_FIND_DATAW fd = {};
			wchar_t pat[VST_PATH_CHARS];
			swprintf_s(pat, L"%s\\*.dll", dir);
			HANDLE h = FindFirstFileW(pat, &fd);
			if (h != INVALID_HANDLE_VALUE) {
				do {
					if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
					swprintf_s(cand, L"%s\\%s", dir, fd.cFileName);
					if (!(DetectMultiTimbralName(cand) || PathLooksLikeScVa(cand) ||
						ContainsI(fd.cFileName, base)))
						continue;
					if (PeArch(cand) != HostArch()) continue;
					SafeCopy(out, outChars, cand);
					FindClose(h);
					return 1;
				} while (FindNextFileW(h, &fd));
				FindClose(h);
			}
		}
	}
	return 0;
}

// Port1 (ch17-32) → 2nd VST2; port2+ (ch33-48) → VST3 (VST2 fallback).
static void LoadPortExtraUnits(const wchar_t* primaryPath, int resetMode)
{
	if (g_eng.maxMidiPort < 1 || g_eng.useMapper || g_eng.useEnsemble) return;

	wchar_t vst2Path[VST_PATH_CHARS];
	if (!ResolveVst2PathForPortB(primaryPath, vst2Path, VST_PATH_CHARS)) {
		EnsLog(L"portB: no VST2 path for maxPort=%d", g_eng.maxMidiPort);
		return;
	}
	if (!LoadVst2(vst2Path, g_eng.moduleB, g_eng.effectB)) {
		EnsLog(L"portB VST2 FAIL %s", vst2Path);
		return;
	}
	SendGmGsReset(g_eng.effectB, NULL, resetMode);
	EnsLog(L"portB VST2 OK (ch17-32) %s", vst2Path);

	if (g_eng.maxMidiPort < 2) return;

	wchar_t vst3Path[VST_PATH_CHARS];
	if (FindSiblingVst3(primaryPath ? primaryPath : vst2Path, vst3Path, VST_PATH_CHARS) ||
		FindSiblingVst3(vst2Path, vst3Path, VST_PATH_CHARS)) {
		g_eng.vst3C = Vst3Open(vst3Path);
		if (!Vst3IsOk(g_eng.vst3C)) {
			EnsLog(L"portC VST3 FAIL %s (%s)", vst3Path,
				Vst3LastError() ? Vst3LastError() : L"?");
			Vst3Close(g_eng.vst3C); g_eng.vst3C = NULL;
		} else {
			SendGmGsReset(NULL, g_eng.vst3C, resetMode);
			EnsLog(L"portC VST3 OK (ch33+) %s", vst3Path);
			return;
		}
	}
	// No usable VST3: third VST2 instance keeps 33+ from going silent.
	if (LoadVst2(vst2Path, g_eng.moduleC, g_eng.effectC)) {
		SendGmGsReset(g_eng.effectC, NULL, resetMode);
		EnsLog(L"portC VST2 fallback OK %s", vst2Path);
	} else {
		EnsLog(L"portC: no unit for maxPort=%d", g_eng.maxMidiPort);
	}
}

static int TryLoadPluginPath(const wchar_t* path, int isVst3)
{
	if (!path || !*path) return 0;
	wchar_t resolved[VST_PATH_CHARS];
	SafeCopy(resolved, VST_PATH_CHARS, path);
	if (!isVst3 && !EqExt(path, L".vst3"))
		ResolveRolandScVaPath(path, resolved, VST_PATH_CHARS, HostArch());
	path = resolved;
	if (isVst3 || EqExt(path, L".vst3")) {
		g_eng.vst3 = Vst3Open(path);
		if (!Vst3IsOk(g_eng.vst3)) {
			Vst3Close(g_eng.vst3); g_eng.vst3 = NULL;
			EnsLog(L"TryLoad VST3 FAIL %s (%s)", path,
				Vst3LastError() ? Vst3LastError() : L"?");
			return 0;
		}
		g_eng.usingBuiltin = 0;
		return 1;
	}
	// Wrong-arch DLL must go through KpiHost64 (x86 app + x64 SC-VA).
	if (PeArch(path) != HostArch()) return 0;
	if (!PathFileExistsW2(path)) return 0;
	if (LoadVst2(path, g_eng.module, g_eng.effect)) {
		g_eng.usingBuiltin = 0; return 1;
	}
	return 0;
}

// Same idea as the scan probe, but against the plug-in already loaded into the
// song engine, where rendering is synchronous and therefore cheap.
static double ProbeEngineStage(int channel0, int note, int velocity)
{
	AEffect* e = g_eng.effect;
	Vst3Inst* v = g_eng.vst3;
	if (!e && !v) return 0.0;

	const DWORD noteOn = (DWORD)(0x90 | (channel0 & 15)) |
		((DWORD)note << 8) | ((DWORD)velocity << 16);
	const DWORD noteOff = (DWORD)(0x80 | (channel0 & 15)) | ((DWORD)note << 8);

	if (v) Vst3MidiShort(v, noteOn, 0);
	if (e) { MidiItem on = {}; on.msg = noteOn; SendVstEvents(e, &on, 1, 0); }

	static __declspec(align(32)) float l[BLOCK_FRAMES];
	static __declspec(align(32)) float r[BLOCK_FRAMES];
	double peak = 0.0;
	const int blocks = SAMPLE_RATE / BLOCK_FRAMES; // one second at most
	for (int b = 0; b < blocks; ++b) {
		if (v) Vst3Process(v, l, r, BLOCK_FRAMES);
		else RenderEffect(e, l, r, BLOCK_FRAMES);
		for (int i = 0; i < BLOCK_FRAMES; ++i) {
			const double a = fabs((double)l[i]), c = fabs((double)r[i]);
			if (a > peak) peak = a;
			if (c > peak) peak = c;
		}
		if (peak * 1000.0 >= (double)PROBE_AUDIBLE_MILLI) break;
	}
	if (v) Vst3MidiShort(v, noteOff, 0);
	if (e) { MidiItem off = {}; off.msg = noteOff; SendVstEvents(e, &off, 1, 0); }
	return peak;
}

static int ProbeEngineAudible(int* outMilli)
{
	double peak = ProbeEngineStage(0, 60, 100);
	if (peak * 1000.0 < (double)PROBE_AUDIBLE_MILLI) {
		const double drum = ProbeEngineStage(9, 36, 110);
		if (drum > peak) peak = drum;
	}
	const int milli = (int)(peak * 1000.0 + 0.5);
	if (outMilli) *outMilli = milli;
	return milli >= PROBE_AUDIBLE_MILLI ? 1 : 0;
}

static int ResetModeForPath(const wchar_t* p)
{
	if (!p) return 0;
	if (ContainsI(p, L"YXG") || ContainsI(p, L"S-YXG") ||
		ContainsI(p, L"SGP2") || ContainsI(p, L"SoftXG") ||
		(ContainsI(p, L"XG") && !ContainsI(p, L"SC")))
		return 2;
	if (DetectMultiTimbralName(p) ||
		ContainsI(p, L"Canvas") || ContainsI(p, L"SC-") ||
		ContainsI(p, L"SCVA") || ContainsI(p, L"8820") ||
		ContainsI(p, L"SC88") || ContainsI(p, L"SGP"))
		return 1;
	return 0;
}

static void UnloadSongPlugin()
{
	if (g_eng.vst3) { Vst3Close(g_eng.vst3); g_eng.vst3 = NULL; }
	if (g_eng.effect || g_eng.module) CloseEffect(g_eng.module, g_eng.effect);
}

// Load a candidate under real playing conditions (reset sent first) and keep it
// only if it actually makes a sound. Leaves nothing loaded when it fails, so the
// caller can simply try the next one.
static int TryLoadAudible(const wchar_t* path, int* outReset, int* outMilli)
{
	if (!TryLoadPluginPath(path, 0)) return 0;
	const int reset = ResetModeForPath(path);
	SendGmGsReset(g_eng.effect, g_eng.vst3, reset);

	/* プラグインがリセット処理を終えるのを待つ */
	if (g_eng.effect || g_eng.vst3) {
		for (int i = 0; i < 12; ++i) {
			__declspec(align(32)) float l[BLOCK_FRAMES] = {};
			__declspec(align(32)) float r[BLOCK_FRAMES] = {};
			if (g_eng.vst3) Vst3Process(g_eng.vst3, l, r, BLOCK_FRAMES);
			else if (g_eng.effect) RenderEffect(g_eng.effect, l, r, BLOCK_FRAMES);
		}
	}

	int milli = 0;
	const int ok = ProbeEngineAudible(&milli);
	EnsLog(L"song probe %s peak=%d/1000 path=%s",
		ok ? L"SOUND" : L"SILENT", milli, path);
	if (outMilli) *outMilli = milli;
	if (!ok) { UnloadSongPlugin(); return 0; }
	// The probe advanced the instrument by up to a second; start the song clean.
	SendGmGsReset(g_eng.effect, g_eng.vst3, reset);
	if (outReset) *outReset = reset;
	return 1;
}

// When the song engine runs inside KpiHost64 there is no plug-in list: scanning
// belongs to ogg.exe. Both executables ship in the same folder, so the cache the
// host UI wrote is readable here and already carries the audible verdicts. Age
// is irrelevant for this use - a stale verdict beats a silent song.
static void EnsureCandidateList()
{
	if (!g_pluginCount) ReadCacheFile();
}

static int AlreadyListed(const wchar_t* path)
{
	for (int i = 0; i < g_pluginCount; ++i)
		if (_wcsicmp(g_plugins[i].path, path) == 0) return 1;
	return 0;
}

static void SweepDirForMulti(const wchar_t* dir, int depth,
	wchar_t out[][VST_PATH_CHARS], int& count, int max)
{
	if (!dir || !*dir || depth > 3 || count >= max || !DirExists(dir)) return;
	wchar_t pat[VST_PATH_CHARS];
	JoinPath(pat, dir, L"*");
	WIN32_FIND_DATAW fd = {};
	HANDLE h = FindFirstFileW(pat, &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.cFileName[0] == L'.') continue;
		wchar_t full[VST_PATH_CHARS];
		JoinPath(full, dir, fd.cFileName);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			SweepDirForMulti(full, depth + 1, out, count, max);
			continue;
		}
		if (count >= max) break;
		if (!EqExt(full, L".dll")) continue;
		if (!DetectMultiTimbralName(full) && !PathLooksLikeScVa(full)) continue;
		if (PeArch(full) != HostArch()) continue;
		int dup = 0;
		for (int i = 0; i < count; ++i)
			if (_wcsicmp(out[i], full) == 0) { dup = 1; break; }
		if (!dup) SafeCopy(out[count++], VST_PATH_CHARS, full);
	} while (FindNextFileW(h, &fd) && count < max);
	FindClose(h);
}

// Last resort when no scan has ever run. The same product is routinely
// installed in several places at once and only some copies work, so sweeping
// the usual locations is what turns "silent song" into "it found the good one".
static int CollectMultiCandidates(wchar_t out[][VST_PATH_CHARS], int max)
{
	wchar_t roots[16][VST_PATH_CHARS];
	int nroots = 0;
	static const wchar_t* fixed[] = {
		L"C:\\Program Files\\Common Files\\VST3",
		L"C:\\Program Files\\Steinberg\\VstPlugins",
		L"C:\\Program Files\\Steinberg\\VSTPlugins",
		L"C:\\Program Files\\Roland\\SOUND Canvas VA",
		L"C:\\Program Files (x86)\\Roland\\SOUND Canvas VA",
		L"C:\\Yamaha\\S-YXG50",
	};
	AddRegRoot(HKEY_LOCAL_MACHINE, roots, nroots, 16);
	AddRegRoot(HKEY_CURRENT_USER, roots, nroots, 16);
	{
		wchar_t pd[MAX_PATH] = {};
		if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, 0, pd)) &&
			nroots < 16)
			_snwprintf_s(roots[nroots++], VST_PATH_CHARS, _TRUNCATE,
				L"%s\\Roland Cloud\\SOUND Canvas VA", pd);
	}
	if (savedata.vstExtraPath[0] && nroots < 16)
		SafeCopy(roots[nroots++], VST_PATH_CHARS, savedata.vstExtraPath);
	if (savedata.vstMultiDll[0] && DirExists(savedata.vstMultiDll) && nroots < 16)
		SafeCopy(roots[nroots++], VST_PATH_CHARS, savedata.vstMultiDll);
	for (int i = 0; i < (int)(sizeof(fixed) / sizeof(fixed[0])) && nroots < 16; ++i)
		SafeCopy(roots[nroots++], VST_PATH_CHARS, fixed[i]);

	int count = 0;
	if (savedata.vstMultiDll[0] && count < max) {
		DWORD a = GetFileAttributesW(savedata.vstMultiDll);
		if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY) &&
			EqExt(savedata.vstMultiDll, L".dll") &&
			PeArch(savedata.vstMultiDll) == HostArch())
			SafeCopy(out[count++], VST_PATH_CHARS, savedata.vstMultiDll);
	}
	for (int i = 0; i < nroots && count < max; ++i)
		SweepDirForMulti(roots[i], 0, out, count, max);
	return count;
}

// The configured instrument first, then every other multi-timbral one the scan
// found, so a dud install degrades into "a different synth plays the song"
// instead of "the song is silent".
static int LoadFirstAudibleCandidate(const wchar_t* preferred, wchar_t* outPath,
	int outChars, int* outReset, int* outMilli)
{
	if (preferred && *preferred && TryLoadAudible(preferred, outReset, outMilli)) {
		SafeCopy(outPath, outChars, preferred);
		return 1;
	}
	EnsureCandidateList();
	// Proven-audible entries first, then untested ones. A sampler waiting for a
	// patch is no use for unattended song playback, so isAudible == 2 is not a
	// candidate here even though the host palette still lists it.
	for (int pass = 0; pass < 2; ++pass) {
		for (int i = 0; i < g_pluginCount; ++i) {
			const VstPluginInfo& p = g_plugins[i];
			if (!p.isInstrument || !p.isMultiTimbral) continue;
			if (p.isAudible == 2) continue;
			if (pass == 0 && p.isAudible != 1) continue;
			if (pass == 1 && p.isAudible == 1) continue;
			if (preferred && *preferred &&
				_wcsicmp(p.path, preferred) == 0) continue;
			if (!p.isVst3 && PeArch(p.path) != HostArch()) continue;
			if (!TryLoadAudible(p.path, outReset, outMilli)) continue;
			SafeCopy(outPath, outChars, p.path);
			EnsLog(L"song fallback picked [%s] after [%s] was silent",
				p.path, preferred ? preferred : L"(none)");
			return 1;
		}
	}

	// Nothing in the list worked, or there was no list to walk.
	enum { SWEEP_MAX = 12 };
	wchar_t cand[SWEEP_MAX][VST_PATH_CHARS];
	const int n = CollectMultiCandidates(cand, SWEEP_MAX);
	EnsLog(L"song sweep found %d candidate(s)", n);
	for (int i = 0; i < n; ++i) {
		if (preferred && *preferred && _wcsicmp(cand[i], preferred) == 0) continue;
		if (AlreadyListed(cand[i])) continue; // already tried above
		if (!TryLoadAudible(cand[i], outReset, outMilli)) continue;
		SafeCopy(outPath, outChars, cand[i]);
		EnsLog(L"song sweep picked [%s] after [%s] was silent",
			cand[i], preferred ? preferred : L"(none)");
		return 1;
	}
	return 0;
}

extern "C" int VstMidiOpen(const wchar_t* midPath,
	const wchar_t hints[][128], int hintCount, HWND parentForWait)
{
	(void)hints; (void)hintCount; (void)parentForWait;
	if (!midPath) return -1;
	EnterCriticalSection(&g_eng.cs);
	FreeSong();
	int rc = LoadSmf(midPath);
	if (rc < 0) { LeaveCriticalSection(&g_eng.cs); return rc; }

	int loaded = 0;
	int resetMode = 0;
	int probeMilli = 0;
	wchar_t pickDll[VST_PATH_CHARS];
	wchar_t usedDll[VST_PATH_CHARS];
	pickDll[0] = 0;
	usedDll[0] = 0;
	const wchar_t* loadedPath = NULL;
	if (!PickGsXgDll(midPath, pickDll, VST_PATH_CHARS)) {
		if (!MapperOpen()) {
			LeaveCriticalSection(&g_eng.cs);
			return -5;
		}
		loaded = 1;
	} else {
		loaded = LoadFirstAudibleCandidate(pickDll, usedDll, VST_PATH_CHARS,
			&resetMode, &probeMilli);
		if (loaded) loadedPath = usedDll;
		else if (TryLoadPluginPath(pickDll, 0)) {
			// Nothing on this machine passed the sound check. Play through the
			// configured instrument anyway rather than refusing to open: the
			// user gets the same result as before plus a log line saying why.
			loaded = 1;
			loadedPath = pickDll;
			resetMode = ResetModeForPath(pickDll);
			EnsLog(L"song NO AUDIBLE CANDIDATE, using [%s] as-is", pickDll);
		}
	}
	if (loadedPath) {
		g_eng.gmResetMode = resetMode;
		SendGmGsReset(g_eng.effect, g_eng.vst3, resetMode);
		LoadPortExtraUnits(loadedPath, resetMode);
	} else {
		g_eng.gmResetMode = 0;
	}
	const int hasOut = (g_eng.useMapper || g_eng.effect || g_eng.vst3 ||
		g_eng.effectB || g_eng.effectC || g_eng.vst3C) ? 1 : 0;
	g_eng.usingBuiltin = 0;
	g_eng.useEnsemble = 0;
	g_eng.eventPos = 0;
	g_eng.playSample = 0;
	g_eng.ringRead = g_eng.ringCount = 0;
	ZeroMemory(g_eng.voices, sizeof(g_eng.voices));
	ZeroMemory(g_eng.drums, sizeof(g_eng.drums));
	ZeroMemory(g_eng.noteState, sizeof(g_eng.noteState));
	if (!g_eng.useMapper)
		ResetSequence();

	/* GM/GS/XGリセット後、プラグイン（特にSC-VAなど）が内部でリセット処理を
	 * 完了する前にCC/PCを送りつけると無視されてしまうため、少しだけ空レンダリングして
	 * リセットを消化させる。 */
	if (g_eng.effect || g_eng.vst3 || g_eng.effectB || g_eng.effectC || g_eng.vst3C || g_eng.useEnsemble) {
		for (int i = 0; i < 12; ++i) { // 12 * 512 = 6144 frames (~139ms)
			if (g_eng.useEnsemble)
				RenderEnsemble(g_eng.outL, g_eng.outR, BLOCK_FRAMES);
			else
				RenderSongUnits(BLOCK_FRAMES);
		}
	}
	EnsLog(L"VstMidiOpen pick=[%s] used=[%s] peak=%d/1000 loaded=%d effect=%p "
		L"vst3=%p mapper=%d hasOut=%d reset=%d gs=[%s] xg=[%s]",
		pickDll[0] ? pickDll : L"(none)",
		loadedPath ? loadedPath : L"(none)", probeMilli, loaded,
		(void*)g_eng.effect, (void*)g_eng.vst3, g_eng.useMapper,
		hasOut, resetMode,
		savedata.vstMultiDll[0] ? savedata.vstMultiDll : L"(empty)",
		savedata.vstExtraPath[0] ? savedata.vstExtraPath : L"(empty)");
	LeaveCriticalSection(&g_eng.cs);
	return hasOut ? 0 : -5;
}

extern "C" void VstMidiLog(const wchar_t* msg)
{
	if (msg && msg[0]) EnsLog(L"%s", msg);
}

static int g_reportedLatencySamples[2] = { 0, 0 };

extern "C" void VstMidiClose(void)
{
	EnterCriticalSection(&g_eng.cs);
	FreeSong();
	LeaveCriticalSection(&g_eng.cs);
	g_reportedLatencySamples[VstIoSlot()] = 0;
}

extern "C" void VstMidiCloseSlot(int slot)
{
	const int prev = VstIoSlot();
	VstMidiSetIoSlot(slot);
	VstMidiClose();
	VstMidiSetIoSlot(prev);
}

extern "C" int VstMidiRead(BYTE* dst, int bytesWanted)
{
	if (!dst || bytesWanted <= 0) return 0;
	EnterCriticalSection(&g_eng.cs);
	const int injPending = (g_injW != g_injR) ? 1 : 0;
	if (!g_eng.events && !injPending && g_liveTailFrames <= 0) {
		LeaveCriticalSection(&g_eng.cs); return 0;
	}
	int written = 0;
	while (written < bytesWanted) {
		if (!g_eng.ringCount) {
			const int past = (!g_eng.events || g_eng.playSample >= g_eng.lengthSamples) ? 1 : 0;
			if (past && (g_injW == g_injR) && g_liveTailFrames <= 0)
				break;
			int frames = BLOCK_FRAMES;
			if (!past && g_eng.lengthSamples - g_eng.playSample < frames)
				frames = (int)(g_eng.lengthSamples - g_eng.playSample);
			if (frames < 1) frames = BLOCK_FRAMES;
			if (g_eng.useEnsemble) {
				DispatchEnsemble(g_eng.playSample, frames);
				RenderEnsemble(g_eng.outL, g_eng.outR, frames);
			} else {
				DispatchDueEvents(g_eng.playSample, frames);
				RenderSongUnits(frames);
			}
			g_eng.ringRead = 0;
			g_eng.ringCount = frames * 2;
			for (int i = 0; i < frames; ++i) {
				float l = g_eng.outL[i], r = g_eng.outR[i];
				if (l < -1) l = -1; if (l > 1) l = 1;
				if (r < -1) r = -1; if (r > 1) r = 1;
				g_eng.ring[i * 2] = (short)(l * 32767.0f);
				g_eng.ring[i * 2 + 1] = (short)(r * 32767.0f);
			}
			g_eng.playSample += frames;
			if (g_liveTailFrames > 0) {
				g_liveTailFrames -= frames;
				if (g_liveTailFrames < 0) g_liveTailFrames = 0;
			}
		}
		int availBytes = g_eng.ringCount * (int)sizeof(short);
		int take = bytesWanted - written;
		if (take > availBytes) take = availBytes;
		// Preserve sample boundaries.
		take &= ~1;
		if (!take) break;
		memcpy(dst + written, g_eng.ring + g_eng.ringRead, take);
		const int samples = take / sizeof(short);
		g_eng.ringRead += samples;
		g_eng.ringCount -= samples;
		written += take;
	}
	LeaveCriticalSection(&g_eng.cs);
	return written;
}

// シーク先までの音色/音量/ノート状態を正しく作るため、MIDI を通常再生と同じ
// Dispatch+process 経路で超高速レンダし、PCM は破棄する（手抜きの CC だけ飛ばしはしない）。
static void VstRenderBlockDiscard(int frames)
{
	if (frames <= 0) return;
	if (frames > BLOCK_FRAMES) frames = BLOCK_FRAMES;
	if (g_eng.useEnsemble) {
		DispatchEnsemble(g_eng.playSample, frames);
		RenderEnsemble(g_eng.outL, g_eng.outR, frames);
	} else {
		DispatchDueEvents(g_eng.playSample, frames);
		RenderSongUnits(frames);
	}
	g_eng.playSample += frames;
}

/* 飛ばす区間は音を作らない。音色/音量/ベンド/ドラムマップを決める非ノート
 * イベントだけを塊で送り、プラグインに確定させる極小ブロックだけ process する。
 * 曲頭から目標まで丸ごとレンダしていた時間（6分の曲で数秒〜十数秒）が消える。
 * ノートは着地直前 preroll を通常レンダで鳴らすので、そこから普通に聞こえる。 */
static void SeekFastForwardEvents(__int64 ffEnd)
{
	enum { FF_BATCH = 256, FF_FRAMES = 64 };
	if (ffEnd <= 0 || !g_eng.events) return;
	MidiItem batch0[FF_BATCH], batch1[FF_BATCH], batch2[FF_BATCH];
	int n0 = 0, n1 = 0, n2 = 0;
	BYTE ensDirty[MIX_SLOTS] = {};
	int pending = 0;
	/* delta を 0 に揃えるため sample を start へ書き換えて送る */
	const __int64 start = 0;
	auto micro = [&]() {
		if (g_eng.useEnsemble)
			RenderEnsemble(g_eng.outL, g_eng.outR, FF_FRAMES);
		else
			RenderSongUnits(FF_FRAMES);
		ZeroMemory(ensDirty, sizeof(ensDirty));
		pending = 0;
	};
	auto flushAll = [&]() {
		FlushUnitShorts(g_eng.effect, g_eng.vst3, batch0, n0, start, FF_FRAMES, 0, 0);
		FlushUnitShorts(g_eng.effectB, NULL, batch1, n1, start, FF_FRAMES, 0, 1);
		FlushUnitShorts(g_eng.effectC, g_eng.vst3C, batch2, n2, start, FF_FRAMES, 0, 2);
	};
	while (g_eng.eventPos < g_eng.eventCount &&
		g_eng.events[g_eng.eventPos].sample < ffEnd) {
		MidiItem e = g_eng.events[g_eng.eventPos++];
		e.msg = SongOverrideMsg(e.port, e.msg);
		if ((e.msg & 0xff) == 0xff) continue;
		const int isSysex = ((e.msg & 0xff) == 0xf0 && e.sysexOff >= 0 && g_eng.sysexData) ? 1 : 0;
		const int type = (int)(e.msg & 0xf0);
		if (!isSysex && (type == 0x80 || type == 0x90))
			continue;
		e.sample = start;
		if (isSysex) {
			flushAll();
			if (pending) micro();
			const int len = (int)e.aux;
			if (e.sysexOff + len <= g_eng.sysexBytes)
				BroadcastSongSysex(g_eng.sysexData + e.sysexOff, len, 0, e.port);
			micro();
			continue;
		}
		if (g_eng.midiOut && (e.port <= 0 || !g_eng.effectB))
			midiOutShortMsg(g_eng.midiOut, e.msg);
		if (g_eng.useEnsemble) {
			const int ch = (int)(e.msg & 15);
			const int s = g_eng.chSlot[ch];
			if (s < 0 || s >= g_eng.mixCount) continue;
			MixSlot& ms = g_eng.mix[s];
			MidiItem m = e;
			if (!ms.keepMidiCh) {
				if (type == 0xc0) continue;
				m.msg = (m.msg & ~0x0fu) | 0u;
			}
			/* VST2 は effProcessEvents を続けて呼ぶと最後の1件しか残らない */
			if (ms.effect && ensDirty[s]) micro();
			if (ms.effect) { SendVstEvents(ms.effect, &m, 1, start); ensDirty[s] = 1; }
			if (ms.vst3) Vst3MidiShort(ms.vst3, m.msg, 0);
			if (++pending >= FF_BATCH) micro();
			continue;
		}
		const int port = e.port < 0 ? 0 : e.port;
		if (port <= 0) {
			if (!UnitHasPlug(0)) continue;
			if (n0 >= FF_BATCH) { flushAll(); micro(); }
			batch0[n0++] = e;
			if (g_eng.mirrorToB && UnitHasPlug(1)) {
				if (n1 >= FF_BATCH) { flushAll(); micro(); }
				batch1[n1++] = e;
			}
		} else if (port == 1) {
			if (!UnitHasPlug(1)) continue;
			if (n1 >= FF_BATCH) { flushAll(); micro(); }
			batch1[n1++] = e;
		} else {
			if (!UnitHasPlug(2)) continue;
			if (n2 >= FF_BATCH) { flushAll(); micro(); }
			batch2[n2++] = e;
		}
		++pending;
	}
	flushAll();
	micro();
}

extern "C" int VstMidiSeekSamples(__int64 samplePos)
{
	EnterCriticalSection(&g_eng.cs);
	if (!g_eng.events) { LeaveCriticalSection(&g_eng.cs); return -1; }
	if (samplePos < 0) samplePos = 0;
	if (samplePos > g_eng.lengthSamples) samplePos = g_eng.lengthSamples;

	g_eng.ringRead = g_eng.ringCount = 0;

	// 常に先頭へ戻してから目標まで進める。途中の PC/CC/ピッチベンド/sysex が
	// 欠けると「音色や音量が違う」になるため、前方シークでも飛ばさない。
	ResetSequence();
	if (g_eng.effect || g_eng.vst3)
		SendGmGsReset(g_eng.effect, g_eng.vst3, g_eng.gmResetMode);
	if (g_eng.effectB)
		SendGmGsReset(g_eng.effectB, NULL, g_eng.gmResetMode);
	if (g_eng.effectC || g_eng.vst3C)
		SendGmGsReset(g_eng.effectC, g_eng.vst3C, g_eng.gmResetMode);
	if (g_eng.useEnsemble) {
		for (int s = 0; s < g_eng.mixCount; ++s) {
			if (g_eng.mix[s].effect || g_eng.mix[s].vst3)
				SendGmGsReset(g_eng.mix[s].effect, g_eng.mix[s].vst3, g_eng.gmResetMode);
		}
	}
	
	/* GM/GS/XGリセット後、プラグイン（特にSC-VAなど）が内部でリセット処理を
	 * 完了する前にCC/PCを送りつけると無視されてしまうため、少しだけ空レンダリングして
	 * リセットを消化させる。 */
	if (g_eng.effect || g_eng.vst3 || g_eng.effectB || g_eng.effectC || g_eng.vst3C || g_eng.useEnsemble) {
		for (int i = 0; i < 12; ++i) { // 12 * 512 = 6144 frames (~139ms)
			if (g_eng.useEnsemble)
				RenderEnsemble(g_eng.outL, g_eng.outR, BLOCK_FRAMES);
			else
				RenderSongUnits(BLOCK_FRAMES);
		}
	}

	/* 着地の少し前までイベント早送り、そこから通常レンダ（鳴っているノート・残響用） */
	const __int64 preroll = SAMPLE_RATE / 2;
	__int64 ffEnd = samplePos - preroll;
	if (ffEnd > 0) {
		SeekFastForwardEvents(ffEnd);
		g_eng.playSample = ffEnd;
	}
	while (g_eng.playSample < samplePos) {
		int frames = BLOCK_FRAMES;
		const __int64 remain = samplePos - g_eng.playSample;
		if (remain < frames) frames = (int)remain;
		VstRenderBlockDiscard(frames);
	}

	// 着地時点で鳴っているノートはそのまま（通常再生と同じ）。リングは空。
	g_eng.ringRead = g_eng.ringCount = 0;
	LeaveCriticalSection(&g_eng.cs);
	return 0;
}

extern "C" int VstMidiHasPluginAudio(void)
{
	return (g_eng.effect || g_eng.vst3 || g_eng.effectB || g_eng.effectC ||
		g_eng.vst3C || g_eng.useEnsemble) ? 1 : 0;
}
extern "C" int VstMidiGetRate(void) { return SAMPLE_RATE; }
extern "C" int VstMidiGetChannels(void) { return 2; }
extern "C" int VstMidiGetBits(void) { return 16; }
extern "C" __int64 VstMidiGetLengthSamples(void) { return g_eng.lengthSamples; }
extern "C" double VstMidiTailPadSec(void) { return (double)VST_TAIL_PAD_SEC; }

static int ClampLat(int d)
{
	if (d < 0) d = 0;
	if (d > SAMPLE_RATE * 2) d = SAMPLE_RATE * 2;
	return d;
}
static int MaxLat(int a, int b) { return a > b ? a : b; }

extern "C" void VstMidiSetReportedLatencySamples(int samples)
{
	if (samples < 0) samples = 0;
	if (samples > SAMPLE_RATE * 2) samples = SAMPLE_RATE * 2;
	g_reportedLatencySamples[VstIoSlot()] = samples;
}

extern "C" int VstMidiGetLatencySamples(void)
{
	int lat = 0;
	if (g_eng.effect) lat = MaxLat(lat, ClampLat(g_eng.effect->initialDelay));
	lat = MaxLat(lat, ClampLat(Vst3GetLatencySamples(g_eng.vst3)));
	if (g_eng.effectB) lat = MaxLat(lat, ClampLat(g_eng.effectB->initialDelay));
	if (g_eng.effectC) lat = MaxLat(lat, ClampLat(g_eng.effectC->initialDelay));
	lat = MaxLat(lat, ClampLat(Vst3GetLatencySamples(g_eng.vst3C)));
	if (g_eng.useEnsemble) {
		for (int s = 0; s < g_eng.mixCount; ++s) {
			if (g_eng.mix[s].effect)
				lat = MaxLat(lat, ClampLat(g_eng.mix[s].effect->initialDelay));
			lat = MaxLat(lat, ClampLat(Vst3GetLatencySamples(g_eng.mix[s].vst3)));
		}
	}
	if (lat > 0) return lat;
	return g_reportedLatencySamples[VstIoSlot()];
}

#ifndef KPIHOST64_BUILD

// ---------------------------------------------------------------------------
// Cross-architecture live parts
//
// A 32-bit process cannot load SOUND Canvas VA (x64), so the plug-in lives in
// KpiHost64. The pipe carries only load/unload/editor: notes go through a MIDI
// ring and audio comes back through an audio ring, so neither the keyboard nor
// the wave-out thread can be blocked by a pending request.
// ---------------------------------------------------------------------------

extern KpiHost64Client g_kpiHost;

enum { LIVE_REMOTE_PREBUFFER = 512 };

struct LiveRemoteShm {
	HANDLE hAudioMap;
	HANDLE hMidiMap;
	HANDLE hWake;
	KPIHOST64_VstLiveAudioShm* audio;
	KPIHOST64_VstLiveMidiShm* midi;
	int parts;
	int primed; // 1 once the ring has reached LIVE_REMOTE_PREBUFFER
};

static LiveRemoteShm g_liveShm;
static volatile LONG g_liveShuttingDown = 0;

// The wave-out thread reads the rings while the UI thread can unload a part and
// unmap them, so the pointers themselves are published under this lock.
static SRWLOCK g_liveShmLock = SRWLOCK_INIT;

static float* LiveShmL(KPIHOST64_VstLiveAudioShm* s) { return (float*)(s + 1); }
static float* LiveShmR(KPIHOST64_VstLiveAudioShm* s)
{
	return LiveShmL(s) + s->capacity;
}

static void LiveRemoteCloseShm()
{
	AcquireSRWLockExclusive(&g_liveShmLock);
	LiveRemoteShm old = g_liveShm;
	g_liveShm.audio = NULL;
	g_liveShm.midi = NULL;
	g_liveShm.hWake = NULL;
	g_liveShm.hAudioMap = NULL;
	g_liveShm.hMidiMap = NULL;
	g_liveShm.primed = 0;
	ReleaseSRWLockExclusive(&g_liveShmLock);
	// Safe to release now: nobody can still be inside the ring, because every
	// reader holds the lock for as long as it touches these pointers.
	if (old.audio) UnmapViewOfFile(old.audio);
	if (old.hAudioMap) CloseHandle(old.hAudioMap);
	if (old.midi) UnmapViewOfFile(old.midi);
	if (old.hMidiMap) CloseHandle(old.hMidiMap);
	if (old.hWake) CloseHandle(old.hWake);
}

static int LiveRemoteOpenShm()
{
	if (g_liveShm.audio && g_liveShm.midi && g_liveShm.hWake) return 1;
	LiveRemoteCloseShm();
	if (!g_kpiHost.VstLiveAudioStart()) return 0;
	const SIZE_T audioBytes = sizeof(KPIHOST64_VstLiveAudioShm) +
		(SIZE_T)KPIHOST64_VST_LIVE_SHM_CAP * 2 * sizeof(float);
	const SIZE_T midiBytes = sizeof(KPIHOST64_VstLiveMidiShm) +
		(SIZE_T)KPIHOST64_VST_LIVE_MIDI_CAP * sizeof(KPIHOST64_VstLiveMidiEvent);
	LiveRemoteShm n = {};
	n.hAudioMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE,
		KPIHOST64_VST_LIVE_SHM_NAME);
	n.hMidiMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE,
		KPIHOST64_VST_LIVE_MIDI_SHM_NAME);
	n.hWake = OpenEventW(EVENT_MODIFY_STATE, FALSE,
		KPIHOST64_VST_LIVE_EVENT_NAME);
	if (n.hAudioMap)
		n.audio = (KPIHOST64_VstLiveAudioShm*)MapViewOfFile(
			n.hAudioMap, FILE_MAP_ALL_ACCESS, 0, 0, audioBytes);
	if (n.hMidiMap)
		n.midi = (KPIHOST64_VstLiveMidiShm*)MapViewOfFile(
			n.hMidiMap, FILE_MAP_ALL_ACCESS, 0, 0, midiBytes);
	if (!n.audio || !n.midi || !n.hWake) {
		if (n.audio) UnmapViewOfFile(n.audio);
		if (n.hAudioMap) CloseHandle(n.hAudioMap);
		if (n.midi) UnmapViewOfFile(n.midi);
		if (n.hMidiMap) CloseHandle(n.hMidiMap);
		if (n.hWake) CloseHandle(n.hWake);
		return 0;
	}
	AcquireSRWLockExclusive(&g_liveShmLock);
	n.parts = g_liveShm.parts;
	g_liveShm = n;
	ReleaseSRWLockExclusive(&g_liveShmLock);
	SetEvent(n.hWake);
	return 1;
}

static void LiveRemoteStop()
{
	g_kpiHost.VstLiveAudioStop();
	LiveRemoteCloseShm();
}

static void LiveRemoteMidi(int portIndex0to2, DWORD msg)
{
	AcquireSRWLockExclusive(&g_liveShmLock);
	KPIHOST64_VstLiveMidiShm* m = g_liveShm.midi;
	if (!m || !m->capacity) { ReleaseSRWLockExclusive(&g_liveShmLock); return; }
	KPIHOST64_VstLiveMidiEvent* ev = (KPIHOST64_VstLiveMidiEvent*)(m + 1);
	const uint32_t cap = m->capacity;
	const uint32_t w = m->writePos;
	if (w - m->readPos < cap) { // else the host stopped draining
		ev[w & (cap - 1)].port = (uint32_t)portIndex0to2;
		ev[w & (cap - 1)].msg = (uint32_t)msg;
		MemoryBarrier();
		InterlockedExchange((LONG*)&m->writePos, (LONG)(w + 1));
		if (g_liveShm.hWake) SetEvent(g_liveShm.hWake);
	}
	ReleaseSRWLockExclusive(&g_liveShmLock);
}

// Adds the host's output on top of whatever the local parts produced.
static void LiveRemoteMix(float* L, float* R, int frames)
{
	AcquireSRWLockExclusive(&g_liveShmLock);
	KPIHOST64_VstLiveAudioShm* s = g_liveShm.audio;
	if (!s || !s->capacity) { ReleaseSRWLockExclusive(&g_liveShmLock); return; }
	const uint32_t need = (uint32_t)frames;
	const uint32_t want = g_liveShm.primed ? need : (uint32_t)LIVE_REMOTE_PREBUFFER;
	const int spins = g_liveShm.primed ? 20 : 300;
	for (int spin = 0; spin < spins; ++spin) {
		s = g_liveShm.audio;
		if (!s || !s->capacity) { ReleaseSRWLockExclusive(&g_liveShmLock); return; }
		if (s->writePos - s->readPos >= want) break;
		HANDLE wake = g_liveShm.hWake;
		ReleaseSRWLockExclusive(&g_liveShmLock);
		if (wake) SetEvent(wake);
		Sleep(1);
		AcquireSRWLockExclusive(&g_liveShmLock);
	}
	s = g_liveShm.audio;
	if (!s || !s->capacity) { ReleaseSRWLockExclusive(&g_liveShmLock); return; }
	const uint32_t cap = s->capacity;
	uint32_t r = s->readPos;
	uint32_t avail = s->writePos - r;
	if (avail > cap) avail = 0;
	const uint32_t n = (avail < need) ? avail : need;
	const float* sl = LiveShmL(s);
	const float* sr = LiveShmR(s);
	for (uint32_t i = 0; i < n; ++i) {
		const uint32_t idx = (r + i) & (cap - 1);
		L[i] += sl[idx];
		R[i] += sr[idx];
	}
	if (n) {
		MemoryBarrier();
		InterlockedExchange((LONG*)&s->readPos, (LONG)(r + n));
		g_liveShm.primed = 1;
	}
	if (g_liveShm.hWake) SetEvent(g_liveShm.hWake);
	ReleaseSRWLockExclusive(&g_liveShmLock);
}

static int LiveRemoteLoad(int part1to32, const wchar_t* pluginPath, int isVst3)
{
	if (!g_kpiHost.VstLiveLoad((uint32_t)part1to32, pluginPath, isVst3 != 0))
		return -3;
	if (!LiveRemoteOpenShm()) {
		g_kpiHost.VstLiveUnload((uint32_t)part1to32);
		return -4;
	}
	++g_liveShm.parts;
	return 0;
}

static void LiveRemoteUnload(int part1to32)
{
	const int last = (g_liveShm.parts <= 1);
	// Stop the host audio thread before closing the last plug-in: otherwise
	// KpiHost64's UI thread waits on g_eng.cs while the audio thread is inside
	// processReplacing, and the pipe never returns.
	if (last) LiveRemoteStop();
	if (!InterlockedCompareExchange(&g_liveShuttingDown, 0, 0))
		g_kpiHost.VstLiveUnload((uint32_t)part1to32);
	if (g_liveShm.parts > 0) --g_liveShm.parts;
	if (g_liveShm.parts == 0) LiveRemoteStop();
}

static int LiveRemoteActive() { return g_liveShm.parts > 0 && g_liveShm.audio != NULL; }

#else  // KPIHOST64_BUILD: the plug-ins are already in-process here

static void LiveRemoteMidi(int, DWORD) {}
static void LiveRemoteMix(float*, float*, int) {}
static void LiveRemoteUnload(int) {}
static int LiveRemoteActive() { return 0; }

#endif

extern "C" int VstLiveLoadPart(int part1to32,
	const wchar_t* pluginPath, int isVst3)
{
	if (part1to32 < 1 || part1to32 > 32 || !pluginPath) return -1;
	EnterCriticalSection(&g_eng.cs);
	LivePart& p = g_eng.live[part1to32 - 1];
	const int wasRemote = p.remote;
	CloseEffect(p.module, p.effect);
	Vst3Close(p.vst3); p.vst3 = NULL;
	p.isMulti = 0;
	p.remote = 0;
	p.sendCh = -1;
	p.prog = -1;
	LeaveCriticalSection(&g_eng.cs);
	if (wasRemote) LiveRemoteUnload(part1to32);

#ifndef KPIHOST64_BUILD
	// x64 plug-in in a 32-bit process: hand it to KpiHost64. Slot state is
	// updated outside the engine lock because the pipe call can start the
	// host process, which takes far too long to hold the audio lock for.
	if (PeArch(pluginPath) != HostArch()) {
		const int rc = LiveRemoteLoad(part1to32, pluginPath, isVst3);
		EnsLog(L"LiveLoad part=%d remote rc=%d arch=%d vst3=%d path=%s",
			part1to32, rc, PeArch(pluginPath), isVst3, pluginPath);
		if (rc != 0) return rc;
		EnterCriticalSection(&g_eng.cs);
		LivePart& rp = g_eng.live[part1to32 - 1];
		rp.remote = 1;
		rp.isMulti = LiveIsMultiPath(pluginPath);
		rp.sendCh = rp.isMulti ? -1 : 0;
		rp.prog = -1;
		LeaveCriticalSection(&g_eng.cs);
		return 0;
	}
#endif

	EnterCriticalSection(&g_eng.cs);
	int ok = 0;
	if (isVst3) {
		p.vst3 = Vst3Open(pluginPath);
		ok = Vst3IsOk(p.vst3);
		if (!ok) { Vst3Close(p.vst3); p.vst3 = NULL; }
		// Small factory banks still get program 0 so they speak on drop.
		// Samplers/kits the user patches themselves, and MediaBay-sized lists
		// (HALion 7), skip it: loading entry 0 can take seconds and the user
		// will pick a sound in the plug-in anyway.
		if (ok) {
			const int nprog = Vst3ProgramCount(p.vst3);
			const int factory = IsFactoryRompler(pluginPath, pluginPath);
			int setFirst = 0;
			if (nprog > 0) {
				if (NeedsUserPatch(pluginPath, pluginPath) && !factory)
					setFirst = 0;
				else if (nprog <= LIVE_FACTORY_PROG_MAX || factory)
					setFirst = 1;
			}
			if (setFirst) {
				Vst3SetProgram(p.vst3, 0);
				p.prog = 0;
			}
			if (!Vst3IsInstrument(p.vst3)) {
				Vst3Close(p.vst3);
				p.vst3 = NULL;
				ok = 0;
			}
		}
	} else {
		ok = LoadVst2(pluginPath, p.module, p.effect);
	}
	if (ok) {
		p.isMulti = LiveIsMultiPath(pluginPath);
		// One part = one instrument track, and the channel already picked the
		// slot, so a single-timbre plug-in is fed on ch1: a kit that listens
		// there (Groove Agent default) plays wherever the user drops it. The
		// slot menu can override this per part.
		p.sendCh = p.isMulti ? -1 : 0;
	}
	LeaveCriticalSection(&g_eng.cs);
	return ok ? 0 : -2;
}

enum { LIVE_PEND_SYSEX_EVENTS = 32 };

static void LivePendPush(LivePart& p, DWORD msg)
{
	LivePending& q = p.pend;
	if (q.count >= LIVE_PEND_EVENTS) { ++q.lostEvents; return; }
	q.ev[q.count].msg = msg;
	q.ev[q.count].sysexOff = -1;
	q.ev[q.count].sysexLen = 0;
	++q.count;
}

static void LivePendPushSysex(LivePart& p, const BYTE* data, int bytes)
{
	LivePending& q = p.pend;
	if (q.count >= LIVE_PEND_EVENTS ||
		bytes <= 0 || q.sysexUsed + bytes > LIVE_PEND_SYSEX_BYTES) {
		++q.lostSysex;
		return;
	}
	memcpy(q.sysex + q.sysexUsed, data, (size_t)bytes);
	q.ev[q.count].msg = 0;
	q.ev[q.count].sysexOff = q.sysexUsed;
	q.ev[q.count].sysexLen = bytes;
	q.sysexUsed += bytes;
	++q.count;
}

// One dispatcher call for the whole block, notes and sysex together, in the
// order they arrived.
static void LivePendFlush(LivePart& p)
{
	LivePending& q = p.pend;
	if (!q.count) return;
	if (p.effect && p.effect->dispatcher) {
		LiveDllDirFromEffect(p.effect);
		const int pi = (int)(&p - g_eng.live);
		VstPendBuf& buf = g_livePend[(pi >= 0 && pi < 32) ? pi : 0];
		buf.block.reserved = 0;
		int n = 0, nsx = 0;
		for (int i = 0; i < q.count; ++i) {
			if (q.ev[i].sysexOff >= 0) {
				if (nsx >= VST_PEND_SX || n >= VST_PEND_N) continue;
				VstMidiSysexEvent& s = buf.sx[nsx];
				ZeroMemory(&s, sizeof(s));
				s.type = kVstSysExType;
				s.byteSize = sizeof(VstMidiSysexEvent);
				s.dumpBytes = (VstInt32)q.ev[i].sysexLen;
				s.sysexDump = (char*)(q.sysex + q.ev[i].sysexOff);
				buf.block.events[n++] = (VstEvent*)&s;
				++nsx;
				continue;
			}
			if (n >= VST_PEND_N) break;
			VstMidiEvent& m = buf.me[n];
			ZeroMemory(&m, sizeof(m));
			m.type = kVstMidiType;
			m.byteSize = sizeof(VstMidiEvent);
			m.flags = kVstMidiEventIsRealtime;
			m.midiData[0] = (char)(q.ev[i].msg & 0xff);
			m.midiData[1] = (char)((q.ev[i].msg >> 8) & 0x7f);
			m.midiData[2] = (char)((q.ev[i].msg >> 16) & 0x7f);
			buf.block.events[n] = (VstEvent*)&m;
			++n;
		}
		buf.block.numEvents = n;
		if (n) {
			__try { p.effect->dispatcher(p.effect, effProcessEvents, 0, 0, &buf.block, 0); }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		}
	}
	if (p.vst3) {
		// Vst3MidiShort already accumulates until the next Vst3Process.
		for (int i = 0; i < q.count; ++i)
			if (q.ev[i].sysexOff < 0) Vst3MidiShort(p.vst3, q.ev[i].msg, 0);
	}
	q.count = 0;
	q.sysexUsed = 0;
}

// Channel-mode messages only: CC64=0 (sustain off), CC120 (all sound off),
// CC123 (all notes off). Data bytes live in bits 8..15 / 16..23, so a wrong
// shift here silently turns into Bank Select MSB and retunes the part.
static void LivePanicPart(LivePart& p)
{
	if (!p.effect && !p.vst3) return;
	// Drop anything still queued: it is stale the moment we panic, and a
	// note-on left in there would start a voice right after the all-notes-off.
	p.pend.count = 0;
	p.pend.sysexUsed = 0;
	for (int ch = 0; ch < 16; ++ch) {
		LivePendPush(p, (DWORD)((0xb0 | ch) | (64 << 8) | (0 << 16)));
		LivePendPush(p, (DWORD)((0xb0 | ch) | (120 << 8)));
		LivePendPush(p, (DWORD)((0xb0 | ch) | (123 << 8)));
	}
	// Panic also runs while unloading, where no render follows, so push it out
	// and give the plug-in the process call that acts on it.
	LivePendFlush(p);
	PumpSilent(p.effect, p.vst3, 1);
}

static void LivePanicRemote()
{
	if (!LiveRemoteActive()) return;
	for (int port = 0; port < 3; ++port)
		for (int ch = 0; ch < 16; ++ch) {
			LiveRemoteMidi(port, (DWORD)((0xb0 | ch) | (64 << 8)));
			LiveRemoteMidi(port, (DWORD)((0xb0 | ch) | (120 << 8)));
			LiveRemoteMidi(port, (DWORD)((0xb0 | ch) | (123 << 8)));
		}
}

// Live input monitor. Written from the audio thread as events are handed to
// the plug-ins and read by the UI timer, so the values are deliberately just
// interlocked scalars rather than a locked snapshot.
struct LiveActEntry {
	volatile LONG down[4]; // bit n of down[n/32] = note n is held
	volatile LONG note;
	volatile LONG vel;
	volatile LONG tick;
	volatile LONG seen;
};

static LiveActEntry g_liveAct[48]; // 3 ports x 16 channels
static wchar_t g_liveSysexText[160];
static volatile LONG g_liveSysexTick;
static volatile LONG g_liveSysexSeen;

static void LiveActTrack(int port, DWORD msg)
{
	const int idx = port * 16 + (int)(msg & 15);
	if (idx < 0 || idx >= 48) return;
	LiveActEntry& a = g_liveAct[idx];
	const int type = (int)(msg & 0xf0);
	const int d1 = (int)((msg >> 8) & 0x7f);
	const int d2 = (int)((msg >> 16) & 0x7f);
	if (type == 0x90 && d2) {
		InterlockedOr(&a.down[d1 >> 5], (LONG)(1u << (d1 & 31)));
		InterlockedExchange(&a.note, d1);
		InterlockedExchange(&a.vel, d2);
	} else if (type == 0x80 || type == 0x90) {
		InterlockedAnd(&a.down[d1 >> 5], (LONG)~(1u << (d1 & 31)));
	} else if (type == 0xb0 && (d1 == 120 || d1 == 123)) {
		for (int i = 0; i < 4; ++i) InterlockedExchange(&a.down[i], 0);
	}
	InterlockedExchange(&a.tick, (LONG)GetTickCount());
	InterlockedExchange(&a.seen, 1);
}

static void LiveActReset()
{
	for (int i = 0; i < 48; ++i)
		for (int b = 0; b < 4; ++b) InterlockedExchange(&g_liveAct[i].down[b], 0);
}

enum { LIVE_TAP_SHORT_N = 2048, LIVE_TAP_SX_N = 32, LIVE_TAP_SX_B = 1024 };
struct LiveTapShort { BYTE port; DWORD msg; };
static LiveTapShort g_liveTapShort[LIVE_TAP_SHORT_N];
static volatile LONG g_liveTapShortW = 0;
static volatile LONG g_liveTapShortR = 0;
struct LiveTapSx { BYTE port; unsigned short len; BYTE d[LIVE_TAP_SX_B]; };
static LiveTapSx g_liveTapSx[LIVE_TAP_SX_N];
static volatile LONG g_liveTapSxW = 0;
static volatile LONG g_liveTapSxR = 0;

extern "C" void VstLiveTapPushShort(int portIndex0to2, DWORD shortMsg)
{
	if (portIndex0to2 < 0) portIndex0to2 = 0;
	if (portIndex0to2 > 2) portIndex0to2 = 2;
	const LONG w = g_liveTapShortW;
	if ((LONG)(w - g_liveTapShortR) >= LIVE_TAP_SHORT_N - 1) return;
	const int i = (int)(w & (LIVE_TAP_SHORT_N - 1));
	g_liveTapShort[i].port = (BYTE)portIndex0to2;
	g_liveTapShort[i].msg = shortMsg;
	MemoryBarrier();
	g_liveTapShortW = w + 1;
}

extern "C" void VstLiveTapPushSysex(int portIndex0to2, const unsigned char* data, int bytes)
{
	if (!data || bytes < 2) return;
	if (portIndex0to2 < 0) portIndex0to2 = 0;
	if (portIndex0to2 > 2) portIndex0to2 = 2;
	if (bytes > LIVE_TAP_SX_B) bytes = LIVE_TAP_SX_B;
	const LONG w = g_liveTapSxW;
	if ((LONG)(w - g_liveTapSxR) >= LIVE_TAP_SX_N - 1) return;
	const int i = (int)(w & (LIVE_TAP_SX_N - 1));
	g_liveTapSx[i].port = (BYTE)portIndex0to2;
	g_liveTapSx[i].len = (unsigned short)bytes;
	memcpy(g_liveTapSx[i].d, data, (size_t)bytes);
	MemoryBarrier();
	g_liveTapSxW = w + 1;
}

extern "C" int VstLiveTapStealShorts(BYTE* ports, DWORD* msgs, int maxCount)
{
	if (!ports || !msgs || maxCount < 1) return 0;
	int n = 0;
	LONG r = g_liveTapShortR;
	const LONG w = g_liveTapShortW;
	while (n < maxCount && r != w) {
		const int i = (int)(r & (LIVE_TAP_SHORT_N - 1));
		ports[n] = g_liveTapShort[i].port;
		msgs[n] = g_liveTapShort[i].msg;
		++n;
		++r;
	}
	MemoryBarrier();
	g_liveTapShortR = r;
	return n;
}

extern "C" int VstLiveTapStealSysex(int* portIndex0to2, unsigned char* data, int maxBytes)
{
	if (!portIndex0to2 || !data || maxBytes < 1) return 0;
	const LONG r = g_liveTapSxR;
	if (r == g_liveTapSxW) return 0;
	const int i = (int)(r & (LIVE_TAP_SX_N - 1));
	int n = (int)g_liveTapSx[i].len;
	if (n > maxBytes) n = maxBytes;
	*portIndex0to2 = (int)g_liveTapSx[i].port;
	if (n > 0) memcpy(data, g_liveTapSx[i].d, (size_t)n);
	MemoryBarrier();
	g_liveTapSxR = r + 1;
	return n;
}

extern "C" int VstLiveActivity(int part1to32, struct VstLiveActInfo* out)
{
	if (!out || part1to32 < 1 || part1to32 > 48) return 0;
	const LiveActEntry& a = g_liveAct[part1to32 - 1];
	int held = 0;
	for (int b = 0; b < 4; ++b) {
		const unsigned w = (unsigned)a.down[b];
		out->mask[b] = w;
		for (unsigned bit = w; bit; bit &= bit - 1) ++held;
	}
	out->held = held;
	out->note = (int)a.note;
	out->vel = (int)a.vel;
	out->ageMs = a.seen ? (int)(GetTickCount() - (DWORD)a.tick) : -1;
	return 1;
}

extern "C" int VstLiveSysexInfo(wchar_t* out, int chars, int* ageMs)
{
	if (!out || chars <= 0) return 0;
	out[0] = 0;
	if (!InterlockedCompareExchange(&g_liveSysexSeen, 0, 0)) {
		if (ageMs) *ageMs = -1;
		return 0;
	}
	wcsncpy_s(out, chars, g_liveSysexText, _TRUNCATE);
	if (ageMs) *ageMs = (int)(GetTickCount() - (DWORD)g_liveSysexTick);
	return 1;
}

// Names the resets everyone actually sends; anything else is shown raw.
static void LiveSysexDescribe(const unsigned char* d, int n)
{
	const wchar_t* known = NULL;
	if (n >= 6 && d[0] == 0xf0 && d[1] == 0x7e && d[3] == 0x09) {
		if (d[4] == 0x01) known = L"GM1 On";
		else if (d[4] == 0x03) known = L"GM2 On";
		else if (d[4] == 0x02) known = L"GM Off";
	} else if (n >= 10 && d[0] == 0xf0 && d[1] == 0x41 && d[3] == 0x42 &&
		d[4] == 0x12 && d[5] == 0x40 && d[6] == 0x00 && d[7] == 0x7f) {
		known = L"GS Reset";
	} else if (n >= 8 && d[0] == 0xf0 && d[1] == 0x43 && d[3] == 0x4c &&
		d[4] == 0x00 && d[5] == 0x00 && d[6] == 0x7e) {
		known = L"XG On";
	} else if (n >= 2 && d[0] == 0xf0 && d[1] == 0x41) {
		known = L"Roland";
	} else if (n >= 2 && d[0] == 0xf0 && d[1] == 0x43) {
		known = L"Yamaha";
	}
	wchar_t head[64] = {};
	const int show = n < 5 ? n : 5;
	for (int i = 0; i < show; ++i) {
		wchar_t b[8];
		_snwprintf_s(b, _TRUNCATE, i ? L" %02X" : L"%02X", d[i]);
		wcsncat_s(head, b, _TRUNCATE);
	}
	if (known)
		_snwprintf_s(g_liveSysexText, _TRUNCATE, L"%s (%dB: %s%s)", known, n,
			head, n > show ? L" …" : L"");
	else
		_snwprintf_s(g_liveSysexText, _TRUNCATE, L"SysEx %dB: %s%s", n, head,
			n > show ? L" …" : L"");
	InterlockedExchange(&g_liveSysexTick, (LONG)GetTickCount());
	InterlockedExchange(&g_liveSysexSeen, 1);
}

extern "C" void VstLiveAllNotesOff()
{
	EnterCriticalSection(&g_eng.cs);
	for (int i = 0; i < 32; ++i) LivePanicPart(g_eng.live[i]);
	LeaveCriticalSection(&g_eng.cs);
	LiveActReset();
	LivePanicRemote();
}

extern "C" void VstLiveUnloadPart(int part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return;
#ifdef KPIHOST64_BUILD
	if (InterlockedCompareExchange(&g_liveAbandonPlugins, 0, 0)) {
		if (TryEnterCriticalSection(&g_eng.cs)) {
			LivePart& p = g_eng.live[part1to32 - 1];
			p.vst3 = NULL;
			p.effect = NULL;
			p.module = NULL;
			p.isMulti = 0;
			p.remote = 0;
			p.edWnd = NULL;
			LeaveCriticalSection(&g_eng.cs);
		}
		return;
	}
#endif
	int wasRemote = 0;
	EnterCriticalSection(&g_eng.cs);
	wasRemote = g_eng.live[part1to32 - 1].remote;
	LeaveCriticalSection(&g_eng.cs);
#ifndef KPIHOST64_BUILD
	// effEditClose / effClose while KpiHost64 is inside processReplacing is
	// the usual "close the host and it hangs" path. Stop that thread first.
	if (wasRemote) LiveRemoteStop();
#endif
	VstLiveEditorClose(part1to32);
	EnterCriticalSection(&g_eng.cs);
	LivePanicPart(g_eng.live[part1to32 - 1]);
	LivePart& p = g_eng.live[part1to32 - 1];
	wasRemote = p.remote;
	CloseEffect(p.module, p.effect);
	Vst3Close(p.vst3); p.vst3 = NULL;
	p.isMulti = 0;
	p.remote = 0;
	LeaveCriticalSection(&g_eng.cs);
	if (wasRemote) LiveRemoteUnload(part1to32);
#ifndef KPIHOST64_BUILD
	if (wasRemote && g_liveShm.parts > 0 &&
		!InterlockedCompareExchange(&g_liveShuttingDown, 0, 0))
		LiveRemoteOpenShm();
#endif
}

extern "C" void VstLiveShutdown(void)
{
#ifndef KPIHOST64_BUILD
	InterlockedExchange(&g_liveShuttingDown, 1);
	LiveRemoteStop();
	g_kpiHost.VstLiveUnloadAll();
	g_liveShm.parts = 0;
#endif
	for (int i = 1; i <= 32; ++i)
		VstLiveUnloadPart(i);
#ifndef KPIHOST64_BUILD
	InterlockedExchange(&g_liveShuttingDown, 0);
#endif
}

static int LivePartFreeForProbe(int part0)
{
	const LivePart& p = g_eng.live[part0];
	return (!p.effect && !p.vst3 && !p.remote && !p.module && !p.edWnd) ? 1 : 0;
}

static double PeakOf(const float* l, const float* r, int n)
{
	double peak = 0.0;
	for (int i = 0; i < n; ++i) {
		const double a = fabs((double)l[i]), b = fabs((double)r[i]);
		if (a > peak) peak = a;
		if (b > peak) peak = b;
	}
	return peak;
}

// A part hosted in this process can be driven and rendered on its own, which
// keeps the measurement clean no matter what else is loaded. Note that feeding
// the note through VstLiveMidiShort would be wrong here: that routes by port,
// so during a rescan the note lands on whichever part owns the port instead of
// the one being tested.
static double LiveProbeLocal(int part1to32, int channel0, int note,
	int velocity, int honourSendCh)
{
	LivePart& p = g_eng.live[part1to32 - 1];
	if (!p.effect && !p.vst3) return 0.0;

	int ch = channel0 & 15;
	if (honourSendCh && p.sendCh >= 0) ch = p.sendCh & 15;
	const DWORD noteOn = (DWORD)(0x90 | ch) |
		((DWORD)note << 8) | ((DWORD)velocity << 16);
	const DWORD noteOff = (DWORD)(0x80 | ch) | ((DWORD)note << 8);

	static __declspec(align(32)) float l[BLOCK_FRAMES];
	static __declspec(align(32)) float r[BLOCK_FRAMES];
	p.pend.count = 0;
	p.pend.sysexUsed = 0;
	LivePendPush(p, noteOn);

	double peak = 0.0;
	const int blocks = SAMPLE_RATE / BLOCK_FRAMES; // one second at most
	for (int b = 0; b < blocks; ++b) {
		LivePendFlush(p);
		if (p.vst3) Vst3Process(p.vst3, l, r, BLOCK_FRAMES);
		else RenderEffect(p.effect, l, r, BLOCK_FRAMES);
		const double got = PeakOf(l, r, BLOCK_FRAMES);
		if (got > peak) peak = got;
		if (peak * 1000.0 >= (double)PROBE_AUDIBLE_MILLI) break;
	}
	LivePendPush(p, noteOff);
	LivePendFlush(p);
	return peak;
}

// A remote part lives in KpiHost64 and only comes back inside the shared mix,
// so this one measures the mix and subtracts what it was already producing.
// The note is aimed with the channel that matches the slot, which is how the
// far side routes it to this part and not another.
static double LiveProbeRemote(int part1to32, int note, int velocity,
	double* outBaseline)
{
	static float l[BLOCK_FRAMES];
	static float r[BLOCK_FRAMES];
	const int ch = (part1to32 - 1) % 16;
	const DWORD blockMs = (DWORD)((BLOCK_FRAMES * 1000) / SAMPLE_RATE); // ~11ms

	// KpiHost64 fills the ring in real time; draining faster only yields
	// starved zeros, which would read as a dead plug-in.
	double base = 0.0;
	const DWORD baseEnd = GetTickCount() + 250;
	while (GetTickCount() < baseEnd) {
		if (VstLiveRender(l, r, BLOCK_FRAMES) <= 0) break;
		const double got = PeakOf(l, r, BLOCK_FRAMES);
		if (got > base) base = got;
		Sleep(blockMs);
	}
	if (outBaseline) *outBaseline = base;

	const DWORD noteOn = (DWORD)(0x90 | ch) |
		((DWORD)note << 8) | ((DWORD)velocity << 16);
	const DWORD noteOff = (DWORD)(0x80 | ch) | ((DWORD)note << 8);
	// Straight into the ring, deliberately not through VstLiveMidiShort: that
	// also pushes the note to the local part owning the port, so a plug-in
	// sitting in another slot would answer and get measured instead.
	const int port = (part1to32 - 1) / 16;
	LiveRemoteMidi(port, noteOn);

	double peak = 0.0;
	const DWORD deadline = GetTickCount() + 2000;
	__int64 rendered = 0;
	while (rendered < SAMPLE_RATE && GetTickCount() < deadline) {
		if (VstLiveRender(l, r, BLOCK_FRAMES) <= 0) break;
		rendered += BLOCK_FRAMES;
		const double got = PeakOf(l, r, BLOCK_FRAMES);
		if (got > peak) peak = got;
		if ((peak - base) * 1000.0 >= (double)PROBE_AUDIBLE_MILLI) break;
		Sleep(blockMs);
	}
	LiveRemoteMidi(port, noteOff);
	return peak;
}

// A melodic note first, then a drum hit on channel 10: a kit-only plug-in has
// nothing mapped at C4 and would otherwise be written off as broken.
static double LiveProbeBothStages(int part1to32, int remote, double* outBase)
{
	double base = 0.0;
	double rise = 0.0;
	if (remote) {
		double b1 = 0.0;
		const double mel = LiveProbeRemote(part1to32, 60, 100, &b1);
		rise = mel - b1;
		base = b1;
		if (rise * 1000.0 < (double)PROBE_AUDIBLE_MILLI) {
			double b2 = 0.0;
			const double drum = LiveProbeRemote(part1to32, 36, 110, &b2);
			if (drum - b2 > rise) { rise = drum - b2; base = b2; }
		}
	} else {
		EnterCriticalSection(&g_eng.cs);
		rise = LiveProbeLocal(part1to32, 0, 60, 100, 1);
		if (rise * 1000.0 < (double)PROBE_AUDIBLE_MILLI) {
			const double drum = LiveProbeLocal(part1to32, 9, 36, 110, 0);
			if (drum > rise) rise = drum;
		}
		LeaveCriticalSection(&g_eng.cs);
	}
	if (outBase) *outBase = base;
	return rise;
}

// Walk a few programs and stop at the first that speaks, for plug-ins whose
// patches really do come from the host-visible program list.
enum { PROBE_PROGRAM_TRIES = 6 };

static int LiveProbePartAudible(int part1to32, int* outPeakMilli,
	int* outBaseMilli, int* outProgram, int tryPrograms)
{
	if (part1to32 < 1 || part1to32 > 32) return 0;
	const int remote = g_eng.live[part1to32 - 1].remote ? 1 : 0;
	double base = 0.0;
	double rise = LiveProbeBothStages(part1to32, remote, &base);
	int usedProgram = -1;

	if (tryPrograms && rise * 1000.0 < (double)PROBE_AUDIBLE_MILLI) {
		const int progs = VstLiveProgramCount(part1to32);
		const int tries = progs < PROBE_PROGRAM_TRIES ? progs : PROBE_PROGRAM_TRIES;
		for (int i = 0; i < tries; ++i) {
			if (!VstLiveSetProgram(part1to32, i)) continue;
			double b = 0.0;
			const double got = LiveProbeBothStages(part1to32, remote, &b);
			if (got > rise) { rise = got; base = b; usedProgram = i; }
			if (rise * 1000.0 >= (double)PROBE_AUDIBLE_MILLI) break;
		}
	}

	if (outPeakMilli) *outPeakMilli = (int)((rise + base) * 1000.0 + 0.5);
	if (outBaseMilli) *outBaseMilli = (int)(base * 1000.0 + 0.5);
	if (outProgram) *outProgram = usedProgram;
	return (rise * 1000.0 >= (double)PROBE_AUDIBLE_MILLI) ? 1 : 0;
}

static int LiveAlreadyDroppable(const VstPluginInfo& p, int beforeIndex)
{
	for (int i = 0; i < beforeIndex; ++i) {
		const VstPluginInfo& q = g_plugins[i];
		if (!q.isInstrument || !q.isLiveOk || q.isAudible == 0) continue;
		if (q.arch != p.arch) continue;
		if (_wcsicmp(q.name, p.name) == 0) return 1;
	}
	return 0;
}

static int LivePaletteListed(const VstPluginInfo& p)
{
	return (p.isInstrument && p.isLiveOk && p.isAudible != 0) ? 1 : 0;
}

#ifndef KPIHOST64_BUILD
void VstHostOnLiveListChanged(void);
#endif

static void LivePalettePushIfOk(const VstPluginInfo& p)
{
	if (!LivePaletteListed(p)) return;
#ifndef KPIHOST64_BUILD
	VstHostOnLiveListChanged();
#endif
	PumpWait(g_waitWnd);
}

// First droppable copy of a name+arch stays. Later copies are hidden even if
// they would also drop. A copy that failed to drop is not listed, so a later
// same-name that does drop can still take the tile.
static int CollapseLiveDuplicates(void)
{
	int n = 0;
	for (int i = 0; i < g_pluginCount; ++i) {
		if (!LivePaletteListed(g_plugins[i])) continue;
		for (int j = i + 1; j < g_pluginCount; ++j) {
			if (!LivePaletteListed(g_plugins[j])) continue;
			if (g_plugins[i].arch != g_plugins[j].arch) continue;
			if (_wcsicmp(g_plugins[i].name, g_plugins[j].name) != 0) continue;
			g_plugins[j].isInstrument = 0;
			g_plugins[j].isLiveOk = 1;
			g_plugins[j].isAudible = 0;
			++n;
		}
	}
	return n;
}

// Same open/close the part grid uses on a drop. A failed open is marked
// checked (isLiveOk, silent) so the palette never offers it again.
extern "C" void VstScanVerifyLiveList(HWND parentForWait)
{
	int todo = 0;
	for (int i = 0; i < g_pluginCount; ++i)
		if (g_plugins[i].isInstrument && !g_plugins[i].isLiveOk) ++todo;
	if (!todo) {
		if (CollapseLiveDuplicates()) SaveCache();
		return;
	}

	HWND wait = g_waitWnd;
	int ownWait = 0;
	if (!wait || !IsWindow(wait)) {
		wait = MakeWait(parentForWait);
		ownWait = wait != NULL;
	}

	int done = 0;
	int changed = 0;
	for (int i = 0; i < g_pluginCount; ++i) {
		VstPluginInfo& p = g_plugins[i];
		if (!p.isInstrument || p.isLiveOk) continue;
		++done;
		if (LiveAlreadyDroppable(p, i)) {
			if (wait) SetVerifyWait(wait, done, todo, p.name,
				LL14(L"同名で既に載るものがあるので飛ばします", L"Same name already drops, skipping",
					L"Même nom déjà déposable, on passe", L"Stesso nome già trascinabile, salto",
					L"El mismo nombre ya se puede soltar, se omite", L"같은 이름이 이미 드롭되므로 건너뜁니다",
					L"同名已可拖放，跳过", L"نفس الاسم قابل للإفلات، يتم التخطي",
					L"То же имя уже ставится, пропуск", L"Gleicher Name schon ablegbar, übersprungen",
					L"O mesmo nome já larga, a saltar", L"Dezelfde naam is al te droppen, overgeslagen",
					L"Ta sama nazwa już się kładzie, pomijam", L"Aynı ad zaten bırakılabiliyor, atlanıyor"));
			p.isInstrument = 0;
			p.isLiveOk = 1;
			p.isAudible = 0;
			changed = 1;
			continue;
		}
		if (IsFxNotInstrument(p.name, p.path)) {
			if (wait) SetVerifyWait(wait, done, todo, p.name,
				LL14(L"エフェクトのため一覧から外します", L"FX, skipping",
					L"Effet, on passe", L"Effetto, salto", L"Efecto, se omite",
					L"이펙트라 목록에서 뺍니다", L"效果器，从列表去掉", L"تأثير، يتم التخطي",
					L"Эффект, пропуск", L"Effekt, übersprungen", L"Efeito, a saltar",
					L"Effect, overgeslagen", L"Efekt, pomijam", L"Efekt, atlanıyor"));
			p.isInstrument = 0;
			changed = 1;
			continue;
		}
		const int factory = IsFactoryRompler(p.name, p.path);
		if (NeedsUserPatch(p.name, p.path) && !factory) {
			// Opening Groove Agent / HALion 7 / Kontakt just to see that they
			// load can take many seconds, and the user still has to pick a
			// patch. Listing them as "needs patch" is the drop check.
			if (wait) SetVerifyWait(wait, done, todo, p.name,
				LL14(L"音色選択として即確認", L"Needs a patch (instant)",
					L"Timbre à choisir (immédiat)", L"Serve una patch (subito)",
					L"Hay que elegir timbre (al instante)", L"음색 선택으로 즉시 확인",
					L"作为选音色立即确认", L"يحتاج رقعة (فوري)",
					L"Нужен патч (сразу)", L"Patch wählen (sofort)", L"Precisa de timbre (já)",
					L"Patch kiezen (meteen)", L"Trzeba barwy (od razu)", L"Yama gerekir (anında)"));
			p.isAudible = 2;
			p.probePeakMilli = 0;
			p.isLiveOk = 1;
			changed = 1;
			EnsLog(L"verify NEEDS-PATCH instant path=%s", p.path);
			LivePalettePushIfOk(p);
			continue;
		}
		int part = 0;
		for (int s = 0; s < 32; ++s)
			if (LivePartFreeForProbe(s)) { part = s + 1; break; }
		if (!part) {
			// Every slot is busy (rescan while the grid is full). Leave the
			// entry unchecked so a later verify can finish the job.
			continue;
		}
		if (wait) SetVerifyWait(wait, done, todo, p.name,
			LL14(L"音源を開いています…", L"Opening…",
				L"Ouverture…", L"Apertura…", L"Abriendo…", L"음원을 여는 중…",
				L"正在打开音源…", L"جارٍ الفتح…", L"Открытие…", L"Wird geöffnet…",
				L"A abrir…", L"Openen…", L"Otwieranie…", L"Açılıyor…"));
		if (VstLiveLoadPart(part, p.path, p.isVst3) != 0) {
			if (wait) SetVerifyWait(wait, done, todo, p.name,
				LL14(L"載せられないため一覧から外します", L"Cannot drop, skipping",
					L"Non déposable, on passe", L"Non trascinabile, salto",
					L"No se puede soltar, se omite", L"드롭할 수 없어 목록에서 뺍니다",
					L"无法拖放，从列表去掉", L"تعذر الإفلات، يتم التخطي",
					L"Не ставится, пропуск", L"Nicht ablegbar, übersprungen",
					L"Não larga, a saltar", L"Niet te droppen, overgeslagen",
					L"Nie da się upuścić, pomijam", L"Bırakılamıyor, atlanıyor"));
			p.isInstrument = 0;
			p.isLiveOk = 1;
			p.isAudible = 0;
			changed = 1;
			continue;
		}
		int milli = 0, base = 0, prog = -1;
		const int wasRemote = g_eng.live[part - 1].remote;
		int needsPatch = 0;
		if (!factory) {
			const int nprog = VstLiveProgramCount(part);
			if (nprog > LIVE_FACTORY_PROG_MAX) needsPatch = 1;
		}
		if (needsPatch) {
			if (wait) SetVerifyWait(wait, done, todo, p.name,
				LL14(L"音色選択として確認", L"Needs a patch",
					L"Timbre à choisir", L"Serve una patch", L"Hay que elegir timbre",
					L"음색 선택으로 확인", L"作为选音色确认", L"يحتاج رقعة",
					L"Нужен патч", L"Patch wählen", L"Precisa de timbre",
					L"Patch kiezen", L"Trzeba barwy", L"Yama gerekir"));
			p.isAudible = 2;
			p.probePeakMilli = 0;
			VstLiveUnloadPart(part);
			p.isLiveOk = 1;
			changed = 1;
			EnsLog(L"verify NEEDS-PATCH skip-probe part=%d remote=%d path=%s",
				part, wasRemote, p.path);
			LivePalettePushIfOk(p);
			continue;
		}
		if (wait) SetVerifyWait(wait, done, todo, p.name,
			LL14(L"発音を確認しています…", L"Checking sound…",
				L"Vérification du son…", L"Controllo del suono…", L"Comprobando el sonido…",
				L"발음을 확인하는 중…", L"正在确认发音…", L"جارٍ التحقق من الصوت…",
				L"Проверка звука…", L"Klang wird geprüft…", L"A verificar o som…",
				L"Geluid controleren…", L"Sprawdzanie dźwięku…", L"Ses kontrol ediliyor…"));
		p.isAudible = LiveProbePartAudible(part, &milli, &base, &prog, 0);
		p.probePeakMilli = milli;
		VstLiveUnloadPart(part);
		if (!p.isAudible && factory) p.isAudible = 2;
		p.isLiveOk = 1;
		changed = 1;
		if (wait && p.isAudible == 1)
			SetVerifyWait(wait, done, todo, p.name,
				LL14(L"発音OK", L"Sounds", L"Ça sonne", L"Suona", L"Suena",
					L"발음 OK", L"发音正常", L"يصدر صوتًا", L"Звучит", L"Klingt",
					L"Soa", L"Klinkt", L"Gra", L"Ses var"));
		EnsLog(L"verify %s part=%d remote=%d peak=%d base=%d prog=%d "
			L"(per 1000) path=%s",
			p.isAudible == 1 ? L"SOUND" :
			(p.isAudible == 2 ? L"NEEDS-PATCH" : L"SILENT"),
			part, wasRemote, milli, base, prog, p.path);
		LivePalettePushIfOk(p);
	}
	if (ownWait && wait) DestroyWait(wait);
	if (CollapseLiveDuplicates()) {
		changed = 1;
#ifndef KPIHOST64_BUILD
		VstHostOnLiveListChanged();
#endif
	}
	if (changed) SaveCache();
}


static int LivePartLoaded(int part)
{
	if (part < 0 || part >= 32) return 0;
	const LivePart& p = g_eng.live[part];
	return (p.effect || p.vst3 || p.remote) ? 1 : 0;
}

// Multi-timbral (SC-VA / SGP2 etc.): one instance receives all 16 channels
// of its port block. The VST host UI covers only that block (parts 1–16 or
// 17–32), so a module on A must not steal MIDI meant for empty B slots.
static int LiveMultiPart(int portIndex0to2)
{
	const int blockStart = portIndex0to2 * 16;
	for (int i = blockStart; i < blockStart + 16 && i < 32; ++i)
		if (g_eng.live[i].isMulti && LivePartLoaded(i))
			return i;
	return -1;
}

// The part decides which channel its plug-in sees. Status 0xF0 and above
// carries no channel, so it passes through untouched.
static DWORD LiveSendMsg(const LivePart& p, DWORD msg)
{
	const DWORD st = msg & 0xf0;
	if (p.sendCh < 0 || st < 0x80 || st >= 0xf0) return msg;
	return (msg & ~(DWORD)0x0f) | (DWORD)(p.sendCh & 15);
}

static volatile LONG g_thruOn = 0;
static int g_thruUseEng = 0;
static MidiItem* g_thruEv = NULL;
static int g_thruCount = 0;
static BYTE* g_thruSx = NULL;
static int g_thruSxBytes = 0;
static int g_thruPos = 0;
static __int64 g_thruLastPb = -1;
static wchar_t g_thruPath[520] = {};

enum { THRU_RAW_SIZE = 1048576, THRU_RAW_MASK = 1048575 };
static BYTE g_thruRaw[THRU_RAW_SIZE];
static LONG g_thruRawWr = 0;
static LONG g_thruRawRd = 0;
static int g_thruFmtRate = 0;
static int g_thruFmtCh = 2;
static int g_thruFmtBits = 16;
static int g_thruFmtBpf = 4;
static CRITICAL_SECTION g_thruPcmCs;
static int g_thruPcmCsOn = 0;
static int g_thruPrimed = 0;
static int g_thruHaveCur = 0;
static int g_thruRsAcc = 0;
static float g_thruHoldL = 0.f, g_thruHoldR = 0.f;
static float g_thruCurL = 0.f, g_thruCurR = 0.f;
static __int64 g_thruPcmPb = -1;

static void ThruPcmCsEnsure()
{
	if (g_thruPcmCsOn) return;
	InitializeCriticalSection(&g_thruPcmCs);
	g_thruPcmCsOn = 1;
}

static void ThruPcmReset()
{
	if (g_thruPcmCsOn) EnterCriticalSection(&g_thruPcmCs);
	g_thruRawWr = 0;
	g_thruRawRd = 0;
	g_thruPcmPb = -1;
	g_thruRsAcc = 0;
	g_thruPrimed = 0;
	g_thruHaveCur = 0;
	g_thruHoldL = 0.f;
	g_thruHoldR = 0.f;
	g_thruCurL = 0.f;
	g_thruCurR = 0.f;
	if (g_thruPcmCsOn) LeaveCriticalSection(&g_thruPcmCs);
}

static float ThruPcmSamp(const BYTE* p, int bits)
{
	if (bits <= 8)
		return ((float)p[0] - 128.f) * (1.f / 128.f);
	if (bits <= 16) {
		short s;
		memcpy(&s, p, 2);
		return (float)s * (1.f / 32768.f);
	}
	if (bits <= 24) {
		const int v = (int)p[0] | ((int)p[1] << 8) | ((int)(signed char)p[2] << 16);
		return (float)v * (1.f / 8388608.f);
	}
	int v;
	memcpy(&v, p, 4);
	return (float)v * (1.f / 2147483648.f);
}

static void ThruCloneFree()
{
	delete[] g_thruEv; g_thruEv = NULL;
	delete[] g_thruSx; g_thruSx = NULL;
	g_thruCount = 0;
	g_thruSxBytes = 0;
}

static int ThruRawPopUnlocked(float* L, float* R)
{
	const int bpf = g_thruFmtBpf;
	const int ch = g_thruFmtCh;
	const int bits = g_thruFmtBits;
	if (bpf < 1 || bpf > 32) return 0;
	const LONG r = g_thruRawRd;
	if (g_thruRawWr - r < bpf) return 0;
	BYTE fr[32];
	const int off = (int)(r & THRU_RAW_MASK);
	if (off + bpf <= THRU_RAW_SIZE)
		memcpy(fr, g_thruRaw + off, (size_t)bpf);
	else {
		const int a = THRU_RAW_SIZE - off;
		memcpy(fr, g_thruRaw + off, (size_t)a);
		memcpy(fr + a, g_thruRaw, (size_t)(bpf - a));
	}
	g_thruRawRd = r + bpf;
	int bps = bits / 8;
	if (bps < 1) bps = 1;
	if (bps > 4) bps = 4;
	float l = ThruPcmSamp(fr, bits);
	float rc = l;
	if (ch >= 2)
		rc = ThruPcmSamp(fr + bps, bits);
	if (ch >= 3) {
		const float c = ThruPcmSamp(fr + bps * 2, bits) * 0.707f;
		l += c;
		rc += c;
	}
	if (ch >= 5) l += ThruPcmSamp(fr + bps * 4, bits) * 0.5f;
	if (ch >= 6) rc += ThruPcmSamp(fr + bps * 5, bits) * 0.5f;
	if (ch >= 7) l += ThruPcmSamp(fr + bps * 6, bits) * 0.5f;
	if (ch >= 8) rc += ThruPcmSamp(fr + bps * 7, bits) * 0.5f;
	*L = l;
	*R = rc;
	return 1;
}

static int ThruRawAvailFramesUnlocked()
{
	const int bpf = g_thruFmtBpf;
	if (bpf < 1) return 0;
	LONG n = g_thruRawWr - g_thruRawRd;
	if (n < 0) n = 0;
	return (int)(n / bpf);
}

static void ThruRawTakeOutUnlocked(float* L, float* R)
{
	const int rate = g_thruFmtRate;
	if (rate == SAMPLE_RATE) {
		if (ThruRawPopUnlocked(L, R)) {
			g_thruHoldL = *L;
			g_thruHoldR = *R;
			g_thruHaveCur = 1;
			g_thruRsAcc = 0;
			return;
		}
		*L = g_thruHoldL;
		*R = g_thruHoldR;
		return;
	}
	if (rate < 8000) {
		*L = g_thruHoldL;
		*R = g_thruHoldR;
		return;
	}
	g_thruRsAcc += rate;
	while (g_thruRsAcc >= SAMPLE_RATE) {
		g_thruRsAcc -= SAMPLE_RATE;
		g_thruHoldL = g_thruCurL;
		g_thruHoldR = g_thruCurR;
		if (!ThruRawPopUnlocked(&g_thruCurL, &g_thruCurR))
			break;
		g_thruHaveCur = 1;
	}
	const float t = (float)g_thruRsAcc / (float)rate;
	*L = g_thruHoldL + (g_thruCurL - g_thruHoldL) * t;
	*R = g_thruHoldR + (g_thruCurR - g_thruHoldR) * t;
}

extern "C" void VstLiveThruSet(int enable)
{
	if (!enable) {
		EnterCriticalSection(&g_eng.cs);
		g_thruOn = 0;
		g_thruUseEng = 0;
		g_thruPos = 0;
		g_thruLastPb = -1;
		g_thruPath[0] = 0;
		ThruCloneFree();
		LeaveCriticalSection(&g_eng.cs);
		ThruPcmReset();
		return;
	}
	ThruPcmCsEnsure();
	if (g_thruOn)
		return;
	EnterCriticalSection(&g_eng.cs);
	g_thruOn = 1;
	LeaveCriticalSection(&g_eng.cs);
	ThruPcmReset();
}

extern "C" int VstLiveThruIsOn(void)
{
	return g_thruOn ? 1 : 0;
}

extern "C" void VstLiveThruBind(const wchar_t* midPath)
{
	EnterCriticalSection(&g_eng.cs);
	if (!g_thruOn) {
		LeaveCriticalSection(&g_eng.cs);
		return;
	}
	if (!midPath || !midPath[0]) {
		g_thruUseEng = 0;
		g_thruPos = 0;
		g_thruLastPb = -1;
		g_thruPath[0] = 0;
		ThruCloneFree();
		LeaveCriticalSection(&g_eng.cs);
		return;
	}
	if (_wcsicmp(g_thruPath, midPath) == 0 &&
		(g_thruUseEng || g_thruEv)) {
		LeaveCriticalSection(&g_eng.cs);
		return;
	}
	wcsncpy_s(g_thruPath, midPath, _TRUNCATE);
	g_thruPos = 0;
	g_thruLastPb = -1;
	if (g_eng.events && g_eng.eventCount > 0) {
		ThruCloneFree();
		g_thruUseEng = 1;
		LeaveCriticalSection(&g_eng.cs);
		return;
	}
	g_thruUseEng = 0;
	ThruCloneFree();
	if (LoadSmf(midPath) < 0) {
		g_thruPath[0] = 0;
		LeaveCriticalSection(&g_eng.cs);
		return;
	}
	g_thruEv = g_eng.events;
	g_thruCount = g_eng.eventCount;
	g_thruSx = g_eng.sysexData;
	g_thruSxBytes = g_eng.sysexBytes;
	g_eng.events = NULL;
	g_eng.eventCount = 0;
	g_eng.eventPos = 0;
	g_eng.sysexData = NULL;
	g_eng.sysexBytes = 0;
	delete[] g_eng.fileData;
	g_eng.fileData = NULL;
	g_eng.fileBytes = 0;
	g_eng.lengthSamples = 0;
	g_eng.playSample = 0;
	LeaveCriticalSection(&g_eng.cs);
}

extern "C" void VstLiveThruPcmPush(const BYTE* pcm, int bytes, int rate, int ch, int bits)
{
	if (!g_thruOn || !pcm || bytes <= 0) return;
	if (rate < 8000 || rate > 384000) return;
	if (ch < 1 || ch > 8) return;
	int bps = bits / 8;
	if (bps < 1) bps = 1;
	if (bps > 4) bps = 4;
	const int bpf = ch * bps;
	if (bpf < 1 || bpf > 32 || bytes < bpf) return;
	const int n = (bytes / bpf) * bpf;
	if (n <= 0) return;
	ThruPcmCsEnsure();
	EnterCriticalSection(&g_thruPcmCs);
	if (rate != g_thruFmtRate || ch != g_thruFmtCh || bits != g_thruFmtBits || bpf != g_thruFmtBpf) {
		g_thruRawWr = 0;
		g_thruRawRd = 0;
		g_thruPrimed = 0;
		g_thruHaveCur = 0;
		g_thruRsAcc = 0;
		g_thruFmtRate = rate;
		g_thruFmtCh = ch;
		g_thruFmtBits = bits;
		g_thruFmtBpf = bpf;
	}
	LONG w = g_thruRawWr;
	LONG r = g_thruRawRd;
	LONG used = w - r;
	if (used < 0) used = 0;
	LONG space = (THRU_RAW_SIZE - 1) - used;
	if (space < n) {
		LONG drop = n - space;
		const int align = g_thruFmtBpf;
		if (align > 1) {
			const LONG rem = drop % align;
			if (rem) drop += align - rem;
		}
		if (drop < used)
			r += drop;
		else
			r = w;
		g_thruRawRd = r;
	}
	const int wi = (int)(w & THRU_RAW_MASK);
	int first = THRU_RAW_SIZE - wi;
	if (first > n) first = n;
	memcpy(g_thruRaw + wi, pcm, (size_t)first);
	if (n > first)
		memcpy(g_thruRaw, pcm + first, (size_t)(n - first));
	g_thruRawWr = w + n;
	LeaveCriticalSection(&g_thruPcmCs);
}

extern "C" void VstLiveThruPcmMix(float* L, float* R, int frames)
{
	if (!g_thruOn || !L || !R || frames <= 0 || !g_thruPcmCsOn) return;
	EnterCriticalSection(&g_thruPcmCs);
	const int rate = g_thruFmtRate;
	if (rate >= 8000 && g_thruFmtBpf >= 1) {
		int avail = ThruRawAvailFramesUnlocked();
		int need = rate * 60 / 1000;
		if (need < BLOCK_FRAMES * 2) need = BLOCK_FRAMES * 2;
		if (!g_thruPrimed) {
			if (avail < need) {
				LeaveCriticalSection(&g_thruPcmCs);
				return;
			}
			g_thruPrimed = 1;
		}
		avail = ThruRawAvailFramesUnlocked();
		const int high = rate / 4;
		if (avail > high) {
			int keep = rate * 80 / 1000;
			if (keep < BLOCK_FRAMES * 3) keep = BLOCK_FRAMES * 3;
			int dump = avail - keep;
			float zL, zR;
			while (dump-- > 0)
				ThruRawPopUnlocked(&zL, &zR);
		}
		for (int i = 0; i < frames; ++i) {
			float sL, sR;
			ThruRawTakeOutUnlocked(&sL, &sR);
			L[i] += sL;
			R[i] += sR;
		}
	}
	LeaveCriticalSection(&g_thruPcmCs);
}

extern "C" void VstLiveThruPoll(__int64 playSample)
{
	if (!g_thruOn) return;
	if (g_thruPcmPb >= 0) {
		const __int64 d = playSample - g_thruPcmPb;
		if (d < -(SAMPLE_RATE / 5) || d > (__int64)SAMPLE_RATE * 2)
			ThruPcmReset();
	}
	g_thruPcmPb = playSample;
	MidiItem batch[64];
	int n = 0;
	EnterCriticalSection(&g_eng.cs);
	if (!g_thruOn) {
		LeaveCriticalSection(&g_eng.cs);
		return;
	}
	MidiItem* ev = g_thruUseEng ? g_eng.events : g_thruEv;
	const int count = g_thruUseEng ? g_eng.eventCount : g_thruCount;
	const BYTE* sxSrc = g_thruUseEng ? g_eng.sysexData : g_thruSx;
	const int sxBytes = g_thruUseEng ? g_eng.sysexBytes : g_thruSxBytes;
	BYTE sxCopy[4096];
	int sxCopyN = 0;
	if (sxSrc && sxBytes > 0 && sxBytes <= 4096) {
		memcpy(sxCopy, sxSrc, (size_t)sxBytes);
		sxCopyN = sxBytes;
	}
	if (!ev || count <= 0) {
		LeaveCriticalSection(&g_eng.cs);
		return;
	}
	if (playSample < g_thruLastPb) {
		g_thruPos = 0;
		g_thruLastPb = -1;
	}
	const int notesOk = (g_thruLastPb >= 0) ? 1 : 0;
	while (g_thruPos < count && ev[g_thruPos].sample <= playSample && n < 64) {
		MidiItem e = ev[g_thruPos++];
		const int st = (int)(e.msg & 0xf0);
		if (!notesOk && (st == 0x80 || st == 0x90 || st == 0xa0))
			continue;
		batch[n++] = e;
	}
	g_thruLastPb = playSample;
	LeaveCriticalSection(&g_eng.cs);
	for (int i = 0; i < n; ++i) {
		MidiItem e = batch[i];
		if ((e.msg & 0xff) == 0xff) continue;
		int port = e.port;
		if (port < 0) port = 0;
		if (port > 1) port = 1;
		if ((e.msg & 0xff) == 0xf0 && e.sysexOff >= 0 && sxCopyN > 0) {
			const int len = (int)e.aux;
			if (e.sysexOff + len <= sxCopyN)
				VstLiveMidiSysex(port, sxCopy + e.sysexOff, len);
			continue;
		}
		VstLiveMidiShort(port, e.msg);
	}
}

extern "C" void VstLiveMidiShort(int portIndex0to2, DWORD shortMsg)
{
	if (portIndex0to2 < 0 || portIndex0to2 > 2) return;
	const int chPart = portIndex0to2 * 16 + (int)(shortMsg & 15);
	EnterCriticalSection(&g_eng.cs);
	const int multi = LiveMultiPart(portIndex0to2);
	const int part = (multi >= 0) ? multi : chPart;
	const int occupied = (multi >= 0) || LivePartLoaded(chPart);
	if (occupied && part >= 0 && part < 32)
		LivePendPush(g_eng.live[part], LiveSendMsg(g_eng.live[part], shortMsg));
	LeaveCriticalSection(&g_eng.cs);
	if (!occupied) return;
	// Remote parts route inside KpiHost64, which holds the same part layout.
	LiveActTrack(portIndex0to2, shortMsg);
	LiveRemoteMidi(portIndex0to2, shortMsg);
}

extern "C" int VstLiveSendChannel(int part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return -1;
	return g_eng.live[part1to32 - 1].sendCh;
}

extern "C" void VstLiveSetSendChannel(int part1to32, int sendCh)
{
	if (part1to32 < 1 || part1to32 > 32) return;
	if (sendCh > 15) sendCh = 15;
	if (sendCh < 0) sendCh = -1;
	LivePart& p = g_eng.live[part1to32 - 1];
	EnterCriticalSection(&g_eng.cs);
	// Whatever is held on the old channel would never get its note-off.
	if (p.sendCh != sendCh && (p.effect || p.vst3)) LivePanicPart(p);
	p.sendCh = sendCh;
	LeaveCriticalSection(&g_eng.cs);
#ifndef KPIHOST64_BUILD
	if (p.remote) g_kpiHost.VstLiveSetSendChannel((uint32_t)part1to32, sendCh);
#endif
}

// Channel the plug-in actually hears for this part, used to aim program
// changes at the right internal part of a multi-timbral instrument.
static int LivePartSendCh(const LivePart& p, int part1to32)
{
	return p.sendCh >= 0 ? p.sendCh : ((part1to32 - 1) & 15);
}

static VstIntPtr LiveVst2Dispatch(AEffect* e, VstInt32 op, VstInt32 index,
	VstIntPtr value, void* ptr)
{
	if (!e || !e->dispatcher) return 0;
	VstIntPtr rc = 0;
	__try { rc = e->dispatcher(e, op, index, value, ptr, 0.0f); }
	__except (EXCEPTION_EXECUTE_HANDLER) { rc = 0; }
	return rc;
}

extern "C" int VstLiveProgramCount(int part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return 0;
	LivePart& p = g_eng.live[part1to32 - 1];
#ifndef KPIHOST64_BUILD
	if (p.remote) {
		uint32_t total = 0, cur = 0;
		std::vector<std::wstring> names;
		if (!g_kpiHost.VstLivePrograms((uint32_t)part1to32, 0, 0, total, cur, names))
			return 0;
		return (int)total;
	}
#endif
	if (p.vst3) return Vst3ProgramCount(p.vst3);
	if (p.effect) return p.effect->numPrograms > 0 ? p.effect->numPrograms : 0;
	return 0;
}

extern "C" int VstLiveProgramCurrent(int part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return -1;
	LivePart& p = g_eng.live[part1to32 - 1];
#ifndef KPIHOST64_BUILD
	if (p.remote) {
		uint32_t total = 0, cur = 0xFFFFFFFFu;
		std::vector<std::wstring> names;
		if (!g_kpiHost.VstLivePrograms((uint32_t)part1to32, 0, 0, total, cur, names))
			return -1;
		return cur == 0xFFFFFFFFu ? -1 : (int)cur;
	}
#endif
	if (p.effect && p.prog < 0)
		return (int)LiveVst2Dispatch(p.effect, effGetProgram, 0, 0, NULL);
	return p.prog;
}

extern "C" int VstLiveProgramName(int part1to32, int index, wchar_t* out,
	int outChars)
{
	if (!out || outChars <= 0) return 0;
	out[0] = 0;
	if (part1to32 < 1 || part1to32 > 32 || index < 0) return 0;
	LivePart& p = g_eng.live[part1to32 - 1];
#ifndef KPIHOST64_BUILD
	if (p.remote) {
		uint32_t total = 0, cur = 0;
		std::vector<std::wstring> names;
		if (!g_kpiHost.VstLivePrograms((uint32_t)part1to32, (uint32_t)index, 1,
			total, cur, names) || names.empty())
			return 0;
		wcsncpy_s(out, outChars, names[0].c_str(), _TRUNCATE);
		return out[0] ? 1 : 0;
	}
#endif
	if (p.vst3) return Vst3ProgramName(p.vst3, index, out, outChars);
	if (p.effect) {
		char nm[64] = {};
		if (LiveVst2Dispatch(p.effect, effGetProgramNameIndexed, index, -1, nm) ||
			nm[0]) {
			MultiByteToWideChar(CP_ACP, 0, nm, -1, out, outChars);
			out[outChars - 1] = 0;
		}
		if (!out[0]) _snwprintf_s(out, outChars, _TRUNCATE, L"Program %d", index + 1);
		return 1;
	}
	return 0;
}

extern "C" int VstLiveProgramNames(int part1to32, int first, int count,
	wchar_t* out, int stride)
{
	if (!out || stride <= 1 || count <= 0 || first < 0) return 0;
	if (part1to32 < 1 || part1to32 > 32) return 0;
	for (int i = 0; i < count; ++i) out[(size_t)i * stride] = 0;
#ifndef KPIHOST64_BUILD
	LivePart& p = g_eng.live[part1to32 - 1];
	if (p.remote) {
		uint32_t total = 0, cur = 0;
		std::vector<std::wstring> names;
		if (!g_kpiHost.VstLivePrograms((uint32_t)part1to32, (uint32_t)first,
			(uint32_t)count, total, cur, names))
			return 0;
		int n = 0;
		for (; n < count && n < (int)names.size(); ++n)
			wcsncpy_s(out + (size_t)n * stride, stride, names[n].c_str(), _TRUNCATE);
		return n;
	}
#endif
	int n = 0;
	for (; n < count; ++n)
		if (!VstLiveProgramName(part1to32, first + n, out + (size_t)n * stride, stride))
			break;
	return n;
}

extern "C" int VstLiveSetProgram(int part1to32, int index)
{
	if (part1to32 < 1 || part1to32 > 32 || index < 0) return 0;
	LivePart& p = g_eng.live[part1to32 - 1];
#ifndef KPIHOST64_BUILD
	if (p.remote) {
		if (!g_kpiHost.VstLiveSetProgram((uint32_t)part1to32, (uint32_t)index))
			return 0;
		p.prog = index;
		return 1;
	}
#endif
	int ok = 0;
	EnterCriticalSection(&g_eng.cs);
	if (p.vst3) {
		// Aimed at the internal part that answers this slot's channel, which
		// is what HALion Sonic / SampleTank / Groove Agent key their program
		// lists off.
		ok = Vst3SetChannelProgram(p.vst3, LivePartSendCh(p, part1to32), index);
	} else if (p.effect) {
		LiveVst2Dispatch(p.effect, effSetProgram, 0, index, NULL);
		ok = 1;
	}
	if (ok) p.prog = index;
	LeaveCriticalSection(&g_eng.cs);
	return ok;
}

extern "C" void VstLiveMidiSysex(int portIndex0to2, const unsigned char* data,
	int bytes)
{
	if (portIndex0to2 < 0 || portIndex0to2 > 2 || !data || bytes <= 0) return;
	LiveSysexDescribe(data, bytes);
#ifndef KPIHOST64_BUILD
	if (LiveRemoteActive())
		g_kpiHost.VstLiveSysex((uint32_t)portIndex0to2, data, (uint32_t)bytes);
#endif
	EnterCriticalSection(&g_eng.cs);
	int part = LiveMultiPart(portIndex0to2);
	if (part < 0) {
		const int b0 = portIndex0to2 * 16;
		for (int i = b0; i < b0 + 16 && i < 32; ++i)
			if (LivePartLoaded(i)) { part = i; break; }
	}
	// Queued with the notes so a GS reset cannot wipe the events around it.
	if (part >= 0 && part < 32)
		LivePendPushSysex(g_eng.live[part], data, bytes);
	LeaveCriticalSection(&g_eng.cs);
}

// The plug-in draws its own editor, and VST2 requires effEditIdle for the
// meters and animations to advance. Both the window and the idle pump must
// belong to the thread that loaded the module, so KpiHost64 marshals every
// call here onto its plug-in UI thread.
struct LiveEditorRect { short top, left, bottom, right; };

enum { LIVE_EDITOR_IDLE_TIMER = 1 };

static LRESULT CALLBACK LiveEditorWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	const int part = (int)GetWindowLongPtrW(h, GWLP_USERDATA);
	if (part >= 1 && part <= 32) {
		if (m == WM_TIMER && w == LIVE_EDITOR_IDLE_TIMER) {
			AEffect* e = g_eng.live[part - 1].effect;
			if (e && e->dispatcher) {
				__try { e->dispatcher(e, effEditIdle, 0, 0, NULL, 0); }
				__except (EXCEPTION_EXECUTE_HANDLER) {}
			}
			return 0;
		}
		if (m == WM_CLOSE) {
#ifdef KPIHOST64_BUILD
			VstHost64_LiveAudioStop();
#endif
			VstLiveEditorClose(part);
#ifdef KPIHOST64_BUILD
			if (VstHost64_LiveActive())
				VstHost64_LiveAudioStart();
#endif
			return 0;
		}
	}
	return DefWindowProcW(h, m, w, l);
}

static HWND LiveEditorCreateHostWnd(int part1to32)
{
	static int registered = 0;
	if (!registered) {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = LiveEditorWndProc;
		wc.hInstance = GetModuleHandleW(NULL);
		wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wc.lpszClassName = L"OggVstLiveEditor";
		RegisterClassExW(&wc);
		registered = 1;
	}
	wchar_t title[64];
	_snwprintf_s(title, _TRUNCATE, L"VST %d", part1to32);
	HWND h = CreateWindowExW(0, L"OggVstLiveEditor", title,
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL,
		GetModuleHandleW(NULL), NULL);
	if (h) SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)part1to32);
	return h;
}

extern "C" int VstLiveEditorOpen(int part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return -1;
	LivePart& p = g_eng.live[part1to32 - 1];
#ifndef KPIHOST64_BUILD
	if (p.remote)
		return g_kpiHost.VstLiveEditorOpen((uint32_t)part1to32) ? 0 : -5;
#endif
	if (p.edWnd && IsWindow(p.edWnd)) {
		ShowWindow(p.edWnd, SW_SHOW);
		SetForegroundWindow(p.edWnd);
		return 0;
	}
	const int wantVst3 = p.vst3 != NULL;
	if (!wantVst3) {
		if (!p.effect || !p.effect->dispatcher) return -2;
		if (!(p.effect->flags & effFlagsHasEditor)) return -3;
	}
	HWND host = LiveEditorCreateHostWnd(part1to32);
	if (!host) return -4;
	int cw = 640, chh = 480;
	if (wantVst3) {
		// VST3 hands back its own IPlugView instead of drawing into ours.
		if (Vst3EditorOpen(p.vst3, host, &cw, &chh) != 0) {
			DestroyWindow(host);
			return -6;
		}
	} else {
		VstIntPtr opened = 0;
		__try { opened = p.effect->dispatcher(p.effect, effEditOpen, 0, 0, host, 0); }
		__except (EXCEPTION_EXECUTE_HANDLER) { opened = 0; }
		LiveEditorRect* r = NULL;
		__try { p.effect->dispatcher(p.effect, effEditGetRect, 0, 0, &r, 0); }
		__except (EXCEPTION_EXECUTE_HANDLER) { r = NULL; }
		if (r && r->right > r->left) cw = r->right - r->left;
		if (r && r->bottom > r->top) chh = r->bottom - r->top;
		(void)opened;
	}
	// The size the plug-in reports is in its own pixels, which are the screen's
	// physical ones when it scales itself for a high-DPI monitor. Sizing the
	// frame with that number in a process that is not DPI aware leaves a black
	// margin around the view, so prefer the size of the window the plug-in
	// actually created: it is measured in the same space as the frame.
	{
		RECT cr = {};
		HWND child = GetWindow(host, GW_CHILD);
		if (child && GetClientRect(child, &cr) && cr.right > 16 && cr.bottom > 16) {
			cw = cr.right;
			chh = cr.bottom;
		}
	}
	RECT wr = { 0, 0, cw, chh };
	AdjustWindowRect(&wr, (DWORD)GetWindowLongW(host, GWL_STYLE), FALSE);
	SetWindowPos(host, NULL, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	p.edWnd = host;
	SetTimer(host, LIVE_EDITOR_IDLE_TIMER, 30, NULL);
	ShowWindow(host, SW_SHOW);
	SetForegroundWindow(host);
	return 0;
}

extern "C" void VstLiveEditorClose(int part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return;
#ifdef KPIHOST64_BUILD
	if (InterlockedCompareExchange(&g_liveAbandonPlugins, 0, 0)) {
		g_eng.live[part1to32 - 1].edWnd = NULL;
		return;
	}
#endif
	LivePart& p = g_eng.live[part1to32 - 1];
#ifndef KPIHOST64_BUILD
	if (p.remote) {
		if (InterlockedCompareExchange(&g_liveShuttingDown, 0, 0))
			return;
		const int resume = (g_liveShm.audio != NULL);
		if (resume) LiveRemoteStop();
		g_kpiHost.VstLiveEditorClose((uint32_t)part1to32);
		if (resume && g_liveShm.parts > 0) LiveRemoteOpenShm();
		return;
	}
#endif
	HWND h = p.edWnd;
	if (!h) return;
	p.edWnd = NULL;
	KillTimer(h, LIVE_EDITOR_IDLE_TIMER);
	if (p.vst3) Vst3EditorClose(p.vst3);
	if (p.effect && p.effect->dispatcher) {
		__try { p.effect->dispatcher(p.effect, effEditClose, 0, 0, NULL, 0); }
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	SetWindowLongPtrW(h, GWLP_USERDATA, 0);
	DestroyWindow(h);
}

extern "C" int VstLiveRender(float* L, float* R, int frames)
{
	if (!L || !R || frames <= 0) return 0;
	EnterCriticalSection(&g_eng.cs);
	ZeroMemory(L, frames * sizeof(float));
	ZeroMemory(R, frames * sizeof(float));
	__declspec(align(32)) float tl[BLOCK_FRAMES];
	__declspec(align(32)) float tr[BLOCK_FRAMES];
	for (int pos = 0; pos < frames;) {
		int n = frames - pos;
		if (n > BLOCK_FRAMES) n = BLOCK_FRAMES;
		for (int p = 0; p < 32; ++p)
			if (g_eng.live[p].effect || g_eng.live[p].vst3) {
			// Immediately before the process call, as VST2 requires.
			LivePendFlush(g_eng.live[p]);
			if (g_eng.live[p].vst3)
				Vst3Process(g_eng.live[p].vst3, tl, tr, n);
			else
				RenderEffect(g_eng.live[p].effect, tl, tr, n);
			for (int i = 0; i < n; ++i) {
				L[pos + i] += tl[i];
				R[pos + i] += tr[i];
			}
		}
		pos += n;
	}
	LeaveCriticalSection(&g_eng.cs);
	LiveRemoteMix(L, R, frames);
	return frames;
}
