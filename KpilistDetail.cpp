// プラグイン一覧の下段。モジュール情報と、KPI設定画面と同じレジストリを編集する。
#include "stdafx.h"
#include "ogg.h"
#include "Kpilist.h"
#include "PluginKinds.h"
#include "KpiV5ConfigStore.h"
#include "KpiHostClient.h"
#include "kpi_decoder.h"
#include "kmp_pi.h"
#include "CCustomPopupMenu.h"
#include <shlobj.h>

extern int kpicnt;
extern CString kpif[400];
extern CString ext[400][300];
extern BYTE plugkind[400];
extern BYTE kpiarch[400];
extern BYTE kvar[400][300];
extern HINSTANCE hDLLk1[500];
extern KMPMODULE* mod1[500];
extern KpiHost32Client g_kpiHost;
extern void MpPersistSavedataQuick();

struct KpiWaPeek
{
	int version;
	char* description;
	HWND hMainWindow;
	HINSTANCE hDllInstance;
	char* FileExtensions;
	int is_seekable;
	int UsesOutputPlug;
	void (__cdecl* Config)(HWND);
	void (__cdecl* About)(HWND);
	int (__cdecl* Init)();
	void (__cdecl* Quit)();
};

static void KpiSafeWaConfig(void (__cdecl* fn)(HWND), HWND hwnd)
{
	if (!fn) return;
	__try { fn(hwnd); }
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Config 中にプラグインが作ったトップレベルだけ前面へ。所有者は触らない。
static HHOOK s_kpiWaHook;
static HWND s_kpiWaMade[8];
static int s_kpiWaMadeN;

static LRESULT CALLBACK KpiWaUiCbt(int code, WPARAM wp, LPARAM lp)
{
	if (code == HCBT_CREATEWND) {
		HWND h = (HWND)wp;
		CBT_CREATEWNDW* cw = (CBT_CREATEWNDW*)lp;
		if (h && cw && cw->lpcs && !(cw->lpcs->style & WS_CHILD) && s_kpiWaMadeN < 8)
			s_kpiWaMade[s_kpiWaMadeN++] = h;
	} else if (code == HCBT_ACTIVATE) {
		HWND h = (HWND)wp;
		for (int i = 0; i < s_kpiWaMadeN; ++i) {
			if (s_kpiWaMade[i] != h) continue;
			::SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
			::SetForegroundWindow(h);
			break;
		}
	}
	return CallNextHookEx(s_kpiWaHook, code, wp, lp);
}

static KpiWaPeek* KpiSafeWaGet(KpiWaPeek* (__cdecl* getIn)())
{
	KpiWaPeek* in = NULL;
	if (!getIn) return NULL;
	__try { in = getIn(); }
	__except (EXCEPTION_EXECUTE_HANDLER) { in = NULL; }
	return in;
}

typedef HRESULT(WINAPI* KpiCreateFn)(REFIID, void**, IKpiUnknown*);

static HRESULT KpiSafeCreate(KpiCreateFn cr, void** pp, IKpiUnknown* unk)
{
	HRESULT hr = E_FAIL;
	if (pp) *pp = NULL;
	if (!cr || !pp) return hr;
	__try { hr = cr(IID_IKpiDecoderModule, pp, unk); }
	__except (EXCEPTION_EXECUTE_HANDLER) { hr = E_FAIL; if (pp) *pp = NULL; }
	return hr;
}

static void KpiSafeModuleInfo(IKpiDecoderModule* mod, const KPI_DECODER_MODULEINFO** out)
{
	if (out) *out = NULL;
	if (!mod || !out) return;
	__try { mod->GetModuleInfo(out); }
	__except (EXCEPTION_EXECUTE_HANDLER) { *out = NULL; }
}

static void KpiSafeEnumConfig(IKpiDecoderModule* mod, IKpiConfigEnumerator* en)
{
	if (!mod || !en) return;
	__try { mod->EnumConfig(en); }
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

static KMPMODULE* KpiSafeKmp(pfnGetKMPModule fn)
{
	KMPMODULE* m = NULL;
	if (!fn) return NULL;
	__try { m = fn(); }
	__except (EXCEPTION_EXECUTE_HANDLER) { m = NULL; }
	return m;
}

static void KpiBlobEsc(CString& dst, const wchar_t* s)
{
	if (!s) return;
	for (const wchar_t* p = s; *p; ++p) {
		if (*p == L'\t' || *p == L'\r') continue;
		if (*p == L'\n') dst += (wchar_t)1;
		else dst += *p;
	}
}

static void KpiBlobI(CString& blob, const wchar_t* name, const wchar_t* val)
{
	if (!name || !name[0]) return;
	blob += L"I\t";
	KpiBlobEsc(blob, name);
	blob += L'\t';
	KpiBlobEsc(blob, val);
	blob += L'\n';
}

static void KpiAcp(CString& dst, const char* s)
{
	dst.Empty();
	if (!s || !s[0]) return;
	wchar_t w[1024];
	if (MultiByteToWideChar(CP_ACP, 0, s, -1, w, 1024) > 0)
		dst = w;
}

// 同一アーキの KPI。設定の実体は KpiV5（テキストの KPI 設定と同じ）。
class KpiListHost : public IKpiUnkProvider, public IKpiConfig
{
	long m_ref;
	wchar_t m_name[128];
public:
	explicit KpiListHost(const wchar_t* path) : m_ref(1)
	{
		m_name[0] = 0;
		std::wstring n = KpiV5PluginNameFromPath(path ? path : L"");
		wcsncpy_s(m_name, n.c_str(), _TRUNCATE);
	}
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		*ppv = NULL;
		if (riid == IID_IUnknown || riid == IID_IKpiUnkProvider)
			*ppv = static_cast<IKpiUnkProvider*>(this);
		else if (riid == IID_IKpiConfig)
			*ppv = static_cast<IKpiConfig*>(this);
		else
			return E_NOINTERFACE;
		AddRef();
		return S_OK;
	}
	STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_ref); }
	STDMETHODIMP_(ULONG) Release()
	{
		ULONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return r;
	}
	STDMETHODIMP_(DWORD) CreateInstance(REFIID riid, void*, void* pvParam2, void*, void*, void** ppvObj)
	{
		if (!ppvObj) return 0;
		*ppvObj = NULL;
		if (riid == IID_IKpiConfig) {
			*ppvObj = static_cast<IKpiConfig*>(this);
			AddRef();
			if (pvParam2) *(DWORD*)pvParam2 = 0;
			return 1;
		}
		return 0;
	}
	virtual void WINAPI SetInt(const wchar_t* sec, const wchar_t* key, INT64 v)
	{
		KpiV5SetInt(m_name, sec ? sec : L"", key ? key : L"", v);
	}
	virtual INT64 WINAPI GetInt(const wchar_t* sec, const wchar_t* key, INT64 nDefault)
	{
		if (_wcsicmp(m_name, L"kbsasami") == 0 && sec && key && _wcsicmp(sec, L"kbsasami") == 0) {
			if (_wcsicmp(key, L"raira") == 0) return 1;
			if (_wcsicmp(key, L"vst") == 0) return (savedata.midPlayPrefer != 0) ? 0 : 1;
		}
		return KpiV5GetInt(m_name, sec ? sec : L"", key ? key : L"", nDefault);
	}
	virtual void WINAPI SetFloat(const wchar_t* sec, const wchar_t* key, double v)
	{
		KpiV5SetFloat(m_name, sec ? sec : L"", key ? key : L"", v);
	}
	virtual double WINAPI GetFloat(const wchar_t* sec, const wchar_t* key, double dDefault)
	{
		return KpiV5GetFloat(m_name, sec ? sec : L"", key ? key : L"", dDefault);
	}
	virtual void WINAPI SetStr(const wchar_t* sec, const wchar_t* key, const wchar_t* value)
	{
		KpiV5SetStr(m_name, sec ? sec : L"", key ? key : L"", value ? value : L"");
	}
	virtual DWORD WINAPI GetStr(const wchar_t* sec, const wchar_t* key, wchar_t* pszValue, DWORD dwSize, const wchar_t* cszDefault)
	{
		std::wstring value = KpiV5GetStr(m_name, sec ? sec : L"", key ? key : L"", cszDefault ? cszDefault : L"");
		DWORD need = (DWORD)((value.size() + 1) * sizeof(wchar_t));
		if (pszValue && dwSize >= sizeof(wchar_t)) {
			if (dwSize >= need) wcscpy_s(pszValue, dwSize / sizeof(wchar_t), value.c_str());
			else pszValue[0] = 0;
		}
		return need;
	}
	virtual void WINAPI SetBin(const wchar_t* sec, const wchar_t* key, const BYTE* p, DWORD size)
	{
		KpiV5SetBin(m_name, sec ? sec : L"", key ? key : L"", p, size);
	}
	virtual DWORD WINAPI GetBin(const wchar_t* sec, const wchar_t* key, BYTE* p, DWORD dwSize)
	{
		return KpiV5GetBin(m_name, sec ? sec : L"", key ? key : L"", p, dwSize);
	}
};

