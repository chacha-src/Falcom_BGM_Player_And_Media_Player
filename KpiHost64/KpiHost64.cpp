#include <windows.h>
#include <unknwn.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "..\kpi_decoder.h"
#include "..\kmp_pi.h"
#include "..\kpi_host_ipc.h"

static std::wstring DirNameOf(const std::wstring& path)
{
	size_t p = path.find_last_of(L"\\/");
	if (p == std::wstring::npos) return L"";
	return path.substr(0, p + 1);
}

struct ScopedDllDirectory
{
	DLL_DIRECTORY_COOKIE cookie = 0;
	ScopedDllDirectory(const std::wstring& dir)
	{
		if (!dir.empty()) cookie = AddDllDirectory(dir.c_str());
	}
	~ScopedDllDirectory()
	{
		if (cookie) RemoveDllDirectory(cookie);
	}
};

static void AppendHostLogLine(const wchar_t* line)
{
	if (!line) return;
	wchar_t tempDir[MAX_PATH]{};
	if (!GetTempPathW(MAX_PATH, tempDir)) return;
	std::wstring path = tempDir;
	path += L"ogg_kpi64_host.log";
	HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	DWORD bytes = (DWORD)(wcslen(line) * sizeof(wchar_t));
	DWORD written = 0;
	WriteFile(h, line, bytes, &written, NULL);
	const wchar_t crlf[] = L"\r\n";
	WriteFile(h, crlf, (DWORD)(2 * sizeof(wchar_t)), &written, NULL);
	CloseHandle(h);
}

// Minimal IKpiConfig + IKpiUnkProvider implementation so v5 plugins that call
// kpi_CreateConfig(pUnknown, ...) can work (at least enough to construct module
// and expose SupportExts).
class DummyConfig : public IKpiConfig
{
	long m_ref = 1;
public:
	ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
	ULONG STDMETHODCALLTYPE Release() override {
		ULONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return r;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IKpiConfig) { *ppv = static_cast<IKpiConfig*>(this); AddRef(); return S_OK; }
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	void WINAPI SetInt(const wchar_t*, const wchar_t*, INT64) override {}
	INT64 WINAPI GetInt(const wchar_t*, const wchar_t*, INT64 nDefault) override { return nDefault; }
	void WINAPI SetFloat(const wchar_t*, const wchar_t*, double) override {}
	double WINAPI GetFloat(const wchar_t*, const wchar_t*, double dDefault) override { return dDefault; }
	void WINAPI SetStr(const wchar_t*, const wchar_t*, const wchar_t*) override {}
	DWORD WINAPI GetStr(const wchar_t*, const wchar_t*, wchar_t* pszValue, DWORD dwSize, const wchar_t* cszDefault) override {
		if (!pszValue || dwSize < 2) return 0;
		if (!cszDefault) cszDefault = L"";
		const size_t len = wcslen(cszDefault);
		const DWORD need = (DWORD)((len + 1) * sizeof(wchar_t));
		if (dwSize >= need) {
			wcscpy_s(pszValue, dwSize / sizeof(wchar_t), cszDefault);
		}
		else {
			pszValue[0] = 0;
		}
		return need;
	}
	void WINAPI SetBin(const wchar_t*, const wchar_t*, const BYTE*, DWORD) override {}
	DWORD WINAPI GetBin(const wchar_t*, const wchar_t*, BYTE* pBuffer, DWORD dwSize) override {
		if (pBuffer && dwSize) ZeroMemory(pBuffer, dwSize);
		return 0;
	}
};

class HostProvider : public IKpiUnkProvider
{
	long m_ref = 1;
public:
	ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
	ULONG STDMETHODCALLTYPE Release() override {
		ULONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return r;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IKpiUnkProvider) { *ppv = static_cast<IKpiUnkProvider*>(this); AddRef(); return S_OK; }
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	DWORD WINAPI CreateInstance(REFIID riid, void* pvParam1, void* pvParam2, void*, void*, void** ppvObj) override {
		if (!ppvObj) return 0;
		*ppvObj = NULL;
		if (riid == IID_IKpiConfig) {
			// x86側(CMyHost)と同じ扱いにする: 0 = 本体が直接呼び出し
			if (pvParam2) *(DWORD*)pvParam2 = 0;
			*ppvObj = (IKpiConfig*)new DummyConfig();
			return 1;
		}
		(void)pvParam1;
		return 0;
	}
};

