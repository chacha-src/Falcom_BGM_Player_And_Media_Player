#include "stdafx.h"

#include "KpiHostClient.h"
#include <tlhelp32.h>

// %TEMP%\ogg_kpi64_client.log。ホスト側の ogg_kpi64_host.log と対。
static void AppendLogLine(const wchar_t* line)
{
	if (!line) return;
	wchar_t tempDir[MAX_PATH]{};
	if (!GetTempPathW(MAX_PATH, tempDir)) return;
	std::wstring path = tempDir;
	path += L"ogg_kpi64_client.log";
	HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	SYSTEMTIME st{};
	GetLocalTime(&st);
	wchar_t stamp[48];
	swprintf_s(stamp, L"%04d-%02d-%02d %02d:%02d:%02d.%03d ",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	// One write keeps interleaved lines from other threads readable.
	std::wstring rec = stamp;
	rec += line;
	rec += L"\r\n";
	DWORD written = 0;
	WriteFile(h, rec.c_str(), (DWORD)(rec.size() * sizeof(wchar_t)), &written, NULL);
	CloseHandle(h);
}

// リトルエンディアン u32。パイプペイロード用。
static void AppendU32(std::vector<uint8_t>& b, uint32_t v)
{
	b.push_back((uint8_t)(v & 0xFF));
	b.push_back((uint8_t)((v >> 8) & 0xFF));
	b.push_back((uint8_t)((v >> 16) & 0xFF));
	b.push_back((uint8_t)((v >> 24) & 0xFF));
}

// [u32 文字数][wchar_t[]]。ホストの ReadWString と対。
static void AppendWString(std::vector<uint8_t>& b, const std::wstring& s)
{
	AppendU32(b, (uint32_t)s.size());
	if (!s.empty()) {
		const uint8_t* p = (const uint8_t*)s.data();
		b.insert(b.end(), p, p + (s.size() * sizeof(wchar_t)));
	}
}

KpiHost64Client::KpiHost64Client() { InitializeCriticalSection(&m_cs); }
KpiHost64Client::~KpiHost64Client()
{
	Disconnect();
	DeleteCriticalSection(&m_cs);
}

static std::wstring JoinPath(const std::wstring& a, const std::wstring& b)
{
	if (a.empty()) return b;
	wchar_t last = a.back();
	if (last == L'\\' || last == L'/') return a + b;
	return a + L"\\" + b;
}

static bool FileExists(const std::wstring& p)
{
	DWORD a = GetFileAttributesW(p.c_str());
	return (a != INVALID_FILE_ATTRIBUTES) && ((a & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

static bool FindHostExeRecursive(const std::wstring& baseDir, std::wstring& outPath, int maxDepth = 6)
{
	outPath.clear();
	if (maxDepth < 0) return false;

	// quick check: baseDir\KpiHost64.exe
	{
		std::wstring direct = JoinPath(baseDir, L"KpiHost64.exe");
		if (FileExists(direct)) { outPath = direct; return true; }
	}

	std::wstring pattern = JoinPath(baseDir, L"*");
	WIN32_FIND_DATAW fd{};
	HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return false;

	do {
		if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;

		// skip reparse points (junctions) to avoid loops
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

		std::wstring subDir = JoinPath(baseDir, fd.cFileName);
		std::wstring candidate = JoinPath(subDir, L"KpiHost64.exe");
		if (FileExists(candidate)) {
			outPath = candidate;
			FindClose(h);
			return true;
		}
		if (FindHostExeRecursive(subDir, outPath, maxDepth - 1)) {
			FindClose(h);
			return true;
		}
	} while (FindNextFileW(h, &fd));

	FindClose(h);
	return false;
}

// パイプを閉じる。次の EnsureConnected でホストを起こし直す。
void KpiHost64Client::Disconnect()
{
	if (m_hPipe != INVALID_HANDLE_VALUE) {
		CloseHandle(m_hPipe);
		m_hPipe = INVALID_HANDLE_VALUE;
	}
	m_sentLang = -1;
}

// 本体 exe の隣（または配下）の KpiHost64.exe を CreateProcess。既に動いていればパイプ接続側が拾う。
bool KpiHost64Client::StartHostProcess()
{
	wchar_t exePath[MAX_PATH]{};
	GetModuleFileNameW(NULL, exePath, MAX_PATH);
	std::wstring dir = exePath;
	size_t pos = dir.find_last_of(L"\\/");
	if (pos != std::wstring::npos) dir = dir.substr(0, pos + 1);

	std::wstring hostExe;
	if (!FindHostExeRecursive(dir, hostExe)) {
		hostExe = L"C:\\projects\\APPLICATION3\\ogg_binary\\KpiHost64.exe";
	}
	if (!FileExists(hostExe)) return false;
	AppendLogLine((L"[StartHostProcess] hostExe=" + hostExe).c_str());

	std::wstring hostDir = hostExe;
	size_t hpos = hostDir.find_last_of(L"\\/");
	if (hpos != std::wstring::npos) hostDir = hostDir.substr(0, hpos + 1);

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	std::wstring cmd = L"\"" + hostExe + L"\" --pipe";
	std::vector<wchar_t> cmdline(cmd.begin(), cmd.end());
	cmdline.push_back(L'\0');
	BOOL ok = CreateProcessW(NULL, cmdline.data(), NULL, NULL, FALSE,
		CREATE_NO_WINDOW, NULL, hostDir.c_str(), &si, &pi);
	if (!ok) {
		DWORD e = GetLastError();
		AppendLogLine((L"[StartHostProcess] CreateProcessW failed err=" + std::to_wstring(e)).c_str());
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

static void KillStaleKpiHost64()
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return;
	PROCESSENTRY32W pe{};
	pe.dwSize = sizeof(pe);
	DWORD self = GetCurrentProcessId();
	if (Process32FirstW(snap, &pe)) {
		do {
			if (_wcsicmp(pe.szExeFile, L"KpiHost64.exe") != 0) continue;
			if (pe.th32ProcessID == self) continue;
			HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
			if (!p) continue;
			TerminateProcess(p, 1);
			CloseHandle(p);
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
}

bool KpiHost64Client::ConnectPipe(bool waitForHost)
{
	const int attempts = waitForHost ? 30 : 1;
	for (int i = 0; i < attempts; i++) {
		HANDLE h = CreateFileW(KPIHOST64_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
		if (h != INVALID_HANDLE_VALUE) {
			DWORD mode = PIPE_READMODE_BYTE;
			SetNamedPipeHandleState(h, &mode, NULL, NULL);
			m_hPipe = h;
			AppendLogLine(L"[ConnectPipe] connected");
			return true;
		}

		DWORD err = GetLastError();
		if (err == ERROR_PIPE_BUSY) {
			if (!WaitNamedPipeW(KPIHOST64_PIPE_NAME, 200)) Sleep(50);
			continue;
		}
		if (waitForHost) {
			AppendLogLine((L"[ConnectPipe] CreateFile failed err=" + std::to_wstring(err)).c_str());
			Sleep(50);
		}
	}
	return false;
}

static int HostLangCode()
{
	int lang = savedata.lang;
	if (lang < 0 || lang > 13) lang = 1;
	return lang;
}

bool KpiHost64Client::SyncHostLang()
{
	if (m_hPipe == INVALID_HANDLE_VALUE) return false;
	const int lang = HostLangCode();
	if (m_sentLang == lang) return true;
	if (m_syncingLang) return true;
	m_syncingLang = 1;
	m_sentLang = lang;
	uint32_t v = (uint32_t)lang;
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	const bool ok = SendRequest(KPIHOST64_CMD_PING, &v, sizeof(v), reply, st) &&
		st == KPIHOST64_STATUS_OK;
	m_syncingLang = 0;
	if (!ok) m_sentLang = -1;
	return ok;
}

// 未接続ならホスト起動→パイプ。接続済みなら言語だけ同期。
bool KpiHost64Client::EnsureConnected()
{
	if (m_hPipe == INVALID_HANDLE_VALUE) {
		if (!ConnectPipe(false)) {
			StartHostProcess();
			if (!ConnectPipe(true)) {
				KillStaleKpiHost64();
				StartHostProcess();
				if (!ConnectPipe(true)) return false;
			}
		}
		m_sentLang = -1;
	}
	if (m_syncingLang) return true;
	return SyncHostLang();
}

namespace {
struct PipeLock
{
	CRITICAL_SECTION* cs;
	explicit PipeLock(CRITICAL_SECTION& c) : cs(&c) { EnterCriticalSection(cs); }
	~PipeLock() { LeaveCriticalSection(cs); }
};

// パイプはバイトストリームなので、1 回の ReadFile が要求より短いことがある。
// それをエラーにすると曲の途中で切断し、再生スレッドが無音バッファを出す。
enum : uint32_t { PIPE_MAX_REPLY_BYTES = 64u * 1024u * 1024u };

/* During VST LIVE_LOAD, pump more UI so HALion/MediaBay in KpiHost64 can finish
   while our wait banner stays responsive (host path feels the same). */
static volatile long g_kpiPipeUiPump = 0;

extern "C" void KpiPipeSetUiPump(int on)
{
	InterlockedExchange(&g_kpiPipeUiPump, on ? 1 : 0);
}

bool PipeXfer(HANDLE pipe, void* buf, uint32_t bytes, int writing, DWORD timeoutMs)
{
	uint8_t* p = (uint8_t*)buf;
	const DWORD t0 = GetTickCount();
	while (bytes) {
		const DWORD elapsed = GetTickCount() - t0;
		if (elapsed >= timeoutMs) return false;
		const DWORD left = timeoutMs - elapsed;
		OVERLAPPED ov{};
		ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (!ov.hEvent) return false;
		DWORD got = 0;
		BOOL ok = writing
			? WriteFile(pipe, p, bytes, &got, &ov)
			: ReadFile(pipe, p, bytes, &got, &ov);
		if (!ok) {
			if (GetLastError() != ERROR_IO_PENDING) {
				CloseHandle(ov.hEvent);
				return false;
			}
			for (;;) {
				const DWORD elapsed2 = GetTickCount() - t0;
				if (elapsed2 >= timeoutMs) {
					CancelIoEx(pipe, &ov);
					WaitForSingleObject(ov.hEvent, 2000);
					CloseHandle(ov.hEvent);
					return false;
				}
				const DWORD slice = timeoutMs - elapsed2;
				const DWORD waitMs = slice > 100 ? 100 : slice;
				const DWORD qs = InterlockedCompareExchange(&g_kpiPipeUiPump, 0, 0)
					? (QS_ALLINPUT) : (QS_PAINT | QS_TIMER | QS_POSTMESSAGE);
				const DWORD wr = MsgWaitForMultipleObjects(1, &ov.hEvent, FALSE, waitMs, qs);
				if (wr == WAIT_OBJECT_0)
					break;
				if (wr == WAIT_OBJECT_0 + 1) {
					MSG msg;
					if (InterlockedCompareExchange(&g_kpiPipeUiPump, 0, 0)) {
						while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
							if (msg.message == WM_QUIT) {
								PostQuitMessage((int)msg.wParam);
								CancelIoEx(pipe, &ov);
								CloseHandle(ov.hEvent);
								return false;
							}
							TranslateMessage(&msg);
							DispatchMessageW(&msg);
						}
					} else {
						while (PeekMessageW(&msg, NULL, WM_PAINT, WM_PAINT, PM_REMOVE))
							DispatchMessageW(&msg);
						while (PeekMessageW(&msg, NULL, WM_TIMER, WM_TIMER, PM_REMOVE))
							DispatchMessageW(&msg);
						while (PeekMessageW(&msg, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {
							PostQuitMessage((int)msg.wParam);
							CancelIoEx(pipe, &ov);
							CloseHandle(ov.hEvent);
							return false;
						}
					}
					continue;
				}
				if (wr == WAIT_TIMEOUT)
					continue;
				CancelIoEx(pipe, &ov);
				WaitForSingleObject(ov.hEvent, 2000);
				CloseHandle(ov.hEvent);
				return false;
			}
			if (!GetOverlappedResult(pipe, &ov, &got, FALSE)) {
				CloseHandle(ov.hEvent);
				return false;
			}
		}
		CloseHandle(ov.hEvent);
		if (got == 0) return false;
		p += got;
		bytes -= got;
	}
	return true;
}

bool PipeReadAll(HANDLE pipe, void* buf, uint32_t bytes, DWORD timeoutMs)
{
	return PipeXfer(pipe, buf, bytes, 0, timeoutMs);
}

bool PipeWriteAll(HANDLE pipe, const void* buf, uint32_t bytes, DWORD timeoutMs)
{
	return PipeXfer(pipe, (void*)buf, bytes, 1, timeoutMs);
}

bool PipeSkip(HANDLE pipe, uint32_t bytes, DWORD timeoutMs)
{
	uint8_t scratch[4096];
	const DWORD t0 = GetTickCount();
	while (bytes) {
		const DWORD elapsed = GetTickCount() - t0;
		if (elapsed >= timeoutMs) return false;
		const uint32_t take = bytes < sizeof(scratch) ? bytes : (uint32_t)sizeof(scratch);
		if (!PipeReadAll(pipe, scratch, take, timeoutMs - elapsed)) return false;
		bytes -= take;
	}
	return true;
}
} // namespace

// ヘッダ＋ペイロードを送り、同じ requestId の応答を待つ。古い応答は捨てて再同期。
bool KpiHost64Client::SendRequest(uint32_t cmd, const void* payload, uint32_t payloadBytes,
	std::vector<uint8_t>& outReplyPayload, uint32_t& outStatus, DWORD timeoutMs)
{
	PipeLock lock(m_cs);
	outReplyPayload.clear();
	outStatus = KPIHOST64_STATUS_FAIL;
	if (!EnsureConnected()) return false;

	KPIHOST64_MsgHeader h{};
	h.cmd = cmd;
	h.requestId = m_reqId++;
	h.payloadBytes = payloadBytes;

	if (!PipeWriteAll(m_hPipe, &h, sizeof(h), timeoutMs)) {
		AppendLogLine(L"[SendRequest] write header failed");
		Disconnect();
		return false;
	}
	if (payloadBytes) {
		if (!PipeWriteAll(m_hPipe, payload, payloadBytes, timeoutMs)) {
			AppendLogLine(L"[SendRequest] write payload failed");
			Disconnect();
			return false;
		}
	}

	for (int attempt = 0; attempt < 16; ++attempt) {
		KPIHOST64_ReplyHeader rh{};
		if (!PipeReadAll(m_hPipe, &rh, sizeof(rh), timeoutMs)) {
			AppendLogLine(L"[SendRequest] read reply header failed");
			Disconnect();
			return false;
		}
		if (rh.payloadBytes > PIPE_MAX_REPLY_BYTES) {
			AppendLogLine((L"[SendRequest] absurd reply size " + std::to_wstring(rh.payloadBytes)).c_str());
			Disconnect();
			return false;
		}
		if (rh.requestId != h.requestId || rh.cmd != cmd) {
			AppendLogLine((L"[SendRequest] stale reply skipped: got cmd=" + std::to_wstring(rh.cmd) +
				L" id=" + std::to_wstring(rh.requestId) + L", want cmd=" + std::to_wstring(cmd) +
				L" id=" + std::to_wstring(h.requestId)).c_str());
			if (!PipeSkip(m_hPipe, rh.payloadBytes, timeoutMs)) {
				Disconnect();
				return false;
			}
			continue;
		}
		if (rh.payloadBytes) {
			outReplyPayload.resize(rh.payloadBytes);
			if (!PipeReadAll(m_hPipe, outReplyPayload.data(), rh.payloadBytes, timeoutMs)) {
				AppendLogLine(L"[SendRequest] read reply payload failed");
				outReplyPayload.clear();
				Disconnect();
				return false;
			}
		}
		outStatus = rh.status;
		return true;
	}
	AppendLogLine(L"[SendRequest] could not resync reply stream");
	Disconnect();
	return false;
}

bool KpiHost64Client::SendSimple(uint32_t cmd, const void* payload, uint32_t payloadBytes,
	DWORD timeoutMs)
{
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(cmd, payload, payloadBytes, reply, st, timeoutMs)) return false;
	return st == KPIHOST64_STATUS_OK;
}

bool KpiHost64Client::Ping()
{
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	uint32_t lang = (uint32_t)HostLangCode();
	if (!SendRequest(KPIHOST64_CMD_PING, &lang, sizeof(lang), reply, st)) return false;
	if (st == KPIHOST64_STATUS_OK) m_sentLang = (int)lang;
	return st == KPIHOST64_STATUS_OK;
}

bool KpiHost64Client::ListExts(const std::wstring& kpiPath, uint32_t& outKpiVer, std::wstring& outSupportExts)
{
	outKpiVer = 0;
	outSupportExts.clear();
	std::vector<uint8_t> req;
	AppendWString(req, kpiPath);
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_LIST_EXTS, req.data(), (uint32_t)req.size(), reply, st)) return false;
	if (st != KPIHOST64_STATUS_OK) return false;
	if (reply.size() < sizeof(KPIHOST64_ListExtsReply) + sizeof(uint32_t)) return false;
	const uint8_t* p = reply.data();
	const KPIHOST64_ListExtsReply* lr = (const KPIHOST64_ListExtsReply*)p;
	outKpiVer = lr->kpiVer;
	p += sizeof(KPIHOST64_ListExtsReply);
	uint32_t chars = *(const uint32_t*)p;
	p += sizeof(uint32_t);
	size_t bytes = (size_t)chars * sizeof(wchar_t);
	if (reply.size() < sizeof(KPIHOST64_ListExtsReply) + sizeof(uint32_t) + bytes) return false;
	outSupportExts.assign((const wchar_t*)p, (const wchar_t*)p + chars);
	return true;
}

bool KpiHost64Client::Open(const std::wstring& kpiPath, const std::wstring& mediaPath, const KPI_MEDIAINFO& request, uint32_t songNo, KpiHost64Session& outSession)
{
	outSession = {};
	std::vector<uint8_t> req;
	AppendU32(req, songNo);
	AppendWString(req, kpiPath);
	AppendWString(req, mediaPath);
	const uint8_t* pr = (const uint8_t*)&request;
	req.insert(req.end(), pr, pr + sizeof(KPI_MEDIAINFO));

	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_OPEN, req.data(), (uint32_t)req.size(), reply, st)) return false;
	if (st != KPIHOST64_STATUS_OK) return false;
	if (reply.size() < sizeof(KPIHOST64_OpenReply) + sizeof(KPI_MEDIAINFO)) return false;

	const KPIHOST64_OpenReply* orp = (const KPIHOST64_OpenReply*)reply.data();
	outSession.sessionId = orp->sessionId;
	outSession.openedSongCount = orp->openedSongCount;
	memcpy(&outSession.mediaInfo, reply.data() + sizeof(KPIHOST64_OpenReply), sizeof(KPI_MEDIAINFO));
	return true;
}

bool KpiHost64Client::RenderBytes(uint32_t sessionId, uint32_t bytesWanted, std::vector<uint8_t>& outPcm, bool& outEof)
{
	outPcm.clear();
	outEof = false;
	KPIHOST64_RenderReq rr{};
	rr.sessionId = sessionId;
	rr.bytesWanted = bytesWanted;
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_RENDER, &rr, sizeof(rr), reply, st)) return false;
	if (st != KPIHOST64_STATUS_OK) return false;
	if (reply.size() < sizeof(KPIHOST64_RenderReply)) return false;
	const KPIHOST64_RenderReply* rep = (const KPIHOST64_RenderReply*)reply.data();
	if (rep->sessionId != sessionId) return false;
	outEof = rep->eof ? true : false;
	if (reply.size() < sizeof(KPIHOST64_RenderReply) + rep->bytesReturned) return false;
	outPcm.assign(reply.begin() + sizeof(KPIHOST64_RenderReply), reply.begin() + sizeof(KPIHOST64_RenderReply) + rep->bytesReturned);
	return true;
}

bool KpiHost64Client::Seek(uint32_t sessionId, uint64_t posSample, uint32_t flag, uint64_t& outNewPosSample)
{
	outNewPosSample = 0;
	KPIHOST64_SeekReq sr{};
	sr.sessionId = sessionId;
	sr.posSample = posSample;
	sr.flag = flag;
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_SEEK, &sr, sizeof(sr), reply, st)) return false;
	if (st != KPIHOST64_STATUS_OK) return false;
	if (reply.size() < sizeof(KPIHOST64_SeekReply)) return false;
	const KPIHOST64_SeekReply* rep = (const KPIHOST64_SeekReply*)reply.data();
	if (rep->sessionId != sessionId) return false;
	outNewPosSample = rep->newPosSample;
	return true;
}

