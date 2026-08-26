// ============================================================================
// KpiHost64.exe — 64bit KPI / VST / 外部プラグインのパイプサーバ
// ----------------------------------------------------------------------------
// 32bit 本体は x64 DLL を LoadLibrary できない。このプロセスが名前付きパイプ
// \\.\pipe\ogg_kpi64 を待ち、Open/Render/Seek/Close を実行する。
//
// スレッド:
//   wmain / ServeOnce … パイプ 1 本を直列処理（本体側も同時リクエストしない）
//   VST ライブ音声   … 別スレッド＋共有メモリ（KpiHost64VstLive.cpp）
//   VST ライブ GUI   … 専用 UI スレッド（SC-VA エディタ用）
//
// アイドル 30 秒で接続が来なければ、KPI セッションもライブパートも無ければ終了。
// ライブが載っているあいだはタイムアウトしない（落とすと音源ごと消える）。
// ============================================================================
#include <windows.h>
#include <unknwn.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>

#include "kpihost_stdafx.h"
#include "..\kpi_decoder.h"
#include "..\kmp_pi.h"
#include "..\kpi_host_ipc.h"
#include "..\KpiV5ConfigStore.h"
#include "KpiHost64Foreign.h"
#include "KpiHost64Vst.h"
#include "KpiHost64VstLive.h"
#include "..\VstMidiEngine.h"

// パスのディレクトリ部分（末尾に \\ または / を残す）。ファイル名だけなら空。
static std::wstring DirNameOf(const std::wstring& path)
{
	size_t p = path.find_last_of(L"\\/");
	if (p == std::wstring::npos) return L"";
	return path.substr(0, p + 1);
}

// 1 段上のディレクトリ。KPI の依存 DLL が親フォルダにあることがある。
static std::wstring ParentDirOf(const std::wstring& path)
{
	if (path.empty()) return L"";
	size_t end = path.size();
	while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/')) --end;
	if (end == 0) return L"";
	size_t p = path.find_last_of(L"\\/", end - 1);
	if (p == std::wstring::npos) return L"";
	return path.substr(0, p + 1);
}

// KPI が隣のサブフォルダの DLL を探すので、深さ depth まで列挙する。ジャンクションは辿らない。
static void CollectSubDirsRecursive(const std::wstring& baseDir, int depth, std::vector<std::wstring>& out)
{
	if (depth <= 0 || baseDir.empty()) return;
	std::wstring pat = baseDir;
	if (!pat.empty() && pat.back() != L'\\' && pat.back() != L'/') pat += L'\\';
	pat += L"*";
	WIN32_FIND_DATAW fd{};
	HANDLE h = FindFirstFileW(pat.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
		std::wstring sub = baseDir;
		if (!sub.empty() && sub.back() != L'\\' && sub.back() != L'/') sub += L'\\';
		sub += fd.cFileName;
		if (!sub.empty() && sub.back() != L'\\' && sub.back() != L'/') sub += L'\\';
		out.push_back(sub);
		CollectSubDirsRecursive(sub, depth - 1, out);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

// KpiHost64.exe のあるフォルダとその配下（深さ 3）。AddDllDirectory 用。
static std::vector<std::wstring> GetExeRelatedDllDirs()
{
	std::vector<std::wstring> dirs;
	wchar_t exePath[MAX_PATH]{};
	if (!GetModuleFileNameW(NULL, exePath, _countof(exePath))) return dirs;
	std::wstring exeDir = DirNameOf(exePath);
	if (exeDir.empty()) return dirs;
	dirs.push_back(exeDir);
	CollectSubDirsRecursive(exeDir, 3, dirs);
	return dirs;
}

// 複数ディレクトリを AddDllDirectory し、スコープ終了で全部 Remove。LoadLibraryEx のあいだだけ有効。
struct ScopedDllDirectories
{
	std::vector<DLL_DIRECTORY_COOKIE> cookies;
	ScopedDllDirectories(const std::vector<std::wstring>& dirs)
	{
		cookies.reserve(dirs.size());
		for (size_t i = 0; i < dirs.size(); ++i) {
			if (dirs[i].empty()) continue;
			DLL_DIRECTORY_COOKIE c = AddDllDirectory(dirs[i].c_str());
			if (c) cookies.push_back(c);
		}
	}
	~ScopedDllDirectories()
	{
		for (size_t i = cookies.size(); i > 0; --i) RemoveDllDirectory(cookies[i - 1]);
	}
};

// 1 ディレクトリ版。KPI 本体のフォルダ／その親用。
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

// %TEMP%\ogg_kpi64_host.log へ 1 行追記。失敗しても再生は続ける（デバッグ用）。
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

// KPI v5 がホストに求める設定ストア。実体は KpiV5ConfigStore（ini 相当）。
// Volume は本体のゲインに任せるので、プラグインには常にフルスケールを返す。
class DummyConfig : public IKpiConfig
{
	long m_ref = 1;              // COM 参照カウント
	std::wstring m_pluginName;   // DLL 名（kbpsf2 など）。キーの名前空間
public:
	DummyConfig(const wchar_t* pluginName) : m_pluginName(pluginName ? pluginName : L"") {}
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
	void WINAPI SetInt(const wchar_t* sec, const wchar_t* key, INT64 v) override {
		KpiV5SetInt(m_pluginName, sec ? sec : L"", key ? key : L"", v);
	}
	INT64 WINAPI GetInt(const wchar_t* sec, const wchar_t* key, INT64 nDefault) override {
		INT64 def = nDefault;
		if (_wcsicmp(m_pluginName.c_str(), L"kbpsf2") == 0 &&
			sec && key &&
			_wcsicmp(sec, L"General") == 0 &&
			_wcsicmp(key, L"IgnoreVolumeTag") == 0) {
			// kbpsf2 の曲内音量タグは本体ゲインと二重になるので無視する。
			def = 1;
		}
		return KpiV5GetInt(m_pluginName, sec ? sec : L"", key ? key : L"", def);
	}
	void WINAPI SetFloat(const wchar_t* sec, const wchar_t* key, double v) override {
		KpiV5SetFloat(m_pluginName, sec ? sec : L"", key ? key : L"", v);
	}
	double WINAPI GetFloat(const wchar_t* sec, const wchar_t* key, double dDefault) override {
		// 音量は本体側のゲインで統一する。プラグインには常にフルスケール（dVolume=1.0 相当）だけ渡す。
		if (key && sec && _wcsicmp(key, L"Volume") == 0 && _wcsicmp(sec, L"General") == 0) {
			const wchar_t* pn = m_pluginName.c_str();
			if (_wcsicmp(pn, L"kbvgm") == 0 || _wcsicmp(pn, L"kbfmoplmidi") == 0)
				return 1.0;
			return 100.0;
		}
		return KpiV5GetFloat(m_pluginName, sec ? sec : L"", key ? key : L"", dDefault);
	}
	void WINAPI SetStr(const wchar_t* sec, const wchar_t* key, const wchar_t* value) override {
		KpiV5SetStr(m_pluginName, sec ? sec : L"", key ? key : L"", value ? value : L"");
	}
	DWORD WINAPI GetStr(const wchar_t* sec, const wchar_t* key, wchar_t* pszValue, DWORD dwSize, const wchar_t* cszDefault) override {
		const std::wstring value = KpiV5GetStr(m_pluginName, sec ? sec : L"", key ? key : L"", cszDefault ? cszDefault : L"");
		const DWORD need = (DWORD)((value.size() + 1) * sizeof(wchar_t));
		if (pszValue && dwSize >= sizeof(wchar_t)) {
			if (dwSize >= need) wcscpy_s(pszValue, dwSize / sizeof(wchar_t), value.c_str());
			else pszValue[0] = 0;
		}
		return need;
	}
	void WINAPI SetBin(const wchar_t* sec, const wchar_t* key, const BYTE* pBuffer, DWORD dwSize) override {
		KpiV5SetBin(m_pluginName, sec ? sec : L"", key ? key : L"", pBuffer, dwSize);
	}
	DWORD WINAPI GetBin(const wchar_t* sec, const wchar_t* key, BYTE* pBuffer, DWORD dwSize) override {
		return KpiV5GetBin(m_pluginName, sec ? sec : L"", key ? key : L"", pBuffer, dwSize);
	}
};

// kpi_CreateInstance に渡すプロバイダ。プラグインが IKpiConfig を要求したら DummyConfig を渡す。
class HostProvider : public IKpiUnkProvider
{
	long m_ref = 1;
	std::wstring m_pluginName;
public:
	HostProvider(const wchar_t* kpiPath) : m_pluginName(KpiV5PluginNameFromPath(kpiPath ? kpiPath : L"")) {}
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
			if (pvParam2) *(DWORD*)pvParam2 = 0;
			*ppvObj = (IKpiConfig*)new DummyConfig(m_pluginName.c_str());
			return 1;
		}
		(void)pvParam1;
		return 0;
	}
};