static HRESULT SafeKpiCreateInstance(pfn_kpiCreateInstance cr, REFIID riid, void** ppvObject, IKpiUnknown* pUnknown)
{
	HRESULT hr = E_FAIL;
	__try {
		hr = cr(riid, ppvObject, pUnknown);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hr = E_FAIL;
	}
	return hr;
}

// Minimal IKpiFolder (dummy)
class DummyFolder : public IKpiFolder
{
	long m_ref = 1;
public:
	ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
	ULONG STDMETHODCALLTYPE Release() override {
		ULONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return r;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IKpiFolder) { *ppv = static_cast<IKpiFolder*>(this); AddRef(); return S_OK; }
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	DWORD WINAPI GetFolderName(wchar_t* pszName, DWORD dwSize) override {
		if (!pszName || dwSize < 2) return 0;
		pszName[0] = 0;
		return 0;
	}
	DWORD WINAPI EnumFiles(DWORD, wchar_t* pszName, DWORD dwSize, DWORD) override {
		if (pszName && dwSize >= 2) pszName[0] = 0;
		return 0;
	}
	BOOL WINAPI OpenFile(const wchar_t*, IKpiFile**) override { return FALSE; }
	BOOL WINAPI OpenFolder(const wchar_t*, IKpiFolder**) override { return FALSE; }
};

// Minimal IKpiFile for local file path
class HostFile : public IKpiFile
{
	long m_ref = 1;
	HANDLE m_h = INVALID_HANDLE_VALUE;
	std::wstring m_path;
	std::wstring m_name;
	BYTE* m_buf = NULL;
	size_t m_bufSize = 0;
public:
	HostFile() {}
	~HostFile() {
		if (m_h != INVALID_HANDLE_VALUE) CloseHandle(m_h);
		if (m_buf) {
			free(m_buf);
			m_buf = NULL;
			m_bufSize = 0;
		}
	}
	bool Open(const std::wstring& path) {
		m_path = path;
		size_t p = path.find_last_of(L"\\/");
		m_name = (p == std::wstring::npos) ? path : path.substr(p + 1);
		if (m_buf) {
			free(m_buf);
			m_buf = NULL;
			m_bufSize = 0;
		}
		m_h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		return m_h != INVALID_HANDLE_VALUE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
	ULONG STDMETHODCALLTYPE Release() override {
		ULONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return r;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IKpiFile) { *ppv = static_cast<IKpiFile*>(this); AddRef(); return S_OK; }
		*ppv = NULL;
		return E_NOINTERFACE;
	}

	DWORD WINAPI Read(void* pBuffer, DWORD dwSize) override {
		if (m_h == INVALID_HANDLE_VALUE) return 0;
		DWORD rd = 0;
		ReadFile(m_h, pBuffer, dwSize, &rd, NULL);
		return rd;
	}
	UINT64 WINAPI Seek(INT64 i64Pos, DWORD dwOrigin) override {
		if (m_h == INVALID_HANDLE_VALUE) return KPI_FILE_EOF;
		LARGE_INTEGER li; li.QuadPart = i64Pos;
		LARGE_INTEGER out{};
		if (!SetFilePointerEx(m_h, li, &out, dwOrigin)) return KPI_FILE_EOF;
		return (UINT64)out.QuadPart;
	}
	UINT64 WINAPI GetSize(void) override {
		if (m_h == INVALID_HANDLE_VALUE) return KPI_FILE_EOF;
		LARGE_INTEGER sz{};
		if (!GetFileSizeEx(m_h, &sz)) return KPI_FILE_EOF;
		return (UINT64)sz.QuadPart;
	}
	BOOL WINAPI CreateClone(IKpiFile** ppFile) override {
		if (!ppFile) return FALSE;
		*ppFile = NULL;
		auto* f = new HostFile();
		if (!f->Open(m_path)) { f->Release(); return FALSE; }
		*ppFile = f;
		return TRUE;
	}
	DWORD WINAPI GetFileName(wchar_t* pszName, DWORD dwSize) override {
		if (!pszName || dwSize < 2) return 0;
		const DWORD need = (DWORD)((m_name.size() + 1) * sizeof(wchar_t));
		if (dwSize >= need) {
			wcscpy_s(pszName, dwSize / sizeof(wchar_t), m_name.c_str());
		}
		else {
			pszName[0] = 0;
		}
		return need;
	}
	BOOL WINAPI GetRealFileW(const wchar_t** ppszFileNameW) override {
		if (!ppszFileNameW) return FALSE;
		*ppszFileNameW = m_path.c_str();
		return TRUE;
	}
	BOOL WINAPI GetRealFileA(const char**) override { return FALSE; }
	BOOL WINAPI GetBuffer(const BYTE** ppBuffer, size_t* pstSize) override {
		if (!ppBuffer || !pstSize) return FALSE;
		*ppBuffer = NULL;
		*pstSize = 0;
		if (m_h == INVALID_HANDLE_VALUE) return FALSE;

		if (!m_buf) {
			UINT64 sz = GetSize();
			if (sz == KPI_FILE_EOF || sz == 0) return FALSE;
			if (sz > (UINT64)IKpiFile::GET_BUFFER_MAXSIZE) return FALSE;

			m_bufSize = (size_t)sz;
			m_buf = (BYTE*)malloc(m_bufSize);
			if (!m_buf) { m_bufSize = 0; return FALSE; }

			Seek(0, FILE_BEGIN);
			size_t off = 0;
			while (off < m_bufSize) {
				DWORD want = (DWORD)std::min((size_t)0x40000, m_bufSize - off);
				DWORD got = Read(m_buf + off, want);
				if (got == 0) break;
				off += got;
			}
			if (off != m_bufSize) {
				free(m_buf);
				m_buf = NULL;
				m_bufSize = 0;
				return FALSE;
			}
		}

		*ppBuffer = m_buf;
		*pstSize = m_bufSize;
		return TRUE;
	}
	BOOL WINAPI Abort(void) override { return TRUE; }
};

