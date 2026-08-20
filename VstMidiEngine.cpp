#include "stdafx.h"
#include "VstMidiEngine.h"
#include "Vst3Host.h"
#include "third_party/vst2/aeffect.h"

#include <math.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdarg.h>
#include <shlobj.h>
#include <mmsystem.h>

#ifndef KPIHOST64_BUILD
#include "kpi_host_ipc.h"
#include "KpiHostClient.h"
#endif

#pragma comment(lib, "winmm.lib")
static int ProbeLoadedEffectAudible(AEffect* effect);
static int ProbeLoadedVst3Audible(Vst3Inst* vst3, int drums);

#pragma comment(lib, "advapi32.lib")

namespace {

enum { SAMPLE_RATE = 44100, BLOCK_FRAMES = 512, MAX_MIDI_EVENTS = 500000 };
enum { CACHE_MAGIC = 0x43545356, CACHE_VERSION = 6 }; // + isLiveOk

struct MidiItem {
	unsigned __int64 tick;
	__int64 sample;
	DWORD msg; // 0xff tempo, 0xf0 sysex, else channel short
	DWORD aux; // tempo usec/qn, or sysex byte length
	int port; // SMF MIDI Port meta (FF 21): 0=ch1-16, 1=17-32, 2=33-48
	int sysexOff; // offset into EngineState::sysexData; -1 if not sysex
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
	float outL[BLOCK_FRAMES];
	float outR[BLOCK_FRAMES];
	float zero[BLOCK_FRAMES];
	float mixL[BLOCK_FRAMES];
	float mixR[BLOCK_FRAMES];
	LivePart live[32];
	int gmResetMode; // 0=GM+GS, 1=GS, 2=XG（シーク巻き戻し時に再送）