bool KpiHost64Client::Close(uint32_t sessionId)
{
	KPIHOST64_U32 u{};
	u.v = sessionId;
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_CLOSE, &u, sizeof(u), reply, st)) return false;
	return st == KPIHOST64_STATUS_OK;
}

bool KpiHost64Client::ForeignListExts(uint32_t pluginKind, const std::wstring& dllPath, std::wstring& outSupportExts)
{
	outSupportExts.clear();
	std::vector<uint8_t> req;
	AppendU32(req, pluginKind);
	AppendWString(req, dllPath);
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_FOREIGN_LIST_EXTS, req.data(), (uint32_t)req.size(), reply, st)) return false;
	if (st != KPIHOST64_STATUS_OK) return false;
	if (reply.size() < sizeof(KPIHOST64_ListExtsReply) + sizeof(uint32_t)) return false;
	const uint8_t* p = reply.data() + sizeof(KPIHOST64_ListExtsReply);
	uint32_t chars = *(const uint32_t*)p;
	p += sizeof(uint32_t);
	if (reply.size() < sizeof(KPIHOST64_ListExtsReply) + sizeof(uint32_t) + chars * sizeof(wchar_t)) return false;
	outSupportExts.assign((const wchar_t*)p, (const wchar_t*)p + chars);
	return true;
}