class KpiListEnum : public IKpiConfigEnumerator
{
	long m_ref;
	CString* m_blob;
	wchar_t m_sec[24][96];
	wchar_t m_secHelp[24][400];
	int m_secN;
public:
	explicit KpiListEnum(CString* blob) : m_ref(1), m_blob(blob), m_secN(0) {}
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IKpiConfigEnumerator) {
			*ppv = static_cast<IKpiConfigEnumerator*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_ref); }
	STDMETHODIMP_(ULONG) Release()
	{
		ULONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return r;
	}
	BOOL WINAPI EnumSection(const KPI_CFG_SECTION* s)
	{
		if (!s || !s->cszSection || !s->cszSection[0]) return TRUE;
		int i = 0;
		for (; i < m_secN; ++i) {
			if (_wcsicmp(m_sec[i], s->cszSection) == 0) break;
		}
		if (i >= 24) return TRUE;
		if (i == m_secN) {
			wcsncpy_s(m_sec[i], s->cszSection, _TRUNCATE);
			m_secHelp[i][0] = 0;
			m_secN++;
		}
		if (s->cszSecHelp && s->cszSecHelp[0])
			wcsncpy_s(m_secHelp[i], s->cszSecHelp, _TRUNCATE);
		return TRUE;
	}
	BOOL WINAPI EnumKey(const KPI_CFG_KEY* k)
	{
		if (!m_blob || !k || !k->cszSection || !k->cszKey || !k->cszKey[0]) return TRUE;
		const wchar_t* secHelp = L"";
		for (int i = 0; i < m_secN; ++i) {
			if (_wcsicmp(m_sec[i], k->cszSection) == 0) { secHelp = m_secHelp[i]; break; }
		}
		CString help;
		help = L"Section:";
		help += k->cszSection;
		if (secHelp[0]) { help += L"\n"; help += secHelp; }
		if (k->cszHelp && k->cszHelp[0]) { help += L"\n"; help += k->cszHelp; }
		CString list;
		if (k->cszList) {
			for (const wchar_t* p = k->cszList; *p; ++p) {
				if (*p == L'\t') list += (wchar_t)2;
				else if (*p != L'\r' && *p != L'\n') list += *p;
			}
		}
		wchar_t typ[16];
		swprintf_s(typ, L"%u", (unsigned)k->dwType);
		*m_blob += L"K\t";
		*m_blob += typ;
		*m_blob += L'\t';
		KpiBlobEsc(*m_blob, k->cszSection);
		*m_blob += L'\t';
		KpiBlobEsc(*m_blob, k->cszKey);
		*m_blob += L'\t';
		KpiBlobEsc(*m_blob, (k->cszKeyDesc && k->cszKeyDesc[0]) ? k->cszKeyDesc : k->cszKey);
		*m_blob += L'\t';
		KpiBlobEsc(*m_blob, k->cszDefault);
		*m_blob += L'\t';
		KpiBlobEsc(*m_blob, help);
		*m_blob += L'\t';
		KpiBlobEsc(*m_blob, list);
		*m_blob += L'\n';
		return TRUE;
	}
};

