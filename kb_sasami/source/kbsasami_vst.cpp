#include <windows.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

#include "kbsasami_vst.h"
#include "kbsasami_lang.h"
#include "../../kpi_host_ipc.h"

extern HINSTANCE g_hKpi;

static const wchar_t* const kPipe32 = L"\\\\.\\pipe\\kbsasami_vst32";
static const wchar_t* const kPipe64 = L"\\\\.\\pipe\\kbsasami_vst64";
static const wchar_t* const kExe32 = L"kbsasami_host32.exe";
static const wchar_t* const kExe64 = L"kbsasami_host64.exe";

struct HostConn {
	CRITICAL_SECTION cs;
	HANDLE pipe;
	HANDLE process;
	int slotUsed[2];
	uint32_t reqId;
	int ready;
};

static HostConn g_host[2];
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK InitHostOnce(PINIT_ONCE, PVOID, PVOID*)
{
	for (int i = 0; i < 2; ++i) {
		InitializeCriticalSection(&g_host[i].cs);
		g_host[i].pipe = INVALID_HANDLE_VALUE;
		g_host[i].process = NULL;
		g_host[i].slotUsed[0] = g_host[i].slotUsed[1] = 0;
		g_host[i].reqId = 1;
		g_host[i].ready = 1;
	}
	return TRUE;
}

static HostConn* HostOf(int arch)
{
	InitOnceExecuteOnce(&g_once, InitHostOnce, NULL, NULL);
	return (arch == 64) ? &g_host[1] : &g_host[0];
}

