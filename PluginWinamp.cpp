// Winamp input plugin host (In_Module + fake Out_Module ring → pull read)
//
// in2.h / out.h と Winamp 本体(In.cpp / InW.cpp)の実挙動に合わせた実装:
//  - version は IN_UNICODE / IN_INIT_RET を落として 0x100(IN_VER_OLD) / 0x101(IN_VER) のみ受理
//  - IN_UNICODE 付きは Play / IsOurFile / GetFileInfo が wchar_t*。ANSI 変換して渡すと開けない
//  - FileExtensions は "ext群\0説明\0ext群\0説明\0\0"。ext群は in_mod の "mod;s3m;..." のように ';' 連結
//  - UsesOutputPlug は flags 化されており bit0(IN_MODULE_FLAG_USES_OUTPUT_PLUGIN) が無いものは自前出力＝ホスト不可
//  - 曲終端はプラグインが hMainWindow へ WM_WA_MPEG_EOF(WM_USER+2) を Post して知らせる。
//    そのため本体ダイアログではなく専用のメッセージ専用ウィンドウ(自前ポンプ)を渡す。
//    本体 HWND を渡すとプラグインの IPC/サブクラス化が本体 UI に流れ込む。
#include "stdafx.h"
#include "PluginWinamp.h"
#include "PluginKinds.h"
#include "third_party/winamp/in2.h"
#include "KpiHostClient.h"

extern CString kpif[];
extern CString ext[][300];
extern BYTE kvar[][300];
extern BYTE kpiarch[];
extern BYTE plugkind[];
extern BOOL kpichk[];
extern int kpicnt;
extern KpiHost64Client g_kpiHost;

enum { WA_RING_BYTES = 2 * 1024 * 1024 };

// Winamp IPC (wa_ipc.h 相当。使うものだけ)
enum {
	WA_WM_IPC = WM_USER,
	WA_WM_MPEG_EOF = WM_USER + 2,
	WA_IPC_GETVERSION = 0,
	WA_IPC_ISPLAYING = 104,
	WA_IPC_GETOUTPUTTIME = 105,
	WA_IPC_GETLISTLENGTH = 124,
	WA_IPC_GETINIFILE = 334,
	WA_IPC_GETINIDIRECTORY = 335,
	WA_IPC_GETPLUGINDIRECTORY = 336,
	WA_IPC_GETINIFILEW = 1334,
	WA_IPC_GETINIDIRECTORYW = 1335,
	WA_IPC_GETPLUGINDIRECTORYW = 1336,
	WA_IPC_GET_API_SERVICE = 3025
};

static HMODULE g_waDll = NULL;
static In_Module* g_waIn = NULL;
static Out_Module g_waOut;
static CRITICAL_SECTION g_waCs;
static BOOL g_waCsInit = FALSE;
static BYTE g_waRing[WA_RING_BYTES];
static volatile LONG g_waRingR = 0;
static volatile LONG g_waRingW = 0;
static volatile LONG g_waUsed = 0;
static volatile LONG g_waPlaying = 0;
static volatile LONG g_waEof = 0;
static volatile LONG g_waStopping = 0;
static volatile LONG g_waFmtKnown = 0;
static int g_waRate = 44100;
static int g_waCh = 2;
static int g_waBps = 16;
static int g_waOpenOk = 0;
static int g_waRemote = 0;
static uint32_t g_waRemoteSid = 0;
static int g_waLengthMs = 0;
static int g_waPause = 0;
static int g_waVol = 255;
static int g_waPan = 0;
static int g_waFlushMs = 0;
static __int64 g_waWrittenBytes = 0;

// プラグインへ渡すメッセージ専用ウィンドウ（EOF 受信と最低限の IPC 応答）
static HWND g_waMsgWnd = NULL;
static HANDLE g_waMsgThread = NULL;
static HANDLE g_waMsgReady = NULL;
static char g_waIniFileA[MAX_PATH * 2] = { 0 };
static char g_waIniDirA[MAX_PATH * 2] = { 0 };
static wchar_t g_waIniFileW[MAX_PATH] = { 0 };
static wchar_t g_waIniDirW[MAX_PATH] = { 0 };