static void KpiFillKmp(KMPMODULE* m, CString& blob)
{
	if (!m) return;
	CString s;
	KpiAcp(s, m->pszDescription);
	if (!s.IsEmpty()) KpiBlobI(blob, L"Description", s);
	KpiAcp(s, m->pszCopyright);
	if (!s.IsEmpty()) KpiBlobI(blob, L"Copyright", s);
	CString exts;
	if (m->ppszSupportExts) {
		for (int i = 0; m->ppszSupportExts[i]; ++i) {
			CString e;
			KpiAcp(e, m->ppszSupportExts[i]);
			if (e.IsEmpty()) continue;
			if (e[0] != L'.') e = L"." + e;
			if (!exts.IsEmpty()) exts += L'/';
			exts += e;
		}
	}
	if (!exts.IsEmpty()) KpiBlobI(blob, L"SupportExts", exts);
	wchar_t tmp[96];
	swprintf_s(tmp, L"%u", (unsigned)m->dwVersion);
	KpiBlobI(blob, L"ModuleVersion", tmp);
	swprintf_s(tmp, L"%u", (unsigned)m->dwPluginVersion);
	KpiBlobI(blob, L"Version", tmp);
	if (m->dwReentrant == 1)
		wcscpy_s(tmp, L"Yes(MultipleInstance=\x221E)");
	else if (m->dwReentrant == 0xFFFFFFFFu)
		wcscpy_s(tmp, L"No(MultipleInstance=0)");
	else
		wcscpy_s(tmp, L"Yes(MultipleInstance=1)");
	KpiBlobI(blob, L"Reentrant", tmp);
	KpiBlobI(blob, L"Config", L"not supported");
}

static void KpiFillLocalKpi(int idx, CString& blob)
{
	if (idx < 0 || idx >= 500) return;
	HMODULE h = hDLLk1[idx];
	if (!h) return;
	KpiBlobI(blob, L"Path", kpif[idx]);
	KpiBlobI(blob, L"Type", L"Decoder");
	KpiCreateFn cr = (KpiCreateFn)GetProcAddress(h, "kpi_CreateInstance");
	if (cr) {
		KpiListHost* host = new KpiListHost(kpif[idx]);
		IKpiDecoderModule* mod = NULL;
		HRESULT hr = KpiSafeCreate(cr, (void**)&mod, static_cast<IKpiUnknown*>(static_cast<IKpiUnkProvider*>(host)));
		if (hr == S_OK && mod) {
			const KPI_DECODER_MODULEINFO* info = NULL;
			KpiSafeModuleInfo(mod, &info);
			if (info) {
				if (info->cszDescription) KpiBlobI(blob, L"Description", info->cszDescription);
				if (info->cszCopyright) KpiBlobI(blob, L"Copyright", info->cszCopyright);
				if (info->cszSupportExts) KpiBlobI(blob, L"SupportExts", info->cszSupportExts);
				if (info->cszMultiSongExts) KpiBlobI(blob, L"MultiSongExts", info->cszMultiSongExts);
				wchar_t tmp[96];
				if (info->dwModuleVersion == KPI_DECODER_MODULE_VERSION)
					swprintf_s(tmp, L"%u(new kpi)", (unsigned)info->dwModuleVersion);
				else
					swprintf_s(tmp, L"%u", (unsigned)info->dwModuleVersion);
				KpiBlobI(blob, L"ModuleVersion", tmp);
				swprintf_s(tmp, L"%u", (unsigned)info->dwPluginVersion);
				KpiBlobI(blob, L"Version", tmp);
				if (info->dwMultipleInstance == KPI_MULTINST_INFINITE)
					wcscpy_s(tmp, L"Yes(MultipleInstance=\x221E)");
				else if (info->dwMultipleInstance == KPI_MULTINST_ZERO)
					wcscpy_s(tmp, L"No(MultipleInstance=0)");
				else if (info->dwMultipleInstance == KPI_MULTINST_UNIQUE)
					wcscpy_s(tmp, L"Unique");
				else if (info->dwMultipleInstance == KPI_MULTINST_ONE)
					wcscpy_s(tmp, L"Yes(MultipleInstance=1)");
				else
					swprintf_s(tmp, L"Yes(MultipleInstance=%u)", (unsigned)info->dwMultipleInstance);
				KpiBlobI(blob, L"Reentrant", tmp);
				wchar_t g[80];
				if (StringFromGUID2(info->guid, g, 80) > 0)
					KpiBlobI(blob, L"GUID", g);
				KpiBlobI(blob, L"TagInfo", info->dwSupportTagInfo ? L"supported" : L"not supported");
				KpiBlobI(blob, L"Config", info->dwSupportConfig ? L"supported" : L"not supported");
			}
			KpiListEnum* en = new KpiListEnum(&blob);
			KpiSafeEnumConfig(mod, en);
			en->Release();
			mod->Release();
		}
		host->Release();
	}
	else if (mod1[idx])
		KpiFillKmp(mod1[idx], blob);
	else {
		pfnGetKMPModule fn = (pfnGetKMPModule)GetProcAddress(h, SZ_KMP_GETMODULE);
		KMPMODULE* m = KpiSafeKmp(fn);
		KpiFillKmp(m, blob);
	}
}