struct Session
{
	HMODULE hDll = NULL;
	IKpiDecoderModule* mod = NULL;
	IKpiDecoder* dec = NULL;
	HostFile* file = NULL;
	DummyFolder* folder = NULL;
	KPI_MEDIAINFO request{};
	KPI_MEDIAINFO selected{};
	DWORD openedSongCount = 0;
	DWORD channels = 2;
	DWORD bps = 16;
};

static uint32_t g_nextSessionId = 1;
static std::unordered_map<uint32_t, Session> g_sessions;

static bool ReadExact(HANDLE h, void* buf, DWORD bytes)
{
	uint8_t* p = (uint8_t*)buf;
	DWORD remain = bytes;
	while (remain) {
		DWORD rd = 0;
		if (!ReadFile(h, p, remain, &rd, NULL) || rd == 0) return false;
		p += rd;
		remain -= rd;
	}
	return true;
}

static bool WriteExact(HANDLE h, const void* buf, DWORD bytes)
{
	const uint8_t* p = (const uint8_t*)buf;
	DWORD remain = bytes;
	while (remain) {
		DWORD wr = 0;
		if (!WriteFile(h, p, remain, &wr, NULL) || wr == 0) return false;
		p += wr;
		remain -= wr;
	}
	return true;
}

static bool ReadWString(const uint8_t*& p, const uint8_t* end, std::wstring& out)
{
	if (end - p < 4) return false;
	uint32_t chars = *(const uint32_t*)p; p += 4;
	size_t bytes = (size_t)chars * sizeof(wchar_t);
	if ((size_t)(end - p) < bytes) return false;
	out.assign((const wchar_t*)p, (const wchar_t*)p + chars);
	p += bytes;
	return true;
}

static void SendReply(HANDLE pipe, uint32_t cmd, uint32_t reqId, uint32_t status, const void* payload, uint32_t payloadBytes)
{
	KPIHOST64_ReplyHeader rh{};
	rh.cmd = cmd;
	rh.requestId = reqId;
	rh.status = status;
	rh.payloadBytes = payloadBytes;
	WriteExact(pipe, &rh, sizeof(rh));
	if (payloadBytes && payload) WriteExact(pipe, payload, payloadBytes);
}

