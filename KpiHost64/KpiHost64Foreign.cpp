// KpiHost64 外部入力プラグイン（Winamp in_ / XMPlay / AIMP）。MFC 無し。
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdlib>

#include "..\kpi_host_ipc.h"
#include "..\PluginKinds.h"
#include "..\third_party\winamp\in2.h"
#include "..\third_party\xmplay\xmpin.h"
#include "..\third_party\aimp\apiPlugin.h"
#include "..\third_party\aimp\apiDecoders.h"

// 外部デコーダ 1 本。kind で Winamp / XMPlay / AIMP のどの経路か決まる。
struct ForeignSession
{
	uint32_t kind = 0; // 1=Winamp 2=XMPlay 3=AIMP
	HMODULE dll = NULL;
	// Winamp in_: 偽 Out_Module へ Write させ、リングから本体が読む
	In_Module* waIn = nullptr;
	Out_Module waOut{};
	CRITICAL_SECTION cs{};
	bool csInit = false;
	std::vector<uint8_t> ring; // PCM リング。プラグインスレッドが書き、Render が読む
	int ringR = 0, ringW = 0, ringUsed = 0;
	int rate = 44100, ch = 2, bits = 16;
	int playing = 0;
	__int64 written = 0; // 書き込んだバイト累計
	int flushMs = 0;     // Flush で渡された再生位置（ms）
	// XMPlay
	XMPIN* xmp = nullptr;
	std::vector<float> fbuf;
	std::vector<uint8_t> pending;
	size_t pendOff = 0;
	int eof = 0;
	// AIMP
	IAIMPPlugin* aimpPlug = nullptr;
	IAIMPAudioDecoder* aimpDec = nullptr;
	IAIMPExtensionAudioDecoderOld* aimpExt = nullptr;
	IUnknown* aimpCore = nullptr;
};

static std::unordered_map<uint32_t, ForeignSession*> g_foreign;
static uint32_t g_nextForeignId = 1;

static ForeignSession* ForeignGet(uint32_t id)
{
	auto it = g_foreign.find(id);
	return it == g_foreign.end() ? nullptr : it->second;
}

// Winamp の out スタブ。同時に再生するセッションは 1 本（g_waCur）。
static ForeignSession* g_waCur = nullptr;
static volatile LONG g_waEof = 0;