static int KpiFillWinamp(const wchar_t* path, CString& blob)
{
	int hasUi = 0;
	if (!path || !path[0]) return 0;
	HMODULE h = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) return 0;
	typedef KpiWaPeek* (__cdecl* pfnGet)();
	pfnGet getIn = (pfnGet)GetProcAddress(h, "winampGetInModule2");
	KpiWaPeek* in = KpiSafeWaGet(getIn);
	if (in) {
		KpiBlobI(blob, L"Type", L"Winamp");
		KpiBlobI(blob, L"Path", path);
		CString desc;
		KpiAcp(desc, in->description);
		if (!desc.IsEmpty()) KpiBlobI(blob, L"Description", desc);
		KpiBlobI(blob, L"Seekable", in->is_seekable ? L"Yes" : L"No");
		wchar_t ver[32];
		swprintf_s(ver, L"0x%X", (unsigned)in->version);
		KpiBlobI(blob, L"Version", ver);
		if (in->Config) hasUi = 1;
	}
	FreeLibrary(h);
	return hasUi;
}

static void KpiOpenWinampUi(const wchar_t* path, HWND owner, BOOL remote)
{
	if (!path || !path[0]) return;
	if (remote) {
		g_kpiHost.ShowPluginUi(PLUGKIND_WINAMP, path);
		return;
	}
	HMODULE h = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) return;
	typedef KpiWaPeek* (__cdecl* pfnGet)();
	pfnGet getIn = (pfnGet)GetProcAddress(h, "winampGetInModule2");
	KpiWaPeek* in = KpiSafeWaGet(getIn);
	if (in && in->Config) {
		in->hDllInstance = (HINSTANCE)h;
		in->hMainWindow = owner;
		s_kpiWaMadeN = 0;
		s_kpiWaHook = SetWindowsHookExW(WH_CBT, KpiWaUiCbt, NULL, GetCurrentThreadId());
		KpiSafeWaConfig(in->Config, owner);
		if (s_kpiWaHook) {
			UnhookWindowsHookEx(s_kpiWaHook);
			s_kpiWaHook = NULL;
		}
	}
	FreeLibrary(h);
}

static void KpiCopyClip(const wchar_t* s)
{
	if (!s) s = L"";
	if (!OpenClipboard(NULL)) return;
	EmptyClipboard();
	const size_t n = wcslen(s) + 1;
	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n * sizeof(wchar_t));
	if (h) {
		wchar_t* p = (wchar_t*)GlobalLock(h);
		if (p) {
			wcscpy_s(p, n, s);
			GlobalUnlock(h);
			SetClipboardData(CF_UNICODETEXT, h);
		}
		else GlobalFree(h);
	}
	CloseClipboard();
}

static BOOL KpiBoolOn(const wchar_t* s)
{
	if (!s || !s[0]) return FALSE;
	if (s[0] == L'1' && s[1] == 0) return TRUE;
	if (_wcsicmp(s, L"true") == 0) return TRUE;
	return FALSE;
}

static void KpiPut(wchar_t* dst, int cap, const CString& s)
{
	wcsncpy_s(dst, cap, s, _TRUNCATE);
}