static int FileExistsW(const wchar_t* p)
{
	if (!p || !p[0]) return 0;
	DWORD a = GetFileAttributesW(p);
	return (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
}

static int PeArch(const wchar_t* path)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	IMAGE_DOS_HEADER dos{};
	DWORD n = 0;
	int arch = 0;
	if (ReadFile(h, &dos, sizeof(dos), &n, NULL) && n == sizeof(dos) &&
		dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0) {
		if (SetFilePointer(h, dos.e_lfanew, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
			DWORD sig = 0;
			IMAGE_FILE_HEADER fh{};
			if (ReadFile(h, &sig, 4, &n, NULL) && sig == IMAGE_NT_SIGNATURE &&
				ReadFile(h, &fh, sizeof(fh), &n, NULL) && n == sizeof(fh)) {
				if (fh.Machine == IMAGE_FILE_MACHINE_I386) arch = 32;
				else if (fh.Machine == IMAGE_FILE_MACHINE_AMD64) arch = 64;
			}
		}
	}
	CloseHandle(h);
	return arch;
}

static void DirOf(HMODULE mod, wchar_t* dir, int n)
{
	dir[0] = 0;
	if (!GetModuleFileNameW(mod, dir, n)) { dir[0] = 0; return; }
	wchar_t* sl = wcsrchr(dir, L'\\');
	if (sl) sl[1] = 0;
	else dir[0] = 0;
}

static int JoinExists(wchar_t* out, int outN, const wchar_t* dir, const wchar_t* name)
{
	if (!dir || !dir[0] || !name) return 0;
	_snwprintf_s(out, outN, _TRUNCATE, L"%s%s", dir, name);
	return FileExistsW(out);
}

static int FindHostExe(int arch, wchar_t* out, int outN)
{
	const wchar_t* name = (arch == 64) ? kExe64 : kExe32;
	wchar_t dir[MAX_PATH];
	DirOf(g_hKpi, dir, MAX_PATH);
	if (JoinExists(out, outN, dir, name)) return 1;
	wchar_t sub[MAX_PATH];
	_snwprintf_s(sub, _TRUNCATE, L"%sx64\\", dir);
	if (JoinExists(out, outN, sub, name)) return 1;
	if (dir[0]) {
		wchar_t parent[MAX_PATH];
		wcsncpy_s(parent, dir, _TRUNCATE);
		size_t L = wcslen(parent);
		if (L && (parent[L - 1] == L'\\' || parent[L - 1] == L'/')) parent[L - 1] = 0;
		wchar_t* sl = wcsrchr(parent, L'\\');
		if (sl) {
			sl[1] = 0;
			if (JoinExists(out, outN, parent, name)) return 1;
			_snwprintf_s(sub, _TRUNCATE, L"%sx64\\", parent);
			if (JoinExists(out, outN, sub, name)) return 1;
		}
	}
	DirOf(NULL, dir, MAX_PATH);
	if (JoinExists(out, outN, dir, name)) return 1;
	_snwprintf_s(sub, _TRUNCATE, L"%sPlugins\\kbsasami\\", dir);
	if (JoinExists(out, outN, sub, name)) return 1;
	_snwprintf_s(sub, _TRUNCATE, L"%sPlugins\\kbsasami\\x64\\", dir);
	if (JoinExists(out, outN, sub, name)) return 1;
	out[0] = 0;
	return 0;
}

/* ホストの SmfBytesHasXgReset と同じ Yamaha XG reset。これで 32/64 のどちらの exe を起こすか決める。 */
static int SmfLooksXg(const BYTE* data, uint32_t size)
{
	if (!data || size < 8) return 0;
	for (uint32_t i = 0; i + 6 < size; ++i) {
		if (data[i] == 0x43 && (data[i + 1] & 0xf0) == 0x10 &&
			data[i + 2] == 0x4c && data[i + 3] == 0x00 &&
			data[i + 4] == 0x00 && data[i + 5] == 0x7e)
			return 1;
	}
	return 0;
}

static int Xfer(HANDLE h, void* buf, DWORD bytes, int writing)
{
	uint8_t* p = (uint8_t*)buf;
	DWORD remain = bytes;
	while (remain) {
		OVERLAPPED ov{};
		ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (!ov.hEvent) return 0;
		DWORD got = 0;
		BOOL ok = writing ? WriteFile(h, p, remain, &got, &ov)
			: ReadFile(h, p, remain, &got, &ov);
		if (!ok && GetLastError() != ERROR_IO_PENDING) {
			CloseHandle(ov.hEvent);
			return 0;
		}
		if (!ok) {
			if (WaitForSingleObject(ov.hEvent, 15000) != WAIT_OBJECT_0) {
				CancelIoEx(h, &ov);
				WaitForSingleObject(ov.hEvent, 500);
				CloseHandle(ov.hEvent);
				return 0;
			}
			if (!GetOverlappedResult(h, &ov, &got, FALSE)) {
				CloseHandle(ov.hEvent);
				return 0;
			}
		}
		CloseHandle(ov.hEvent);
		if (!got) return 0;
		p += got;
		remain -= got;
	}
	return 1;
}

static void DropPipe(HostConn* c)
{
	if (c->pipe != INVALID_HANDLE_VALUE) {
		CloseHandle(c->pipe);
		c->pipe = INVALID_HANDLE_VALUE;
	}
}

void KbVstDisconnect()
{
	InitOnceExecuteOnce(&g_once, InitHostOnce, NULL, NULL);
	for (int i = 0; i < 2; ++i) {
		HostConn* c = &g_host[i];
		EnterCriticalSection(&c->cs);
		DropPipe(c);
		LeaveCriticalSection(&c->cs);
	}
}

static int EnsurePipe(HostConn* c, int arch)
{
	if (c->pipe != INVALID_HANDLE_VALUE) return 1;
	const wchar_t* pipeName = (arch == 64) ? kPipe64 : kPipe32;
	c->pipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (c->pipe != INVALID_HANDLE_VALUE) return 1;

	wchar_t exe[MAX_PATH];
	if (!FindHostExe(arch, exe, MAX_PATH)) return 0;
	wchar_t dir[MAX_PATH];
	wcsncpy_s(dir, exe, _TRUNCATE);
	wchar_t* sl = wcsrchr(dir, L'\\');
	if (sl) sl[1] = 0;

	wchar_t cmd[MAX_PATH + 32];
	_snwprintf_s(cmd, _TRUNCATE, L"\"%s\"", exe);
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
		dir[0] ? dir : NULL, &si, &pi))
		return 0;
	CloseHandle(pi.hThread);
	if (c->process) CloseHandle(c->process);
	c->process = pi.hProcess;

	for (int i = 0; i < 50; ++i) {
		if (WaitNamedPipeW(pipeName, 100)) {
			c->pipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL,
				OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
			if (c->pipe != INVALID_HANDLE_VALUE) return 1;
		}
		if (WaitForSingleObject(c->process, 0) == WAIT_OBJECT_0) break;
		Sleep(100);
	}
	return 0;
}

