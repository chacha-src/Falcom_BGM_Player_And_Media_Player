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

#pragma comment(lib, "winmm.lib")
static int ProbeLoadedEffectAudible(AEffect* effect);
static int ProbeLoadedVst3Audible(Vst3Inst* vst3, int drums);

#pragma comment(lib, "advapi32.lib")

namespace {

enum { SAMPLE_RATE = 44100, BLOCK_FRAMES = 512, MAX_MIDI_EVENTS = 500000 };
enum { CACHE_MAGIC = 0x43545356, CACHE_VERSION = 5 };

struct MidiItem {
	unsigned __int64 tick;
	__int64 sample;
	DWORD msg;
	DWORD aux; // tempo usec/qn for status 0xff
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

struct LivePart {
	HMODULE module;
	AEffect* effect;
	Vst3Inst* vst3;
	int isMulti;
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

	EngineState() : csReady(0), fileData(NULL), fileBytes(0), events(NULL),
		eventCount(0), eventPos(0), playSample(0), lengthSamples(0),
		module(NULL), effect(NULL), vst3(NULL), usingBuiltin(1),
		useEnsemble(0), useDrums(0), useMapper(0), midiOut(NULL), mixCount(0),
		ringRead(0), ringCount(0)
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

static int MidiStatusRank(DWORD msg)
{
	const BYTE st = (BYTE)(msg & 0xff);
	if (st == 0xff) return 0;
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
	if (memcmp(data, "MThd", 4) || ReadBE(data + 4, 4) < 6) {
		delete[] data; return -4;
	}
	const int tracks = (int)ReadBE(data + 10, 2);
	const int division = (int)ReadBE(data + 12, 2);
	if (division <= 0 || (division & 0x8000)) { delete[] data; return -5; }
	MidiItem* ev = new MidiItem[MAX_MIDI_EVENTS];
	int count = 0;
	const BYTE* p = data + 8 + ReadBE(data + 4, 4);
	const BYTE* fileEnd = data + size;
	for (int tr = 0; tr < tracks && p + 8 <= fileEnd; ++tr) {
		if (memcmp(p, "MTrk", 4)) break;
		DWORD len = ReadBE(p + 4, 4);
		const BYTE* q = p + 8;
		const BYTE* end = q + len <= fileEnd ? q + len : fileEnd;
		unsigned __int64 tick = 0;
		BYTE running = 0;
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
					++count;
				}
				q += ml;
			} else if (st == 0xf0 || st == 0xf7) {
				unsigned sl = 0;
				if (!ReadVar(q, end, sl) || q + sl > end) break;
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
					++count;
				}
			}
		}
		p = end;
	}
	if (!count) { delete[] ev; delete[] data; return -6; }
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
	g_eng.events = ev;
	g_eng.eventCount = count;
	g_eng.lengthSamples = sample + SAMPLE_RATE * 2;
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