	EngineState() : csReady(0), fileData(NULL), fileBytes(0), events(NULL),
		eventCount(0), eventPos(0), playSample(0), lengthSamples(0),
		module(NULL), effect(NULL), vst3(NULL),
		moduleB(NULL), effectB(NULL), moduleC(NULL), effectC(NULL), vst3C(NULL),
		sysexData(NULL), sysexBytes(0), maxMidiPort(0), usingBuiltin(1),
		useEnsemble(0), useDrums(0), useMapper(0), midiOut(NULL), mixCount(0),
		ringRead(0), ringCount(0), gmResetMode(0)
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

static EngineState g_eng;
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
	const UINT dpi = WaitDpi(owner);
	const int w = MulDiv(520, (int)dpi, 96);
	const int h = MulDiv(112, (int)dpi, 96);
	HWND wnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"STATIC",
		L"VSTプラグインを検索しています…\nSearching VST plug-ins…",
		WS_POPUP | WS_BORDER | SS_CENTER,
		r.left + ((r.right - r.left) - w) / 2,
		r.top + ((r.bottom - r.top) - h) / 2, w, h,
		owner, NULL, GetModuleHandleW(NULL), NULL);
	if (wnd) {
		SendMessage(wnd, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
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

static VstTimeInfo g_vstTime = {};
static char g_vstHostDirA[VST_PATH_CHARS] = {};

static VstIntPtr VSTCALLBACK HostCallback(AEffect*, VstInt32 opcode,
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
	case audioMasterGetDirectory:
		if (g_vstHostDirA[0]) return (VstIntPtr)g_vstHostDirA;
		return 0;
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
}

static void ProbeVst2(const wchar_t* path)
{
	const int arch = PeArch(path);
	wchar_t base[VST_NAME_CHARS];
	BaseNameNoExt(path, base);
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
		if (plugDir[0]) SetDllDirectoryW(plugDir);
		mod = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		if (plugDir[0]) SetDllDirectoryW(NULL);
	}
	if (!mod) return;
	VSTPluginMainProc mainProc = (VSTPluginMainProc)GetProcAddress(mod, "VSTPluginMain");
	if (!mainProc) mainProc = (VSTPluginMainProc)GetProcAddress(mod, "main");
	if (!mainProc) { FreeLibrary(mod); return; }
	AEffect* e = NULL;
	__try { e = mainProc(HostCallback); }
	__except (EXCEPTION_EXECUTE_HANDLER) { e = NULL; }
	if (!e || e->magic != kEffectMagic || !e->dispatcher) {
		FreeLibrary(mod); return;
	}
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
	wchar_t wide[VST_NAME_CHARS] = {};
	if (nm[0]) MultiByteToWideChar(CP_ACP, 0, nm, -1, wide, VST_NAME_CHARS);
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
			wchar_t msg[384];
			_snwprintf_s(msg, _TRUNCATE,
				L"VSTスキャン %d / %d（発見 %d）\nScanning %d / %d  (%d found)\n%s",
				g_scanIndex, g_scanTotal, g_pluginCount,
				g_scanIndex, g_scanTotal, g_pluginCount, fd.cFileName);
			SetWaitStatus(wait, msg);
		}
	} while (FindNextFileW(h, &fd) && g_pluginCount < VST_MAX_PLUGINS);
	FindClose(h);
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
	// Same tick: tempo → bank → other CC → program → bend → note off → note on.
	const int ra = MidiStatusRank(a->msg), rb = MidiStatusRank(b->msg);
	if (ra < rb) return -1;
	if (ra > rb) return 1;
	const int ca = (int)(a->msg & 15), cb = (int)(b->msg & 15);
	if (ca < cb) return -1;
	if (ca > cb) return 1;
	return 0;
}

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
	BYTE* sxData = new BYTE[size + 8];
	int sxUsed = 0;
	int count = 0;
	int maxPort = 0;
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
					++count;
				} else if (type == 0x21 && ml >= 1) {
					// RP-019 MIDI Port Prefix — port A/B/... for 32ch modules.
					curPort = (int)q[0];
					if (curPort > maxPort) maxPort = curPort;
				}
				q += ml;
			} else if (st == 0xf0 || st == 0xf7) {
				unsigned sl = 0;
				if (!ReadVar(q, end, sl) || q + sl > end) break;
				// Rebuild a full dump (F0 + payload). F7 escape is raw payload.
				const int need = (st == 0xf0) ? (1 + (int)sl) : (int)sl;
				if (need > 0 && sxUsed + need <= (int)size + 8) {
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
					++count;
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
					++count;
				}
			}
		}
		p = end;
	}
	if (!count) {
		delete[] ev; delete[] data; delete[] sxData; return -6;
	}
	qsort(ev, count, sizeof(MidiItem), MidiCmp);
	unsigned __int64 lastTick = 0;
	unsigned tempo = 500000;
	__int64 sample = 0;
	for (int i = 0; i < count; ++i) {
		const unsigned __int64 dt = ev[i].tick - lastTick;
		sample += (__int64)((dt * tempo * (unsigned __int64)SAMPLE_RATE) /
			((unsigned __int64)division * 1000000ULL));
		ev[i].sample = sample;
		lastTick = ev[i].tick;
		if ((ev[i].msg & 0xff) == 0xff && ev[i].aux) tempo = ev[i].aux;
	}
	g_eng.fileData = data;
	g_eng.fileBytes = size;
	g_eng.sysexData = sxData;
	g_eng.sysexBytes = sxUsed;
	g_eng.events = ev;
	g_eng.eventCount = count;
	g_eng.maxMidiPort = maxPort;
	g_eng.lengthSamples = sample + SAMPLE_RATE * 2;
	EnsLog(L"LoadSmf events=%d maxPort=%d sysexBytes=%d (ports: %dch)",
		count, maxPort, sxUsed, (maxPort + 1) * 16);
	return 0;
}