bool KpiHost64Client::ForeignOpen(uint32_t pluginKind, const std::wstring& dllPath, const std::wstring& mediaPath, KPIHOST64_ForeignOpenReply& out)
{
	ZeroMemory(&out, sizeof(out));
	std::vector<uint8_t> req;
	AppendU32(req, pluginKind);
	AppendWString(req, dllPath);
	AppendWString(req, mediaPath);
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_FOREIGN_OPEN, req.data(), (uint32_t)req.size(), reply, st)) return false;
	if (st != KPIHOST64_STATUS_OK) return false;
	if (reply.size() < sizeof(KPIHOST64_ForeignOpenReply)) return false;
	memcpy(&out, reply.data(), sizeof(out));
	return true;
}

bool KpiHost64Client::ForeignRender(uint32_t sessionId, uint32_t bytesWanted, std::vector<uint8_t>& outPcm, bool& outEof)
{
	outPcm.clear();
	outEof = false;
	KPIHOST64_RenderReq rr{};
	rr.sessionId = sessionId;
	rr.bytesWanted = bytesWanted;
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_FOREIGN_RENDER, &rr, sizeof(rr), reply, st)) return false;
	if (st != KPIHOST64_STATUS_OK) return false;
	if (reply.size() < sizeof(KPIHOST64_RenderReply)) return false;
	const KPIHOST64_RenderReply* rep = (const KPIHOST64_RenderReply*)reply.data();
	outEof = rep->eof ? true : false;
	if (reply.size() < sizeof(KPIHOST64_RenderReply) + rep->bytesReturned) return false;
	outPcm.assign(reply.begin() + sizeof(KPIHOST64_RenderReply), reply.begin() + sizeof(KPIHOST64_RenderReply) + rep->bytesReturned);
	return true;
}