static int SendReq(HostConn* c, uint32_t cmd, const void* payload, uint32_t payloadBytes,
	uint8_t* reply, uint32_t replyCap, uint32_t* replyN, uint32_t* status)
{
	*replyN = 0;
	*status = KPIHOST32_STATUS_FAIL;
	KPIHOST32_MsgHeader h{};
	h.cmd = cmd;
	h.requestId = c->reqId++;
	if (!c->reqId) c->reqId = 1;
	h.payloadBytes = payloadBytes;
	if (!Xfer(c->pipe, &h, sizeof(h), 1)) return 0;
	if (payloadBytes && !Xfer(c->pipe, (void*)payload, payloadBytes, 1)) return 0;
	KPIHOST32_ReplyHeader rh{};
	if (!Xfer(c->pipe, &rh, sizeof(rh), 0)) return 0;
	if (rh.requestId != h.requestId || rh.cmd != cmd) return 0;
	if (rh.payloadBytes > replyCap) return 0;
	if (rh.payloadBytes && !Xfer(c->pipe, reply, rh.payloadBytes, 0)) return 0;
	*replyN = rh.payloadBytes;
	*status = rh.status;
	return 1;
}

static void PushLang(HostConn* c)
{
	uint32_t lang = (uint32_t)KbsDetectUiLang();
	uint8_t reply[8];
	uint32_t rn = 0, st = 0;
	SendReq(c, KPIHOST32_CMD_PING, &lang, sizeof(lang), reply, sizeof(reply), &rn, &st);
}

int KbVstMonCmd(int show)
{
#ifdef _WIN64
	const int arch = 64;
#else
	const int arch = 32;
#endif
	HostConn* c = HostOf(arch);
	EnterCriticalSection(&c->cs);
	if (!EnsurePipe(c, arch)) {
		LeaveCriticalSection(&c->cs);
		return 0;
	}
	uint8_t reply[16];
	uint32_t replyN = 0, st = 0;
	PushLang(c);
	const uint32_t cmd = show ? KPIHOST32_CMD_MON_SHOW : KPIHOST32_CMD_MON_HIDE;
	const int ok = SendReq(c, cmd, NULL, 0, reply, sizeof(reply), &replyN, &st);
	LeaveCriticalSection(&c->cs);
	return (ok && st == KPIHOST32_STATUS_OK) ? 1 : 0;
}

