#include "kpihost_stdafx.h"
#include "../kpi_host_ipc.h"
#include "../VstMidiEngine.h"
#include "KpiHost64VstLive.h"

#include <process.h>
#include <string>
#include <vector>

namespace {

// LIVE_MAX_BUFFERED は鍵盤が感じるレイテンシ。小さく保つ。
// 本体は再生開始前にこの 1/4 だけ先読みする。
enum { LIVE_RENDER_FRAMES = 512, LIVE_MAX_BUFFERED = 2048 };

volatile LONG g_liveParts = 0; // 載っているパート数。アイドル終了抑制に使う

struct LiveAudioState {
	HANDLE hMap = NULL;                         // 音声共有メモリのファイルマッピング
	KPIHOST64_VstLiveAudioShm* shm = NULL;      // 本体が読む PCM リング
	HANDLE hMidiMap = NULL;
	KPIHOST64_VstLiveMidiShm* midiShm = NULL;   // 本体が書くノートリング
	HANDLE stopEvent = NULL;                    // レンダースレッド終了
	HANDLE wakeEvent = NULL;                    // 本体がノートを置いた合図
	HANDLE thread = NULL;
	volatile LONG running = 0;
	volatile LONG editPause = 0; /* >0: skip process (refcount) */
};

LiveAudioState g_liveAudio;

static HANDLE g_edNotifyMap = NULL;
static KPIHOST64_VstLiveEdNotifyShm* g_edNotify = NULL;

static void EnsureEdNotifyShm(void)
{
	if (g_edNotify) return;
	g_edNotifyMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
		0, (DWORD)sizeof(KPIHOST64_VstLiveEdNotifyShm), KPIHOST64_VST_LIVE_EDNOTIFY_NAME);
	if (!g_edNotifyMap) return;
	g_edNotify = (KPIHOST64_VstLiveEdNotifyShm*)MapViewOfFile(
		g_edNotifyMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(KPIHOST64_VstLiveEdNotifyShm));
	if (!g_edNotify) {
		CloseHandle(g_edNotifyMap);
		g_edNotifyMap = NULL;
		return;
	}
	/* First creator zeros; reopen keeps prior openMask/seq. */
	if (GetLastError() != ERROR_ALREADY_EXISTS)
		ZeroMemory((void*)g_edNotify, sizeof(*g_edNotify));
}

static float* ShmL(KPIHOST64_VstLiveAudioShm* s) { return (float*)(s + 1); } // ヘッダ直後が L 平面
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
		const uint32_t idx = r & (cap - 1); // capacity は 2 の冪。ロック無しラップ
		const uint32_t port = ev[idx].port;
		const DWORD msg = (DWORD)ev[idx].msg;
		r++;
		if (port & 0x80000000u) {
			const int part1 = (int)(port & 0x7fffffffu);
			VstLiveMidiToPart(part1, msg);
		} else {
			VstLiveMidiShort((int)port, msg);
		}
	}
	InterlockedExchange((LONG*)&m->readPos, (LONG)r);
}

static int LiveAudioStopSignalled()
{
	return g_liveAudio.stopEvent &&
		WaitForSingleObject(g_liveAudio.stopEvent, 0) == WAIT_OBJECT_0;
}

// 共有メモリへ PCM を書き続ける。ノートはブロック直前に Drain。
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
			if (InterlockedCompareExchange(&g_liveAudio.editPause, 0, 0) > 0) {
				/* Editor opening/closing or state xfer — do not enter process(). */
				Sleep(5);
				break;
			}
			const uint32_t wr = s->writePos;
			const uint32_t r = s->readPos;
			const uint32_t buffered = wr - r;
			if (buffered >= cap - LIVE_RENDER_FRAMES) break;
			if (buffered >= LIVE_MAX_BUFFERED) break;
			LiveAudioDrainMidi(); // このブロックに載せるノートを先に渡す
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
// プラグイン UI スレッド
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
	UIMSG_PROG_SET,
	UIMSG_STATE_GET,
	UIMSG_STATE_SET,
	UIMSG_SOFT_TEARDOWN
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

struct UiStateRequest {
	int part;
	int which; /* 0=comp 1=ctrl */
	const unsigned char* inBytes; /* SET */
	int inLen;
	unsigned char* outBytes; /* GET: filled by handler via malloc */
	int outLen;
};