void CKpilist::ShowPluginDetail(int idx)
{
	if (idx == m_detailIdx) return;
	m_detailIdx = idx;
	m_inN = 0;
	m_cfgN = 0;
	if (m_info.GetSafeHwnd()) {
		m_info.SetRedraw(FALSE);
		m_info.DeleteAllItems();
	}
	if (m_cfg.GetSafeHwnd()) {
		m_bFillingDetail = TRUE;
		m_cfg.SetLiveEditHold(TRUE);
		m_cfg.SetRedraw(FALSE);
		m_cfg.DeleteAllItems();
	}
	if (m_cfgHelp.GetSafeHwnd())
		m_cfgHelp.SetWindowText(L"");
	if (idx < 0 || idx >= kpicnt || idx >= 400) {
		m_bFillingDetail = FALSE;
		if (m_cfg.GetSafeHwnd()) m_cfg.SetLiveEditHold(FALSE);
		if (m_info.GetSafeHwnd()) { m_info.SetRedraw(TRUE); m_info.Invalidate(FALSE); }
		if (m_cfg.GetSafeHwnd()) { m_cfg.SetRedraw(TRUE); m_cfg.Invalidate(FALSE); }
		return;
	}

#ifdef _WIN64
	const BOOL remote = (kpiarch[idx] == 32) ? TRUE : FALSE;
#else
	const BOOL remote = (kpiarch[idx] == 64) ? TRUE : FALSE;
#endif
	CString blob;
	int winUi = 0;
	if (remote) {
		std::wstring t;
		if (g_kpiHost.InspectPlugin((uint32_t)plugkind[idx], (LPCWSTR)kpif[idx], t))
			blob = t.c_str();
	}
	else if (plugkind[idx] == PLUGKIND_KPI)
		KpiFillLocalKpi(idx, blob);
	else if (plugkind[idx] == PLUGKIND_WINAMP)
		winUi = KpiFillWinamp(kpif[idx], blob);

	int pos = 0;
	while (pos < blob.GetLength()) {
		int nl = blob.Find(L'\n', pos);
		CString line = (nl < 0) ? blob.Mid(pos) : blob.Mid(pos, nl - pos);
		pos = (nl < 0) ? blob.GetLength() : nl + 1;
		if (line.IsEmpty()) continue;
		CString f[8];
		int nf = 0;
		int a = 0;
		while (nf < 8) {
			int tab = line.Find(L'\t', a);
			CString part = (tab < 0) ? line.Mid(a) : line.Mid(a, tab - a);
			part.Replace((wchar_t)1, L'\n');
			f[nf++] = part;
			if (tab < 0) break;
			a = tab + 1;
		}
		if (f[0] == L"I" && nf >= 3 && m_inN < 32) {
			KpiPut(m_inName[m_inN], 48, f[1]);
			KpiPut(m_inVal[m_inN], 512, f[2]);
			m_inN++;
		}
		else if (f[0] == L"W")
			winUi = 1;
		else if (f[0] == L"K" && nf >= 8 && m_cfgN < 96) {
			int row = m_cfgN++;
			m_cfgType[row] = _wtoi(f[1]);
			m_cfgLock[row] = 0;
			KpiPut(m_cfgSec[row], 96, f[2]);
			KpiPut(m_cfgKey[row], 96, f[3]);
			KpiPut(m_cfgDesc[row], 192, f[4]);
			KpiPut(m_cfgDef[row], 256, f[5]);
			KpiPut(m_cfgHelpTxt[row], 768, f[6]);
			KpiPut(m_cfgList[row], 512, f[7]);
			m_cfgVal[row][0] = 0;
		}
	}

	std::wstring plug = KpiV5PluginNameFromPath((LPCWSTR)kpif[idx]);
	const BOOL sasami = (_wcsicmp(plug.c_str(), L"kbsasami") == 0) ? TRUE : FALSE;
	if (plugkind[idx] == PLUGKIND_KPI) {
		const std::vector<KpiV5ConfigEntry>& known = GetKpiV5KnownEntries();
		for (size_t i = 0; i < known.size() && m_cfgN < 96; ++i) {
			if (_wcsicmp(known[i].plugin, plug.c_str()) != 0) continue;
			int found = -1;
			for (int r = 0; r < m_cfgN; ++r) {
				if (_wcsicmp(m_cfgSec[r], known[i].section) == 0 && _wcsicmp(m_cfgKey[r], known[i].key) == 0) {
					found = r;
					break;
				}
			}
			if (found >= 0) continue;
			int row = m_cfgN++;
			m_cfgType[row] = KPI_CFG_TYPE_STR;
			m_cfgLock[row] = 0;
			wcsncpy_s(m_cfgSec[row], known[i].section, _TRUNCATE);
			wcsncpy_s(m_cfgKey[row], known[i].key, _TRUNCATE);
			wcsncpy_s(m_cfgDesc[row], known[i].key, _TRUNCATE);
			wcsncpy_s(m_cfgDef[row], known[i].defaultValue ? known[i].defaultValue : L"", _TRUNCATE);
			m_cfgHelpTxt[row][0] = 0;
			swprintf_s(m_cfgHelpTxt[row], L"Section:%s", known[i].section);
			m_cfgList[row][0] = 0;
			m_cfgVal[row][0] = 0;
		}
	}
	for (int r = 0; r < m_cfgN; ++r) {
		if (sasami && _wcsicmp(m_cfgKey[r], L"raira") == 0) {
			m_cfgType[r] = KPI_CFG_TYPE_BOOL;
			m_cfgLock[r] = 1;
			wcscpy_s(m_cfgVal[r], L"1");
			KpiV5SetInt(plug, m_cfgSec[r], m_cfgKey[r], 1);
			continue;
		}
		std::wstring cur = KpiV5GetStr(plug, m_cfgSec[r], m_cfgKey[r], m_cfgDef[r]);
		wcsncpy_s(m_cfgVal[r], cur.c_str(), _TRUNCATE);
	}
	if (winUi && m_cfgN < 96) {
		int row = m_cfgN++;
		m_cfgType[row] = KPI_ROW_WINUI;
		m_cfgLock[row] = 0;
		m_cfgSec[row][0] = 0;
		m_cfgKey[row][0] = 0;
		wcsncpy_s(m_cfgDesc[row], LL14(L"プラグインの設定画面", L"Plugin setup", L"Reglages du plugin", L"Impostazioni plugin",
			L"Ajustes del plugin", L"플러그인 설정 화면", L"插件设置窗口", L"نافذة إعداد الإضافة",
			L"Окно настроек плагина", L"Plugin-Einstellungen", L"Configuracao do plugin", L"Plugin-instellingen",
			L"Ustawienia wtyczki", L"Eklenti ayarlari"), _TRUNCATE);
		m_cfgDef[row][0] = 0;
		wcsncpy_s(m_cfgHelpTxt[row], LL14(L"ダブルクリックでプラグイン自身の設定画面を開きます",
			L"Double-click to open the plugin's own setup",
			L"Double-clic pour ouvrir le reglage du plugin",
			L"Doppio clic per aprire le impostazioni del plugin",
			L"Doble clic para abrir los ajustes del plugin",
			L"더블클릭하면 플러그인 설정 화면을 엽니다",
			L"双击打开插件自己的设置窗口",
			L"انقر نقراً مزدوجاً لفتح إعداد الإضافة",
			L"Двойной щелчок откроет окно настроек плагина",
			L"Doppelklick oeffnet die eigenen Einstellungen",
			L"Duplo clique abre a configuracao do plugin",
			L"Dubbelklik opent de eigen instellingen",
			L"Podwojny klik otwiera ustawienia wtyczki",
			L"Cift tik eklentinin kendi ayarini acar"), _TRUNCATE);
		m_cfgList[row][0] = 0;
		wcsncpy_s(m_cfgVal[row], LL14(L"開く", L"Open", L"Ouvrir", L"Apri", L"Abrir", L"열기", L"打开", L"فتح", L"Открыть", L"Oeffnen", L"Abrir", L"Openen", L"Otworz", L"Ac"), _TRUNCATE);
	}

	BOOL hasPath = FALSE, hasType = FALSE, hasVer = FALSE, hasExt = FALSE, hasPlat = FALSE;
	for (int i = 0; i < m_inN; ++i) {
		if (_wcsicmp(m_inName[i], L"Path") == 0) hasPath = TRUE;
		else if (_wcsicmp(m_inName[i], L"Type") == 0) hasType = TRUE;
		else if (_wcsicmp(m_inName[i], L"Version") == 0) hasVer = TRUE;
		else if (_wcsicmp(m_inName[i], L"SupportExts") == 0) hasExt = TRUE;
		else if (_wcsicmp(m_inName[i], L"Platform") == 0) hasPlat = TRUE;
	}
	if (!hasPath && m_inN < 32) {
		KpiPut(m_inName[m_inN], 48, CString(L"Path"));
		KpiPut(m_inVal[m_inN], 512, kpif[idx]);
		m_inN++;
	}
	if (!hasType && m_inN < 32) {
		CString kind = L"KPI";
		if (plugkind[idx] == PLUGKIND_WINAMP) kind = L"Winamp";
		else if (plugkind[idx] == PLUGKIND_XMPLAY) kind = L"XMPlay";
		else if (plugkind[idx] == PLUGKIND_AIMP) kind = L"AIMP";
		KpiPut(m_inName[m_inN], 48, CString(L"Type"));
		KpiPut(m_inVal[m_inN], 512, kind);
		m_inN++;
	}
	if (!hasVer && m_inN < 32) {
		CString ver; ver.Format(L"%u", (unsigned)kvar[idx][0]);
		KpiPut(m_inName[m_inN], 48, CString(L"Version"));
		KpiPut(m_inVal[m_inN], 512, ver);
		m_inN++;
	}
	if (!hasExt && m_inN < 32) {
		CString exts;
		for (int i = 0; i < 299 && !ext[idx][i].IsEmpty(); ++i) {
			if (!exts.IsEmpty()) exts += L'/';
			exts += ext[idx][i];
		}
		if (!exts.IsEmpty()) {
			KpiPut(m_inName[m_inN], 48, CString(L"SupportExts"));
			KpiPut(m_inVal[m_inN], 512, exts);
			m_inN++;
		}
	}
	if (!hasPlat && m_inN < 32) {
		CString plat = L"?";
		if (kpiarch[idx] == 64) plat = L"x64(64bit)";
		else if (kpiarch[idx] == 32) plat = L"x86(32bit)";
		KpiPut(m_inName[m_inN], 48, CString(L"Platform"));
		KpiPut(m_inVal[m_inN], 512, plat);
		m_inN++;
	}

	if (m_info.GetSafeHwnd()) {
		for (int i = 0; i < m_inN; ++i) {
			int id = m_info.InsertItem(i, m_inName[i]);
			m_info.SetItemText(id, 1, m_inVal[i]);
		}
		m_info.SetRedraw(TRUE);
		m_info.Invalidate(FALSE);
	}
	if (m_cfg.GetSafeHwnd()) {
		for (int r = 0; r < m_cfgN; ++r) {
			int id = m_cfg.InsertItem(r, m_cfgDesc[r][0] ? m_cfgDesc[r] : m_cfgKey[r]);
			CString shown;
			if (m_cfgType[r] == KPI_CFG_TYPE_BOOL)
				shown = KpiBoolOn(m_cfgVal[r]) ? L"true" : L"false";
			else
				shown = m_cfgVal[r];
			m_cfg.SetItemText(id, 1, shown);
			if (m_cfgType[r] == KPI_CFG_TYPE_BOOL)
				m_cfg.SetCheck(id, KpiBoolOn(m_cfgVal[r]) ? TRUE : FALSE);
			else
				m_cfg.SetItemState(id, 0, LVIS_STATEIMAGEMASK);
		}
		m_bFillingDetail = FALSE;
		if (m_cfgN > 0) {
			m_cfg.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			m_cfgHelp.SetWindowText(m_cfgHelpTxt[0]);
		}
		m_cfg.SetRedraw(TRUE);
		m_cfg.Invalidate(FALSE);
		m_cfg.SetLiveEditHold(FALSE);
	}
	else
		m_bFillingDetail = FALSE;
}

