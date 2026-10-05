/* kbsasami.kpi 専用の VST ホスト。ogg の \\.\pipe\ogg_kpi32 には触らない。
   Win32 ビルドが kbsasami_host32、x64 が kbsasami_host64。
   中身は本体と同じ VstMidiEngine（juicysf 含む）。MIDI マッパーへは落とさない。 */
#include "stdafx.h"
#include "kb_sasami/source/kbsasami_monhost.h"
#include "UiTickPump.h"
#include "../kpi_host_ipc.h"
#include "KpiHost32Vst.h"
#include "KpiHost32VstLive.h"

#include <new>
#include <string>
#include <vector>

#ifdef _WIN64
static const wchar_t* const kPipeName = L"\\\\.\\pipe\\kbsasami_vst64";
#else
static const wchar_t* const kPipeName = L"\\\\.\\pipe\\kbsasami_vst32";
#endif

extern volatile LONG g_appExiting;

static void PumpServeMsgs()
{
	MSG msg;
	while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			KbsHostMonRequestQuit();
			continue;
		}
		if (InterlockedCompareExchange(&g_appExiting, 0, 0)) {
			if (msg.message == WM_PAINT) {
				if (msg.hwnd)
					ValidateRect(msg.hwnd, NULL);
				continue;
			}
			if (msg.message == WM_UITICK_VSYNC)
				continue;
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

static HANDLE g_clientProc = NULL;

static void WatchClientPid(ULONG pid)
{
	if (g_clientProc) {
		CloseHandle(g_clientProc);
		g_clientProc = NULL;
	}
	if (pid)
		g_clientProc = OpenProcess(SYNCHRONIZE, FALSE, pid);
}

static int ClientAlive()
{
	if (!g_clientProc) return 0;
	return (WaitForSingleObject(g_clientProc, 0) != WAIT_OBJECT_0) ? 1 : 0;
}

static bool XferExact(HANDLE h, void* buf, DWORD bytes, int writing)
{
	uint8_t* p = (uint8_t*)buf;
	DWORD remain = bytes;
	while (remain) {
		OVERLAPPED ov{};
		ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (!ov.hEvent) return false;
		DWORD got = 0;
		BOOL ok = writing
			? WriteFile(h, p, remain, &got, &ov)
			: ReadFile(h, p, remain, &got, &ov);
		if (!ok && GetLastError() != ERROR_IO_PENDING) {
			CloseHandle(ov.hEvent);
			return false;
		}
		if (!ok) {
			for (;;) {
				HANDLE waits[2];
				DWORD n = 1;
				waits[0] = ov.hEvent;
				if (g_clientProc)
					waits[n++] = g_clientProc;
				const DWORD w = MsgWaitForMultipleObjects(n, waits, FALSE, 250, QS_ALLINPUT);
				if (w == WAIT_OBJECT_0)
					break;
				if (g_clientProc && (w == WAIT_OBJECT_0 + 1 || !ClientAlive())) {
					CancelIoEx(h, &ov);
					WaitForSingleObject(ov.hEvent, 200);
					CloseHandle(ov.hEvent);
					return false;
				}
				if (w == WAIT_OBJECT_0 + n) {
					PumpServeMsgs();
					continue;
				}
				if (w == WAIT_TIMEOUT) {
					PumpServeMsgs();
					if (!ClientAlive() && g_clientProc) {
						CancelIoEx(h, &ov);
						WaitForSingleObject(ov.hEvent, 200);
						CloseHandle(ov.hEvent);
						return false;
					}
					continue;
				}
				CancelIoEx(h, &ov);
				WaitForSingleObject(ov.hEvent, INFINITE);
				CloseHandle(ov.hEvent);
				return false;
			}
			if (!GetOverlappedResult(h, &ov, &got, FALSE)) {
				CloseHandle(ov.hEvent);
				return false;
			}
		}
		CloseHandle(ov.hEvent);
		if (!got) return false;
		p += got;
		remain -= got;
	}
	return true;
}

static bool ReadExact(HANDLE h, void* buf, DWORD bytes) { return XferExact(h, buf, bytes, 0); }
static bool WriteExact(HANDLE h, const void* buf, DWORD bytes) { return XferExact(h, (void*)buf, bytes, 1); }

static void SendReply(HANDLE pipe, uint32_t cmd, uint32_t reqId, uint32_t status,
	const void* payload, uint32_t payloadBytes)
{
	KPIHOST32_ReplyHeader rh{};
	rh.cmd = cmd;
	rh.requestId = reqId;
	rh.status = status;
	rh.payloadBytes = payloadBytes;
	WriteExact(pipe, &rh, sizeof(rh));
	if (payloadBytes && payload) WriteExact(pipe, payload, payloadBytes);
}

static void ServeOnce(HANDLE pipe)
{
	std::vector<uint8_t> payload;
	std::vector<uint8_t> reply;
	for (;;) {
		KPIHOST32_MsgHeader h{};
		if (!ReadExact(pipe, &h, sizeof(h))) break;
		if (h.payloadBytes > 16u * 1024u * 1024u) break;
		payload.resize(h.payloadBytes ? h.payloadBytes : 1);
		if (h.payloadBytes && !ReadExact(pipe, payload.data(), h.payloadBytes)) break;

		reply.clear();
		uint32_t status = KPIHOST32_STATUS_FAIL;
		const uint8_t* p = payload.data();
		const uint8_t* end = p + h.payloadBytes;

		switch (h.cmd) {
		case KPIHOST32_CMD_PING:
			if (h.payloadBytes >= sizeof(uint32_t)) {
				int lang = (int)*(const uint32_t*)p;
				if (lang < 0 || lang > 13) lang = 1;
				savedata.lang = lang;
			}
			status = KPIHOST32_STATUS_OK;
			break;
		case KPIHOST32_CMD_VST_OPEN: {
			if ((size_t)(end - p) < sizeof(uint32_t) * 2) { status = KPIHOST32_STATUS_BAD_REQUEST; break; }
			uint32_t slot = *(const uint32_t*)p; p += sizeof(uint32_t);
			if (slot > 1) slot = 0;
			uint32_t nMid = *(const uint32_t*)p; p += sizeof(uint32_t);
			if ((size_t)(end - p) < nMid * sizeof(wchar_t)) { status = KPIHOST32_STATUS_BAD_REQUEST; break; }
			std::wstring mid((const wchar_t*)p, (const wchar_t*)p + nMid);
			p += nMid * sizeof(wchar_t);
			std::wstring dll, extra;
			if ((size_t)(end - p) >= sizeof(uint32_t)) {
				uint32_t nDll = *(const uint32_t*)p; p += sizeof(uint32_t);
				if ((size_t)(end - p) < nDll * sizeof(wchar_t)) { status = KPIHOST32_STATUS_BAD_REQUEST; break; }
				dll.assign((const wchar_t*)p, (const wchar_t*)p + nDll);
				p += nDll * sizeof(wchar_t);
			}
			if ((size_t)(end - p) >= sizeof(uint32_t)) {
				uint32_t nEx = *(const uint32_t*)p; p += sizeof(uint32_t);
				if ((size_t)(end - p) < nEx * sizeof(wchar_t)) { status = KPIHOST32_STATUS_BAD_REQUEST; break; }
				extra.assign((const wchar_t*)p, (const wchar_t*)p + nEx);
			}
			status = VstHost32_Open((int)slot, mid.c_str(),
				dll.empty() ? nullptr : dll.c_str(),
				extra.empty() ? nullptr : extra.c_str());
			if (status == KPIHOST32_STATUS_OK) {
				KPIHOST32_ForeignOpenReply orp{};
				orp.sessionId = slot;
				orp.sampleRate = (uint32_t)VstHost32_Rate((int)slot);
				orp.channels = (uint32_t)VstHost32_Channels((int)slot);
				orp.bitsPerSample = VstHost32_Bits((int)slot);
				orp.lengthSamples = VstHost32_Length((int)slot);
				orp.latencySamples = (uint32_t)VstHost32_Latency((int)slot);
				reply.resize(sizeof(orp));
				memcpy(reply.data(), &orp, sizeof(orp));
			}
			break;
		}
		case KPIHOST32_CMD_VST_RENDER: {
			if ((size_t)(end - p) < sizeof(KPIHOST32_RenderReq)) { status = KPIHOST32_STATUS_BAD_REQUEST; break; }
			auto* rr = (const KPIHOST32_RenderReq*)p;
			int slot = (rr->sessionId == 1) ? 1 : 0;
			uint32_t eof = 0, gotBytes = 0;
			uint32_t want = rr->bytesWanted;
			if (want > 4u * 1024u * 1024u) want = 4u * 1024u * 1024u;
			reply.resize(sizeof(KPIHOST32_RenderReply) + want);
			status = VstHost32_Render(slot, want,
				reply.data() + sizeof(KPIHOST32_RenderReply), want, gotBytes, eof);
			if (status == KPIHOST32_STATUS_OK) {
				if (gotBytes > want) gotBytes = want;
				KPIHOST32_RenderReply rrep{};
				rrep.sessionId = rr->sessionId;
				rrep.bytesReturned = gotBytes;
				rrep.eof = eof;
				reply.resize(sizeof(rrep) + gotBytes);
				memcpy(reply.data(), &rrep, sizeof(rrep));
			} else {
				reply.clear();
			}
			break;
		}
		case KPIHOST32_CMD_VST_SEEK: {
			if ((size_t)(end - p) < sizeof(KPIHOST32_SeekReq)) { status = KPIHOST32_STATUS_BAD_REQUEST; break; }
			auto* sr = (const KPIHOST32_SeekReq*)p;
			int slot = (sr->sessionId == 1) ? 1 : 0;
			status = VstHost32_Seek(slot, sr->posSample);
			if (status == KPIHOST32_STATUS_OK) {
				KPIHOST32_SeekReply srep{};
				srep.sessionId = sr->sessionId;
				srep.newPosSample = sr->posSample;
				reply.resize(sizeof(srep));
				memcpy(reply.data(), &srep, sizeof(srep));
			}
			break;
		}
		case KPIHOST32_CMD_VST_CLOSE: {
			if ((size_t)(end - p) >= sizeof(KPIHOST32_U32)) {
				auto* u = (const KPIHOST32_U32*)p;
				status = VstHost32_Close((int)u->v);
			} else {
				status = VstHost32_CloseAll();
			}
			break;
		}
		case KPIHOST32_CMD_MON_SHOW:
			/* プラグインが raira=0 かつ fmmidimonitor=1 のときだけ送る。ogg からは来ない。 */
			KbsHostMonShow();
			status = KPIHOST32_STATUS_OK;
			break;
		case KPIHOST32_CMD_MON_HIDE:
			KbsHostMonHide();
			status = KPIHOST32_STATUS_OK;
			break;
		default:
			status = KPIHOST32_STATUS_NOT_SUPPORTED;
			break;
		}
		SendReply(pipe, h.cmd, h.requestId, status, reply.data(), (uint32_t)reply.size());
	}
}

int wmain()
{
	KbsHostMonStartup();
	{
		wchar_t ud[MAX_PATH];
		ud[0] = 0;
		GetTempPathW(MAX_PATH, ud);
		wcsncat_s(ud, L"kbsasami_host_webview2", _TRUNCATE);
		CreateDirectoryW(ud, NULL);
		SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", ud);
		SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
			L"--disable-gpu --disable-gpu-compositing");
	}

	HANDLE pipe = CreateNamedPipeW(
		kPipeName,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,
		1024 * 1024,
		1024 * 1024,
		0,
		NULL);
	if (pipe == INVALID_HANDLE_VALUE) return 2;

	const DWORD idleMs = 30000;
	for (;;) {
		OVERLAPPED ov{};
		ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (!ov.hEvent) break;
		BOOL ok = ConnectNamedPipe(pipe, &ov);
		if (!ok) {
			DWORD err = GetLastError();
			if (err == ERROR_PIPE_CONNECTED)
				SetEvent(ov.hEvent);
			else if (err != ERROR_IO_PENDING) {
				CloseHandle(ov.hEvent);
				break;
			}
		}
		DWORD waited = 0;
		DWORD wr = WAIT_TIMEOUT;
		while (waited < idleMs) {
			const DWORD slice = 15;
			wr = MsgWaitForMultipleObjects(1, &ov.hEvent, FALSE, slice, QS_ALLINPUT);
			if (wr == WAIT_OBJECT_0)
				break;
			PumpServeMsgs();
			if (g_clientProc && !ClientAlive()) {
				wr = WAIT_TIMEOUT;
				break;
			}
			if (wr != WAIT_OBJECT_0 + 1 && wr != WAIT_TIMEOUT)
				break;
			waited += slice;
		}
		if (wr != WAIT_OBJECT_0) {
			CancelIoEx(pipe, &ov);
			CloseHandle(ov.hEvent);
			if (!ClientAlive() || (!VstHost32_LiveActive() && !KbsHostMonHolding())) {
				KbsHostMonQuit();
				if (VstHost32_SongActive())
					(void)VstHost32_CloseAll();
				break;
			}
			continue;
		}
		CloseHandle(ov.hEvent);
		ULONG clientPid = 0;
		GetNamedPipeClientProcessId(pipe, &clientPid);
		WatchClientPid(clientPid);
		ServeOnce(pipe);
		DisconnectNamedPipe(pipe);
		if (!ClientAlive() || !KbsHostMonHolding()) {
			KbsHostMonQuit();
			if (VstHost32_LiveActive())
				(void)VstHost32_LiveUnloadAll();
			(void)VstHost32_CloseAll();
			break;
		}
		if (VstHost32_LiveActive())
			(void)VstHost32_LiveUnloadAll();
	}
	if (g_clientProc) {
		CloseHandle(g_clientProc);
		g_clientProc = NULL;
	}
	(void)VstHost32_CloseAll();
	CloseHandle(pipe);
	return 0;
}