HWND g_uiWnd = NULL;
HANDLE g_uiThread = NULL;
volatile LONG g_uiStarted = 0;
/* Bitmask of parts whose UIMSG_EDITOR_OPEN is on the UI stack (createView).
   PROG_* must not reenter HALion mid-open — that crashes Host64 (and then ogg). */
volatile LONG g_editorOpeningMask = 0;

extern "C" void VstLiveSoftTeardownPart(int part1to32, void* hwnd);

const wchar_t* UiWndClass() { return L"OggKpiHost64VstUi"; }

static LRESULT CALLBACK UiWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case UIMSG_LOAD: {
		auto* req = (UiLoadRequest*)lp;
		if (!req) return -1;
		/* Drop stale OPEN/CLOSE for this part so a late CLOSE cannot hit the new load. */
		{
			MSG m;
			while (PeekMessageW(&m, hwnd, UIMSG_EDITOR_OPEN, UIMSG_EDITOR_CLOSE, PM_REMOVE)) {
				if ((int)m.wParam == req->part) continue;
				PostMessageW(hwnd, m.message, m.wParam, m.lParam);
			}
		}
		return VstLiveLoadPart(req->part, req->path, req->isVst3);
	}
	case UIMSG_UNLOAD: {
		MSG m;
		while (PeekMessageW(&m, hwnd, UIMSG_EDITOR_OPEN, UIMSG_EDITOR_CLOSE, PM_REMOVE)) {
			if ((int)m.wParam == (int)wp) continue;
			PostMessageW(hwnd, m.message, m.wParam, m.lParam);
		}
		VstLiveUnloadPart((int)wp);
		return 0;
	}
	case UIMSG_UNLOAD_ALL:
		for (int i = 1; i <= 32; ++i) VstLiveUnloadPart(i);
		return 0;
	case UIMSG_EDITOR_OPEN: {
		const int part = (int)wp;
		if (part < 1 || part > 32) return -1;
		InterlockedOr(&g_editorOpeningMask, (LONG)(1u << (part - 1)));
		/* Pause render during createView/attached (HALion WebView2). Do not
		   LiveAudioStop — that remapped SHM and crashed ogg. */
		VstHost64_EditorCloseBegin();
		const LRESULT rc = VstLiveEditorOpen(part);
		VstHost64_EditorCloseEnd();
		InterlockedAnd(&g_editorOpeningMask, (LONG)~(1u << (part - 1)));
		return rc;
	}
	case UIMSG_EDITOR_CLOSE:
		VstLiveEditorClose((int)wp);
		return 0;
	case UIMSG_SOFT_TEARDOWN: {
		VstLiveSoftTeardownPart((int)wp, (void*)lp);
		return 0;
	}
	case UIMSG_SEND_CH:
		VstLiveSetSendChannel((int)wp, (int)lp);
		return 0;
	case UIMSG_PROG_COUNT: {
		const int part = (int)wp;
		if (part >= 1 && part <= 32 &&
			((InterlockedCompareExchange(&g_editorOpeningMask, 0, 0) & (1u << (part - 1))) ||
			 VstLivePartEditorIsOpen(part)))
			return 0;
		return VstLiveProgramCount(part);
	}
	case UIMSG_PROG_CURRENT: {
		const int part = (int)wp;
		if (part >= 1 && part <= 32 &&
			((InterlockedCompareExchange(&g_editorOpeningMask, 0, 0) & (1u << (part - 1))) ||
			 VstLivePartEditorIsOpen(part)))
			return -1;
		return VstLiveProgramCurrent(part);
	}
	case UIMSG_PROG_NAME: {
		auto* req = (UiProgNameRequest*)lp;
		if (!req) return 0;
		if (req->part >= 1 && req->part <= 32 &&
			((InterlockedCompareExchange(&g_editorOpeningMask, 0, 0) & (1u << (req->part - 1))) ||
			 VstLivePartEditorIsOpen(req->part)))
			return 0;
		return VstLiveProgramName(req->part, req->index, req->out, req->chars);
	}
	case UIMSG_PROG_SET: {
		const int part = (int)wp;
		if (part >= 1 && part <= 32 &&
			((InterlockedCompareExchange(&g_editorOpeningMask, 0, 0) & (1u << (part - 1))) ||
			 VstLivePartEditorIsOpen(part)))
			return 0;
		return VstLiveSetProgram(part, (int)lp);
	}
	case UIMSG_STATE_GET: {
		auto* req = (UiStateRequest*)lp;
		if (!req) return 0;
		/* Pipe GET while HALion Home/WebView is up AVs Host64 (score false-close
		   / deferred labels). Close-path snapshot uses in-process getState. */
		if (req->part >= 1 && req->part <= 32 &&
			((InterlockedCompareExchange(&g_editorOpeningMask, 0, 0) & (1u << (req->part - 1))) ||
			 VstLivePartEditorIsOpen(req->part)))
			return 0;
		/* Soft-hidden SampleTank still has a view; live getState on siblings is OK
		   under editPause. Soft part itself: snap-only (VstLiveGetState). */
		VstHost64_EditorCloseBegin();
		unsigned char* bytes = NULL;
		int len = 0;
		const int ok = VstLiveGetState(req->part, req->which, &bytes, &len);
		VstHost64_EditorCloseEnd();
		if (!ok) return 0;
		req->outBytes = bytes;
		req->outLen = len;
		return 1;
	}
	case UIMSG_STATE_SET: {
		auto* req = (UiStateRequest*)lp;
		if (!req) return 0;
		if (req->part >= 1 && req->part <= 32 &&
			((InterlockedCompareExchange(&g_editorOpeningMask, 0, 0) & (1u << (req->part - 1))) ||
			 VstLivePartEditorIsOpen(req->part)))
			return 0;
		VstHost64_EditorCloseBegin();
		const int ok = VstLiveSetState(req->part, req->which, req->inBytes, req->inLen);
		VstHost64_EditorCloseEnd();
		return ok;
	}
	default:
		break;
	}
	return DefWindowProcW(hwnd, msg, wp, lp);
}