static void WaEnsureCs()
{
	if (!g_waCsInit) {
		InitializeCriticalSection(&g_waCs);
		g_waCsInit = TRUE;
	}
}

static void WaRingReset()
{
	WaEnsureCs();
	EnterCriticalSection(&g_waCs);
	g_waRingR = 0;
	g_waRingW = 0;
	g_waUsed = 0;
	g_waWrittenBytes = 0;
	LeaveCriticalSection(&g_waCs);
}

static int WaOutputTimeMs()
{
	if (g_waRate <= 0 || g_waCh <= 0 || g_waBps <= 0) return g_waFlushMs;
	const int bpf = (g_waBps / 8) * g_waCh;
	if (bpf <= 0) return g_waFlushMs;
	WaEnsureCs();
	EnterCriticalSection(&g_waCs);
	__int64 written = g_waWrittenBytes;
	int used = (int)g_waUsed;
	LeaveCriticalSection(&g_waCs);
	__int64 playedBytes = written - used;
	if (playedBytes < 0) playedBytes = 0;
	return g_waFlushMs + (int)((playedBytes * 1000) / ((__int64)g_waRate * bpf));
}

static LRESULT CALLBACK WaMsgWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WA_WM_MPEG_EOF) {
		InterlockedExchange(&g_waEof, 1);
		return 0;
	}
	if (msg == WA_WM_IPC) {
		switch (lParam) {
		case WA_IPC_GETVERSION:        return 0x5066; // Winamp 5.66 相当
		case WA_IPC_ISPLAYING:         return g_waOpenOk ? 1 : 0;
		case WA_IPC_GETOUTPUTTIME:     return (wParam == 1) ? (g_waLengthMs / 1000) : WaOutputTimeMs();
		case WA_IPC_GETLISTLENGTH:     return 1;
		// 設定パスを問い合わせるプラグインは戻り値を必ず参照する。NULL を返すと落ちる。
		case WA_IPC_GETINIFILE:        return (LRESULT)g_waIniFileA;
		case WA_IPC_GETINIDIRECTORY:   return (LRESULT)g_waIniDirA;
		case WA_IPC_GETPLUGINDIRECTORY:return (LRESULT)g_waIniDirA;
		case WA_IPC_GETINIFILEW:       return (LRESULT)g_waIniFileW;
		case WA_IPC_GETINIDIRECTORYW:  return (LRESULT)g_waIniDirW;
		case WA_IPC_GETPLUGINDIRECTORYW:return (LRESULT)g_waIniDirW;
		case WA_IPC_GET_API_SERVICE:   return 0;
		default:                       return 0;
		}
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static DWORD WINAPI WaMsgThreadProc(LPVOID)
{
	HINSTANCE hInst = AfxGetInstanceHandle();
	WNDCLASSEXW wc;
	ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = WaMsgWndProc;
	wc.hInstance = hInst;
	wc.lpszClassName = L"OggWinampPluginHost";
	RegisterClassExW(&wc);
	g_waMsgWnd = CreateWindowExW(0, L"OggWinampPluginHost", L"", 0, 0, 0, 0, 0,
		HWND_MESSAGE, NULL, hInst, NULL);
	SetEvent(g_waMsgReady);
	if (!g_waMsgWnd) return 0;
	MSG m;
	while (GetMessageW(&m, NULL, 0, 0) > 0) {
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	return 0;
}

// アプリ生存中は使い回す（プラグインが HWND を保持したまま破棄されるのを防ぐ）
static HWND WaEnsureMsgWindow()
{
	if (g_waMsgWnd) return g_waMsgWnd;
	if (g_waIniDirW[0] == 0) {
		GetModuleFileNameW(NULL, g_waIniDirW, MAX_PATH);
		wchar_t* sl = wcsrchr(g_waIniDirW, L'\\');
		if (sl) *sl = 0;
		_snwprintf_s(g_waIniFileW, MAX_PATH, _TRUNCATE, L"%s\\winamp.ini", g_waIniDirW);
		WideCharToMultiByte(CP_ACP, 0, g_waIniDirW, -1, g_waIniDirA, (int)sizeof(g_waIniDirA), NULL, NULL);
		WideCharToMultiByte(CP_ACP, 0, g_waIniFileW, -1, g_waIniFileA, (int)sizeof(g_waIniFileA), NULL, NULL);
	}
	if (!g_waMsgReady)
		g_waMsgReady = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!g_waMsgThread) {
		DWORD tid = 0;
		g_waMsgThread = CreateThread(NULL, 0, WaMsgThreadProc, NULL, 0, &tid);
	}
	if (g_waMsgReady)
		WaitForSingleObject(g_waMsgReady, 3000);
	return g_waMsgWnd;
}

static void __cdecl WaOut_Config(HWND) {}
static void __cdecl WaOut_About(HWND) {}
static void __cdecl WaOut_Init() {}
static void __cdecl WaOut_Quit() {}

static int __cdecl WaOut_Open(int samplerate, int numchannels, int bitspersamp, int, int)
{
	g_waRate = samplerate > 0 ? samplerate : 44100;
	g_waCh = numchannels > 0 ? numchannels : 2;
	g_waBps = bitspersamp > 0 ? bitspersamp : 16;
	WaRingReset();
	g_waPlaying = 1;
	g_waFlushMs = 0;
	InterlockedExchange(&g_waFmtKnown, 1);
	return 50;
}

static void __cdecl WaOut_Close()
{
	g_waPlaying = 0;
}

static int __cdecl WaOut_Write(char* buf, int len)
{
	// 生PCMをリングへそのまま格納。ここで g_waVol を掛けない。
	// 音量は本体 equaliser の「その他のkpi」(kpivol) と DS 主音量だけが担当する。
	if (!buf || len <= 0) return 0;
	WaEnsureCs();
	EnterCriticalSection(&g_waCs);
	int freeB = WA_RING_BYTES - (int)g_waUsed;
	if (len > freeB) {
		LeaveCriticalSection(&g_waCs);
		return 1;
	}
	int w = (int)g_waRingW;
	const int first = (len < WA_RING_BYTES - w) ? len : (WA_RING_BYTES - w);
	memcpy(g_waRing + w, buf, (size_t)first);
	if (len > first)
		memcpy(g_waRing, buf + first, (size_t)(len - first));
	w += len;
	if (w >= WA_RING_BYTES) w -= WA_RING_BYTES;
	g_waRingW = w;
	g_waUsed += len;
	g_waWrittenBytes += len;
	LeaveCriticalSection(&g_waCs);
	return 0;
}

static int __cdecl WaOut_CanWrite()
{
	WaEnsureCs();
	EnterCriticalSection(&g_waCs);
	int freeB = WA_RING_BYTES - (int)g_waUsed;
	LeaveCriticalSection(&g_waCs);
	return freeB;
}

static int __cdecl WaOut_IsPlaying()
{
	WaEnsureCs();
	EnterCriticalSection(&g_waCs);
	int u = (int)g_waUsed;
	LeaveCriticalSection(&g_waCs);
	return (g_waPlaying && u > 0) ? 1 : 0;
}

static int __cdecl WaOut_Pause(int pause)
{
	int prev = g_waPause;
	g_waPause = pause ? 1 : 0;
	return prev;
}

static void __cdecl WaOut_SetVolume(int volume) { g_waVol = volume; }
static void __cdecl WaOut_SetPan(int pan) { g_waPan = pan; }

static void __cdecl WaOut_Flush(int t)
{
	WaRingReset();
	g_waFlushMs = t;
	InterlockedExchange(&g_waEof, 0);
}

static int __cdecl WaOut_GetOutputTime() { return WaOutputTimeMs(); }

static int __cdecl WaOut_GetWrittenTime()
{
	if (g_waRate <= 0 || g_waCh <= 0 || g_waBps <= 0) return 0;
	const int bpf = (g_waBps / 8) * g_waCh;
	if (bpf <= 0) return 0;
	return g_waFlushMs + (int)((g_waWrittenBytes * 1000) / ((__int64)g_waRate * bpf));
}

static void __cdecl WaVis_SAVSAInit(int, int) {}
static void __cdecl WaVis_SAVSADeInit() {}
static void __cdecl WaVis_SAAddPCMData(void*, int, int, int) {}
static int __cdecl WaVis_SAGetMode() { return 0; }
static int __cdecl WaVis_SAAdd(void*, int, int) { return 0; }
static void __cdecl WaVis_VSAAddPCMData(void*, int, int, int) {}
static int __cdecl WaVis_VSAGetMode(int*, int*) { return 0; }
static int __cdecl WaVis_VSAAdd(void*, int) { return 0; }
static void __cdecl WaVis_VSASetInfo(int, int) {}
static int __cdecl WaVis_dsp_isactive() { return 0; }
static int __cdecl WaVis_dsp_dosamples(short int* samples, int numsamples, int, int, int) { return numsamples; }
static void __cdecl WaVis_EQSet(int, char[10], int) {}
static void __cdecl WaVis_SetInfo(int, int, int, int) {}

static void WaFillOutModule(HWND hwndMain)
{
	ZeroMemory(&g_waOut, sizeof(g_waOut));
	g_waOut.version = OUT_VER;
	g_waOut.description = (char*)"ogg host out";
	g_waOut.id = 65536;
	g_waOut.hMainWindow = hwndMain;
	g_waOut.hDllInstance = NULL;
	g_waOut.Config = WaOut_Config;
	g_waOut.About = WaOut_About;
	g_waOut.Init = WaOut_Init;
	g_waOut.Quit = WaOut_Quit;
	g_waOut.Open = WaOut_Open;
	g_waOut.Close = WaOut_Close;
	g_waOut.Write = WaOut_Write;
	g_waOut.CanWrite = WaOut_CanWrite;
	g_waOut.IsPlaying = WaOut_IsPlaying;
	g_waOut.Pause = WaOut_Pause;
	g_waOut.SetVolume = WaOut_SetVolume;
	g_waOut.SetPan = WaOut_SetPan;
	g_waOut.Flush = WaOut_Flush;
	g_waOut.GetOutputTime = WaOut_GetOutputTime;
	g_waOut.GetWrittenTime = WaOut_GetWrittenTime;
}

static void WaBindInHost(In_Module* in, HWND hwndMain, HMODULE hDll)
{
	if (!in) return;
	in->hMainWindow = hwndMain;
	in->hDllInstance = hDll;
	in->outMod = &g_waOut;
	in->SAVSAInit = WaVis_SAVSAInit;
	in->SAVSADeInit = WaVis_SAVSADeInit;
	in->SAAddPCMData = WaVis_SAAddPCMData;
	in->SAGetMode = WaVis_SAGetMode;
	in->SAAdd = WaVis_SAAdd;
	in->VSAAddPCMData = WaVis_VSAAddPCMData;
	in->VSAGetMode = WaVis_VSAGetMode;
	in->VSAAdd = WaVis_VSAAdd;
	in->VSASetInfo = WaVis_VSASetInfo;
	in->dsp_isactive = WaVis_dsp_isactive;
	in->dsp_dosamples = WaVis_dsp_dosamples;
	in->EQSet = WaVis_EQSet;
	in->SetInfo = WaVis_SetInfo;
	// EQ / ReplayGain はホスト側で処理しないのでプラグイン側フラグを落とさない
}

// version から IN_UNICODE / IN_INIT_RET を除いた素の型番が 0x100 / 0x101 か
static int WaVersionOk(const In_Module* in)
{
	if (!in) return 0;
	const int ver = (in->version & ~IN_UNICODE) & ~IN_INIT_RET;
	return (ver == IN_VER_OLD || ver == IN_VER) ? 1 : 0;
}

static int WaIsUnicode(const In_Module* in)
{
	return (in && (in->version & IN_UNICODE)) ? 1 : 0;
}

// bit0 が無いプラグインは出力プラグインを使わない＝リングに PCM が来ない
static int WaUsesOutput(const In_Module* in)
{
	if (!in) return 0;
	return (in->UsesOutputPlug & IN_MODULE_FLAG_USES_OUTPUT_PLUGIN) ? 1 : 0;
}

static int WaCallPlay(In_Module* in, const wchar_t* mediaPath)
{
	if (!in || !in->Play) return 1;
	if (WaIsUnicode(in))
		return in->Play((const in_char*)mediaPath);
	char pathA[MAX_PATH * 2];
	pathA[0] = 0;
	WideCharToMultiByte(CP_ACP, 0, mediaPath, -1, pathA, (int)sizeof(pathA), NULL, NULL);
	return in->Play((const in_char*)pathA);
}

static void WaAddExt(int& ei, const CStringA& tokA)
{
	if (ei >= 298) return;
	CStringA t = tokA;
	t.Trim();
	if (t.IsEmpty()) return;
	CString e(t);
	e.MakeLower();
	// "*.mp3" / ".mp3" / "mp3" をすべて ".mp3" に正規化
	int st = 0;
	while (st < e.GetLength() && (e[st] == L'*' || e[st] == L'.')) st++;
	e = e.Mid(st);
	if (e.IsEmpty()) return;
	e = L"." + e;
	for (int k = 0; k < ei; k++) {
		if (ext[kpicnt][k] == e) return;
	}
	ext[kpicnt][ei] = e;
	kvar[kpicnt][ei] = 0;
	ei++;
}

// "ext群\0説明\0ext群\0説明\0\0"。ext群は ';' 連結（例 in_mod の "mod;s3m;xm;it;..."）
static void WaParseExts(const char* fileExts)
{
	ext[kpicnt][0] = L"";
	ext[kpicnt][299] = L"";
	if (!fileExts) return;
	int ei = 0;
	const char* p = fileExts;
	while (*p && ei < 298) {
		CStringA group(p);
		p += group.GetLength() + 1;
		if (*p) p += (int)strlen(p) + 1; // 説明を読み飛ばす（無いまま終端でも群は採用する）
		int start = 0;
		for (;;) {
			int sc = group.Find(';', start);
			WaAddExt(ei, (sc < 0) ? group.Mid(start) : group.Mid(start, sc - start));
			if (sc < 0) break;
			start = sc + 1;
		}
	}
	ext[kpicnt][ei] = L"";
}

typedef In_Module* (__cdecl* pfn_winampGetInModule2)();

int PluginWinamp_TryEnum(const wchar_t* dllPath, int is64)
{
	if (!dllPath || !dllPath[0] || kpicnt >= 149) return 0;
	if (is64) {
		plugkind[kpicnt] = PLUGKIND_WINAMP;
		kpiarch[kpicnt] = 64;
		kpif[kpicnt] = dllPath;
		ext[kpicnt][0] = L"";
		ext[kpicnt][299] = L"";
		kvar[kpicnt][0] = 0;
		std::wstring exts;
		if (g_kpiHost.ForeignListExts(PLUGKIND_WINAMP, dllPath, exts) && !exts.empty()) {
			CString cs(exts.c_str());
			int ei = 0, start = 0;
			for (;;) {
				int slash = cs.Find(L'/', start);
				CString tok = (slash < 0) ? cs.Mid(start) : cs.Mid(start, slash - start);
				tok.MakeLower();
				if (!tok.IsEmpty() && ei < 298) {
					if (tok[0] != L'.') tok = L"." + tok;
					ext[kpicnt][ei] = tok;
					kvar[kpicnt][ei] = 0;
					ei++;
				}
				if (slash < 0) break;
				start = slash + 1;
			}
			ext[kpicnt][ei] = L"";
		}
		if (ext[kpicnt][0] == L"") return 0; // 拡張子不明では一生マッチしないので載せない
		kpichk[kpicnt] = TRUE;
		kpicnt++;
		return 1;
	}
	HMODULE h = LoadLibraryExW(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) return 0;
	pfn_winampGetInModule2 getIn = (pfn_winampGetInModule2)GetProcAddress(h, "winampGetInModule2");
	if (!getIn) {
		FreeLibrary(h);
		return 0;
	}
	int ok = 0;
	try {
		In_Module* in = getIn();
		if (in && WaVersionOk(in) && WaUsesOutput(in)) {
			WaEnsureCs();
			WaFillOutModule(WaEnsureMsgWindow());
			WaBindInHost(in, g_waMsgWnd, h);
			if (in->Init) in->Init();
			plugkind[kpicnt] = PLUGKIND_WINAMP;
			kpiarch[kpicnt] = 32;
			kpif[kpicnt] = dllPath;
			WaParseExts(in->FileExtensions);
			if (in->Quit) in->Quit();
			// 拡張子を1つも公開しないプラグインは plugswinamp で永久にマッチしない
			if (ext[kpicnt][0] != L"") {
				kpichk[kpicnt] = TRUE;
				kpicnt++;
				ok = 1;
			}
		}
	}
	catch (...) {
		ok = 0;
	}
	FreeLibrary(h);
	return ok;
}

int PluginWinamp_Open(const wchar_t* dllPath, const wchar_t* mediaPath, HWND /*hwndMain*/)
{
	PluginWinamp_Close();
	WaEnsureCs();
	HWND host = WaEnsureMsgWindow();
	HMODULE h = LoadLibraryExW(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) return 0;
	pfn_winampGetInModule2 getIn = (pfn_winampGetInModule2)GetProcAddress(h, "winampGetInModule2");
	if (!getIn) {
		FreeLibrary(h);
		return 0;
	}
	In_Module* in = getIn();
	if (!in || !in->Play || !WaVersionOk(in) || !WaUsesOutput(in)) {
		FreeLibrary(h);
		return 0;
	}
	WaFillOutModule(host);
	WaBindInHost(in, host, h);
	if (in->Init) in->Init();
	g_waDll = h;
	g_waIn = in;
	if (g_waOut.Init) g_waOut.Init();
	WaRingReset();
	g_waOpenOk = 0;
	g_waRemote = 0;
	g_waLengthMs = 0;
	InterlockedExchange(&g_waEof, 0);
	InterlockedExchange(&g_waStopping, 0);
	InterlockedExchange(&g_waFmtKnown, 0);
	if (WaCallPlay(in, mediaPath) != 0) {
		PluginWinamp_Close();
		return 0;
	}
	// フォーマットはデコードスレッドが outMod->Open() を呼んだ時点で確定する。
	// Play() 直後に読むと既定値(44100/2/16)のままになるので確定まで待つ。
	for (int i = 0; i < 5000; i++) {
		if (InterlockedCompareExchange(&g_waFmtKnown, 0, 0)) break;
		if (InterlockedCompareExchange(&g_waEof, 0, 0)) break;
		Sleep(1);
	}
	if (!InterlockedCompareExchange(&g_waFmtKnown, 0, 0)) {
		PluginWinamp_Close();
		return 0;
	}
	g_waOpenOk = 1;
	if (in->SetVolume) in->SetVolume(255);
	if (in->GetLength) g_waLengthMs = in->GetLength();
	return 1;
}

int PluginWinamp_OpenRemote(const wchar_t* dllPath, const wchar_t* mediaPath)
{
	PluginWinamp_Close();
	KPIHOST64_ForeignOpenReply fr{};
	if (!g_kpiHost.ForeignOpen(PLUGKIND_WINAMP, dllPath, mediaPath, fr))
		return 0;
	g_waRemote = 1;
	g_waRemoteSid = fr.sessionId;
	g_waRate = fr.sampleRate > 0 ? (int)fr.sampleRate : 44100;
	g_waCh = fr.channels > 0 ? (int)fr.channels : 2;
	g_waBps = fr.bitsPerSample > 0 ? fr.bitsPerSample : 16;
	g_waLengthMs = (fr.lengthSamples > 0 && g_waRate > 0) ? (int)(fr.lengthSamples * 1000 / g_waRate) : 0;
	g_waOpenOk = 1;
	return 1;
}

void PluginWinamp_Close()
{
	InterlockedExchange(&g_waStopping, 1);
	if (g_waRemote) {
		if (g_waRemoteSid)
			g_kpiHost.ForeignClose(g_waRemoteSid);
		g_waRemote = 0;
		g_waRemoteSid = 0;
		g_waOpenOk = 0;
		return;
	}
	if (g_waIn) {
		In_Module* in = g_waIn;
		g_waIn = NULL;
		try {
			if (in->Stop) in->Stop();
			if (in->Quit) in->Quit();
		}
		catch (...) {}
	}
	if (g_waOut.Close) g_waOut.Close(); // 初回 Open 前は out モジュール未構築
	if (g_waDll) {
		FreeLibrary(g_waDll);
		g_waDll = NULL;
	}
	g_waOpenOk = 0;
	InterlockedExchange(&g_waFmtKnown, 0);
	InterlockedExchange(&g_waEof, 0);
	WaRingReset();
}

int PluginWinamp_SeekMs(int timeMs)
{
	if (!g_waOpenOk) return 0;
	if (g_waRemote) {
		if (g_waRate <= 0) return 0;
		uint64_t samp = (uint64_t)timeMs * (uint64_t)g_waRate / 1000;
		return g_kpiHost.ForeignSeek(g_waRemoteSid, samp) ? 1 : 0;
	}
	if (!g_waIn || !g_waIn->SetOutputTime) return 0;
	InterlockedExchange(&g_waEof, 0);
	g_waIn->SetOutputTime(timeMs);
	return 1;
}

int PluginWinamp_IsOpen() { return g_waOpenOk; }
int PluginWinamp_SampleRate() { return g_waRate; }
int PluginWinamp_Channels() { return g_waCh; }
int PluginWinamp_Bits() { return g_waBps; }
int PluginWinamp_LengthMs()
{
	if (g_waRemote) return g_waLengthMs;
	if (g_waIn && g_waIn->GetLength) return g_waIn->GetLength();
	return g_waLengthMs;
}

int PluginWinamp_Read(BYTE* dst, int bytesWanted)
{
	if (!dst || bytesWanted <= 0 || !g_waOpenOk) return 0;
	if (g_waRemote) {
		std::vector<uint8_t> pcm;
		bool eof = false;
		if (!g_kpiHost.ForeignRender(g_waRemoteSid, (uint32_t)bytesWanted, pcm, eof))
			return 0;
		int n = (int)pcm.size();
		if (n > bytesWanted) n = bytesWanted;
		if (n > 0) memcpy(dst, pcm.data(), n);
		return n;
	}
	WaEnsureCs();
	int got = 0;
	DWORD tIdle = GetTickCount();
	while (got < bytesWanted) {
		if (InterlockedCompareExchange(&g_waStopping, 0, 0)) break;
		EnterCriticalSection(&g_waCs);
		int take = bytesWanted - got;
		if (take > (int)g_waUsed) take = (int)g_waUsed;
		if (take > 0) {
			int r = (int)g_waRingR;
			const int first = (take < WA_RING_BYTES - r) ? take : (WA_RING_BYTES - r);
			memcpy(dst + got, g_waRing + r, (size_t)first);
			if (take > first)
				memcpy(dst + got + first, g_waRing, (size_t)(take - first));
			r += take;
			if (r >= WA_RING_BYTES) r -= WA_RING_BYTES;
			g_waRingR = r;
			g_waUsed -= take;
		}
		LeaveCriticalSection(&g_waCs);
		if (take > 0) {
			got += take;
			tIdle = GetTickCount();
			continue;
		}
		// リングが空。EOF 通知済み／出力クローズ済みなら本当に終端
		if (InterlockedCompareExchange(&g_waEof, 0, 0)) break;
		if (!g_waPlaying) break;
		if (GetTickCount() - tIdle > 5000) break; // デコーダ無応答の保険
		Sleep(1);
	}
	return got;
}

int readwinamp(BYTE* bw, int cnt)
{
	return PluginWinamp_Read(bw, cnt);
}