bool KpiHost64Client::ForeignSeek(uint32_t sessionId, uint64_t posSample)
{
	KPIHOST64_SeekReq sr{};
	sr.sessionId = sessionId;
	sr.posSample = posSample;
	sr.flag = 0;
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_FOREIGN_SEEK, &sr, sizeof(sr), reply, st)) return false;
	return st == KPIHOST64_STATUS_OK;
}

bool KpiHost64Client::ForeignClose(uint32_t sessionId)
{
	KPIHOST64_U32 u{};
	u.v = sessionId;
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_FOREIGN_CLOSE, &u, sizeof(u), reply, st)) return false;
	return st == KPIHOST64_STATUS_OK;
}

// 曲スロットを開く。slot はクロスフェード用 0/1。vstDllPath=GS、extraScanPath=XG。
bool KpiHost64Client::VstOpen(const std::wstring& midPath, const std::wstring& vstDllPath,
	const std::wstring& extraScanPath, KPIHOST64_ForeignOpenReply& out, uint32_t slot)
{
	if (slot > 1) slot = 0;
	std::vector<uint8_t> req;
	uint32_t nMid = (uint32_t)midPath.size();
	uint32_t nDll = (uint32_t)vstDllPath.size();
	uint32_t nEx = (uint32_t)extraScanPath.size();
	req.resize(sizeof(uint32_t) * 4 + (nMid + nDll + nEx) * sizeof(wchar_t));
	uint8_t* p = req.data();
	memcpy(p, &slot, sizeof(slot)); p += sizeof(slot);
	memcpy(p, &nMid, sizeof(nMid)); p += sizeof(nMid);
	if (nMid) { memcpy(p, midPath.c_str(), nMid * sizeof(wchar_t)); p += nMid * sizeof(wchar_t); }
	memcpy(p, &nDll, sizeof(nDll)); p += sizeof(nDll);
	if (nDll) { memcpy(p, vstDllPath.c_str(), nDll * sizeof(wchar_t)); p += nDll * sizeof(wchar_t); }
	memcpy(p, &nEx, sizeof(nEx)); p += sizeof(nEx);
	if (nEx) { memcpy(p, extraScanPath.c_str(), nEx * sizeof(wchar_t)); p += nEx * sizeof(wchar_t); }
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_VST_OPEN, req.data(), (uint32_t)req.size(), reply, st)) return false;
	if (st != KPIHOST64_STATUS_OK || reply.size() < sizeof(KPIHOST64_ForeignOpenReply)) return false;
	memcpy(&out, reply.data(), sizeof(out));
	return true;
}