IMPLEMENT_DYNAMIC(CKpiCfgList, CCustomListCtrl)

BOOL CKpiCfgList::WantLiveEditRow(int row)
{
	CKpilist* d = (CKpilist*)GetParent();
	if (!d || row < 0 || row >= d->m_cfgN) return FALSE;
	if (d->m_cfgLock[row]) return FALSE;
	const int t = d->m_cfgType[row];
	if (t == KPI_CFG_TYPE_BOOL || t == KPI_CFG_TYPE_BIN || t == KPI_ROW_WINUI) return FALSE;
	return TRUE;
}

void CKpiCfgList::OnLiveEditText(int row, LPCTSTR text)
{
	CKpilist* d = (CKpilist*)GetParent();
	if (!d || row < 0 || row >= d->m_cfgN || !text) return;
	if (wcscmp(d->m_cfgVal[row], text) == 0) return;
	wcsncpy_s(d->m_cfgVal[row], text, _TRUNCATE);
	d->SaveCfgRow(row);
}

void CKpilist::SaveCfgRow(int row)
{
	if (row < 0 || row >= m_cfgN) return;
	if (m_cfgType[row] == KPI_ROW_WINUI) return;
	if (m_detailIdx < 0 || m_detailIdx >= kpicnt) return;
	std::wstring plug = KpiV5PluginNameFromPath((LPCWSTR)kpif[m_detailIdx]);
	const BOOL sasami = (_wcsicmp(plug.c_str(), L"kbsasami") == 0) ? TRUE : FALSE;
	if (m_cfgLock[row]) {
		wcscpy_s(m_cfgVal[row], L"1");
		KpiV5SetInt(plug, m_cfgSec[row], m_cfgKey[row], 1);
		return;
	}
	if (m_cfgType[row] == KPI_CFG_TYPE_BOOL) {
		if (KpiBoolOn(m_cfgVal[row])) wcscpy_s(m_cfgVal[row], L"1");
		else wcscpy_s(m_cfgVal[row], L"0");
	}
	KpiV5SetStr(plug, m_cfgSec[row], m_cfgKey[row], m_cfgVal[row]);
	if (sasami && _wcsicmp(m_cfgKey[row], L"vst") == 0) {
		const BOOL on = KpiBoolOn(m_cfgVal[row]);
		if (on)
			savedata.midPlayPrefer = 0;
		else if (savedata.midPlayPrefer == 0)
			savedata.midPlayPrefer = 1;
		MpPersistSavedataQuick();
		extern void PlRefreshMidiPlayModes();
		extern void CEmuRequestMidiEngineReplay();
		PlRefreshMidiPlayModes();
		CEmuRequestMidiEngineReplay();
	}
}

