#include "kpihost_stdafx.h"
#include "../kpi_host_ipc.h"
#include "../VstMidiEngine.h"
#include "KpiHost64VstLive.h"

#include <process.h>
#include <string>
#include <vector>

namespace {

// LIVE_MAX_BUFFERED is the play-out latency the keyboard has to live with, so
// keep it small; the app only pre-rolls a quarter of it before it starts.
enum { LIVE_RENDER_FRAMES = 512, LIVE_MAX_BUFFERED = 2048 };

volatile LONG g_liveParts = 0;

struct LiveAudioState {
	HANDLE hMap = NULL;
	KPIHOST64_VstLiveAudioShm* shm = NULL;
	HANDLE hMidiMap = NULL;
	KPIHOST64_VstLiveMidiShm* midiShm = NULL;
	HANDLE stopEvent = NULL;
	HANDLE wakeEvent = NULL;
	HANDLE thread = NULL;
	volatile LONG running = 0;
};

LiveAudioState g_liveAudio;

static float* ShmL(KPIHOST64_VstLiveAudioShm* s) { return (float*)(s + 1); }
static float* ShmR(KPIHOST64_VstLiveAudioShm* s) { return ShmL(s) + s->capacity; }
static KPIHOST64_VstLiveMidiEvent* ShmMidiEvents(KPIHOST64_VstLiveMidiShm* s)
{
	return (KPIHOST64_VstLiveMidiEvent*)(s + 1);
}

static void LiveAudioCloseMapping()
{
	if (g_liveAudio.shm) {
		UnmapViewOfFile(g_liveAudio.shm);
		g_liveAudio.shm = NULL;
	}
	if (g_liveAudio.hMap) {
		CloseHandle(g_liveAudio.hMap);
		g_liveAudio.hMap = NULL;
	}
	if (g_liveAudio.midiShm) {
		UnmapViewOfFile(g_liveAudio.midiShm);
		g_liveAudio.midiShm = NULL;
	}
	if (g_liveAudio.hMidiMap) {
		CloseHandle(g_liveAudio.hMidiMap);
		g_liveAudio.hMidiMap = NULL;
	}
}

static void LiveAudioDrainMidi()
{
	KPIHOST64_VstLiveMidiShm* m = g_liveAudio.midiShm;
	if (!m || !m->capacity) return;
	KPIHOST64_VstLiveMidiEvent* ev = ShmMidiEvents(m);
	const uint32_t cap = m->capacity;
	uint32_t r = m->readPos;
	const uint32_t w = m->writePos;
	while (r != w) {
		const uint32_t idx = r & (cap - 1);
		const uint32_t port = ev[idx].port;
		const DWORD msg = (DWORD)ev[idx].msg;
		r++;
		VstLiveMidiShort((int)port, msg);
	}
	InterlockedExchange((LONG*)&m->readPos, (LONG)r);
}

static int LiveAudioStopSignalled()
{
	return g_liveAudio.stopEvent &&
		WaitForSingleObject(g_liveAudio.stopEvent, 0) == WAIT_OBJECT_0;
}

static unsigned __stdcall LiveAudioThreadProc(void*)
{
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	__declspec(align(32)) float tl[LIVE_RENDER_FRAMES], tr[LIVE_RENDER_FRAMES];
	HANDLE waits[2] = { g_liveAudio.stopEvent, g_liveAudio.wakeEvent };
	for (;;) {
		DWORD w = WaitForMultipleObjects(2, waits, FALSE, 50);
		if (w == WAIT_OBJECT_0 || LiveAudioStopSignalled()) break;
		KPIHOST64_VstLiveAudioShm* s = g_liveAudio.shm;
		if (!s) continue;
		const uint32_t cap = s->capacity;
		for (;;) {
			if (LiveAudioStopSignalled()) break;
			const uint32_t wr = s->writePos;
			const uint32_t r = s->readPos;
			const uint32_t buffered = wr - r;
			if (buffered >= cap - LIVE_RENDER_FRAMES) break;
			if (buffered >= LIVE_MAX_BUFFERED) break;
			LiveAudioDrainMidi();
			VstLiveRender(tl, tr, LIVE_RENDER_FRAMES);
			uint32_t wp = wr;
			float* sl = ShmL(s);
			float* sr = ShmR(s);
			for (int i = 0; i < LIVE_RENDER_FRAMES; ++i) {
				const uint32_t idx = wp & (cap - 1);
				sl[idx] = tl[i];
				sr[idx] = tr[i];
				wp++;
			}
			MemoryBarrier();
			InterlockedExchange((LONG*)&s->writePos, (LONG)wp);
		}
	}
	CoUninitialize();
	return 0;
}

static int LiveAudioStopThread()
{
	if (g_liveAudio.stopEvent)
		SetEvent(g_liveAudio.stopEvent);
	if (g_liveAudio.wakeEvent)
		SetEvent(g_liveAudio.wakeEvent);
	int stopped = 1;
	if (g_liveAudio.thread) {
		if (WaitForSingleObject(g_liveAudio.thread, 4000) != WAIT_OBJECT_0)
			stopped = 0;
		if (stopped) {
			CloseHandle(g_liveAudio.thread);
			g_liveAudio.thread = NULL;
		}
	}
	InterlockedExchange(&g_liveAudio.running, 0);
	if (stopped && g_liveAudio.stopEvent)
		ResetEvent(g_liveAudio.stopEvent);
	return stopped;
}

// ---------------------------------------------------------------------------
// Plug-in UI thread
//
// Every plug-in lifecycle call (load, unload, editor open/close) is marshalled
// onto one dedicated thread that owns a permanently running message loop.
// The pipe thread only forwards requests. This mirrors VSTHost, where module
// loading and all GUI work happen on the single application UI thread; audio
// is the only thing that runs elsewhere. Without it, SC-VA's editor is created
// on a thread that has no relationship to the one that initialised the plug-in
// module, so the view paints once and then never receives another update.
// ---------------------------------------------------------------------------

enum {
	UIMSG_LOAD = WM_APP + 11,
	UIMSG_UNLOAD,
	UIMSG_UNLOAD_ALL,
	UIMSG_EDITOR_OPEN,
	UIMSG_EDITOR_CLOSE,
	UIMSG_SEND_CH,
	UIMSG_PROG_COUNT,
	UIMSG_PROG_CURRENT,
	UIMSG_PROG_NAME,
	UIMSG_PROG_SET
};

struct UiLoadRequest {
	int part;
	const wchar_t* path;
	int isVst3;
};

struct UiProgNameRequest {
	int part;
	int index;
	wchar_t* out;
	int chars;
};

HWND g_uiWnd = NULL;
HANDLE g_uiThread = NULL;
volatile LONG g_uiStarted = 0;

const wchar_t* UiWndClass() { return L"OggKpiHost64VstUi"; }

static LRESULT CALLBACK UiWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case UIMSG_LOAD: {
		auto* req = (UiLoadRequest*)lp;
		if (!req) return -1;
		return VstLiveLoadPart(req->part, req->path, req->isVst3);
	}
	case UIMSG_UNLOAD:
		VstLiveUnloadPart((int)wp);
		return 0;
	case UIMSG_UNLOAD_ALL:
		for (int i = 1; i <= 32; ++i) VstLiveUnloadPart(i);
		return 0;
	case UIMSG_EDITOR_OPEN:
		return VstLiveEditorOpen((int)wp);
	case UIMSG_EDITOR_CLOSE:
		VstLiveEditorClose((int)wp);
		return 0;
	case UIMSG_SEND_CH:
		VstLiveSetSendChannel((int)wp, (int)lp);
		return 0;
	case UIMSG_PROG_COUNT:
		return VstLiveProgramCount((int)wp);
	case UIMSG_PROG_CURRENT:
		return VstLiveProgramCurrent((int)wp);
	case UIMSG_PROG_NAME: {
		auto* req = (UiProgNameRequest*)lp;
		if (!req) return 0;
		return VstLiveProgramName(req->part, req->index, req->out, req->chars);
	}
	case UIMSG_PROG_SET:
		return VstLiveSetProgram((int)wp, (int)lp);
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

static unsigned __stdcall UiThreadProc(void* ctx)
{
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	HANDLE ready = (HANDLE)ctx;

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = UiWndProc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = UiWndClass();
	RegisterClassExW(&wc);

	g_uiWnd = CreateWindowExW(0, UiWndClass(), L"", 0, 0, 0, 0, 0,
		HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
	SetEvent(ready);
	if (!g_uiWnd) return 1;

	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	CoUninitialize();
	return 0;
}

static bool EnsureUiThread()
{
	if (InterlockedCompareExchange(&g_uiStarted, 1, 0) != 0)
		return g_uiWnd != NULL;
	HANDLE ready = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!ready) return false;
	unsigned tid = 0;
	g_uiThread = (HANDLE)_beginthreadex(NULL, 0, UiThreadProc, ready, 0, &tid);
	if (g_uiThread)
		WaitForSingleObject(ready, 10000);
	CloseHandle(ready);
	return g_uiWnd != NULL;
}

} // namespace

