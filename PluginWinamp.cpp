// Winamp input plugin host (In_Module + fake Out_Module ring → pull read)
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

static void WaEnsureCs()
{
	if (!g_waCsInit) {
		InitializeCriticalSection(&g_waCs);
		g_waCsInit = TRUE;
	}
}

static void WaRingReset()
{
	EnterCriticalSection(&g_waCs);
	g_waRingR = 0;
	g_waRingW = 0;
	g_waUsed = 0;
	g_waWrittenBytes = 0;
	LeaveCriticalSection(&g_waCs);
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
	return 50;
}

static void __cdecl WaOut_Close()
{
	g_waPlaying = 0;
}

static int __cdecl WaOut_Write(char* buf, int len)
{
	if (!buf || len <= 0) return 0;
	WaEnsureCs();
	EnterCriticalSection(&g_waCs);
	int freeB = WA_RING_BYTES - (int)g_waUsed;
	if (len > freeB) {
		LeaveCriticalSection(&g_waCs);
		return 1;
	}
	int w = (int)g_waRingW;
	for (int i = 0; i < len; ++i) {
		g_waRing[w] = (BYTE)buf[i];
		w++;
		if (w >= WA_RING_BYTES) w = 0;
	}
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
	return (g_waPlaying || u > 0) ? 1 : 0;
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
}

static int __cdecl WaOut_GetOutputTime()
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
	return g_waFlushMs + (int)((playedBytes * 1000) / (g_waRate * bpf));
}

static int __cdecl WaOut_GetWrittenTime()
{
	if (g_waRate <= 0 || g_waCh <= 0 || g_waBps <= 0) return 0;
	const int bpf = (g_waBps / 8) * g_waCh;
	if (bpf <= 0) return 0;
	return (int)((g_waWrittenBytes * 1000) / (g_waRate * bpf));
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
}

static void WaParseExts(const char* fileExts)
{
	ext[kpicnt][0] = L"";
	if (!fileExts) return;
	int ei = 0;
	const char* p = fileExts;
	while (*p && ei < 298) {
		CStringA extA(p);
		p += extA.GetLength() + 1;
		if (*p) p += (int)strlen(p) + 1;
		else break;
		if (extA.IsEmpty()) continue;
		CString e(extA);
		e.MakeLower();
		if (e[0] != L'.')
			e = L"." + e;
		ext[kpicnt][ei] = e;
		kvar[kpicnt][ei] = 0;
		ei++;
	}
	ext[kpicnt][ei] = L"";
	ext[kpicnt][299] = L"";
}

static void WaPathToAnsi(const wchar_t* w, char* out, int outc)
{
	if (!out || outc <= 0) return;
	out[0] = 0;
	if (!w) return;
	WideCharToMultiByte(CP_ACP, 0, w, -1, out, outc, NULL, NULL);
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
	In_Module* in = getIn();
	if (!in) {
		FreeLibrary(h);
		return 0;
	}
	WaEnsureCs();
	WaFillOutModule(NULL);
	WaBindInHost(in, NULL, h);
	if (in->Init) in->Init();
	plugkind[kpicnt] = PLUGKIND_WINAMP;
	kpiarch[kpicnt] = 32;
	kpif[kpicnt] = dllPath;
	WaParseExts(in->FileExtensions);
	if (in->Quit) in->Quit();
	FreeLibrary(h);
	kpicnt++;
	return 1;
}

int PluginWinamp_Open(const wchar_t* dllPath, const wchar_t* mediaPath, HWND hwndMain)
{
	PluginWinamp_Close();
	WaEnsureCs();
	HMODULE h = LoadLibraryExW(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) return 0;
	pfn_winampGetInModule2 getIn = (pfn_winampGetInModule2)GetProcAddress(h, "winampGetInModule2");
	if (!getIn) {
		FreeLibrary(h);
		return 0;
	}
	In_Module* in = getIn();
	if (!in) {
		FreeLibrary(h);
		return 0;
	}
	WaFillOutModule(hwndMain);
	WaBindInHost(in, hwndMain, h);
	if (in->Init) in->Init();
	g_waDll = h;
	g_waIn = in;
	g_waOut.Init();
	WaRingReset();
	g_waOpenOk = 0;
	g_waRemote = 0;
	if (!in->Play) {
		PluginWinamp_Close();
		return 0;
	}
	char pathA[MAX_PATH * 2];
	WaPathToAnsi(mediaPath, pathA, (int)sizeof(pathA));
	int pr = in->Play(pathA);
	if (pr != 0) {
		PluginWinamp_Close();
		return 0;
	}
	g_waOpenOk = 1;
	if (in->SetVolume) in->SetVolume(255);
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
	if (g_waRemote) {
		if (g_waRemoteSid)
			g_kpiHost.ForeignClose(g_waRemoteSid);
		g_waRemote = 0;
		g_waRemoteSid = 0;
		g_waOpenOk = 0;
		return;
	}
	if (g_waIn) {
		if (g_waIn->Stop) g_waIn->Stop();
		if (g_waIn->Quit) g_waIn->Quit();
		g_waIn = NULL;
	}
	g_waOut.Close();
	if (g_waDll) {
		FreeLibrary(g_waDll);
		g_waDll = NULL;
	}
	g_waOpenOk = 0;
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
	if (!g_waIn) return 0;
	if (g_waIn->SetOutputTime) g_waIn->SetOutputTime(timeMs);
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
	return 0;
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
	const DWORD t0 = GetTickCount();
	while (got < bytesWanted) {
		EnterCriticalSection(&g_waCs);
		int avail = (int)g_waUsed;
		int take = bytesWanted - got;
		if (take > avail) take = avail;
		int r = (int)g_waRingR;
		for (int i = 0; i < take; ++i) {
			dst[got + i] = g_waRing[r];
			r++;
			if (r >= WA_RING_BYTES) r = 0;
		}
		g_waRingR = r;
		g_waUsed -= take;
		LeaveCriticalSection(&g_waCs);
		got += take;
		if (got >= bytesWanted) break;
		if (!g_waPlaying && avail == 0) {
			if (GetTickCount() - t0 > 200)
				break;
		}
		Sleep(1);
	}
	return got;
}

int readwinamp(BYTE* bw, int cnt)
{
	return PluginWinamp_Read(bw, cnt);
}