static unsigned __stdcall UiThreadProc(void* ctx)
{
	/* WebView2 / HALion UI wants OLE STA, not just CoInitializeEx. */
	OleInitialize(NULL);
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
	OleUninitialize();
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

static void LiveEditorCloseAllOnUiThread(void);

uint32_t VstHost64_LiveLoad(uint32_t part1to32, const wchar_t* path, uint32_t isVst3)
{
	if (!path || !*path || part1to32 < 1 || part1to32 > 32)
		return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	// オーディオスレッドが processReplacing 内でエンジンロックを持つ。
	// Load は UI スレッドで同じロックを取るので、先にレンダーを止める。
	// ここでは音声を再開しない。本体が VstLiveAudioStart でリングを開く。
	const LONG hadParts = InterlockedCompareExchange(&g_liveParts, 0, 0);
	if (hadParts > 0) {
		/* Pause process only — full LiveAudioStop remaps SHM and kills siblings. */
		VstHost64_EditorCloseBegin();
	}
	UiLoadRequest req = { (int)part1to32, path, (int)isVst3 };
	/* Timeout: HALion can sit behind a MediaBay dialog; do not block the pipe forever. */
	DWORD_PTR result = (DWORD_PTR)-1;
	const DWORD timeoutMs = 300000; /* 5 min — MediaBay pick on first load */
	if (!SendMessageTimeoutW(g_uiWnd, UIMSG_LOAD, 0, (LPARAM)&req,
		SMTO_ABORTIFHUNG, timeoutMs, &result)) {
		if (hadParts > 0) VstHost64_EditorCloseEnd();
		return KPIHOST64_STATUS_FAIL;
	}
	if ((LRESULT)result != 0) {
		if (hadParts > 0) VstHost64_EditorCloseEnd();
		return KPIHOST64_STATUS_FAIL;
	}
	InterlockedIncrement(&g_liveParts);
	if (hadParts > 0) VstHost64_EditorCloseEnd();
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
	/* abandon 中の UnloadPart は effEditClose を飛ばす — 先に UI を閉じる */
	LiveEditorCloseAllOnUiThread();
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

// 音声・MIDI 共有メモリを作り、優先度高めのレンダースレッドを起こす。
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
	/* Async: SendMessage blocked the pipe (and ogg UI) for the whole effEditOpen /
	   VST3 open — often minutes / forever. Post and ack immediately. */
	if (!PostMessageW(g_uiWnd, UIMSG_EDITOR_OPEN, (WPARAM)part1to32, 0))
		return KPIHOST64_STATUS_FAIL;
	return KPIHOST64_STATUS_OK;
}

static void LiveEditorCloseAllOnUiThread(void)
{
	if (!g_uiWnd) return;
	VstHost64_EditorCloseBegin();
	for (int i = 1; i <= 32; ++i) {
		DWORD_PTR dummy = 0;
		SendMessageTimeoutW(g_uiWnd, UIMSG_EDITOR_CLOSE, (WPARAM)i, 0,
			SMTO_ABORTIFHUNG, 30000, &dummy);
	}
	VstHost64_EditorCloseEnd();
}

uint32_t VstHost64_LiveEditorClose(uint32_t part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	/* Async like EDITOR_OPEN — SendMessage blocked the pipe while Vst3EditorClose
	   ran under concurrent process() (ogg UI freeze). */
	if (!PostMessageW(g_uiWnd, UIMSG_EDITOR_CLOSE, (WPARAM)part1to32, 0))
		return KPIHOST64_STATUS_FAIL;
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_LiveEditorCloseAll(void)
{
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	LiveEditorCloseAllOnUiThread();
	return KPIHOST64_STATUS_OK;
}

void VstHost64_PostSoftTeardown(uint32_t part1to32, void* hwnd)
{
	if (!g_uiWnd || part1to32 < 1 || part1to32 > 32) return;
	PostMessageW(g_uiWnd, UIMSG_SOFT_TEARDOWN, (WPARAM)part1to32, (LPARAM)hwnd);
}

void VstHost64_NotifyEditorOpened(int part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return;
	EnsureEdNotifyShm();
	if (!g_edNotify) return;
	InterlockedOr((LONG*)&g_edNotify->openMask, (LONG)(1u << (part1to32 - 1)));
}

void VstHost64_NotifyEditorSnapLens(int part1to32, uint32_t compLen, uint32_t ctrlLen)
{
	if (part1to32 < 1 || part1to32 > 32) return;
	EnsureEdNotifyShm();
	if (!g_edNotify) return;
	g_edNotify->snapCompLen = compLen;
	g_edNotify->snapCtrlLen = ctrlLen;
}

void VstHost64_NotifyEditorClosed(int part1to32, int prog)
{
	VstHost64_NotifyEditorClosedEx(part1to32, prog, 1);
}

void VstHost64_NotifyEditorClosedEx(int part1to32, int prog, int clearOpenMask)
{
	if (part1to32 < 1 || part1to32 > 32) return;
	EnsureEdNotifyShm();
	if (g_edNotify) {
		g_edNotify->part = part1to32;
		g_edNotify->prog = prog;
		if (clearOpenMask)
			InterlockedAnd((LONG*)&g_edNotify->openMask, (LONG)~(1u << (part1to32 - 1)));
		MemoryBarrier();
		InterlockedIncrement((LONG*)&g_edNotify->seq);
	}
	/* Legacy path (audio ring may be null if monitor never started). */
	KPIHOST64_VstLiveAudioShm* s = g_liveAudio.shm;
	if (!s) return;
	s->editorClosedPart = part1to32;
	s->editorClosedProg = prog;
	MemoryBarrier();
	InterlockedIncrement((LONG*)&s->editorClosedSeq);
}

void VstHost64_ClearEditorOpenMask(int part1to32)
{
	if (part1to32 < 1 || part1to32 > 32) return;
	EnsureEdNotifyShm();
	if (!g_edNotify) return;
	InterlockedAnd((LONG*)&g_edNotify->openMask, (LONG)~(1u << (part1to32 - 1)));
}

void VstHost64_EditorCloseBegin(void)
{
	/* Nested pause (deferred editor open + GET/SET_STATE). */
	if (InterlockedIncrement(&g_liveAudio.editPause) == 1) {
		if (g_liveAudio.wakeEvent) SetEvent(g_liveAudio.wakeEvent);
		Sleep(40);
	}
}

void VstHost64_EditorCloseEnd(void)
{
	const LONG v = InterlockedDecrement(&g_liveAudio.editPause);
	if (v < 0) InterlockedExchange(&g_liveAudio.editPause, 0);
	if (g_liveAudio.wakeEvent) SetEvent(g_liveAudio.wakeEvent);
}

uint32_t VstHost64_LiveSetSendChannel(uint32_t part1to32, int32_t sendCh)
{
	if (part1to32 < 1 || part1to32 > 32) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	SendMessageW(g_uiWnd, UIMSG_SEND_CH, (WPARAM)part1to32, (LPARAM)sendCh);
	return KPIHOST64_STATUS_OK;
}

// プログラム名はコントローラ側＝プラグイン UI スレッドの所有物なので、問い合わせもそちらへ。
uint32_t VstHost64_LivePrograms(uint32_t part1to32, uint32_t first, uint32_t count,
	std::vector<uint8_t>& reply)
{
	if (part1to32 < 1 || part1to32 > 32) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	/* Timeout: SampleTank DestroyWindow can hang UI; plain SendMessage freezes ogg. */
	DWORD_PTR result = 0;
	if (!SendMessageTimeoutW(g_uiWnd, UIMSG_PROG_COUNT, (WPARAM)part1to32, 0,
		SMTO_ABORTIFHUNG, 1500, &result))
		return KPIHOST64_STATUS_FAIL;
	const int total = (int)result;
	if (!SendMessageTimeoutW(g_uiWnd, UIMSG_PROG_CURRENT, (WPARAM)part1to32, 0,
		SMTO_ABORTIFHUNG, 1500, &result))
		return KPIHOST64_STATUS_FAIL;
	const int current = (int)result;

	std::vector<std::wstring> names;
	if (total > 0 && count) {
		if (count > 256) count = 256;
		for (uint32_t i = 0; i < count; ++i) {
			const uint32_t idx = first + i;
			if ((int)idx >= total) break;
			wchar_t nm[128] = {};
			UiProgNameRequest req = { (int)part1to32, (int)idx, nm, 128 };
			if (!SendMessageTimeoutW(g_uiWnd, UIMSG_PROG_NAME, 0, (LPARAM)&req,
				SMTO_ABORTIFHUNG, 1500, &result) || !result)
				break;
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

uint32_t VstHost64_LiveGetState(uint32_t part1to32, uint32_t which, std::vector<uint8_t>& reply)
{
	if (part1to32 < 1 || part1to32 > 32 || which > 1) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	UiStateRequest req = {};
	req.part = (int)part1to32;
	req.which = (int)which;
	/* Timeout: SampleTank getState can hang forever on SendMessage. */
	DWORD_PTR result = 0;
	if (!SendMessageTimeoutW(g_uiWnd, UIMSG_STATE_GET, 0, (LPARAM)&req,
		SMTO_ABORTIFHUNG, 8000, &result)) {
		KPIHOST64_VstLiveStateReply head{};
		head.part = part1to32;
		head.which = which;
		head.bytes = 0;
		const uint8_t* h = (const uint8_t*)&head;
		reply.assign(h, h + sizeof(head));
		return KPIHOST64_STATUS_FAIL;
	}
	const LRESULT rc = (LRESULT)result;
	KPIHOST64_VstLiveStateReply head{};
	head.part = part1to32;
	head.which = which;
	head.bytes = 0;
	reply.clear();
	if (!rc || !req.outBytes || req.outLen <= 0) {
		const uint8_t* h = (const uint8_t*)&head;
		reply.assign(h, h + sizeof(head));
		return rc ? KPIHOST64_STATUS_OK : KPIHOST64_STATUS_FAIL;
	}
	head.bytes = (uint32_t)req.outLen;
	const uint8_t* h = (const uint8_t*)&head;
	reply.assign(h, h + sizeof(head));
	reply.insert(reply.end(), req.outBytes, req.outBytes + req.outLen);
	free(req.outBytes);
	return KPIHOST64_STATUS_OK;
}

uint32_t VstHost64_LiveSetState(uint32_t part1to32, uint32_t which,
	const uint8_t* bytes, uint32_t len)
{
	if (part1to32 < 1 || part1to32 > 32 || which > 1) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!bytes || !len) return KPIHOST64_STATUS_BAD_REQUEST;
	if (!EnsureUiThread()) return KPIHOST64_STATUS_FAIL;
	UiStateRequest req = {};
	req.part = (int)part1to32;
	req.which = (int)which;
	req.inBytes = bytes;
	req.inLen = (int)len;
	const LRESULT rc = SendMessageW(g_uiWnd, UIMSG_STATE_SET, 0, (LPARAM)&req);
	return rc ? KPIHOST64_STATUS_OK : KPIHOST64_STATUS_FAIL;
}