// 壊れた KPI が SEH で落ちてもホストごと死なないように包む。
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

// 壊れた KPI の Open が SEH で落ちてもホストを生かす。
static DWORD SafeModuleOpen(IKpiDecoderModule* mod, const KPI_MEDIAINFO* req, IKpiFile* file, IKpiFolder* folder, IKpiDecoder** ppDec)
{
	DWORD count = 0;
	__try {
		count = mod->Open(req, file, folder, ppDec);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		count = 0;
	}
	return count;
}

// 曲番号を選び KPI_MEDIAINFO を得る。タグは捨てる（NullTagInfo）。
static DWORD SafeDecoderSelect(IKpiDecoder* dec, uint32_t songNo, const KPI_MEDIAINFO** ppSel)
{
	DWORD selected = 0;
	class NullTagInfo : public IKpiTagInfo
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
			if (riid == IID_IUnknown || riid == IID_IKpiTagInfo) { *ppv = static_cast<IKpiTagInfo*>(this); AddRef(); return S_OK; }
			*ppv = NULL;
			return E_NOINTERFACE;
		}
		DWORD WINAPI GetTagInfo(IKpiFile*, IKpiFolder*, DWORD, DWORD) override { return 1; }
		DWORD WINAPI GetValue(const wchar_t*, wchar_t* pszValue, int nSize) override {
			if (pszValue && nSize > 0) pszValue[0] = 0;
			return 0;
		}
		void WINAPI SetOverwrite(BOOL) override {}
		void WINAPI SetPicture(DWORD, const wchar_t*, const wchar_t*, const wchar_t*, DWORD, DWORD, const BYTE*, DWORD) override {}
		void WINAPI aSetValueA(const char*, int, const char*, int) override {}
		void WINAPI aSetValueW(const char*, int, const wchar_t*, int) override {}
		void WINAPI aSetValueU8(const char*, int, const char*, int) override {}
		void WINAPI wSetValueA(const wchar_t*, int, const char*, int) override {}
		void WINAPI wSetValueW(const wchar_t*, int, const wchar_t*, int) override {}
		void WINAPI wSetValueU8(const wchar_t*, int, const char*, int) override {}
		void WINAPI u8SetValueA(const char*, int, const char*, int) override {}
		void WINAPI u8SetValueW(const char*, int, const wchar_t*, int) override {}
		void WINAPI u8SetValueU8(const char*, int, const char*, int) override {}
	};
	NullTagInfo tagInfo;
	__try {
		selected = dec->Select(songNo, ppSel, &tagInfo, KPI_TAGGET_FLAG_NONE);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		selected = 0;
	}
	return selected;
}

static DWORD SafeDecoderRender(IKpiDecoder* dec, BYTE* pBuffer, DWORD samples, bool* pHadException)
{
	DWORD got = 0;
	if (pHadException) *pHadException = false;
	__try {
		got = dec->Render(pBuffer, samples);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		got = 0;
		if (pHadException) *pHadException = true;
	}
	return got;
}