static uint32_t Cmd_ListExts(const std::wstring& kpiPath, std::vector<uint8_t>& out)
{
	out.clear();
	// Ensure dependent DLLs are resolved from KPI's directory too.
	ScopedDllDirectory addDir(DirNameOf(kpiPath));
	HMODULE h = LoadLibraryExW(
		kpiPath.c_str(),
		NULL,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
	);
	if (!h) {
		AppendHostLogLine((L"[LIST_EXTS] LoadLibraryExW failed err=" + std::to_wstring(GetLastError()) + L" path=" + kpiPath).c_str());
		return KPIHOST64_STATUS_FAIL;
	}
	std::wstring exts;
	uint32_t ver = 0;

	// Try KPI v5 first (kpi_CreateInstance)
	if (auto cr = (pfn_kpiCreateInstance)GetProcAddress(h, "kpi_CreateInstance")) {
		IKpiDecoderModule* mod = NULL;
		HostProvider* prov = new HostProvider();
		HRESULT hr = SafeKpiCreateInstance(cr, IID_IKpiDecoderModule, (void**)&mod, (IKpiUnknown*)prov);
		if (hr == S_OK && mod) {
			const KPI_DECODER_MODULEINFO* info = NULL;
			mod->GetModuleInfo(&info);
			if (info && info->cszSupportExts) exts = info->cszSupportExts;
			ver = 5;
			mod->Release();
		}
		prov->Release();
	}

	// Fallback: KPI v2 (KMPMODULE)
	if (exts.empty()) {
		if (auto fn = (pfnGetKMPModule)GetProcAddress(h, SZ_KMP_GETMODULE)) {
			KMPMODULE* m = fn();
			if (m && m->ppszSupportExts) {
				for (int i = 0; m->ppszSupportExts[i] != NULL; i++) {
					if (i != 0) exts += L"/";
					const char* e = m->ppszSupportExts[i];
					if (!e) continue;
					// ANSI -> UTF16
					int wlen = MultiByteToWideChar(CP_ACP, 0, e, -1, NULL, 0);
					if (wlen > 1) {
						std::wstring ws;
						ws.resize((size_t)wlen - 1);
						MultiByteToWideChar(CP_ACP, 0, e, -1, ws.data(), wlen);
						// normalize: ensure it starts with '.'
						if (!ws.empty() && ws[0] != L'.') ws = L"." + ws;
						exts += ws;
					}
				}
				ver = 2;
			}
		}
	}

	FreeLibrary(h);

	KPIHOST64_ListExtsReply rep{};
	rep.kpiVer = ver;
	uint32_t chars = (uint32_t)exts.size();
	out.resize(sizeof(rep) + 4 + chars * sizeof(wchar_t));
	memcpy(out.data(), &rep, sizeof(rep));
	memcpy(out.data() + sizeof(rep), &chars, 4);
	if (chars) memcpy(out.data() + sizeof(rep) + 4, exts.data(), chars * sizeof(wchar_t));
	return KPIHOST64_STATUS_OK;
}

static uint32_t Cmd_Open(const std::wstring& kpiPath, const std::wstring& mediaPath, const KPI_MEDIAINFO& request, uint32_t songNo, std::vector<uint8_t>& out)
{
	out.clear();
	ScopedDllDirectory addDir(DirNameOf(kpiPath));
	HMODULE h = LoadLibraryExW(
		kpiPath.c_str(),
		NULL,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
	);
	if (!h) {
		AppendHostLogLine((L"[OPEN] LoadLibraryExW failed err=" + std::to_wstring(GetLastError()) + L" kpi=" + kpiPath).c_str());
		return KPIHOST64_STATUS_FAIL;
	}
	auto cr = (pfn_kpiCreateInstance)GetProcAddress(h, "kpi_CreateInstance");
	if (!cr) { FreeLibrary(h); return KPIHOST64_STATUS_NOT_SUPPORTED; }

	IKpiDecoderModule* mod = NULL;
	HostProvider* prov = new HostProvider();
	HRESULT hr = SafeKpiCreateInstance(cr, IID_IKpiDecoderModule, (void**)&mod, (IKpiUnknown*)prov);
	prov->Release();
	if (hr != S_OK || !mod) { FreeLibrary(h); return KPIHOST64_STATUS_FAIL; }

	auto* f = new HostFile();
	if (!f->Open(mediaPath)) { f->Release(); mod->Release(); FreeLibrary(h); return KPIHOST64_STATUS_NOT_FOUND; }
	auto* folder = new DummyFolder();

	IKpiDecoder* dec = NULL;
	DWORD count = mod->Open(&request, f, folder, &dec);
	if (!dec || count == 0) {
		if (dec) dec->Release();
		f->Release();
		folder->Release();
		mod->Release();
		FreeLibrary(h);
		return KPIHOST64_STATUS_FAIL;
	}

	const KPI_MEDIAINFO* sel = NULL;
	uint32_t selNo = (songNo == 0) ? 1 : songNo;
	DWORD selected = dec->Select(selNo, &sel, NULL, 0);
	if (selected == 0 || !sel) {
		dec->Release();
		f->Release();
		folder->Release();
		mod->Release();
		FreeLibrary(h);
		return KPIHOST64_STATUS_FAIL;
	}

	Session s{};
	s.hDll = h;
	s.mod = mod;
	s.dec = dec;
	s.file = f;
	s.folder = folder;
	s.request = request;
	s.selected = *sel;
	s.openedSongCount = count;
	s.channels = s.selected.dwChannels ? s.selected.dwChannels : 2;
	s.bps = (DWORD)(s.selected.nBitsPerSample ? (s.selected.nBitsPerSample < 0 ? -s.selected.nBitsPerSample : s.selected.nBitsPerSample) : 16);
	if (s.bps == 0) s.bps = 16;

	const uint32_t id = g_nextSessionId++;
	g_sessions[id] = s;

	KPIHOST64_OpenReply rep{};
	rep.sessionId = id;
	rep.openedSongCount = count;

	out.resize(sizeof(rep) + sizeof(KPI_MEDIAINFO));
	memcpy(out.data(), &rep, sizeof(rep));
	memcpy(out.data() + sizeof(rep), &s.selected, sizeof(KPI_MEDIAINFO));
	return KPIHOST64_STATUS_OK;
}