static void DispatchDueEvents(__int64 start, int frames, AEffect* effect,
	Vst3Inst* vst3)
{
	MidiItem batch[256];
	int n = 0;
	const __int64 end = start + frames;
	auto flush = [&]() {
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
	};
	while (g_eng.eventPos < g_eng.eventCount &&
		g_eng.events[g_eng.eventPos].sample < end) {
		const MidiItem& e = g_eng.events[g_eng.eventPos++];
		if ((e.msg & 0xff) == 0xff) continue;
		if (g_eng.midiOut)
			midiOutShortMsg(g_eng.midiOut, e.msg);
		if (!effect && !vst3) continue;
		if (n >= 256) flush();
		batch[n++] = e;
	}
	flush();
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
	while (g_eng.eventPos < g_eng.eventCount &&
		g_eng.events[g_eng.eventPos].sample < end) {
		const MidiItem& e = g_eng.events[g_eng.eventPos++];
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
	BYTE tmp[64];
	if (bytes > sizeof(tmp)) return;
	memcpy(tmp, data, bytes);
	MIDIHDR hdr = {};
	hdr.lpData = (LPSTR)tmp;
	hdr.dwBufferLength = bytes;
	if (midiOutPrepareHeader(h, &hdr, sizeof(hdr)) != MMSYSERR_NOERROR) return;
	midiOutLongMsg(h, &hdr, sizeof(hdr));
	for (int i = 0; i < 50 && !(hdr.dwFlags & MHDR_DONE); ++i)
		Sleep(1);
	midiOutUnprepareHeader(h, &hdr, sizeof(hdr));
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
	CloseMixSlots();
	delete[] g_eng.events; g_eng.events = NULL;
	delete[] g_eng.fileData; g_eng.fileData = NULL;
	g_eng.fileBytes = 0; g_eng.eventCount = g_eng.eventPos = 0;
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
	if (g_eng.effect) {
		MidiItem alloff[16] = {};
		for (int ch = 0; ch < 16; ++ch) {
			alloff[ch].msg = (0xb0 | ch) | (123 << 8);
			alloff[ch].sample = 0;
		}
		SendVstEvents(g_eng.effect, alloff, 16, 0);
	}
	if (g_eng.vst3) {
		for (int ch = 0; ch < 16; ++ch)
			Vst3MidiShort(g_eng.vst3, (0xb0 | ch) | (123 << 8), 0);
	}
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

extern "C" int VstPickPreferredPlugin(wchar_t* outPath, int outChars)
{
	if (!outPath || outChars <= 0) return 0;
	outPath[0] = 0;
	if (!savedata.vstMultiDll[0]) return 0;
	DWORD a = GetFileAttributesW(savedata.vstMultiDll);
	if (a == INVALID_FILE_ATTRIBUTES) return 0;
	const int isDir = (a & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
	if (isDir && !EqExt(savedata.vstMultiDll, L".vst3")) return 0;
	SafeCopy(outPath, outChars, savedata.vstMultiDll);
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
	int want = PeArch(savedata.vstMultiDll);
	if (!want) want = HostArch();
	if (ResolveRolandScVaPath(savedata.vstMultiDll, resolved, VST_PATH_CHARS, want))
		SafeCopy(outPath, outChars, resolved);
	int arch = PeArch(outPath);
	return arch ? arch : want;
}

extern "C" int VstShouldOpenRemote64(wchar_t* outDll, int outChars)
{
	if (!outDll || outChars <= 0) return 0;
	outDll[0] = 0;
	if (!savedata.vstMultiDll[0]) return 0;
	wchar_t multi[VST_PATH_CHARS]; multi[0] = 0;
	const int march = VstPickPreferredPlugin(multi, VST_PATH_CHARS);
	if (march == 64 && multi[0]) {
		SafeCopy(outDll, outChars, multi);
		return 1;
	}
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
	return EqExt(path, L".mid") || EqExt(path, L".midi") || EqExt(path, L".kar");
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
	const wchar_t* loadedPath = NULL;
	if (!savedata.vstMultiDll[0]) {
		if (!MapperOpen()) {
			LeaveCriticalSection(&g_eng.cs);
			return -5;
		}
		loaded = 1;
	} else {
		loaded = TryLoadPluginPath(savedata.vstMultiDll, 0);
		if (loaded) loadedPath = savedata.vstMultiDll;
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
		SendGmGsReset(g_eng.effect, g_eng.vst3, resetMode);
	}
	const int hasOut = (g_eng.useMapper || g_eng.effect || g_eng.vst3) ? 1 : 0;
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

extern "C" void VstMidiClose(void)
{
	EnterCriticalSection(&g_eng.cs);
	FreeSong();
	LeaveCriticalSection(&g_eng.cs);
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
				DispatchDueEvents(g_eng.playSample, frames, g_eng.effect, g_eng.vst3);
				if (g_eng.vst3)
					Vst3Process(g_eng.vst3, g_eng.outL, g_eng.outR, frames);
				else if (g_eng.effect)
					RenderEffect(g_eng.effect, g_eng.outL, g_eng.outR, frames);
				else {
					ZeroMemory(g_eng.outL, frames * sizeof(float));
					ZeroMemory(g_eng.outR, frames * sizeof(float));
				}
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

extern "C" int VstMidiSeekSamples(__int64 samplePos)
{
	EnterCriticalSection(&g_eng.cs);
	if (!g_eng.events) { LeaveCriticalSection(&g_eng.cs); return -1; }
	if (samplePos < 0) samplePos = 0;
	if (samplePos > g_eng.lengthSamples) samplePos = g_eng.lengthSamples;
	ResetSequence();
	// Replay bank/program/controller state and synth note state up to target.
	while (g_eng.eventPos < g_eng.eventCount &&
		g_eng.events[g_eng.eventPos].sample < samplePos) {
		MidiItem& e = g_eng.events[g_eng.eventPos++];
		BYTE type = (BYTE)(e.msg & 0xf0);
		if ((e.msg & 0xff) != 0xff) {
			if (g_eng.useEnsemble) {
				const int ch = (int)(e.msg & 15);
				const int slot = g_eng.chSlot[ch];
				if (slot < 0) continue;
				else if (type == 0xb0 || type == 0xc0) {
					MidiItem m = e;
					if (!g_eng.mix[slot].keepMidiCh) {
						if (type == 0xc0) continue;
						m.msg = (m.msg & ~0x0fu) | 0u;
					}
					if (g_eng.mix[slot].effect) SendVstEvents(g_eng.mix[slot].effect, &m, 1, samplePos);
					if (g_eng.mix[slot].vst3) Vst3MidiShort(g_eng.mix[slot].vst3, m.msg, 0);
				}
			} else {
				if (g_eng.effect && (type == 0xb0 || type == 0xc0))
					SendVstEvents(g_eng.effect, &e, 1, samplePos);
				if (g_eng.vst3 && (type == 0xb0 || type == 0xc0))
					Vst3MidiShort(g_eng.vst3, e.msg, 0);
				if (g_eng.midiOut && (type == 0xb0 || type == 0xc0))
					midiOutShortMsg(g_eng.midiOut, e.msg);
			}
		}
	}
	g_eng.playSample = samplePos;
	LeaveCriticalSection(&g_eng.cs);
	return 0;
}

extern "C" int VstMidiHasPluginAudio(void)
{
	return (g_eng.effect || g_eng.vst3 || g_eng.useEnsemble || g_eng.useMapper) ? 1 : 0;
}
extern "C" int VstMidiGetRate(void) { return SAMPLE_RATE; }
extern "C" int VstMidiGetChannels(void) { return 2; }
extern "C" int VstMidiGetBits(void) { return 16; }
extern "C" __int64 VstMidiGetLengthSamples(void) { return g_eng.lengthSamples; }

extern "C" int VstLiveLoadPart(int part1to32,
	const wchar_t* pluginPath, int isVst3)
{
	if (part1to32 < 1 || part1to32 > 32 || !pluginPath) return -1;
	EnterCriticalSection(&g_eng.cs);
	LivePart& p = g_eng.live[part1to32 - 1];
	CloseEffect(p.module, p.effect);
	Vst3Close(p.vst3); p.vst3 = NULL;
	p.isMulti = 0;
	int ok = 0;
	if (isVst3) {
		p.vst3 = Vst3Open(pluginPath);
		ok = Vst3IsOk(p.vst3);
		if (!ok) { Vst3Close(p.vst3); p.vst3 = NULL; }
	} else {
		ok = LoadVst2(pluginPath, p.module, p.effect);
	}
	if (ok) p.isMulti = DetectMultiTimbralName(pluginPath) ? 1 : 0;
	LeaveCriticalSection(&g_eng.cs);
	return ok ? 0 : -2;
}

extern "C" void VstLiveUnloadPart(int part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return;
	EnterCriticalSection(&g_eng.cs);
	LivePart& p = g_eng.live[part1to32 - 1];
	CloseEffect(p.module, p.effect);
	Vst3Close(p.vst3); p.vst3 = NULL;
	p.isMulti = 0;
	LeaveCriticalSection(&g_eng.cs);
}

extern "C" void VstLiveMidiShort(int portIndex0to2, DWORD shortMsg)
{
	if (portIndex0to2 < 0 || portIndex0to2 > 2) return;
	EnterCriticalSection(&g_eng.cs);
	MidiItem e = {};
	e.msg = shortMsg;
	// Multi-timbral (SC-VA / SGP2 etc.): one instance receives all channels.
	int multi = -1;
	for (int i = 0; i < 32; ++i)
		if (g_eng.live[i].isMulti && (g_eng.live[i].effect || g_eng.live[i].vst3)) {
			multi = i; break;
		}
	if (multi >= 0) {
		SendVstEvents(g_eng.live[multi].effect, &e, 1, 0);
		Vst3MidiShort(g_eng.live[multi].vst3, shortMsg, 0);
		LeaveCriticalSection(&g_eng.cs);
		return;
	}
	const int ch = shortMsg & 15;
	const int part = portIndex0to2 * 16 + ch;
	if (part < 32) {
		SendVstEvents(g_eng.live[part].effect, &e, 1, 0);
		Vst3MidiShort(g_eng.live[part].vst3, shortMsg, 0);
	}
	LeaveCriticalSection(&g_eng.cs);
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
	return frames;
}