void CKpilist::OnLvnItemchangedCfg(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLISTVIEW p = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	*pResult = 0;
	if (m_bFillingDetail || !p) return;
	const int row = p->iItem;
	if (row < 0 || row >= m_cfgN) return;
	if ((p->uNewState & LVIS_SELECTED) && !(p->uOldState & LVIS_SELECTED) && m_cfgHelp.GetSafeHwnd())
		m_cfgHelp.SetWindowText(m_cfgHelpTxt[row]);
	if ((p->uChanged & LVIF_STATE) == 0) return;
	if ((p->uOldState & LVIS_STATEIMAGEMASK) == (p->uNewState & LVIS_STATEIMAGEMASK)) return;
	if (m_cfgType[row] != KPI_CFG_TYPE_BOOL) return;
	if (m_cfgLock[row]) {
		m_bFillingDetail = TRUE;
		m_cfg.SetCheck(row, TRUE);
		m_cfg.SetItemText(row, 1, L"true");
		m_bFillingDetail = FALSE;
		return;
	}
	wcscpy_s(m_cfgVal[row], m_cfg.GetCheck(row) ? L"1" : L"0");
	SaveCfgRow(row);
	m_cfg.SetItemText(row, 1, KpiBoolOn(m_cfgVal[row]) ? L"true" : L"false");
}

void CKpilist::OnCfgDblClk(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE p = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	*pResult = 0;
	if (!p || p->iItem < 0 || p->iItem >= m_cfgN) return;
	const int row = p->iItem;
	if (m_cfgLock[row] || m_cfgType[row] == KPI_CFG_TYPE_BOOL) return;
	if (m_cfgType[row] == KPI_ROW_WINUI) {
		if (m_detailIdx < 0 || m_detailIdx >= kpicnt) return;
#ifdef _WIN64
		const BOOL remote = (kpiarch[m_detailIdx] == 32) ? TRUE : FALSE;
#else
		const BOOL remote = (kpiarch[m_detailIdx] == 64) ? TRUE : FALSE;
#endif
		KpiOpenWinampUi(kpif[m_detailIdx], m_hWnd, remote);
		return;
	}
	if (m_cfgType[row] == KPI_CFG_TYPE_FILE) {
		CFileDialog dlg(TRUE, NULL, m_cfgVal[row], OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, NULL, this);
		if (dlg.DoModal() != IDOK) return;
		wcsncpy_s(m_cfgVal[row], dlg.GetPathName(), _TRUNCATE);
		SaveCfgRow(row);
		m_cfg.SetItemText(row, 1, m_cfgVal[row]);
		return;
	}
	if (m_cfgType[row] == KPI_CFG_TYPE_FOLDER) {
		BROWSEINFO bi = {};
		bi.hwndOwner = m_hWnd;
		bi.lpszTitle = m_cfgDesc[row];
		bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
		LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
		if (!pidl) return;
		wchar_t path[MAX_PATH] = {};
		BOOL got = SHGetPathFromIDList(pidl, path);
		CoTaskMemFree(pidl);
		if (!got || !path[0]) return;
		wcsncpy_s(m_cfgVal[row], path, _TRUNCATE);
		size_t n = wcslen(m_cfgVal[row]);
		if (n > 0 && n + 1 < 512 && m_cfgVal[row][n - 1] != L'\\') {
			m_cfgVal[row][n] = L'\\';
			m_cfgVal[row][n + 1] = 0;
		}
		SaveCfgRow(row);
		m_cfg.SetItemText(row, 1, m_cfgVal[row]);
		return;
	}
	if (m_cfgList[row][0]) {
		CCustomPopupMenu menu;
		menu.SetSkipChrome(TRUE);
		int n = 0;
		int a = 0;
		const CString all(m_cfgList[row]);
		while (n < 24) {
			int cut = all.Find((wchar_t)2, a);
			CString part = (cut < 0) ? all.Mid(a) : all.Mid(a, cut - a);
			if (!part.IsEmpty())
				menu.AddCommand((UINT)(n + 1), part, NULL, TRUE);
			n++;
			if (cut < 0) break;
			a = cut + 1;
		}
		CPoint pt;
		GetCursorPos(&pt);
		UINT cmd = menu.Track(pt, this);
		if (cmd >= 1 && cmd <= 24) {
			int pick = (int)cmd - 1;
			a = 0;
			CString chosen;
			for (int i = 0; i <= pick; ++i) {
				int cut = all.Find((wchar_t)2, a);
				chosen = (cut < 0) ? all.Mid(a) : all.Mid(a, cut - a);
				if (cut < 0) break;
				a = cut + 1;
			}
			wcsncpy_s(m_cfgVal[row], chosen, _TRUNCATE);
			SaveCfgRow(row);
			m_cfg.SetItemText(row, 1, m_cfgVal[row]);
		}
		return;
	}
	if (m_cfgType[row] == KPI_CFG_TYPE_BIN) return;
}