static UINT64 SafeDecoderSeek(IKpiDecoder* dec, UINT64 posSample, DWORD flag, bool* pHadException)
{
	if (pHadException) *pHadException = false;
	UINT64 ret = 0;
	__try {
		ret = dec->Seek(posSample, flag);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		ret = 0;
		if (pHadException) *pHadException = true;
	}
	return ret;
}

class HostFile;

// KPI が「同じフォルダの .minipsf / .psf2lib」を開くためのフォルダ実装。
class HostFolder : public IKpiFolder
{
	long m_ref = 1;
	std::wstring m_baseDir; // メディアファイルのあるディレクトリ
public:
	HostFolder(const std::wstring& baseDir) : m_baseDir(baseDir) {}
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
		const DWORD need = (DWORD)((m_baseDir.size() + 1) * sizeof(wchar_t));
		if (pszName && dwSize >= sizeof(wchar_t)) {
			if (dwSize >= need) wcscpy_s(pszName, dwSize / sizeof(wchar_t), m_baseDir.c_str());
			else pszName[0] = 0;
		}
		return need;
	}
	DWORD WINAPI EnumFiles(DWORD, wchar_t* pszName, DWORD dwSize, DWORD) override {
		if (pszName && dwSize >= 2) pszName[0] = 0;
		return 0;
	}
	BOOL WINAPI OpenFile(const wchar_t* cszName, IKpiFile** ppFile) override;
	BOOL WINAPI OpenFolder(const wchar_t* cszName, IKpiFolder** ppFolder) override;
};

// KPI の IKpiFile。巨大 psf2lib は Read を分割し、GetBuffer は一度メモリへ載せる。
class HostFile : public IKpiFile
{
	long m_ref = 1;
	HANDLE m_h = INVALID_HANDLE_VALUE;
	std::wstring m_path; // フルパス（GetRealFileW / CreateClone 用）
	std::wstring m_name; // ファイル名だけ（GetFileName）
	BYTE* m_buf = NULL;  // GetBuffer 用キャッシュ。未要求なら NULL
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
		if (m_h == INVALID_HANDLE_VALUE) return false;
		LARGE_INTEGER sz{};
		if (GetFileSizeEx(m_h, &sz)) {
			AppendHostLogLine((L"[HostFile] opened bytes=" + std::to_wstring(sz.QuadPart) + L" path=" + path).c_str());
		}
		return true;
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
		DWORD total = 0;
		BYTE* ptr = (BYTE*)pBuffer;
		// 巨大なpsf2libファイルなどを最後まで確実に読み切るためのループですわ！
		while (dwSize > 0) {
			DWORD rd = 0;
			if (!ReadFile(m_h, ptr, dwSize, &rd, NULL) || rd == 0) break;
			ptr += rd;
			total += rd;
			dwSize -= rd;
		}
		return total;
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

static bool IsAbsoluteLikePath(const std::wstring& path)
{
	if (path.size() >= 2 && path[1] == L':') return true;
	if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') return true;
	if (!path.empty() && (path[0] == L'\\' || path[0] == L'/')) return true;
	return false;
}

static std::wstring JoinPathSimple(const std::wstring& baseDir, const std::wstring& name)
{
	if (name.empty()) return baseDir;
	if (IsAbsoluteLikePath(name)) return name;
	if (baseDir.empty()) return name;
	if (baseDir.back() == L'\\' || baseDir.back() == L'/') return baseDir + name;
	return baseDir + L"\\" + name;
}

static std::wstring TrimString(const std::wstring& str) {
	size_t first = str.find_first_not_of(L" \t\r\n");
	if (first == std::wstring::npos) return L"";
	size_t last = str.find_last_not_of(L" \t\r\n");
	return str.substr(first, (last - first + 1));
}

static std::wstring UrlDecode(const std::wstring& str) {
	// PSF の lib 名が %XX のことがある。先に UTF-8 バイトとして %XX を戻し、
	// MultiByteToWideChar できなければ 1 バイトずつ widen する。
	std::string bytes;
	bytes.reserve(str.size());
	for (size_t i = 0; i < str.length(); ++i) {
		if (str[i] == L'%' && i + 2 < str.length()) {
			wchar_t hex[3] = { str[i + 1], str[i + 2], 0 };
			wchar_t* end = NULL;
			long c = wcstol(hex, &end, 16);
			if (end == hex + 2 && c >= 0 && c <= 255) {
				bytes.push_back((char)c);
				i += 2;
				continue;
			}
		}
		if (str[i] <= 0x7F) {
			bytes.push_back((char)str[i]);
		}
		else {
			// ワイドの非 ASCII はこの経路では捨てる（lib 名はほぼ ASCII）。
			bytes.push_back('?');
		}
	}
	if (bytes.empty()) return L"";
	int wlen = MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), (int)bytes.size(), NULL, 0);
	if (wlen > 0) {
		std::wstring out;
		out.resize((size_t)wlen);
		MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), (int)bytes.size(), &out[0], wlen);
		return out;
	}
	// UTF-8 にならなかったときの最終手段。
	std::wstring out;
	out.reserve(bytes.size());
	for (size_t i = 0; i < bytes.size(); ++i) out.push_back((unsigned char)bytes[i]);
	return out;
}

