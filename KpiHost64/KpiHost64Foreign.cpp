// KpiHost64 foreign plugin decode (Winamp / XMPlay / AIMP) — no MFC
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

struct ForeignSession
{
	uint32_t kind = 0;
	HMODULE dll = NULL;
	// winamp
	In_Module* waIn = nullptr;
	Out_Module waOut{};
	CRITICAL_SECTION cs{};
	bool csInit = false;
	std::vector<uint8_t> ring;
	int ringR = 0, ringW = 0, ringUsed = 0;
	int rate = 44100, ch = 2, bits = 16;
	int playing = 0;
	__int64 written = 0;
	int flushMs = 0;
	// xmplay
	XMPIN* xmp = nullptr;
	std::vector<float> fbuf;
	std::vector<uint8_t> pending;
	size_t pendOff = 0;
	int eof = 0;
	// aimp
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

// ---- Winamp out stubs (per-session via TLS-like: only one session plays) ----
static ForeignSession* g_waCur = nullptr;

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
static void __cdecl FWa_Flush(int t) { if (g_waCur) { EnterCriticalSection(&g_waCur->cs); g_waCur->ringR = g_waCur->ringW = g_waCur->ringUsed = 0; g_waCur->flushMs = t; LeaveCriticalSection(&g_waCur->cs); } }
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
static void __cdecl FWa_SetInfo(int, int, int, int) {}

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

static std::wstring ParseWaExts(const char* fe)
{
	std::wstring out;
	if (!fe) return out;
	const char* p = fe;
	while (*p) {
		std::string e(p); p += e.size() + 1;
		if (*p) p += strlen(p) + 1; else break;
		if (e.empty()) continue;
		if (!out.empty()) out += L'/';
		out += L'.';
		for (char c : e) out += (wchar_t)tolower((unsigned char)c);
	}
	return out;
}

typedef In_Module* (__cdecl* pfn_wa)();

uint32_t ForeignHost_ListExts(uint32_t kind, const std::wstring& path, std::wstring& outExts)
{
	outExts.clear();
	HMODULE h = LoadLibraryExW(path.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) return KPIHOST64_STATUS_FAIL;
	if (kind == PLUGKIND_WINAMP) {
		pfn_wa getIn = (pfn_wa)GetProcAddress(h, "winampGetInModule2");
		if (!getIn) { FreeLibrary(h); return KPIHOST64_STATUS_FAIL; }
		In_Module* in = getIn();
		if (!in) { FreeLibrary(h); return KPIHOST64_STATUS_FAIL; }
		outExts = ParseWaExts(in->FileExtensions);
		FreeLibrary(h);
		return KPIHOST64_STATUS_OK;
	}
	if (kind == PLUGKIND_XMPLAY) {
		auto getIf = (XMPIN*(WINAPI*)(UINT32, InterfaceProc))GetProcAddress(h, "XMPIN_GetInterface");
		if (!getIf) { FreeLibrary(h); return KPIHOST64_STATUS_FAIL; }
		// minimal face: return null faces — plugin may still return XMPIN*
		auto face = [](DWORD) -> void* { return nullptr; };
		XMPIN* in = getIf(XMPIN_FACE, face);
		if (!in) { FreeLibrary(h); return KPIHOST64_STATUS_FAIL; }
		if (in->exts) {
			const char* p = in->exts;
			if (*p) p += strlen(p) + 1;
			std::string list = p ? p : "";
			std::wstring w;
			for (size_t i = 0; i < list.size(); ) {
				size_t slash = list.find('/', i);
				std::string tok = (slash == std::string::npos) ? list.substr(i) : list.substr(i, slash - i);
				if (!tok.empty()) {
					if (!w.empty()) w += L'/';
					w += L'.';
					for (char c : tok) w += (wchar_t)tolower((unsigned char)c);
				}
				if (slash == std::string::npos) break;
				i = slash + 1;
			}
			outExts = w;
		}
		FreeLibrary(h);
		return KPIHOST64_STATUS_OK;
	}
	if (kind == PLUGKIND_AIMP) {
		auto getHdr = (TAIMPPluginGetHeaderProc)GetProcAddress(h, "AIMPPluginGetHeader");
		FreeLibrary(h);
		return getHdr ? KPIHOST64_STATUS_OK : KPIHOST64_STATUS_FAIL;
	}
	FreeLibrary(h);
	return KPIHOST64_STATUS_NOT_SUPPORTED;
}

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
		if (!s->waIn || !s->waIn->Play) { FreeLibrary(s->dll); delete s; return KPIHOST64_STATUS_FAIL; }
		g_waCur = s;
		BindWa(s, GetConsoleWindow());
		if (s->waIn->Init) s->waIn->Init();
		char pathA[MAX_PATH * 2];
		WideCharToMultiByte(CP_ACP, 0, media.c_str(), -1, pathA, (int)sizeof(pathA), NULL, NULL);
		if (s->waIn->Play(pathA) != 0) {
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
			if (!s->playing && avail == 0) {
				if (GetTickCount() - t0 > 200) { eof = 1; break; }
			}
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