int KbVstSessionOpen(const void* smf, uint32_t smfLen,
	const wchar_t* gsPath, const wchar_t* xgPath, KbVstSession* out)
{
	if (!out || !smf || smfLen < 22) return 0;
	memset(out, 0, sizeof(*out));
	const int wantXg = SmfLooksXg((const BYTE*)smf, smfLen);
	const wchar_t* use = wantXg ? xgPath : gsPath;
	if (!use || !use[0] || !FileExistsW(use)) return 0;
	const int arch = PeArch(use);
	if (arch != 32 && arch != 64) return 0;

	wchar_t tempDir[MAX_PATH];
	if (!GetTempPathW(MAX_PATH, tempDir)) return 0;
	wchar_t tempFile[MAX_PATH];
	if (!GetTempFileNameW(tempDir, L"ksv", 0, tempFile)) return 0;
	HANDLE hf = CreateFileW(tempFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
	if (hf == INVALID_HANDLE_VALUE) { DeleteFileW(tempFile); return 0; }
	DWORD wr = 0;
	BOOL wok = WriteFile(hf, smf, smfLen, &wr, NULL);
	CloseHandle(hf);
	if (!wok || wr != smfLen) { DeleteFileW(tempFile); return 0; }

	HostConn* c = HostOf(arch);
	EnterCriticalSection(&c->cs);
	int slot = -1;
	for (int i = 0; i < 2; ++i) if (!c->slotUsed[i]) { slot = i; break; }
	if (slot < 0 || !EnsurePipe(c, arch)) {
		LeaveCriticalSection(&c->cs);
		DeleteFileW(tempFile);
		return 0;
	}

	std::wstring mid = tempFile;
	std::wstring gs = gsPath ? gsPath : L"";
	std::wstring xg = xgPath ? xgPath : L"";
	/* ホストは GS=vstMultiDll、XG=vstExtraPath。曲側が無い方は空のまま（フォールバックしない）。 */
	if (wantXg) gs.clear();
	else xg.clear();
	uint32_t nMid = (uint32_t)mid.size();
	uint32_t nGs = (uint32_t)gs.size();
	uint32_t nXg = (uint32_t)xg.size();
	uint32_t slotU = (uint32_t)slot;
	std::vector<uint8_t> req;
	req.resize(sizeof(uint32_t) * 4 + (nMid + nGs + nXg) * sizeof(wchar_t));
	uint8_t* p = req.data();
	memcpy(p, &slotU, 4); p += 4;
	memcpy(p, &nMid, 4); p += 4;
	if (nMid) { memcpy(p, mid.c_str(), nMid * sizeof(wchar_t)); p += nMid * sizeof(wchar_t); }
	memcpy(p, &nGs, 4); p += 4;
	if (nGs) { memcpy(p, gs.c_str(), nGs * sizeof(wchar_t)); p += nGs * sizeof(wchar_t); }
	memcpy(p, &nXg, 4); p += 4;
	if (nXg) memcpy(p, xg.c_str(), nXg * sizeof(wchar_t));

	uint8_t reply[256];
	uint32_t replyN = 0, st = 0;
	PushLang(c);
	int ok = SendReq(c, KPIHOST32_CMD_VST_OPEN, req.data(), (uint32_t)req.size(),
		reply, sizeof(reply), &replyN, &st);
	if (!ok || st != KPIHOST32_STATUS_OK || replyN < sizeof(KPIHOST32_ForeignOpenReply)) {
		DropPipe(c);
		LeaveCriticalSection(&c->cs);
		DeleteFileW(tempFile);
		return 0;
	}
	KPIHOST32_ForeignOpenReply orp{};
	memcpy(&orp, reply, sizeof(orp));
	if (orp.channels != 2 || orp.bitsPerSample != 16 || orp.sampleRate < 8000) {
		KPIHOST32_U32 u{};
		u.v = slotU;
		uint32_t rn = 0, rst = 0;
		uint8_t dump[16];
		SendReq(c, KPIHOST32_CMD_VST_CLOSE, &u, sizeof(u), dump, sizeof(dump), &rn, &rst);
		LeaveCriticalSection(&c->cs);
		DeleteFileW(tempFile);
		return 0;
	}
	c->slotUsed[slot] = 1;
	LeaveCriticalSection(&c->cs);

	out->open = 1;
	out->arch = arch;
	out->slot = slot;
	out->rate = orp.sampleRate;
	out->lengthSamples = orp.lengthSamples;
	wcsncpy_s(out->tempMid, tempFile, _TRUNCATE);
	return 1;
}

int KbVstSessionRender(KbVstSession* s, void* pcm, uint32_t bytes, uint32_t* got, uint32_t* eof)
{
	if (got) *got = 0;
	if (eof) *eof = 0;
	if (!s || !s->open || !pcm || !bytes) return 0;
	HostConn* c = HostOf(s->arch);
	EnterCriticalSection(&c->cs);
	if (c->pipe == INVALID_HANDLE_VALUE) { LeaveCriticalSection(&c->cs); return 0; }
	KPIHOST32_RenderReq rr{};
	rr.sessionId = (uint32_t)s->slot;
	rr.bytesWanted = bytes;
	uint32_t cap = sizeof(KPIHOST32_RenderReply) + bytes;
	uint8_t* reply = (uint8_t*)malloc(cap);
	if (!reply) { LeaveCriticalSection(&c->cs); return 0; }
	uint32_t replyN = 0, st = 0;
	int ok = SendReq(c, KPIHOST32_CMD_VST_RENDER, &rr, sizeof(rr), reply, cap, &replyN, &st);
	if (!ok || st != KPIHOST32_STATUS_OK || replyN < sizeof(KPIHOST32_RenderReply)) {
		free(reply);
		DropPipe(c);
		LeaveCriticalSection(&c->cs);
		return 0;
	}
	KPIHOST32_RenderReply rrep{};
	memcpy(&rrep, reply, sizeof(rrep));
	uint32_t n = rrep.bytesReturned;
	if (n > bytes) n = bytes;
	if (replyN < sizeof(rrep) + n) n = (replyN > sizeof(rrep)) ? (uint32_t)(replyN - sizeof(rrep)) : 0;
	if (n) memcpy(pcm, reply + sizeof(rrep), n);
	free(reply);
	LeaveCriticalSection(&c->cs);
	if (got) *got = n;
	if (eof) *eof = rrep.eof;
	return 1;
}

int KbVstSessionSeek(KbVstSession* s, uint64_t sample)
{
	if (!s || !s->open) return 0;
	HostConn* c = HostOf(s->arch);
	EnterCriticalSection(&c->cs);
	if (c->pipe == INVALID_HANDLE_VALUE) { LeaveCriticalSection(&c->cs); return 0; }
	KPIHOST32_SeekReq sr{};
	sr.sessionId = (uint32_t)s->slot;
	sr.posSample = sample;
	uint8_t reply[64];
	uint32_t replyN = 0, st = 0;
	int ok = SendReq(c, KPIHOST32_CMD_VST_SEEK, &sr, sizeof(sr), reply, sizeof(reply), &replyN, &st);
	if (!ok || st != KPIHOST32_STATUS_OK) {
		DropPipe(c);
		LeaveCriticalSection(&c->cs);
		return 0;
	}
	LeaveCriticalSection(&c->cs);
	return 1;
}

void KbVstSessionClose(KbVstSession* s)
{
	if (!s || !s->open) return;
	HostConn* c = HostOf(s->arch);
	EnterCriticalSection(&c->cs);
	if (c->pipe != INVALID_HANDLE_VALUE && s->slot >= 0 && s->slot < 2) {
		KPIHOST32_U32 u{};
		u.v = (uint32_t)s->slot;
		uint8_t reply[32];
		uint32_t replyN = 0, st = 0;
		if (!SendReq(c, KPIHOST32_CMD_VST_CLOSE, &u, sizeof(u), reply, sizeof(reply), &replyN, &st))
			DropPipe(c);
		c->slotUsed[s->slot] = 0;
		/* パイプは残す。切るとホストが ServeOnce を抜け、モニタを破棄する。 */
	}
	LeaveCriticalSection(&c->cs);
	if (s->tempMid[0]) DeleteFileW(s->tempMid);
	s->open = 0;
	s->tempMid[0] = 0;
}