// inj* はモニタ／鍵盤からのショート。ホストが次ブロックで VSTi へ注入する。
bool KpiHost64Client::VstRender(uint32_t bytesWanted, std::vector<uint8_t>& outPcm, bool& outEof,
	const uint8_t* injPorts, const uint32_t* injMsgs, const int32_t* injOfs, uint32_t injCount, uint32_t slot,
	uint32_t* outMidiFlags)
{
	if (slot > 1) slot = 0;
	if (outMidiFlags) *outMidiFlags = 0;
	KPIHOST64_RenderReq rr{};
	rr.sessionId = slot;
	rr.bytesWanted = bytesWanted;
	if (injCount > 64) injCount = 64;
	if (!injPorts || !injMsgs) injCount = 0;
	std::vector<uint8_t> req(sizeof(rr) + sizeof(uint32_t) +
		(size_t)injCount * sizeof(KPIHOST64_VstLiveMidiReq));
	uint8_t* p = req.data();
	memcpy(p, &rr, sizeof(rr)); p += sizeof(rr);
	memcpy(p, &injCount, sizeof(injCount)); p += sizeof(injCount);
	for (uint32_t i = 0; i < injCount; ++i) {
		KPIHOST64_VstLiveMidiReq mr{};
		mr.port = injPorts[i];
		mr.msg = injMsgs[i];
		mr.sampleOfs = injOfs ? injOfs[i] : 0;
		memcpy(p, &mr, sizeof(mr));
		p += sizeof(mr);
	}
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_VST_RENDER, req.data(), (uint32_t)req.size(), reply, st)) return false;
	if (st != KPIHOST64_STATUS_OK || reply.size() < sizeof(KPIHOST64_RenderReply)) return false;
	const KPIHOST64_RenderReply* r = (const KPIHOST64_RenderReply*)reply.data();
	if (outMidiFlags) *outMidiFlags = r->eof;
	outEof = (r->eof & KPIHOST64_EOF_SHORT) != 0;
	outPcm.clear();
	if (r->bytesReturned > 0 && reply.size() >= sizeof(KPIHOST64_RenderReply) + r->bytesReturned) {
		outPcm.resize(r->bytesReturned);
		memcpy(outPcm.data(), reply.data() + sizeof(KPIHOST64_RenderReply), r->bytesReturned);
	}
	return true;
}