// PSF の lib 名。URL デコードしてから開き、失敗したら生の名前で再試行。
BOOL WINAPI HostFolder::OpenFile(const wchar_t* cszName, IKpiFile** ppFile)
{
	if (!ppFile) return FALSE;
	*ppFile = NULL;
	if (!cszName || !cszName[0]) return FALSE;

	std::wstring nameStr = TrimString(cszName);
	if (!nameStr.empty() && nameStr.front() == L'"' && nameStr.back() == L'"') {
		nameStr = nameStr.substr(1, nameStr.size() - 2);
	}
	std::wstring decodedName = UrlDecode(nameStr);
	std::wstring fullPath = JoinPathSimple(m_baseDir, decodedName);
	AppendHostLogLine((L"[OpenFile] req=" + nameStr + L" decoded=" + decodedName + L" base=" + m_baseDir).c_str());

	auto* f = new HostFile();
	if (!f->Open(fullPath)) {
		f->Release();

		fullPath = JoinPathSimple(m_baseDir, nameStr);
		f = new HostFile();
		if (!f->Open(fullPath)) {
			f->Release();
			AppendHostLogLine((L"[OpenFile] FAILED req=" + nameStr + L" path=" + fullPath).c_str());
			return FALSE;
		}
	}
	AppendHostLogLine((L"[OpenFile] SUCCESS req=" + nameStr + L" path=" + fullPath).c_str());
	*ppFile = f;
	return TRUE;
}

BOOL WINAPI HostFolder::OpenFolder(const wchar_t* cszName, IKpiFolder** ppFolder)
{
	if (!ppFolder) return FALSE;
	*ppFolder = NULL;
	std::wstring folderPath = m_baseDir;
	if (cszName && cszName[0]) {
		std::wstring nameStr = TrimString(cszName);
		std::wstring decodedName = UrlDecode(nameStr);
		folderPath = JoinPathSimple(m_baseDir, decodedName);
	}
	auto* folder = new HostFolder(folderPath);
	*ppFolder = folder;
	return TRUE;
}

// 開いている KPI デコーダ 1 本。Render/Seek/Close は sessionId でここを探す。
struct Session
{
	HMODULE hDll = NULL;                 // LoadLibrary した KPI DLL。Close で FreeLibrary
	IKpiDecoderModule* mod = NULL;
	IKpiDecoder* dec = NULL;
	HostFile* file = NULL;
	HostFolder* folder = NULL;
	KPI_MEDIAINFO request{};             // 本体が希望したフォーマット
	KPI_MEDIAINFO selected{};            // 実際に開いたフォーマット
	int sourceBitsPerSample = 16;        // MIDI シークの破棄 Render でバッファサイズ計算
	DWORD openedSongCount = 0;           // マルチソング形式の曲数
	DWORD channels = 2;
	DWORD bps = 16;                      // 絶対値（float は nBitsPerSample が負）
	uint32_t zeroRenderStreak = 0;       // 連続 0 サンプル。ループ無し曲の EOF 判定
	std::wstring mediaPath;              // MIDI なら Seek を「先頭＋破棄再生」にする
};

static uint32_t g_nextSessionId = 1;
static std::unordered_map<uint32_t, Session> g_sessions;

// パイプはバイトモードなので、要求サイズまで繰り返して読む。途中で切れたらクライアント切断。
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

// ペイロードから [u32 文字数][wchar_t[]] を読む。p を進める。
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