static void SendVstEvents(AEffect* e, const MidiItem* ev, int count,
	__int64 blockStart)
{
	if (!e || !e->dispatcher || count <= 0) return;
	struct EventBlock {
		VstInt32 numEvents;
		VstIntPtr reserved;
		VstEvent* events[256];
	} block = {};
	VstMidiEvent me[256] = {};
	if (count > 256) count = 256;
	for (int i = 0; i < count; ++i) {
		me[i].type = kVstMidiType;
		me[i].byteSize = sizeof(VstMidiEvent);
		__int64 d = ev[i].sample - blockStart;
		me[i].deltaFrames = d < 0 ? 0 : (d > BLOCK_FRAMES ? BLOCK_FRAMES : (int)d);
		me[i].midiData[0] = (char)(ev[i].msg & 0xff);
		me[i].midiData[1] = (char)((ev[i].msg >> 8) & 0x7f);
		me[i].midiData[2] = (char)((ev[i].msg >> 16) & 0x7f);
		block.events[i] = (VstEvent*)&me[i];
	}
	block.numEvents = count;
	e->dispatcher(e, effProcessEvents, 0, 0, &block, 0);
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

// Broadcast SysEx to every active song unit (SC-88: GM/GS resets must hit both).
static void BroadcastSongSysex(const BYTE* data, int len, int deltaFrames)
{
	if (!data || len < 2) return;
	if (g_eng.effect) SendVstSysex(g_eng.effect, data, len, deltaFrames);
	if (g_eng.effectB) SendVstSysex(g_eng.effectB, data, len, deltaFrames);
	if (g_eng.effectC) SendVstSysex(g_eng.effectC, data, len, deltaFrames);
	if (g_eng.midiOut) MapperSysex(g_eng.midiOut, data, (DWORD)len);
}

static void FlushUnitShorts(AEffect* effect, Vst3Inst* vst3, MidiItem* batch,
	int& n, __int64 start, int frames)
{
	if (!n) return;
	if (effect) SendVstEvents(effect, batch, n, start);
	if (vst3) {
		for (int i = 0; i < n; ++i) {
			__int64 d = batch[i].sample - start;
			int offset = d < 0 ? 0 : (d >= frames ? frames - 1 : (int)d);
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

extern "C" void VstMidiInjectShort(int portIndex0to2, DWORD shortMsg)
{
	int port = portIndex0to2;
	if (port < 0) port = 0;
	if (port > 2) port = 2;
	SongOvSet(port, shortMsg);
	const LONG w = g_injW;
	if ((w - g_injR) >= (SONG_INJ_CAP - 1)) return;
	const int i = (int)(w & (SONG_INJ_CAP - 1));
	g_injMsg[i] = shortMsg;
	g_injPort[i] = (BYTE)port;
	MemoryBarrier();
	g_injW = w + 1;
}

extern "C" int VstMidiStealInjects(BYTE* ports, DWORD* msgs, int maxCount)
{
	if (!ports || !msgs || maxCount < 1) return 0;
	int n = 0;
	LONG r = g_injR;
	const LONG w = g_injW;
	while (n < maxCount && r != w) {
		const int i = (int)(r & (SONG_INJ_CAP - 1));
		ports[n] = g_injPort[i];
		msgs[n] = g_injMsg[i];
		++n;
		++r;
	}
	g_injR = r;
	return n;
}

namespace {

static void EmitSongShort(int port, DWORD msg, __int64 start, int frames)
{
	MidiItem it = {};
	it.msg = msg;
	it.sample = start;
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
		if (ms.vst3) Vst3MidiShort(ms.vst3, m.msg, 0);
		return;
	}
	if (g_eng.midiOut && (port <= 0 || !g_eng.effectB))
		midiOutShortMsg(g_eng.midiOut, msg);
	int n = 1;
	if (port <= 0) {
		if (g_eng.effect || g_eng.vst3)
			FlushUnitShorts(g_eng.effect, g_eng.vst3, &it, n, start, frames);
	} else if (port == 1) {
		if (g_eng.effectB)
			FlushUnitShorts(g_eng.effectB, NULL, &it, n, start, frames);
	} else if (g_eng.effectC || g_eng.vst3C) {
		FlushUnitShorts(g_eng.effectC, g_eng.vst3C, &it, n, start, frames);
	}
}

static void FlushInjectQueue(__int64 start, int frames)
{
	SongOvExpire();
	LONG r = g_injR;
	const LONG w = g_injW;
	while (r != w) {
		const int i = (int)(r & (SONG_INJ_CAP - 1));
		EmitSongShort((int)g_injPort[i], g_injMsg[i], start, frames);
		++r;
	}
	g_injR = r;
}

static void DispatchDueEvents(__int64 start, int frames)
{
	MidiItem batch0[256], batch1[256], batch2[256];
	int n0 = 0, n1 = 0, n2 = 0;
	const __int64 end = start + frames;
	auto flushAll = [&]() {
		FlushUnitShorts(g_eng.effect, g_eng.vst3, batch0, n0, start, frames);
		FlushUnitShorts(g_eng.effectB, NULL, batch1, n1, start, frames);
		FlushUnitShorts(g_eng.effectC, g_eng.vst3C, batch2, n2, start, frames);
	};
	FlushInjectQueue(start, frames);
	while (g_eng.eventPos < g_eng.eventCount &&
		g_eng.events[g_eng.eventPos].sample < end) {
		MidiItem e = g_eng.events[g_eng.eventPos++];
		e.msg = SongOverrideMsg(e.port, e.msg);
		if ((e.msg & 0xff) == 0xff) continue;

		if ((e.msg & 0xff) == 0xf0 && e.sysexOff >= 0 && g_eng.sysexData) {
			flushAll();
			__int64 d = e.sample - start;
			int offset = d < 0 ? 0 : (d >= frames ? frames - 1 : (int)d);
			const int len = (int)e.aux;
			if (e.sysexOff + len <= g_eng.sysexBytes)
				BroadcastSongSysex(g_eng.sysexData + e.sysexOff, len, offset);
			continue;
		}

		if (g_eng.midiOut && (e.port <= 0 || !g_eng.effectB))
			midiOutShortMsg(g_eng.midiOut, e.msg);

		const int port = e.port < 0 ? 0 : e.port;
		MidiItem* batch = batch0;
		int* n = &n0;
		int hasTgt = (g_eng.effect || g_eng.vst3) ? 1 : 0;
		if (port == 1) {
			if (g_eng.effectB) { batch = batch1; n = &n1; hasTgt = 1; }
			else { batch = batch0; n = &n0; } // no B: drop onto A would collide — skip
			if (!g_eng.effectB) continue;
		} else if (port >= 2) {
			if (g_eng.effectC || g_eng.vst3C) {
				batch = batch2; n = &n2; hasTgt = 1;
			} else if (g_eng.effectB) {
				// No 3rd unit: keep port2+ silent rather than squash onto B.
				continue;
			} else {
				continue;
			}
		}
		if (!hasTgt) continue;
		if (*n >= 256) flushAll();
		batch[(*n)++] = e;
	}
	flushAll();
}

static void RenderSongUnits(int frames)
{
	ZeroMemory(g_eng.outL, frames * sizeof(float));
	ZeroMemory(g_eng.outR, frames * sizeof(float));
	if (g_eng.vst3)
		Vst3Process(g_eng.vst3, g_eng.outL, g_eng.outR, frames);
	else if (g_eng.effect)
		RenderEffect(g_eng.effect, g_eng.outL, g_eng.outR, frames);

	if (g_eng.effectB) {
		RenderEffect(g_eng.effectB, g_eng.mixL, g_eng.mixR, frames);
		for (int i = 0; i < frames; ++i) {
			g_eng.outL[i] += g_eng.mixL[i];
			g_eng.outR[i] += g_eng.mixR[i];
		}
	}
	if (g_eng.effectC) {
		RenderEffect(g_eng.effectC, g_eng.mixL, g_eng.mixR, frames);
		for (int i = 0; i < frames; ++i) {
			g_eng.outL[i] += g_eng.mixL[i];
			g_eng.outR[i] += g_eng.mixR[i];
		}
	} else if (g_eng.vst3C) {
		ZeroMemory(g_eng.mixL, frames * sizeof(float));
		ZeroMemory(g_eng.mixR, frames * sizeof(float));
		Vst3Process(g_eng.vst3C, g_eng.mixL, g_eng.mixR, frames);
		for (int i = 0; i < frames; ++i) {
			g_eng.outL[i] += g_eng.mixL[i];
			g_eng.outR[i] += g_eng.mixR[i];
		}
	}
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

static void RenderEffect(AEffect* e, float* l, float* r, int frames)
{
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
	wchar_t prevDllDir[VST_PATH_CHARS]; prevDllDir[0] = 0;
	const DWORD prevLen = GetDllDirectoryW(VST_PATH_CHARS, prevDllDir);
	if (plugDir[0]) SetDllDirectoryW(plugDir);

	module = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!module) {
		if (plugDir[0]) SetDllDirectoryW(prevLen ? prevDllDir : NULL);
		return 0;
	}
	VSTPluginMainProc proc = (VSTPluginMainProc)GetProcAddress(module, "VSTPluginMain");
	if (!proc) proc = (VSTPluginMainProc)GetProcAddress(module, "main");
	if (!proc) {
		FreeLibrary(module); module = NULL;
		if (plugDir[0]) SetDllDirectoryW(prevLen ? prevDllDir : NULL);
		return 0;
	}
	__try { effect = proc(HostCallback); }
	__except (EXCEPTION_EXECUTE_HANDLER) { effect = NULL; }
	if (!effect || effect->magic != kEffectMagic || !effect->dispatcher ||
		!effect->processReplacing) {
		if (effect && effect->dispatcher)
			__try { effect->dispatcher(effect, effClose, 0, 0, NULL, 0); }
			__except (EXCEPTION_EXECUTE_HANDLER) {}
		FreeLibrary(module); module = NULL; effect = NULL;
		if (plugDir[0]) SetDllDirectoryW(prevLen ? prevDllDir : NULL);
		return 0;
	}
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
	__except (EXCEPTION_EXECUTE_HANDLER) {
		FreeLibrary(module); module = NULL; effect = NULL;
		if (plugDir[0]) SetDllDirectoryW(prevLen ? prevDllDir : NULL);
		return 0;
	}
	// Keep plugin dir on the search path while the module stays loaded
	// (SCCore / companion DLLs may delay-load). Restore only on close via
	// tracking would be complex; leave SetDllDirectory to this plug's dir
	// for the session — FreeSong/CloseEffect does not clear it; next Load
	// overwrites. Acceptable for single-song MIDI host.
	if (plugDir[0])
		WideCharToMultiByte(CP_ACP, 0, plugDir, -1, g_vstHostDirA, VST_PATH_CHARS, NULL, NULL);
	EnsLog(L"LoadVst2 OK dir=%s path=%s outs=%d flags=0x%X sccore=%p",
		plugDir, path, effect->numOutputs, (unsigned)effect->flags,
		(void*)GetModuleHandleW(L"SCCore.dll"));
	return 1;
}

static void CloseEffect(HMODULE& module, AEffect*& effect)
{
	if (effect && effect->dispatcher) {
		__try {
			effect->dispatcher(effect, effStopProcess, 0, 0, NULL, 0);
			effect->dispatcher(effect, effMainsChanged, 0, 0, NULL, 0);
			effect->dispatcher(effect, effClose, 0, 0, NULL, 0);
		} __except (EXCEPTION_EXECUTE_HANDLER) {}
	}
	effect = NULL;
	if (module) FreeLibrary(module);
	module = NULL;
	SetDllDirectoryW(NULL);
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

static void RescoreMultiFlags(void)
{
	for (int i = 0; i < g_pluginCount; ++i) {
		VstPluginInfo& p = g_plugins[i];
		p.isMultiTimbral =
			(DetectMultiTimbralName(p.name) || DetectMultiTimbralName(p.path)) ? 1 : 0;
		// Cross-arch multi (x86 app + x64 SC-VA) stays pickable for KpiHost64.
		if (p.isMultiTimbral && p.arch != HostArch())
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
		SetWaitStatus(g_waitWnd, L"ロムプラーを開いています…\nOpening rompler…");
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
				SetWaitStatus(g_waitWnd, L"ドラム音源を開いています…\nOpening drum plug-in…");
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
				_snwprintf_s(msg, _TRUNCATE,
					L"類似確認 %d / %d\nMatching timbre %d / %d\n%s",
					nCatDone, nCat, nCatDone, nCat, p.name);
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
	g_eng.eventCount = g_eng.eventPos = 0;
	g_eng.playSample = g_eng.lengthSamples = 0;
	g_eng.ringRead = g_eng.ringCount = 0;
	ZeroMemory(g_eng.voices, sizeof(g_eng.voices));
	ZeroMemory(g_eng.drums, sizeof(g_eng.drums));
	ZeroMemory(g_eng.noteState, sizeof(g_eng.noteState));
	g_eng.usingBuiltin = 1;
	g_eng.useDrums = 0;
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

extern "C" int VstScanEnsure(HWND parentForWait)
{
	if (g_scanReady && !g_scanInvalid) return 0;
	if (!g_scanInvalid && LoadCache()) {
		RescoreMultiFlags();
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
	if (savedata.vstExtraPath[0] && DirExists(savedata.vstExtraPath) && count < 16) {
		SafeCopy(roots[count], VST_PATH_CHARS, savedata.vstExtraPath);
		++count;
	}
	static const wchar_t* knownMulti[] = {
		L"C:\\Roland VS\\64",
		L"C:\\Roland VS",
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
	if (savedata.vstMultiDll[0]) ++g_scanTotal;
	if (savedata.vstExtraPath[0] && !DirExists(savedata.vstExtraPath)) ++g_scanTotal;
	{
		wchar_t msg[256];
		_snwprintf_s(msg, _TRUNCATE,
			L"VSTスキャン 0 / %d\nScanning VST plug-ins… 0 / %d",
			g_scanTotal, g_scanTotal);
		SetWaitStatus(wait, msg);
	}
	for (int i = 0; i < count; ++i) ScanDir(roots[i], 0, wait);
	if (savedata.vstMultiDll[0]) {
		DWORD a = GetFileAttributesW(savedata.vstMultiDll);
		if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
			if (EqExt(savedata.vstMultiDll, L".vst3"))
				ProbeVst3Bundle(savedata.vstMultiDll);
			else
				ProbeVst2(savedata.vstMultiDll);
		} else if (DirExists(savedata.vstMultiDll)) {
			ScanDir(savedata.vstMultiDll, 0, wait);
		} else {
			wchar_t dir[VST_PATH_CHARS];
			SafeCopy(dir, VST_PATH_CHARS, savedata.vstMultiDll);
			wchar_t* slash = wcsrchr(dir, L'\\');
			if (slash) { *slash = 0; ScanDir(dir, 0, wait); }
		}
	}
	if (savedata.vstExtraPath[0]) {
		DWORD a = GetFileAttributesW(savedata.vstExtraPath);
		if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
			if (EqExt(savedata.vstExtraPath, L".vst3"))
				ProbeVst3Bundle(savedata.vstExtraPath);
			else
				ProbeVst2(savedata.vstExtraPath);
		} else if (EqExt(savedata.vstExtraPath, L".vst3") && DirExists(savedata.vstExtraPath)) {
			ProbeVst3Bundle(savedata.vstExtraPath);
		}
	}
	RescoreMultiFlags();
	if (wait) { DestroyWindow(wait); if (g_waitWnd == wait) g_waitWnd = NULL; }
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
	float z[BLOCK_FRAMES];
	float l[BLOCK_FRAMES], r[BLOCK_FRAMES];
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
		PumpSilent(NULL, vst3, 2);
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
	wchar_t pickDll[VST_PATH_CHARS];
	pickDll[0] = 0;
	const wchar_t* loadedPath = NULL;
	if (!PickGsXgDll(midPath, pickDll, VST_PATH_CHARS)) {
		if (!MapperOpen()) {
			LeaveCriticalSection(&g_eng.cs);
			return -5;
		}
		loaded = 1;
	} else {
		loaded = TryLoadPluginPath(pickDll, 0);
		if (loaded) loadedPath = pickDll;
	}
	if (loadedPath) {
		if (ContainsI(loadedPath, L"YXG") || ContainsI(loadedPath, L"S-YXG") ||
			ContainsI(loadedPath, L"SGP2") || ContainsI(loadedPath, L"SoftXG") ||
			(ContainsI(loadedPath, L"XG") && !ContainsI(loadedPath, L"SC")))
			resetMode = 2;
		else if (DetectMultiTimbralName(loadedPath) ||
			ContainsI(loadedPath, L"Canvas") || ContainsI(loadedPath, L"SC-") ||
			ContainsI(loadedPath, L"SCVA") || ContainsI(loadedPath, L"8820") ||
			ContainsI(loadedPath, L"SC88") || ContainsI(loadedPath, L"SGP"))
			resetMode = 1;
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
	LeaveCriticalSection(&g_eng.cs);
	return hasOut ? 0 : -5;
}

extern "C" void VstMidiLog(const wchar_t* msg)
{
	if (msg && msg[0]) EnsLog(L"%s", msg);
}

static int g_reportedLatencySamples = 0;

extern "C" void VstMidiClose(void)
{
	EnterCriticalSection(&g_eng.cs);
	FreeSong();
	LeaveCriticalSection(&g_eng.cs);
	g_reportedLatencySamples = 0;
}

extern "C" int VstMidiRead(BYTE* dst, int bytesWanted)
{
	if (!dst || bytesWanted <= 0) return 0;
	EnterCriticalSection(&g_eng.cs);
	if (!g_eng.events || g_eng.playSample >= g_eng.lengthSamples) {
		LeaveCriticalSection(&g_eng.cs); return 0;
	}
	int written = 0;
	while (written < bytesWanted) {
		if (!g_eng.ringCount) {
			if (g_eng.playSample >= g_eng.lengthSamples) break;
			int frames = BLOCK_FRAMES;
			if (g_eng.lengthSamples - g_eng.playSample < frames)
				frames = (int)(g_eng.lengthSamples - g_eng.playSample);
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

extern "C" int VstMidiSeekSamples(__int64 samplePos)
{
	EnterCriticalSection(&g_eng.cs);
	if (!g_eng.events) { LeaveCriticalSection(&g_eng.cs); return -1; }
	if (samplePos < 0) samplePos = 0;
	if (samplePos > g_eng.lengthSamples) samplePos = g_eng.lengthSamples;

	g_eng.ringRead = g_eng.ringCount = 0;

	// 常に先頭へ戻してから目標まで高速再生。途中の PC/CC/ピッチベンド/ノートオンが
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
		g_eng.vst3C || g_eng.useEnsemble || g_eng.useMapper) ? 1 : 0;
}
extern "C" int VstMidiGetRate(void) { return SAMPLE_RATE; }
extern "C" int VstMidiGetChannels(void) { return 2; }
extern "C" int VstMidiGetBits(void) { return 16; }
extern "C" __int64 VstMidiGetLengthSamples(void) { return g_eng.lengthSamples; }

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
	g_reportedLatencySamples = samples;
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
	return g_reportedLatencySamples;
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
	LiveRemoteCloseShm();
	g_kpiHost.VstLiveAudioStop();
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
	const uint32_t cap = s->capacity;
	const uint32_t need = (uint32_t)frames;
	const uint32_t want = g_liveShm.primed ? need : (uint32_t)LIVE_REMOTE_PREBUFFER;
	// Wave-out already holds queued blocks, so a short wait here costs nothing
	// audible while it keeps a slow first render from turning into a gap. Once
	// primed the wait stays well inside the queued time, so a dead host cannot
	// stall this thread.
	const int spins = g_liveShm.primed ? 20 : 300;
	for (int spin = 0; spin < spins; ++spin) {
		if (s->writePos - s->readPos >= want) break;
		if (g_liveShm.hWake) SetEvent(g_liveShm.hWake);
		Sleep(1);
	}
	uint32_t r = s->readPos;
	uint32_t avail = s->writePos - r;
	if (avail > cap) avail = 0; // producer restarted
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
		if (rc != 0) return rc;
		EnterCriticalSection(&g_eng.cs);
		LivePart& rp = g_eng.live[part1to32 - 1];
		rp.remote = 1;
		rp.isMulti = DetectMultiTimbralName(pluginPath) ? 1 : 0;
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
		// Preset-based instruments (HALion Sonic, Groove Agent, SampleTank)
		// come up with nothing loaded and stay silent no matter what you play,
		// so start them on their first program.
		if (ok && Vst3ProgramCount(p.vst3) > 0) { Vst3SetProgram(p.vst3, 0); p.prog = 0; }
	} else {
		ok = LoadVst2(pluginPath, p.module, p.effect);
	}
	if (ok) {
		p.isMulti = DetectMultiTimbralName(pluginPath) ? 1 : 0;
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
		struct EventBlock {
			VstInt32 numEvents;
			VstIntPtr reserved;
			VstEvent* events[LIVE_PEND_EVENTS];
		} block;
		VstMidiEvent me[LIVE_PEND_EVENTS];
		VstMidiSysexEvent sx[LIVE_PEND_SYSEX_EVENTS];
		block.reserved = 0;
		int n = 0, nsx = 0;
		for (int i = 0; i < q.count; ++i) {
			if (q.ev[i].sysexOff >= 0) {
				if (nsx >= LIVE_PEND_SYSEX_EVENTS) continue;
				VstMidiSysexEvent& s = sx[nsx];
				ZeroMemory(&s, sizeof(s));
				s.type = kVstSysExType;
				s.byteSize = sizeof(VstMidiSysexEvent);
				s.dumpBytes = (VstInt32)q.ev[i].sysexLen;
				s.sysexDump = (char*)(q.sysex + q.ev[i].sysexOff);
				block.events[n++] = (VstEvent*)&s;
				++nsx;
				continue;
			}
			VstMidiEvent& m = me[n];
			ZeroMemory(&m, sizeof(m));
			m.type = kVstMidiType;
			m.byteSize = sizeof(VstMidiEvent);
			m.midiData[0] = (char)(q.ev[i].msg & 0xff);
			m.midiData[1] = (char)((q.ev[i].msg >> 8) & 0x7f);
			m.midiData[2] = (char)((q.ev[i].msg >> 16) & 0x7f);
			block.events[n] = (VstEvent*)&m;
			++n;
		}
		block.numEvents = n;
		if (n) {
			__try { p.effect->dispatcher(p.effect, effProcessEvents, 0, 0, &block, 0); }
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
	VstLiveEditorClose(part1to32);
	EnterCriticalSection(&g_eng.cs);
	LivePanicPart(g_eng.live[part1to32 - 1]);
	LivePart& p = g_eng.live[part1to32 - 1];
	const int wasRemote = p.remote;
	CloseEffect(p.module, p.effect);
	Vst3Close(p.vst3); p.vst3 = NULL;
	p.isMulti = 0;
	p.remote = 0;
	LeaveCriticalSection(&g_eng.cs);
	if (wasRemote) LiveRemoteUnload(part1to32);
}

static int LivePartFreeForProbe(int part0)
{
	const LivePart& p = g_eng.live[part0];
	return (!p.effect && !p.vst3 && !p.remote && !p.module && !p.edWnd) ? 1 : 0;
}

// Same open/close the part grid uses on a drop. Failures leave isLiveOk at 0
// so the host palette never offers a plug-in that would bounce on drop.
extern "C" void VstScanVerifyLiveList(HWND parentForWait)
{
	int todo = 0;
	for (int i = 0; i < g_pluginCount; ++i)
		if (g_plugins[i].isInstrument && !g_plugins[i].isLiveOk) ++todo;
	if (!todo) return;

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
		if (wait) {
			wchar_t msg[384];
			_snwprintf_s(msg, _TRUNCATE,
				L"D&&D確認 %d / %d\nChecking droppable plug-ins %d / %d\n%s",
				done, todo, done, todo, p.name);
			SetWaitStatus(wait, msg);
		}
		if (IsFxNotInstrument(p.name, p.path)) {
			p.isInstrument = 0;
			changed = 1;
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
		if (VstLiveLoadPart(part, p.path, p.isVst3) == 0) {
			VstLiveUnloadPart(part);
			p.isLiveOk = 1;
			changed = 1;
		} else {
			p.isInstrument = 0;
			changed = 1;
		}
	}
	if (ownWait && wait) {
		DestroyWindow(wait);
		if (g_waitWnd == wait) g_waitWnd = NULL;
	}
	if (changed) SaveCache();
}


// Multi-timbral (SC-VA / SGP2 etc.): one instance receives all channels.
// Prefer the instance inside this port's 16-part block, so two multi instances
// on different blocks stay separated; otherwise fall back to the first one
// anywhere, as a single instance must hear every port.
static int LiveMultiPart(int portIndex0to2)
{
	const int blockStart = portIndex0to2 * 16;
	for (int i = blockStart; i < blockStart + 16 && i < 32; ++i)
		if (g_eng.live[i].isMulti && (g_eng.live[i].effect || g_eng.live[i].vst3))
			return i;
	for (int i = 0; i < 32; ++i)
		if (g_eng.live[i].isMulti && (g_eng.live[i].effect || g_eng.live[i].vst3))
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

extern "C" void VstLiveMidiShort(int portIndex0to2, DWORD shortMsg)
{
	if (portIndex0to2 < 0 || portIndex0to2 > 2) return;
	// Remote parts route inside KpiHost64, which holds the same part layout.
	LiveActTrack(portIndex0to2, shortMsg);
	LiveRemoteMidi(portIndex0to2, shortMsg);
	EnterCriticalSection(&g_eng.cs);
	int part = LiveMultiPart(portIndex0to2);
	if (part < 0) part = portIndex0to2 * 16 + (int)(shortMsg & 15);
	if (part < 32)
		LivePendPush(g_eng.live[part], LiveSendMsg(g_eng.live[part], shortMsg));
	LeaveCriticalSection(&g_eng.cs);
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
	if (part < 0) part = portIndex0to2 * 16;
	// Queued with the notes so a GS reset cannot wipe the events around it.
	if (part < 32) LivePendPushSysex(g_eng.live[part], data, bytes);
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
		if (m == WM_CLOSE) { VstLiveEditorClose(part); return 0; }
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
	LivePart& p = g_eng.live[part1to32 - 1];
#ifndef KPIHOST64_BUILD
	if (p.remote) {
		g_kpiHost.VstLiveEditorClose((uint32_t)part1to32);
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
	float tl[BLOCK_FRAMES], tr[BLOCK_FRAMES];
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