bool KpiHost64Client::VstSeek(uint64_t posSample, uint32_t slot)
{
	if (slot > 1) slot = 0;
	KPIHOST64_SeekReq sr{};
	sr.sessionId = slot;
	sr.posSample = posSample;
	sr.flag = 0;
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_VST_SEEK, &sr, sizeof(sr), reply, st)) return false;
	return st == KPIHOST64_STATUS_OK;
}

bool KpiHost64Client::VstClose(uint32_t slot)
{
	if (slot > 1) slot = 0;
	KPIHOST64_U32 u{};
	u.v = slot;
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_VST_CLOSE, &u, sizeof(u), reply, st)) return false;
	return st == KPIHOST64_STATUS_OK;
}

bool KpiHost64Client::VstCloseAll()
{
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_VST_CLOSE, NULL, 0, reply, st)) return false;
	return st == KPIHOST64_STATUS_OK;
}

bool KpiHost64Client::VstLiveLoad(uint32_t part1to32, const std::wstring& pluginPath, bool isVst3)
{
	if (pluginPath.empty()) return false;
	const uint32_t nPath = (uint32_t)pluginPath.size();
	std::vector<uint8_t> req(sizeof(KPIHOST64_VstLiveLoadReq) + sizeof(uint32_t) +
		nPath * sizeof(wchar_t));
	uint8_t* p = req.data();
	KPIHOST64_VstLiveLoadReq lr{};
	lr.part = part1to32;
	lr.isVst3 = isVst3 ? 1u : 0u;
	memcpy(p, &lr, sizeof(lr)); p += sizeof(lr);
	memcpy(p, &nPath, sizeof(nPath)); p += sizeof(nPath);
	memcpy(p, pluginPath.c_str(), nPath * sizeof(wchar_t));
	/* HALion MediaBay can take minutes; pump UI while waiting. */
	KpiPipeSetUiPump(1);
	const bool ok = SendSimple(KPIHOST64_CMD_VST_LIVE_LOAD, req.data(), (uint32_t)req.size(), 300000);
	KpiPipeSetUiPump(0);
	return ok;
}