// KPI DLL を一時ロードして supportExts を取る。セッションは残さない。
static uint32_t Cmd_ListExts(const std::wstring& kpiPath, std::vector<uint8_t>& out)
{
	out.clear();
	const std::wstring kpiDir = DirNameOf(kpiPath);
	ScopedDllDirectory addDir(kpiDir);
	ScopedDllDirectory addParentDir(ParentDirOf(kpiDir));
	ScopedDllDirectories addExeSubDirs(GetExeRelatedDllDirs());
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

	if (auto cr = (pfn_kpiCreateInstance)GetProcAddress(h, "kpi_CreateInstance")) {
		IKpiDecoderModule* mod = NULL;
		HostProvider* prov = new HostProvider(kpiPath.c_str());
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

	if (exts.empty()) {
		// v5 入口が無い／空なら旧 KMP の GetKMPModule。
		if (auto fn = (pfnGetKMPModule)GetProcAddress(h, SZ_KMP_GETMODULE)) {
			KMPMODULE* m = fn();
			if (m && m->ppszSupportExts) {
				for (int i = 0; m->ppszSupportExts[i] != NULL; i++) {
					if (i != 0) exts += L"/";
					const char* e = m->ppszSupportExts[i];
					if (!e) continue;
					int wlen = MultiByteToWideChar(CP_ACP, 0, e, -1, NULL, 0);
					if (wlen > 1) {
						std::wstring ws;
						ws.resize((size_t)wlen - 1);
						MultiByteToWideChar(CP_ACP, 0, e, -1, ws.data(), wlen);
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

// メディアを開き Session をマップへ入れる。成功時 out は OpenReply + KPI_MEDIAINFO。
static uint32_t Cmd_Open(const std::wstring& kpiPath, const std::wstring& mediaPath, const KPI_MEDIAINFO& request, uint32_t songNo, std::vector<uint8_t>& out)
{
	out.clear();
	AppendHostLogLine((L"[OPEN] begin kpi=" + kpiPath + L" media=" + mediaPath + L" songNo=" + std::to_wstring(songNo)).c_str());
	const std::wstring kpiDir = DirNameOf(kpiPath);
	ScopedDllDirectory addDir(kpiDir);
	ScopedDllDirectory addParentDir(ParentDirOf(kpiDir));
	ScopedDllDirectories addExeSubDirs(GetExeRelatedDllDirs());
	HMODULE h = LoadLibraryExW(
		kpiPath.c_str(),
		NULL,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
	);
	if (!h) {
		AppendHostLogLine((L"[OPEN] LoadLibraryExW failed err=" + std::to_wstring(GetLastError()) + L" kpi=" + kpiPath).c_str());
		return KPIHOST64_STATUS_FAIL;
	}
	AppendHostLogLine(L"[OPEN] LoadLibraryExW ok");
	auto cr = (pfn_kpiCreateInstance)GetProcAddress(h, "kpi_CreateInstance");
	if (!cr) { FreeLibrary(h); return KPIHOST64_STATUS_NOT_SUPPORTED; }

	IKpiDecoderModule* mod = NULL;
	HostProvider* prov = new HostProvider(kpiPath.c_str());
	HRESULT hr = SafeKpiCreateInstance(cr, IID_IKpiDecoderModule, (void**)&mod, (IKpiUnknown*)prov);
	prov->Release();
	if (hr != S_OK || !mod) { FreeLibrary(h); return KPIHOST64_STATUS_FAIL; }
	AppendHostLogLine(L"[OPEN] kpi_CreateInstance ok");

	auto* f = new HostFile();
	if (!f->Open(mediaPath)) { f->Release(); mod->Release(); FreeLibrary(h); return KPIHOST64_STATUS_NOT_FOUND; }
	auto* folder = new HostFolder(DirNameOf(mediaPath));
	AppendHostLogLine((L"[OPEN] media folder=" + DirNameOf(mediaPath)).c_str());

	IKpiDecoder* dec = NULL;
	DWORD count = SafeModuleOpen(mod, &request, f, folder, &dec);
	if (!dec || count == 0) {
		AppendHostLogLine((L"[OPEN] mod->Open failed count=" + std::to_wstring(count) + L" kpi=" + kpiPath + L" media=" + mediaPath).c_str());
		if (dec) dec->Release();
		f->Release();
		folder->Release();
		mod->Release();
		FreeLibrary(h);
		return KPIHOST64_STATUS_FAIL;
	}
	AppendHostLogLine((L"[OPEN] mod->Open ok count=" + std::to_wstring(count)).c_str());

	const KPI_MEDIAINFO* sel = NULL;
	uint32_t selNo = (songNo == 0) ? 1 : songNo;
	DWORD selected = SafeDecoderSelect(dec, selNo, &sel);
	if (selected == 0 || !sel) {
		AppendHostLogLine((L"[OPEN] dec->Select failed selected=" + std::to_wstring(selected)).c_str());
		dec->Release();
		f->Release();
		folder->Release();
		mod->Release();
		FreeLibrary(h);
		return KPIHOST64_STATUS_FAIL;
	}
	AppendHostLogLine(L"[OPEN] dec->Select ok");
	AppendHostLogLine((L"[OPEN] mediaInfo rate=" + std::to_wstring(sel->dwSampleRate) +
		L" ch=" + std::to_wstring(sel->dwChannels) +
		L" bps=" + std::to_wstring(sel->nBitsPerSample) +
		L" length100ns=" + std::to_wstring((unsigned long long)sel->qwLength)).c_str());

	Session s{};
	s.hDll = h;
	s.mod = mod;
	s.dec = dec;
	s.file = f;
	s.folder = folder;
	s.request = request;
	s.selected = *sel;
	s.sourceBitsPerSample = s.selected.nBitsPerSample;

	s.openedSongCount = count;
	s.channels = s.selected.dwChannels ? s.selected.dwChannels : 2;
	s.bps = (DWORD)(s.selected.nBitsPerSample ? (s.selected.nBitsPerSample < 0 ? -s.selected.nBitsPerSample : s.selected.nBitsPerSample) : 16);
	if (s.bps == 0) s.bps = 16;
	s.mediaPath = mediaPath;

	const uint32_t id = g_nextSessionId++;
	g_sessions[id] = s;

	KPIHOST64_OpenReply rep{};
	rep.sessionId = id;
	rep.openedSongCount = count;

	out.resize(sizeof(rep) + sizeof(KPI_MEDIAINFO));
	memcpy(out.data(), &rep, sizeof(rep));
	memcpy(out.data() + sizeof(rep), &s.selected, sizeof(KPI_MEDIAINFO));
	AppendHostLogLine((L"[OPEN] success sessionId=" + std::to_wstring(id)).c_str());
	return KPIHOST64_STATUS_OK;
}

// PCM を samplesWanted まで読む。KPI は 576 サンプルずつ。out は RenderReply + PCM。
static uint32_t Cmd_Render(uint32_t sessionId, uint32_t bytesWanted, std::vector<uint8_t>& out)
{
	out.clear();
	auto it = g_sessions.find(sessionId);
	if (it == g_sessions.end()) return KPIHOST64_STATUS_NOT_FOUND;
	Session& s = it->second;
	if (!s.dec) return KPIHOST64_STATUS_FAIL;

	const uint32_t bytesPerFrame = s.channels * (s.bps / 8);
	if (bytesPerFrame == 0) return KPIHOST64_STATUS_BAD_REQUEST;
	uint32_t samplesWanted = bytesWanted / bytesPerFrame;
	if (samplesWanted == 0) return KPIHOST64_STATUS_BAD_REQUEST;
	if (samplesWanted > 65536) samplesWanted = 65536;

	std::vector<uint8_t> pcm;
	uint32_t gotBytes = 0;
	DWORD gotSamples = 0;
	bool hadRenderException = false;
	pcm.reserve((size_t)samplesWanted * bytesPerFrame);
	DWORD remain = samplesWanted;
	const DWORD kChunkSamples = 576; // 小さめに切って KPI の内部バッファ溢れを避ける

	while (remain > 0) {
		const DWORD ask = (remain > kChunkSamples) ? kChunkSamples : remain;
		std::vector<uint8_t> part;
		part.resize((size_t)ask * bytesPerFrame);
		DWORD got = SafeDecoderRender(s.dec, part.data(), ask, &hadRenderException);
		if (got == 0 && hadRenderException && s.selected.qwLoop == (UINT64)-1) {
			// 無限ループ曲で SEH したら先頭へ戻して一度だけリトライ。
			bool seekEx = false;
			SafeDecoderSeek(s.dec, 0, 0, &seekEx);
			got = SafeDecoderRender(s.dec, part.data(), ask, &hadRenderException);
		}
		if (got == 0) break;
		uint32_t partBytes = got * bytesPerFrame;
		if (partBytes > part.size()) partBytes = (uint32_t)part.size();
		pcm.insert(pcm.end(), part.begin(), part.begin() + partBytes);
		gotSamples += got;
		gotBytes += partBytes;
		remain -= got;
	}

	KPIHOST64_RenderReply rep{};
	rep.sessionId = sessionId;
	rep.bytesReturned = gotBytes;
	if (gotSamples == 0) s.zeroRenderStreak++; else s.zeroRenderStreak = 0;
	if (s.selected.qwLoop == (UINT64)-1) rep.eof = 0; // 無限ループは短い読みでも終わらせない
	else rep.eof = (s.zeroRenderStreak >= 3) ? 1 : 0;

	out.resize(sizeof(rep) + gotBytes);
	memcpy(out.data(), &rep, sizeof(rep));
	if (gotBytes) memcpy(out.data() + sizeof(rep), pcm.data(), gotBytes);
	return KPIHOST64_STATUS_OK;
}

// MIDI KPI は Seek が音色を戻さないので、この拡張子だけ破棄再生でシークする。
static bool IsMidiLikePathW(const std::wstring& path)
{
	size_t d = path.find_last_of(L'.');
	if (d == std::wstring::npos) return false;
	std::wstring e = path.substr(d);
	for (size_t i = 0; i < e.size(); ++i) {
		wchar_t c = e[i];
		if (c >= L'A' && c <= L'Z') e[i] = (wchar_t)(c - L'A' + L'a');
	}
	return e == L".mid" || e == L".midi" || e == L".kar" || e == L".rmi";
}

// MIDI は破棄 Render でシーク。それ以外はデコーダの Seek。
static uint32_t Cmd_Seek(uint32_t sessionId, uint64_t posSample, uint32_t flag, std::vector<uint8_t>& out)
{
	out.clear();
	auto it = g_sessions.find(sessionId);
	if (it == g_sessions.end()) return KPIHOST64_STATUS_NOT_FOUND;
	Session& s = it->second;
	if (!s.dec) return KPIHOST64_STATUS_FAIL;

	UINT64 newPos = 0;
	if (IsMidiLikePathW(s.mediaPath)) {
		// MIDI KPI: プラグインの Seek は音色/音量を復元しないことが多い。
		// 先頭へ戻してから目標サンプルまで Render 破棄で超高速再生する。
		s.zeroRenderStreak = 0;
		bool seekEx = false;
		SafeDecoderSeek(s.dec, 0, KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE, &seekEx);
		(void)seekEx;
		uint64_t left = posSample;
		const DWORD ch = s.channels ? s.channels : 2;
		const int srcBits = s.sourceBitsPerSample;
		const DWORD chunk = 8192;
		std::vector<uint8_t> junk;
		while (left > 0) {
			DWORD ask = (DWORD)((left > chunk) ? chunk : left);
			if (srcBits == -32) {
				junk.resize((size_t)ask * ch * sizeof(float));
			} else if (srcBits == -64) {
				junk.resize((size_t)ask * ch * sizeof(double));
			} else {
				DWORD bps = s.bps ? s.bps : 16;
				junk.resize((size_t)ask * ch * ((bps ? bps : 16) / 8));
			}
			bool hadEx = false;
			DWORD got = SafeDecoderRender(s.dec, junk.data(), ask, &hadEx);
			(void)hadEx;
			if (got == 0) break;
			if ((uint64_t)got > left) got = (DWORD)left;
			left -= got;
			newPos += got;
		}
	} else {
		newPos = s.dec->Seek(posSample, flag);
	}

	KPIHOST64_SeekReply rep{};
	rep.sessionId = sessionId;
	rep.newPosSample = newPos;
	out.resize(sizeof(rep));
	memcpy(out.data(), &rep, sizeof(rep));
	return KPIHOST64_STATUS_OK;
}

// デコーダ・ファイル・DLL を解放してマップから消す。
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

// 1 クライアント接続のあいだ、ヘッダ＋ペイロードを読んで応答する。切断で抜ける。
static void ServeOnce(HANDLE pipe)
{
	for (;;) {
		KPIHOST64_MsgHeader h{};
		// バイトモードなのでヘッダが分割到着する。短い読みで切ると曲の途中で落ちる。
		if (!ReadExact(pipe, &h, sizeof(h))) break;

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
			// 任意ペイロード: u32 lang。ホスト側 VST メッセージの言語に使う。
			if (payload.size() >= sizeof(uint32_t)) {
				int lang = (int)*(const uint32_t*)payload.data();
				if (lang < 0 || lang > 13) lang = 1;
				savedata.lang = lang;
			}
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
		case KPIHOST64_CMD_FOREIGN_LIST_EXTS: {
			if (end - p < 4) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			uint32_t kind = *(const uint32_t*)p; p += 4;
			std::wstring path;
			if (!ReadWString(p, end, path) || p != end) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			std::wstring exts;
			status = ForeignHost_ListExts(kind, path, exts);
			if (status == KPIHOST64_STATUS_OK) {
				KPIHOST64_ListExtsReply lr{};
				lr.kpiVer = 0;
				reply.resize(sizeof(lr));
				memcpy(reply.data(), &lr, sizeof(lr));
				uint32_t n = (uint32_t)exts.size();
				size_t off = reply.size();
				reply.resize(off + 4 + n * sizeof(wchar_t));
				memcpy(reply.data() + off, &n, 4);
				if (n) memcpy(reply.data() + off + 4, exts.data(), n * sizeof(wchar_t));
			}
			break;
		}
		case KPIHOST64_CMD_FOREIGN_OPEN: {
			if (end - p < 4) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			uint32_t kind = *(const uint32_t*)p; p += 4;
			std::wstring dll, media;
			if (!ReadWString(p, end, dll)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			if (!ReadWString(p, end, media) || p != end) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			KPIHOST64_ForeignOpenReply fr{};
			status = ForeignHost_Open(kind, dll, media, fr);
			if (status == KPIHOST64_STATUS_OK) {
				reply.resize(sizeof(fr));
				memcpy(reply.data(), &fr, sizeof(fr));
			}
			break;
		}
		case KPIHOST64_CMD_FOREIGN_RENDER: {
			if ((size_t)(end - p) != sizeof(KPIHOST64_RenderReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* rr = (const KPIHOST64_RenderReq*)p;
			std::vector<uint8_t> pcm;
			uint32_t eof = 0;
			status = ForeignHost_Render(rr->sessionId, rr->bytesWanted, pcm, eof);
			if (status == KPIHOST64_STATUS_OK) {
				KPIHOST64_RenderReply rrep{};
				rrep.sessionId = rr->sessionId;
				rrep.bytesReturned = (uint32_t)pcm.size();
				rrep.eof = eof;
				reply.resize(sizeof(rrep) + pcm.size());
				memcpy(reply.data(), &rrep, sizeof(rrep));
				if (!pcm.empty()) memcpy(reply.data() + sizeof(rrep), pcm.data(), pcm.size());
			}
			break;
		}
		case KPIHOST64_CMD_FOREIGN_SEEK: {
			if ((size_t)(end - p) != sizeof(KPIHOST64_SeekReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* sr = (const KPIHOST64_SeekReq*)p;
			status = ForeignHost_Seek(sr->sessionId, sr->posSample);
			if (status == KPIHOST64_STATUS_OK) {
				KPIHOST64_SeekReply srep{};
				srep.sessionId = sr->sessionId;
				srep.newPosSample = sr->posSample;
				reply.resize(sizeof(srep));
				memcpy(reply.data(), &srep, sizeof(srep));
			}
			break;
		}
		case KPIHOST64_CMD_FOREIGN_CLOSE: {
			if ((size_t)(end - p) != sizeof(KPIHOST64_U32)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* u = (const KPIHOST64_U32*)p;
			status = ForeignHost_Close(u->v);
			break;
		}
		case KPIHOST64_CMD_VST_OPEN: {
			// payload: [u32 slot][u32 midChars][mid][u32 dllChars][dll][u32 extraChars][extra]
			if ((size_t)(end - p) < sizeof(uint32_t) * 2) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			uint32_t slot = *(const uint32_t*)p; p += sizeof(uint32_t);
			if (slot > 1) slot = 0;
			uint32_t nMid = *(const uint32_t*)p; p += sizeof(uint32_t);
			if ((size_t)(end - p) < nMid * sizeof(wchar_t)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			std::wstring mid((const wchar_t*)p, (const wchar_t*)p + nMid);
			p += nMid * sizeof(wchar_t);
			std::wstring dll, extra;
			if ((size_t)(end - p) >= sizeof(uint32_t)) {
				uint32_t nDll = *(const uint32_t*)p; p += sizeof(uint32_t);
				if ((size_t)(end - p) < nDll * sizeof(wchar_t)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
				dll.assign((const wchar_t*)p, (const wchar_t*)p + nDll);
				p += nDll * sizeof(wchar_t);
			}
			if ((size_t)(end - p) >= sizeof(uint32_t)) {
				uint32_t nEx = *(const uint32_t*)p; p += sizeof(uint32_t);
				if ((size_t)(end - p) < nEx * sizeof(wchar_t)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
				extra.assign((const wchar_t*)p, (const wchar_t*)p + nEx);
			}
			status = VstHost64_Open((int)slot, mid.c_str(),
				dll.empty() ? nullptr : dll.c_str(),
				extra.empty() ? nullptr : extra.c_str());
			if (status == KPIHOST64_STATUS_OK) {
				KPIHOST64_ForeignOpenReply orp{};
				orp.sessionId = slot;
				orp.sampleRate = (uint32_t)VstHost64_Rate((int)slot);
				orp.channels = (uint32_t)VstHost64_Channels((int)slot);
				orp.bitsPerSample = VstHost64_Bits((int)slot);
				orp.lengthSamples = VstHost64_Length((int)slot);
				orp.latencySamples = (uint32_t)VstHost64_Latency((int)slot);
				reply.resize(sizeof(orp));
				memcpy(reply.data(), &orp, sizeof(orp));
			}
			break;
		}
		case KPIHOST64_CMD_VST_RENDER: {
			// RenderReq の後ろに任意で注入ショート（モニタ／鍵盤からの CC）。次ブロックで送る。
			if ((size_t)(end - p) < sizeof(KPIHOST64_RenderReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* rr = (const KPIHOST64_RenderReq*)p;
			int slot = (rr->sessionId == 1) ? 1 : 0;
			const uint8_t* q = p + sizeof(KPIHOST64_RenderReq);
			if ((size_t)(end - q) >= sizeof(uint32_t)) {
				uint32_t nInj = *(const uint32_t*)q; q += sizeof(uint32_t);
				if (nInj > 64) nInj = 64;
				VstMidiSetIoSlot(slot);
				for (uint32_t i = 0; i < nInj; ++i) {
					if ((size_t)(end - q) < sizeof(KPIHOST64_VstLiveMidiReq)) break;
					auto* mr = (const KPIHOST64_VstLiveMidiReq*)q;
					VstMidiInjectShort((int)mr->port, mr->msg, (int)mr->sampleOfs);
					q += sizeof(KPIHOST64_VstLiveMidiReq);
				}
			}
			std::vector<uint8_t> pcm;
			uint32_t eof = 0;
			status = VstHost64_Render(slot, rr->bytesWanted, pcm, eof);
			if (status == KPIHOST64_STATUS_OK) {
				KPIHOST64_RenderReply rrep{};
				rrep.sessionId = rr->sessionId;
				rrep.bytesReturned = (uint32_t)pcm.size();
				rrep.eof = eof;
				reply.resize(sizeof(rrep) + pcm.size());
				memcpy(reply.data(), &rrep, sizeof(rrep));
				if (!pcm.empty())
					memcpy(reply.data() + sizeof(rrep), pcm.data(), pcm.size());
			}
			break;
		}
		case KPIHOST64_CMD_VST_SEEK: {
			if ((size_t)(end - p) < sizeof(KPIHOST64_SeekReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* sr = (const KPIHOST64_SeekReq*)p;
			int slot = (sr->sessionId == 1) ? 1 : 0;
			status = VstHost64_Seek(slot, sr->posSample);
			if (status == KPIHOST64_STATUS_OK) {
				KPIHOST64_SeekReply srep{};
				srep.sessionId = sr->sessionId;
				srep.newPosSample = sr->posSample;
				reply.resize(sizeof(srep));
				memcpy(reply.data(), &srep, sizeof(srep));
			}
			break;
		}
		case KPIHOST64_CMD_VST_CLOSE: {
			if ((size_t)(end - p) >= sizeof(KPIHOST64_U32)) {
				auto* u = (const KPIHOST64_U32*)p;
				status = VstHost64_Close((int)u->v);
			} else {
				status = VstHost64_CloseAll();
			}
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_LOAD: {
			if ((size_t)(end - p) < sizeof(KPIHOST64_VstLiveLoadReq) + sizeof(uint32_t)) {
				status = KPIHOST64_STATUS_BAD_REQUEST; break;
			}
			auto* lr = (const KPIHOST64_VstLiveLoadReq*)p;
			p += sizeof(KPIHOST64_VstLiveLoadReq);
			uint32_t nPath = *(const uint32_t*)p; p += sizeof(uint32_t);
			if ((size_t)(end - p) < nPath * sizeof(wchar_t) || !nPath) {
				status = KPIHOST64_STATUS_BAD_REQUEST; break;
			}
			std::wstring path((const wchar_t*)p, (const wchar_t*)p + nPath);
			status = VstHost64_LiveLoad(lr->part, path.c_str(), lr->isVst3);
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_UNLOAD: {
			if ((size_t)(end - p) < sizeof(KPIHOST64_U32)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			status = VstHost64_LiveUnload(((const KPIHOST64_U32*)p)->v);
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_UNLOAD_ALL: {
			status = VstHost64_LiveUnloadAll();
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_MIDI: {
			if ((size_t)(end - p) < sizeof(KPIHOST64_VstLiveMidiReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* mr = (const KPIHOST64_VstLiveMidiReq*)p;
			status = VstHost64_LiveMidi(mr->port, mr->msg);
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_SYSEX: {
			if ((size_t)(end - p) < sizeof(KPIHOST64_VstLiveSysexReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* sr = (const KPIHOST64_VstLiveSysexReq*)p;
			p += sizeof(KPIHOST64_VstLiveSysexReq);
			if ((size_t)(end - p) < sr->bytes) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			status = VstHost64_LiveSysex(sr->port, (const uint8_t*)p, sr->bytes);
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_RENDER: {
			if ((size_t)(end - p) < sizeof(KPIHOST64_VstLiveRenderReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* rr = (const KPIHOST64_VstLiveRenderReq*)p;
			status = VstHost64_LiveRender(rr->frames, reply);
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_AUDIO_START: {
			status = VstHost64_LiveAudioStart();
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_AUDIO_STOP: {
			status = VstHost64_LiveAudioStop();
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_EDITOR_OPEN: {
			if ((size_t)(end - p) < sizeof(KPIHOST64_U32)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			status = VstHost64_LiveEditorOpen(((const KPIHOST64_U32*)p)->v);
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_EDITOR_CLOSE: {
			if ((size_t)(end - p) < sizeof(KPIHOST64_U32)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			status = VstHost64_LiveEditorClose(((const KPIHOST64_U32*)p)->v);
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_SEND_CH: {
			if ((size_t)(end - p) < sizeof(KPIHOST64_VstLiveSendChReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* sc = (const KPIHOST64_VstLiveSendChReq*)p;
			status = VstHost64_LiveSetSendChannel(sc->part, sc->sendCh);
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_PROGRAMS: {
			if ((size_t)(end - p) < sizeof(KPIHOST64_VstLiveProgramsReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* pr = (const KPIHOST64_VstLiveProgramsReq*)p;
			status = VstHost64_LivePrograms(pr->part, pr->first, pr->count, reply);
			break;
		}
		case KPIHOST64_CMD_VST_LIVE_SET_PROGRAM: {
			if ((size_t)(end - p) < sizeof(KPIHOST64_VstLiveSetProgramReq)) { status = KPIHOST64_STATUS_BAD_REQUEST; break; }
			auto* sp = (const KPIHOST64_VstLiveSetProgramReq*)p;
			status = VstHost64_LiveSetProgram(sp->part, sp->index);
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

	HANDLE pipe = CreateNamedPipeW(
		KPIHOST64_PIPE_NAME,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
		1,                 // インスタンス 1。本体はパイプを直列利用
		1024 * 1024,
		1024 * 1024,
		0,
		NULL
	);
	if (pipe == INVALID_HANDLE_VALUE) return 2;

	const DWORD idleMs = 30000; // 接続待ちタイムアウト。セッションが空ならプロセス終了
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
			if (g_sessions.empty() && !VstHost64_LiveActive()) {
				// アイドル窓のあいだ本体が戻ってこない＝ストリーミング中の曲も戻らない。
				if (VstHost64_SongActive())
					(void)VstHost64_CloseAll();
				break;
			}
			continue;
		}

		CloseHandle(ov.hEvent);
		ServeOnce(pipe);
		DisconnectNamedPipe(pipe);
		// ライブパートは本体が所有する。本体が切れたあとも載せると、
		// LiveActive がアイドル終了を止めてホスト（とプラグイン）が残る。
		if (VstHost64_LiveActive())
			(void)VstHost64_LiveUnloadAll();
	}

	for (auto& kv : g_sessions) {
		(void)Cmd_Close(kv.first);
	}
	g_sessions.clear();
	CloseHandle(pipe);
	return 0;
}