static int __cdecl FWa_Open(int sr, int nch, int bps, int, int)
{
	if (!g_waCur) return -1;
	g_waCur->rate = sr > 0 ? sr : 44100;
	g_waCur->ch = nch > 0 ? nch : 2;
	g_waCur->bits = bps > 0 ? bps : 16;
	g_waCur->ring.assign(2 * 1024 * 1024, 0);
	g_waCur->ringR = g_waCur->ringW = g_waCur->ringUsed = 0;
	g_waCur->playing = 1;
	g_waCur->written = 0;
	return 50;
}
static void __cdecl FWa_Close() { if (g_waCur) g_waCur->playing = 0; }
static int __cdecl FWa_Write(char* buf, int len)
{
	if (!g_waCur || !buf || len <= 0) return 0;
	EnterCriticalSection(&g_waCur->cs);
	int freeB = (int)g_waCur->ring.size() - g_waCur->ringUsed;
	if (len > freeB) { LeaveCriticalSection(&g_waCur->cs); return 1; }
	for (int i = 0; i < len; ++i) {
		g_waCur->ring[g_waCur->ringW] = (uint8_t)buf[i];
		g_waCur->ringW = (g_waCur->ringW + 1) % (int)g_waCur->ring.size();
	}
	g_waCur->ringUsed += len;
	g_waCur->written += len;
	LeaveCriticalSection(&g_waCur->cs);
	return 0;
}
static int __cdecl FWa_CanWrite()
{
	if (!g_waCur) return 0;
	EnterCriticalSection(&g_waCur->cs);
	int f = (int)g_waCur->ring.size() - g_waCur->ringUsed;
	LeaveCriticalSection(&g_waCur->cs);
	return f;
}
static int __cdecl FWa_IsPlaying()
{
	if (!g_waCur) return 0;
	EnterCriticalSection(&g_waCur->cs);
	int u = g_waCur->ringUsed;
	LeaveCriticalSection(&g_waCur->cs);
	return (g_waCur->playing || u > 0) ? 1 : 0;
}
static int __cdecl FWa_Pause(int) { return 0; }
static void __cdecl FWa_SetVolume(int) {}
static void __cdecl FWa_SetPan(int) {}
static void __cdecl FWa_Flush(int t) { InterlockedExchange(&g_waEof, 0); if (g_waCur) { EnterCriticalSection(&g_waCur->cs); g_waCur->ringR = g_waCur->ringW = g_waCur->ringUsed = 0; g_waCur->flushMs = t; LeaveCriticalSection(&g_waCur->cs); } }
static int __cdecl FWa_GetOutputTime() { return g_waCur ? g_waCur->flushMs : 0; }
static int __cdecl FWa_GetWrittenTime() { return 0; }
static void __cdecl FWa_nop() {}
static void __cdecl FWa_SAVSAInit(int, int) {}
static void __cdecl FWa_SAAddPCM(void*, int, int, int) {}
static int __cdecl FWa_SAGetMode() { return 0; }
static int __cdecl FWa_SAAdd(void*, int, int) { return 0; }
static void __cdecl FWa_VSAAddPCM(void*, int, int, int) {}
static int __cdecl FWa_VSAGetMode(int*, int*) { return 0; }
static int __cdecl FWa_VSAAdd(void*, int) { return 0; }
static void __cdecl FWa_VSASetInfo(int, int) {}
static int __cdecl FWa_dsp_isactive() { return 0; }
static int __cdecl FWa_dsp_dosamples(short* s, int n, int, int, int) { return n; }
static void __cdecl FWa_EQSet(int, char[10], int) {}
static void FWa_SetInfo(int, int, int, int) {}

// in_ モジュールに偽 out を繋ぐ。可視化／DSP は全部 nop。
static void BindWa(ForeignSession* s, HWND hwnd)
{
	ZeroMemory(&s->waOut, sizeof(s->waOut));
	s->waOut.version = OUT_VER;
	s->waOut.description = (char*)"host";
	s->waOut.id = 65536;
	s->waOut.hMainWindow = hwnd;
	s->waOut.Open = FWa_Open;
	s->waOut.Close = FWa_Close;
	s->waOut.Write = FWa_Write;
	s->waOut.CanWrite = FWa_CanWrite;
	s->waOut.IsPlaying = FWa_IsPlaying;
	s->waOut.Pause = FWa_Pause;
	s->waOut.SetVolume = FWa_SetVolume;
	s->waOut.SetPan = FWa_SetPan;
	s->waOut.Flush = FWa_Flush;
	s->waOut.GetOutputTime = FWa_GetOutputTime;
	s->waOut.GetWrittenTime = FWa_GetWrittenTime;
	s->waOut.Init = FWa_nop;
	s->waOut.Quit = FWa_nop;
	s->waOut.Config = (void(__cdecl*)(HWND))FWa_nop;
	s->waOut.About = (void(__cdecl*)(HWND))FWa_nop;
	if (s->waIn) {
		s->waIn->hMainWindow = hwnd;
		s->waIn->hDllInstance = s->dll;
		s->waIn->outMod = &s->waOut;
		s->waIn->SAVSAInit = FWa_SAVSAInit;
		s->waIn->SAVSADeInit = FWa_nop;
		s->waIn->SAAddPCMData = FWa_SAAddPCM;
		s->waIn->SAGetMode = FWa_SAGetMode;
		s->waIn->SAAdd = FWa_SAAdd;
		s->waIn->VSAAddPCMData = FWa_VSAAddPCM;
		s->waIn->VSAGetMode = FWa_VSAGetMode;
		s->waIn->VSAAdd = FWa_VSAAdd;
		s->waIn->VSASetInfo = FWa_VSASetInfo;
		s->waIn->dsp_isactive = FWa_dsp_isactive;
		s->waIn->dsp_dosamples = FWa_dsp_dosamples;
		s->waIn->EQSet = FWa_EQSet;
		s->waIn->SetInfo = FWa_SetInfo;
	}
}