uint32_t VstHost64_LiveLoad(uint32_t part1to32, const wchar_t* path, uint32_t isVst3)
{
	if (!path || !*path || part1to32 < 1 || part1to32 > 32)
		return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	// The audio thread holds the engine lock inside processReplacing. Load
	// takes that lock on the UI thread, so stop rendering first. Do not start
	// audio here: the app calls VstLiveAudioStart afterwards to open the rings.
	const LONG hadParts = InterlockedCompareExchange(&g_liveParts, 0, 0);
	if (hadParts > 0)
		VstHost64_LiveAudioStop();
	UiLoadRequest req = { (int)part1to32, path, (int)isVst3 };
	const LRESULT rc = SendMessageW(g_uiWnd, UIMSG_LOAD, 0, (LPARAM)&req);
	if (rc != 0)
		return KPIHOST64_STATUS_FAIL;
	InterlockedIncrement(&g_liveParts);
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_LiveUnload(uint32_t part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	const LONG left = InterlockedCompareExchange(&g_liveParts, 0, 0);
	VstHost64_LiveAudioStop();
	DWORD_PTR dummy = 0;
	SendMessageTimeoutW(g_uiWnd, UIMSG_UNLOAD, (WPARAM)part1to32, 0,
		SMTO_ABORTIFHUNG, 4000, &dummy);
	if (InterlockedCompareExchange(&g_liveParts, 0, 0) > 0)
		InterlockedDecrement(&g_liveParts);
	if (left > 1 && InterlockedCompareExchange(&g_liveParts, 0, 0) > 0)
		VstHost64_LiveAudioStart();
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_LiveUnloadAll()
{
	VstHost64_LiveAudioStop();
	VstLiveAbandonHostPlugins(1);
	if (g_uiWnd) {
		DWORD_PTR dummy = 0;
		SendMessageTimeoutW(g_uiWnd, UIMSG_UNLOAD_ALL, 0, 0,
			SMTO_ABORTIFHUNG, 2000, &dummy);
	}
	VstLiveAbandonHostPlugins(0);
	InterlockedExchange(&g_liveParts, 0);
	return KPIHOST64_STATUS_OK;
}

int VstHost64_LiveActive()
{
	return InterlockedCompareExchange(&g_liveParts, 0, 0) > 0 ? 1 : 0;
}

uint32_t VstHost64_LiveMidi(uint32_t port, uint32_t msg)
{
	if (port > 2) return KPIHOST64_STATUS_BAD_REQUEST;
	VstLiveMidiShort((int)port, (DWORD)msg);
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_LiveSysex(uint32_t port, const uint8_t* data, uint32_t len)
{
	if (port > 2 || !data || !len) return KPIHOST64_STATUS_BAD_REQUEST;
	VstLiveMidiSysex((int)port, data, (int)len);
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_LiveRender(uint32_t frames, std::vector<uint8_t>& reply)
{
	if (!frames || frames > 4096) return KPIHOST64_STATUS_BAD_REQUEST;
	std::vector<float> l(frames), r(frames);
	VstLiveRender(l.data(), r.data(), (int)frames);
	reply.resize(sizeof(KPIHOST64_VstLiveRenderReply) + frames * 2 * sizeof(float));
	auto* hdr = (KPIHOST64_VstLiveRenderReply*)reply.data();
	hdr->frames = frames;
	float* out = (float*)(reply.data() + sizeof(*hdr));
	for (uint32_t i = 0; i < frames; ++i) {
		out[i * 2] = l[i];
		out[i * 2 + 1] = r[i];
	}
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_LiveAudioStart()
{
	if (g_liveAudio.thread) {
		if (WaitForSingleObject(g_liveAudio.thread, 0) != WAIT_OBJECT_0) {
			if (g_liveAudio.shm && g_liveAudio.midiShm)
				return KPIHOST64_STATUS_OK;
			return KPIHOST64_STATUS_FAIL;
		}
		CloseHandle(g_liveAudio.thread);
		g_liveAudio.thread = NULL;
	}
	if (InterlockedCompareExchange(&g_liveAudio.running, 1, 0) != 0) {
		if (g_liveAudio.shm && g_liveAudio.midiShm) return KPIHOST64_STATUS_OK;
		VstHost64_LiveAudioStop();
		if (InterlockedCompareExchange(&g_liveAudio.running, 1, 0) != 0)
			return KPIHOST64_STATUS_FAIL;
	}
	const SIZE_T audioBytes = sizeof(KPIHOST64_VstLiveAudioShm) +
		(SIZE_T)KPIHOST64_VST_LIVE_SHM_CAP * 2 * sizeof(float);
	const SIZE_T midiBytes = sizeof(KPIHOST64_VstLiveMidiShm) +
		(SIZE_T)KPIHOST64_VST_LIVE_MIDI_CAP * sizeof(KPIHOST64_VstLiveMidiEvent);
	g_liveAudio.hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
		0, (DWORD)audioBytes, KPIHOST64_VST_LIVE_SHM_NAME);
	g_liveAudio.hMidiMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
		0, (DWORD)midiBytes, KPIHOST64_VST_LIVE_MIDI_SHM_NAME);
	if (!g_liveAudio.hMap || !g_liveAudio.hMidiMap) {
		LiveAudioCloseMapping();
		InterlockedExchange(&g_liveAudio.running, 0);
		return KPIHOST64_STATUS_FAIL;
	}
	g_liveAudio.shm = (KPIHOST64_VstLiveAudioShm*)MapViewOfFile(g_liveAudio.hMap,
		FILE_MAP_ALL_ACCESS, 0, 0, audioBytes);
	g_liveAudio.midiShm = (KPIHOST64_VstLiveMidiShm*)MapViewOfFile(g_liveAudio.hMidiMap,
		FILE_MAP_ALL_ACCESS, 0, 0, midiBytes);
	if (!g_liveAudio.shm || !g_liveAudio.midiShm) {
		LiveAudioCloseMapping();
		InterlockedExchange(&g_liveAudio.running, 0);
		return KPIHOST64_STATUS_FAIL;
	}
	ZeroMemory(g_liveAudio.shm, audioBytes);
	ZeroMemory(g_liveAudio.midiShm, midiBytes);
	g_liveAudio.shm->capacity = KPIHOST64_VST_LIVE_SHM_CAP;
	g_liveAudio.shm->sampleRate = 44100;
	g_liveAudio.midiShm->capacity = KPIHOST64_VST_LIVE_MIDI_CAP;
	g_liveAudio.stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	g_liveAudio.wakeEvent = CreateEventW(NULL, FALSE, FALSE, KPIHOST64_VST_LIVE_EVENT_NAME);
	if (!g_liveAudio.stopEvent || !g_liveAudio.wakeEvent) {
		LiveAudioCloseMapping();
		InterlockedExchange(&g_liveAudio.running, 0);
		return KPIHOST64_STATUS_FAIL;
	}
	unsigned tid = 0;
	g_liveAudio.thread = (HANDLE)_beginthreadex(NULL, 0, LiveAudioThreadProc, NULL, 0, &tid);
	if (g_liveAudio.thread)
		SetThreadPriority(g_liveAudio.thread, THREAD_PRIORITY_TIME_CRITICAL);
	if (!g_liveAudio.thread) {
		CloseHandle(g_liveAudio.stopEvent);
		g_liveAudio.stopEvent = NULL;
		LiveAudioCloseMapping();
		InterlockedExchange(&g_liveAudio.running, 0);
		return KPIHOST64_STATUS_FAIL;
	}
	SetThreadPriority(g_liveAudio.thread, THREAD_PRIORITY_TIME_CRITICAL);
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_LiveAudioStop()
{
	if (!LiveAudioStopThread())
		return KPIHOST64_STATUS_FAIL;
	LiveAudioCloseMapping();
	if (g_liveAudio.stopEvent) {
		CloseHandle(g_liveAudio.stopEvent);
		g_liveAudio.stopEvent = NULL;
	}
	if (g_liveAudio.wakeEvent) {
		CloseHandle(g_liveAudio.wakeEvent);
		g_liveAudio.wakeEvent = NULL;
	}
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_LiveEditorOpen(uint32_t part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	const LRESULT rc = SendMessageW(g_uiWnd, UIMSG_EDITOR_OPEN, (WPARAM)part1to32, 0);
	return (rc == 0) ? KPIHOST64_STATUS_OK : KPIHOST64_STATUS_FAIL;
}

uint32_t VstHost64_LiveEditorClose(uint32_t part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	DWORD_PTR dummy = 0;
	SendMessageTimeoutW(g_uiWnd, UIMSG_EDITOR_CLOSE, (WPARAM)part1to32, 0,
		SMTO_ABORTIFHUNG, 4000, &dummy);
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_LiveSetSendChannel(uint32_t part1to32, int32_t sendCh)
{
	if (part1to32 < 1 || part1to32 > 32) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	SendMessageW(g_uiWnd, UIMSG_SEND_CH, (WPARAM)part1to32, (LPARAM)sendCh);
	return KPIHOST64_STATUS_OK;
}

// Program names live on the controller, which belongs to the plug-in UI
// thread, so every query is marshalled there as well.
uint32_t VstHost64_LivePrograms(uint32_t part1to32, uint32_t first, uint32_t count,
	std::vector<uint8_t>& reply)
{
	if (part1to32 < 1 || part1to32 > 32) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	const int total = (int)SendMessageW(g_uiWnd, UIMSG_PROG_COUNT, (WPARAM)part1to32, 0);
	const int current = (int)SendMessageW(g_uiWnd, UIMSG_PROG_CURRENT, (WPARAM)part1to32, 0);

	std::vector<std::wstring> names;
	if (total > 0 && count) {
		if (count > 256) count = 256;
		for (uint32_t i = 0; i < count; ++i) {
			const uint32_t idx = first + i;
			if ((int)idx >= total) break;
			wchar_t nm[128] = {};
			UiProgNameRequest req = { (int)part1to32, (int)idx, nm, 128 };
			if (!SendMessageW(g_uiWnd, UIMSG_PROG_NAME, 0, (LPARAM)&req)) break;
			names.push_back(nm);
		}
	}

	KPIHOST64_VstLiveProgramsReply head{};
	head.total = (uint32_t)(total > 0 ? total : 0);
	head.current = (current >= 0) ? (uint32_t)current : 0xFFFFFFFFu;
	head.got = (uint32_t)names.size();
	const uint8_t* h = (const uint8_t*)&head;
	reply.assign(h, h + sizeof(head));
	for (size_t i = 0; i < names.size(); ++i) {
		const uint32_t chars = (uint32_t)names[i].size();
		const uint8_t* c = (const uint8_t*)&chars;
		reply.insert(reply.end(), c, c + sizeof(chars));
		const uint8_t* t = (const uint8_t*)names[i].c_str();
		reply.insert(reply.end(), t, t + (size_t)chars * sizeof(wchar_t));
	}
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_LiveSetProgram(uint32_t part1to32, uint32_t index)
{
	if (part1to32 < 1 || part1to32 > 32) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	const LRESULT rc = SendMessageW(g_uiWnd, UIMSG_PROG_SET, (WPARAM)part1to32,
		(LPARAM)index);
	return rc ? KPIHOST64_STATUS_OK : KPIHOST64_STATUS_FAIL;
}