static uint32_t Cmd_Render(uint32_t sessionId, uint32_t bytesWanted, std::vector<uint8_t>& out)
{
	out.clear();
	auto it = g_sessions.find(sessionId);
	if (it == g_sessions.end()) return KPIHOST64_STATUS_NOT_FOUND;
	Session& s = it->second;
	if (!s.dec) return KPIHOST64_STATUS_FAIL;

	const uint32_t bytesPerFrame = s.channels * (s.bps / 8);
	if (bytesPerFrame == 0) return KPIHOST64_STATUS_BAD_REQUEST;
	const uint32_t samplesWanted = bytesWanted / bytesPerFrame;
	if (samplesWanted == 0) return KPIHOST64_STATUS_BAD_REQUEST;

	std::vector<uint8_t> pcm;
	pcm.resize((size_t)samplesWanted * bytesPerFrame);
	DWORD gotSamples = s.dec->Render(pcm.data(), samplesWanted);
	uint32_t gotBytes = gotSamples * bytesPerFrame;
	if (gotBytes > pcm.size()) gotBytes = (uint32_t)pcm.size();

	KPIHOST64_RenderReply rep{};
	rep.sessionId = sessionId;
	rep.bytesReturned = gotBytes;
	rep.eof = (gotSamples < samplesWanted) ? 1 : 0;

	out.resize(sizeof(rep) + gotBytes);
	memcpy(out.data(), &rep, sizeof(rep));
	if (gotBytes) memcpy(out.data() + sizeof(rep), pcm.data(), gotBytes);
	return KPIHOST64_STATUS_OK;
}

static uint32_t Cmd_Seek(uint32_t sessionId, uint64_t posSample, uint32_t flag, std::vector<uint8_t>& out)
{
	out.clear();
	auto it = g_sessions.find(sessionId);
	if (it == g_sessions.end()) return KPIHOST64_STATUS_NOT_FOUND;
	Session& s = it->second;
	if (!s.dec) return KPIHOST64_STATUS_FAIL;

	UINT64 newPos = s.dec->Seek(posSample, flag);
	KPIHOST64_SeekReply rep{};
	rep.sessionId = sessionId;
	rep.newPosSample = newPos;
	out.resize(sizeof(rep));
	memcpy(out.data(), &rep, sizeof(rep));
	return KPIHOST64_STATUS_OK;
}

static uint32_t Cmd_Close(uint32_t sessionId)
{
	auto it = g_sessions.find(sessionId);
	if (it == g_sessions.end()) return KPIHOST64_STATUS_NOT_FOUND;
	Session s = it->second;
	g_sessions.erase(it);

	if (s.dec) s.dec->Release();
	if (s.file) s.file->Release();
	if (s.folder) s.folder->Release();
	if (s.mod) s.mod->Release();
	if (s.hDll) FreeLibrary(s.hDll);
	return KPIHOST64_STATUS_OK;
}