static void AppendWaExt(std::wstring& out, const std::string& tok)
{
	size_t b = tok.find_first_not_of(" \t*.");
	if (b == std::string::npos) return;
	size_t e = tok.find_last_not_of(" \t");
	std::wstring w = L".";
	for (size_t i = b; i <= e; ++i) w += (wchar_t)tolower((unsigned char)tok[i]);
	if (w.size() <= 1) return;
	if (!out.empty()) out += L'/';
	out += w;
}

// "ext群\0説明\0ext群\0説明\0\0"。ext群は in_mod の "mod;s3m;xm" のように ';' 連結される。
// ここを1つの拡張子として扱うと本体側の照合が一生ヒットしない。
static std::wstring ParseWaExts(const char* fe)
{
	std::wstring out;
	if (!fe) return out;
	const char* p = fe;
	while (*p) {
		std::string group(p); p += group.size() + 1;
		if (*p) p += strlen(p) + 1;
		size_t start = 0;
		for (;;) {
			size_t sc = group.find(';', start);
			AppendWaExt(out, (sc == std::string::npos) ? group.substr(start) : group.substr(start, sc - start));
			if (sc == std::string::npos) break;
			start = sc + 1;
		}
	}
	return out;
}

// version から IN_UNICODE / IN_INIT_RET を除いた素の型番だけを見る
static bool WaVersionOk(const In_Module* in)
{
	if (!in) return false;
	const int v = (in->version & ~IN_UNICODE) & ~IN_INIT_RET;
	return v == IN_VER_OLD || v == IN_VER;
}
static bool WaIsUnicode(const In_Module* in) { return in && (in->version & IN_UNICODE) == IN_UNICODE; }
static bool WaUsesOutput(const In_Module* in) { return in && (in->UsesOutputPlug & IN_MODULE_FLAG_USES_OUTPUT_PLUGIN) != 0; }

// 曲終端は WM_WA_MPEG_EOF(WM_USER+2) の PostMessage で来る。
// GetConsoleWindow() はホストが GUI 無しだと NULL になり通知を取りこぼすため専用窓を持つ。
enum { WA_WM_IPC = WM_USER, WA_WM_MPEG_EOF = WM_USER + 2 };
static HWND g_waWnd = NULL;
static HANDLE g_waWndThread = NULL, g_waWndReady = NULL;