bool KpiHost64Client::VstLiveUnload(uint32_t part1to32)
{
	KPIHOST64_U32 u{ part1to32 };
	return SendSimple(KPIHOST64_CMD_VST_LIVE_UNLOAD, &u, sizeof(u), 8000);
}

bool KpiHost64Client::VstLiveUnloadAll()
{
	return SendSimple(KPIHOST64_CMD_VST_LIVE_UNLOAD_ALL, NULL, 0, 8000);
}

bool KpiHost64Client::VstLiveMidi(uint32_t port, uint32_t msg)
{
	KPIHOST64_VstLiveMidiReq mr{};
	mr.port = port;
	mr.msg = msg;
	return SendSimple(KPIHOST64_CMD_VST_LIVE_MIDI, &mr, sizeof(mr));
}

bool KpiHost64Client::VstLiveSysex(uint32_t port, const uint8_t* data, uint32_t bytes)
{
	if (!data || !bytes) return false;
	std::vector<uint8_t> req(sizeof(KPIHOST64_VstLiveSysexReq) + bytes);
	KPIHOST64_VstLiveSysexReq sr{};
	sr.port = port;
	sr.bytes = bytes;
	memcpy(req.data(), &sr, sizeof(sr));
	memcpy(req.data() + sizeof(sr), data, bytes);
	return SendSimple(KPIHOST64_CMD_VST_LIVE_SYSEX, req.data(), (uint32_t)req.size());
}

bool KpiHost64Client::VstLiveAudioStart()
{
	return SendSimple(KPIHOST64_CMD_VST_LIVE_AUDIO_START, NULL, 0);
}

bool KpiHost64Client::VstLiveAudioStop()
{
	return SendSimple(KPIHOST64_CMD_VST_LIVE_AUDIO_STOP, NULL, 0, 8000);
}