static void ServeOnce(HANDLE pipe)
{
	for (;;) {
		KPIHOST64_MsgHeader h{};
		DWORD rd = 0;
		BOOL ok = ReadFile(pipe, &h, sizeof(h), &rd, NULL);
		if (!ok || rd == 0) break;
		if (rd != sizeof(h)) break;

		std::vector<uint8_t> payload;
		payload.resize(h.payloadBytes);
		if (h.payloadBytes) {
			if (!ReadExact(pipe, payload.data(), h.payloadBytes)) break;
		}

		std::vector<uint8_t> reply;
		uint32_t status = KPIHOST64_STATUS_FAIL;

		const uint8_t* p = payload.data();
		const uint8_t* end = payload.data() + payload.size();

		switch (h.cmd) {
		case KPIHOST64_CMD_PING:
			status = KPIHOST64_STATUS_OK;
			break;
		case KPIHOST64_CMD_LIST_EXTS: {
			std::wstring kpiPath;
			if (!ReadWString(p, end, kpiPath) || p != end) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			status = Cmd_ListExts(kpiPath, reply);
			break;
		}
		case KPIHOST64_CMD_OPEN: {
			if (end - p < 4) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			uint32_t songNo = *(const uint32_t*)p; p += 4;
			std::wstring kpiPath, mediaPath;
			if (!ReadWString(p, end, kpiPath)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			if (!ReadWString(p, end, mediaPath)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			if ((size_t)(end - p) != sizeof(KPI_MEDIAINFO)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			KPI_MEDIAINFO req{};
			memcpy(&req, p, sizeof(req));
			status = Cmd_Open(kpiPath, mediaPath, req, songNo, reply);
			break;
		}
		case KPIHOST64_CMD_RENDER: {
			if ((size_t)(end - p) != sizeof(KPIHOST64_RenderReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* rr = (const KPIHOST64_RenderReq*)p;
			status = Cmd_Render(rr->sessionId, rr->bytesWanted, reply);
			break;
		}
		case KPIHOST64_CMD_SEEK: {
			if ((size_t)(end - p) != sizeof(KPIHOST64_SeekReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* sr = (const KPIHOST64_SeekReq*)p;
			status = Cmd_Seek(sr->sessionId, sr->posSample, sr->flag, reply);
			break;
		}
		case KPIHOST64_CMD_CLOSE: {
			if ((size_t)(end - p) != sizeof(KPIHOST64_U32)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* u = (const KPIHOST64_U32*)p;
			status = Cmd_Close(u->v);
			break;
		}
		default:
			status = KPIHOST64_STATUS_BAD_REQUEST;
			break;
		}

		SendReply(pipe, h.cmd, h.requestId, status, reply.data(), (uint32_t)reply.size());
	}
}

int wmain(int argc, wchar_t** argv)
{
	(void)argc; (void)argv;

	// Opt-in to safe DLL search paths and enable AddDllDirectory.
	SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);

	HANDLE pipe = CreateNamedPipeW(
		KPIHOST64_PIPE_NAME,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,
		1024 * 1024,
		1024 * 1024,
		0,
		NULL
	);
	if (pipe == INVALID_HANDLE_VALUE) return 2;

	const DWORD idleMs = 30000; // 30s idle => exit
	for (;;) {
		OVERLAPPED ov{};
		ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (!ov.hEvent) break;

		BOOL ok = ConnectNamedPipe(pipe, &ov);
		if (!ok) {
			DWORD err = GetLastError();
			if (err == ERROR_PIPE_CONNECTED) {
				SetEvent(ov.hEvent);
			}
			else if (err != ERROR_IO_PENDING) {
				CloseHandle(ov.hEvent);
				break;
			}
		}

		DWORD wr = WaitForSingleObject(ov.hEvent, idleMs);
		if (wr == WAIT_TIMEOUT) {
			CancelIoEx(pipe, &ov);
			CloseHandle(ov.hEvent);
			if (g_sessions.empty()) {
				break; // idle and no active sessions
			}
			continue;
		}

		CloseHandle(ov.hEvent);
		ServeOnce(pipe);
		DisconnectNamedPipe(pipe);
	}

	// cleanup any leaked sessions
	for (auto& kv : g_sessions) {
		(void)Cmd_Close(kv.first);
	}
	g_sessions.clear();
	CloseHandle(pipe);
	return 0;
}