static LRESULT CALLBACK WaHostWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WA_WM_MPEG_EOF) { InterlockedExchange(&g_waEof, 1); return 0; }
	if (m == WA_WM_IPC) {
		if (l == 0) return 0x5066;  // IPC_GETVERSION
		if (l == 104) return 1;     // IPC_ISPLAYING
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

static DWORD WINAPI WaHostWndThread(LPVOID)
{
	HINSTANCE hi = GetModuleHandleW(NULL);
	WNDCLASSEXW wc; ZeroMemory(&wc, sizeof(wc));
	wc.cbSize = sizeof(wc); wc.lpfnWndProc = WaHostWndProc;
	wc.hInstance = hi; wc.lpszClassName = L"KpiHost64WinampHost";
	RegisterClassExW(&wc);
	g_waWnd = CreateWindowExW(0, L"KpiHost64WinampHost", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hi, NULL);
	SetEvent(g_waWndReady);
	if (!g_waWnd) return 0;
	MSG m;
	while (GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
	return 0;
}

static HWND WaEnsureWnd()
{
	if (g_waWnd) return g_waWnd;
	if (!g_waWndReady) g_waWndReady = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!g_waWndThread) g_waWndThread = CreateThread(NULL, 0, WaHostWndThread, NULL, 0, NULL);
	if (g_waWndReady) WaitForSingleObject(g_waWndReady, 3000);
	return g_waWnd;
}

typedef In_Module* (__cdecl* pfn_wa)();

// DLL を一時ロードして拡張子文字列だけ取る。セッションは残さない。
uint32_t ForeignHost_ListExts(uint32_t kind, const std::wstring& path, std::wstring& outExts)
{
	outExts.clear();
	HMODULE h = LoadLibraryExW(path.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) return KPIHOST64_STATUS_FAIL;
	if (kind == PLUGKIND_WINAMP) {
		pfn_wa getIn = (pfn_wa)GetProcAddress(h, "winampGetInModule2");
		if (!getIn) { FreeLibrary(h); return KPIHOST64_STATUS_FAIL; }
		In_Module* in = getIn();
		if (!WaVersionOk(in) || !WaUsesOutput(in)) { FreeLibrary(h); return KPIHOST64_STATUS_FAIL; }
		// vgmstream 系は Init() を通すまで FileExtensions が空。列挙前に必ず初期化する。
		ForeignSession probe;
		probe.dll = h;
		probe.waIn = in;
		BindWa(&probe, WaEnsureWnd());
		if (in->Init) in->Init();
		outExts = ParseWaExts(in->FileExtensions);
		if (in->Quit) in->Quit();
		FreeLibrary(h);
		return outExts.empty() ? KPIHOST64_STATUS_FAIL : KPIHOST64_STATUS_OK;
	}
	// XMPlay / AIMP は x64 の再生系が無い。ここで拡張子を返すと本体台帳に載ってしまい、
	// 再生時に必ず Open 失敗 → DirectShow(-2) に落ちるだけなので列挙自体を断る。
	FreeLibrary(h);
	return KPIHOST64_STATUS_NOT_SUPPORTED;
}

// いまは Winamp in_ のみ。XMPlay/AIMP x64 は列挙しない（再生経路が無い）。
uint32_t ForeignHost_Open(uint32_t kind, const std::wstring& dll, const std::wstring& media, KPIHOST64_ForeignOpenReply& reply)
{
	ZeroMemory(&reply, sizeof(reply));
	ForeignSession* s = new ForeignSession();
	s->kind = kind;
	InitializeCriticalSection(&s->cs);
	s->csInit = true;
	s->dll = LoadLibraryExW(dll.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!s->dll) { delete s; return KPIHOST64_STATUS_FAIL; }

	if (kind == PLUGKIND_WINAMP) {
		pfn_wa getIn = (pfn_wa)GetProcAddress(s->dll, "winampGetInModule2");
		if (!getIn) { FreeLibrary(s->dll); delete s; return KPIHOST64_STATUS_FAIL; }
		s->waIn = getIn();
		if (!s->waIn || !s->waIn->Play || !WaVersionOk(s->waIn) || !WaUsesOutput(s->waIn)) {
			FreeLibrary(s->dll); delete s; return KPIHOST64_STATUS_FAIL;
		}
		g_waCur = s;
		InterlockedExchange(&g_waEof, 0);
		BindWa(s, WaEnsureWnd());
		if (s->waIn->Init) s->waIn->Init();
		int rc;
		if (WaIsUnicode(s->waIn)) {
			// IN_UNICODE プラグインの Play は wchar_t*。ANSI 変換して渡すと開けない
			rc = s->waIn->Play((const in_char*)media.c_str());
		} else {
			char pathA[MAX_PATH * 2];
			WideCharToMultiByte(CP_ACP, 0, media.c_str(), -1, pathA, (int)sizeof(pathA), NULL, NULL);
			rc = s->waIn->Play((const in_char*)pathA);
		}
		if (rc != 0) {
			if (s->waIn->Quit) s->waIn->Quit();
			FreeLibrary(s->dll); delete s; g_waCur = nullptr; return KPIHOST64_STATUS_FAIL;
		}
		// フォーマットはデコードスレッドが outMod->Open() を呼ぶまで確定しない。
		// Play() 直後に読むと既定値(44100/2/16)を本体へ返してしまう。
		for (int i = 0; i < 5000 && !s->playing && !InterlockedCompareExchange(&g_waEof, 0, 0); ++i)
			Sleep(1);
		if (!s->playing) {
			if (s->waIn->Stop) s->waIn->Stop();
			if (s->waIn->Quit) s->waIn->Quit();
			FreeLibrary(s->dll); delete s; g_waCur = nullptr; return KPIHOST64_STATUS_FAIL;
		}
		reply.sampleRate = (uint32_t)s->rate;
		reply.channels = (uint32_t)s->ch;
		reply.bitsPerSample = s->bits;
		int ms = s->waIn->GetLength ? s->waIn->GetLength() : 0;
		reply.lengthSamples = (ms > 0 && s->rate > 0) ? (uint64_t)ms * s->rate / 1000 : 0;
	}
	else {
		FreeLibrary(s->dll); delete s; return KPIHOST64_STATUS_NOT_SUPPORTED;
	}

	uint32_t id = g_nextForeignId++;
	if (id == 0) id = g_nextForeignId++;
	reply.sessionId = id;
	g_foreign[id] = s;
	return KPIHOST64_STATUS_OK;
}

uint32_t ForeignHost_Render(uint32_t sessionId, uint32_t bytesWanted, std::vector<uint8_t>& out, uint32_t& eof)
{
	eof = 0;
	out.clear();
	ForeignSession* s = ForeignGet(sessionId);
	if (!s) return KPIHOST64_STATUS_NOT_FOUND;
	if (s->kind == PLUGKIND_WINAMP) {
		out.resize(bytesWanted);
		int got = 0;
		DWORD t0 = GetTickCount();
		while (got < (int)bytesWanted) {
			EnterCriticalSection(&s->cs);
			int avail = s->ringUsed;
			int take = (int)bytesWanted - got;
			if (take > avail) take = avail;
			for (int i = 0; i < take; ++i) {
				out[got + i] = s->ring[s->ringR];
				s->ringR = (s->ringR + 1) % (int)s->ring.size();
			}
			s->ringUsed -= take;
			LeaveCriticalSection(&s->cs);
			got += take;
			if (got >= (int)bytesWanted) break;
			if (take > 0) { t0 = GetTickCount(); continue; }
			// リングが空。WM_WA_MPEG_EOF 受信済み／出力クローズ済みなら本当に終端
			if (InterlockedCompareExchange(&g_waEof, 0, 0)) { eof = 1; break; }
			if (!s->playing) { eof = 1; break; }
			if (GetTickCount() - t0 > 5000) break; // デコーダ無応答の保険
			Sleep(1);
		}
		out.resize(got);
		return KPIHOST64_STATUS_OK;
	}
	return KPIHOST64_STATUS_NOT_SUPPORTED;
}

uint32_t ForeignHost_Seek(uint32_t sessionId, uint64_t posSample)
{
	ForeignSession* s = ForeignGet(sessionId);
	if (!s) return KPIHOST64_STATUS_NOT_FOUND;
	if (s->kind == PLUGKIND_WINAMP && s->waIn && s->waIn->SetOutputTime && s->rate > 0) {
		InterlockedExchange(&g_waEof, 0);
		s->waIn->SetOutputTime((int)(posSample * 1000 / s->rate));
		return KPIHOST64_STATUS_OK;
	}
	return KPIHOST64_STATUS_NOT_SUPPORTED;
}

uint32_t ForeignHost_Close(uint32_t sessionId)
{
	auto it = g_foreign.find(sessionId);
	if (it == g_foreign.end()) return KPIHOST64_STATUS_NOT_FOUND;
	ForeignSession* s = it->second;
	if (s->kind == PLUGKIND_WINAMP && s->waIn) {
		if (s->waIn->Stop) s->waIn->Stop();
		if (s->waIn->Quit) s->waIn->Quit();
	}
	if (s->dll) FreeLibrary(s->dll);
	if (s->csInit) DeleteCriticalSection(&s->cs);
	if (g_waCur == s) g_waCur = nullptr;
	delete s;
	g_foreign.erase(it);
	return KPIHOST64_STATUS_OK;
}