bool KpiHost64Client::VstLiveEditorOpen(uint32_t part1to32)
{
	KPIHOST64_U32 u{ part1to32 };
	return SendSimple(KPIHOST64_CMD_VST_LIVE_EDITOR_OPEN, &u, sizeof(u));
}

bool KpiHost64Client::VstLiveEditorClose(uint32_t part1to32)
{
	KPIHOST64_U32 u{ part1to32 };
	return SendSimple(KPIHOST64_CMD_VST_LIVE_EDITOR_CLOSE, &u, sizeof(u), 8000);
}

bool KpiHost64Client::VstLiveEditorCloseAll()
{
	return SendSimple(KPIHOST64_CMD_VST_LIVE_EDITOR_CLOSE_ALL, NULL, 0, 120000);
}

bool KpiHost64Client::VstLiveSetSendChannel(uint32_t part1to32, int32_t sendCh)
{
	KPIHOST64_VstLiveSendChReq r{};
	r.part = part1to32;
	r.sendCh = sendCh;
	return SendSimple(KPIHOST64_CMD_VST_LIVE_SEND_CH, &r, sizeof(r));
}

bool KpiHost64Client::VstLivePrograms(uint32_t part1to32, uint32_t first, uint32_t count,
	uint32_t& outTotal, uint32_t& outCurrent, std::vector<std::wstring>& outNames)
{
	outTotal = 0;
	outCurrent = 0xFFFFFFFFu;
	outNames.clear();
	KPIHOST64_VstLiveProgramsReq r{};
	r.part = part1to32;
	r.first = first;
	r.count = count;
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_VST_LIVE_PROGRAMS, &r, sizeof(r), reply, st)) return false;
	if (st != KPIHOST64_STATUS_OK || reply.size() < sizeof(KPIHOST64_VstLiveProgramsReply))
		return false;
	const KPIHOST64_VstLiveProgramsReply* rep =
		(const KPIHOST64_VstLiveProgramsReply*)reply.data();
	outTotal = rep->total;
	outCurrent = rep->current;
	const uint8_t* p = reply.data() + sizeof(*rep);
	const uint8_t* end = reply.data() + reply.size();
	for (uint32_t i = 0; i < rep->got; ++i) {
		if ((size_t)(end - p) < sizeof(uint32_t)) break;
		const uint32_t chars = *(const uint32_t*)p;
		p += sizeof(uint32_t);
		const size_t bytes = (size_t)chars * sizeof(wchar_t);
		if ((size_t)(end - p) < bytes) break;
		outNames.push_back(std::wstring((const wchar_t*)p, (const wchar_t*)p + chars));
		p += bytes;
	}
	return true;
}

bool KpiHost64Client::VstLiveSetProgram(uint32_t part1to32, uint32_t index)
{
	KPIHOST64_VstLiveSetProgramReq r{};
	r.part = part1to32;
	r.index = index;
	return SendSimple(KPIHOST64_CMD_VST_LIVE_SET_PROGRAM, &r, sizeof(r));
}

bool KpiHost64Client::VstLiveGetState(uint32_t part1to32, uint32_t which,
	std::vector<uint8_t>& outBytes)
{
	outBytes.clear();
	KPIHOST64_VstLiveStateReq r{};
	r.part = part1to32;
	r.which = which;
	r.bytes = 0;
	std::vector<uint8_t> reply;
	uint32_t st = 0;
	if (!SendRequest(KPIHOST64_CMD_VST_LIVE_GET_STATE, &r, sizeof(r), reply, st, 10000))
		return false;
	if (st != KPIHOST64_STATUS_OK || reply.size() < sizeof(KPIHOST64_VstLiveStateReply))
		return false;
	auto* head = (const KPIHOST64_VstLiveStateReply*)reply.data();
	if (reply.size() < sizeof(*head) + head->bytes) return false;
	if (head->bytes)
		outBytes.assign(reply.begin() + sizeof(*head), reply.begin() + sizeof(*head) + head->bytes);
	return true;
}

bool KpiHost64Client::VstLiveSetState(uint32_t part1to32, uint32_t which,
	const uint8_t* bytes, uint32_t len)
{
	if (!bytes || !len) return false;
	std::vector<uint8_t> req(sizeof(KPIHOST64_VstLiveStateReq) + len);
	auto* r = (KPIHOST64_VstLiveStateReq*)req.data();
	r->part = part1to32;
	r->which = which;
	r->bytes = len;
	memcpy(req.data() + sizeof(*r), bytes, len);
	return SendSimple(KPIHOST64_CMD_VST_LIVE_SET_STATE, req.data(), (uint32_t)req.size(), 120000);
}