void CKpilist::OnCfgRClick(NMHDR*, LRESULT* pResult)
{
	*pResult = 0;
	if (!m_cfg.GetSafeHwnd()) return;
	DWORD pos = ::GetMessagePos();
	CPoint pt((short)LOWORD(pos), (short)HIWORD(pos));
	CPoint client = pt;
	m_cfg.ScreenToClient(&client);
	LVHITTESTINFO ht = {};
	ht.pt = client;
	int row = m_cfg.SubItemHitTest(&ht);
	if (row < 0 || row >= m_cfgN) return;
	m_cfg.SetItemState(row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	CCustomPopupMenu menu;
	menu.SetSkipChrome(TRUE);
	if (!m_cfgLock[row] && m_cfgType[row] != KPI_ROW_WINUI)
		menu.AddCommand(1, LL14(L"既定値に戻す", L"Reset to default", L"Valeur par defaut", L"Ripristina predefinito",
			L"Restablecer", L"기본값으로", L"恢复默认", L"استعادة الافتراضي", L"Сбросить", L"Zuruecksetzen",
			L"Restaurar padrao", L"Standaard", L"Przywroc domyslne", L"Varsayilana don"), NULL, TRUE);
	menu.AddCommand(2, LL14(L"値をコピー", L"Copy value", L"Copier la valeur", L"Copia valore",
		L"Copiar valor", L"값 복사", L"复制值", L"نسخ القيمة", L"Копировать", L"Wert kopieren",
		L"Copiar valor", L"Waarde kopieren", L"Kopiuj wartosc", L"Degeri kopyala"), NULL, TRUE);
	if (m_cfgType[row] == KPI_CFG_TYPE_FILE || m_cfgType[row] == KPI_CFG_TYPE_FOLDER)
		menu.AddCommand(3, LL14(L"参照", L"Browse", L"Parcourir", L"Sfoglia", L"Examinar", L"찾아보기", L"浏览",
			L"استعراض", L"Обзор", L"Durchsuchen", L"Procurar", L"Bladeren", L"Przegladaj", L"Gozat"), NULL, TRUE);
	if (m_cfgType[row] == KPI_ROW_WINUI)
		menu.AddCommand(4, LL14(L"設定画面を開く", L"Open setup", L"Ouvrir", L"Apri", L"Abrir", L"설정 열기", L"打开设置",
			L"فتح", L"Открыть", L"Oeffnen", L"Abrir", L"Openen", L"Otworz", L"Ac"), NULL, TRUE);
	UINT cmd = menu.Track(pt, this);
	if (cmd == 1) {
		wcsncpy_s(m_cfgVal[row], m_cfgDef[row], _TRUNCATE);
		SaveCfgRow(row);
		if (m_cfgType[row] == KPI_CFG_TYPE_BOOL) {
			m_bFillingDetail = TRUE;
			m_cfg.SetCheck(row, KpiBoolOn(m_cfgVal[row]));
			m_cfg.SetItemText(row, 1, KpiBoolOn(m_cfgVal[row]) ? L"true" : L"false");
			m_bFillingDetail = FALSE;
		}
		else
			m_cfg.SetItemText(row, 1, m_cfgVal[row]);
	}
	else if (cmd == 2) {
		if (m_cfgType[row] == KPI_CFG_TYPE_BOOL)
			KpiCopyClip(KpiBoolOn(m_cfgVal[row]) ? L"true" : L"false");
		else
			KpiCopyClip(m_cfgVal[row]);
	}
	else if (cmd == 3 || cmd == 4) {
		NMITEMACTIVATE act = {};
		act.iItem = row;
		LRESULT lr = 0;
		OnCfgDblClk(reinterpret_cast<NMHDR*>(&act), &lr);
	}
}

void CKpilist::OnInfoRClick(NMHDR*, LRESULT* pResult)
{
	*pResult = 0;
	if (!m_info.GetSafeHwnd()) return;
	DWORD pos = ::GetMessagePos();
	CPoint pt((short)LOWORD(pos), (short)HIWORD(pos));
	CPoint client = pt;
	m_info.ScreenToClient(&client);
	LVHITTESTINFO ht = {};
	ht.pt = client;
	int row = m_info.SubItemHitTest(&ht);
	if (row < 0 || row >= m_inN) return;
	CCustomPopupMenu menu;
	menu.SetSkipChrome(TRUE);
	menu.AddCommand(1, LL14(L"値をコピー", L"Copy value", L"Copier la valeur", L"Copia valore",
		L"Copiar valor", L"값 복사", L"复制值", L"نسخ القيمة", L"Копировать", L"Wert kopieren",
		L"Copiar valor", L"Waarde kopieren", L"Kopiuj wartosc", L"Degeri kopyala"), NULL, TRUE);
	if (menu.Track(pt, this) == 1)
		KpiCopyClip(m_inVal[row]);
}

