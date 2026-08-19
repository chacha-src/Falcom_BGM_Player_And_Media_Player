// AIMP decoder plugin host (AIMPPluginGetHeader + IAIMPExtensionAudioDecoderOld)
#include "stdafx.h"
#include "PluginAimp.h"
#include "PluginKinds.h"

// AIMP SDK headers live under third_party/aimp (AdditionalIncludeDirectories)
#include "apiPlugin.h"
#include "apiDecoders.h"

extern CString kpif[];
extern CString ext[][300];
extern BYTE kvar[][300];
extern BYTE kpiarch[];
extern BYTE plugkind[];
extern BOOL kpichk[];
extern int kpicnt;

static HMODULE g_aimpDll = NULL;
static IAIMPPlugin* g_aimpPlugin = NULL;
static IAIMPAudioDecoder* g_aimpDec = NULL;
static IAIMPExtensionAudioDecoderOld* g_aimpExtOld = NULL;
static int g_aimpOpen = 0;
static int g_aimpRate = 44100;
static int g_aimpCh = 2;
static int g_aimpBits = 16;
static INT64 g_aimpSize = 0;

// ---- minimal IAIMPString ----
class CAimpString : public IAIMPString
{
	LONG m_ref;
	WCHAR* m_data;
	int m_len;
public:
	CAimpString() : m_ref(1), m_data(NULL), m_len(0) {}
	explicit CAimpString(const wchar_t* s) : m_ref(1), m_data(NULL), m_len(0)
	{
		if (s) SetData((WCHAR*)s, (int)wcslen(s));
	}
	~CAimpString() { free(m_data); }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IAIMPString) { *ppv = (IAIMPString*)this; AddRef(); return S_OK; }
		*ppv = NULL; return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	ULONG STDMETHODCALLTYPE Release()
	{
		LONG r = InterlockedDecrement(&m_ref);
		if (r == 0) delete this;
		return (ULONG)r;
	}
	HRESULT WINAPI GetChar(int Index, WCHAR* Char) { if (!Char || Index < 0 || Index >= m_len) return E_INVALIDARG; *Char = m_data[Index]; return S_OK; }
	WCHAR* WINAPI GetData() { return m_data ? m_data : (WCHAR*)L""; }
	int WINAPI GetLength() { return m_len; }
	int WINAPI GetHashCode() { return m_len; }
	HRESULT WINAPI SetChar(int Index, WCHAR Char) { if (Index < 0 || Index >= m_len) return E_INVALIDARG; m_data[Index] = Char; return S_OK; }
	HRESULT WINAPI SetData(WCHAR* Chars, int CharsCount)
	{
		free(m_data); m_data = NULL; m_len = 0;
		if (CharsCount < 0) return E_INVALIDARG;
		m_data = (WCHAR*)malloc((CharsCount + 1) * sizeof(WCHAR));
		if (!m_data) return E_OUTOFMEMORY;
		if (CharsCount > 0 && Chars) memcpy(m_data, Chars, CharsCount * sizeof(WCHAR));
		m_data[CharsCount] = 0; m_len = CharsCount; return S_OK;
	}
	HRESULT WINAPI Add(IAIMPString* S) { return S ? Add2(S->GetData(), S->GetLength()) : E_INVALIDARG; }
	HRESULT WINAPI Add2(WCHAR* Chars, int CharsCount)
	{
		if (CharsCount <= 0) return S_OK;
		WCHAR* n = (WCHAR*)realloc(m_data, (m_len + CharsCount + 1) * sizeof(WCHAR));
		if (!n) return E_OUTOFMEMORY;
		m_data = n;
		memcpy(m_data + m_len, Chars, CharsCount * sizeof(WCHAR));
		m_len += CharsCount; m_data[m_len] = 0; return S_OK;
	}
	HRESULT WINAPI ChangeCase(int) { return S_OK; }
	HRESULT WINAPI Clone(IAIMPString** S) { if (!S) return E_POINTER; *S = new CAimpString(m_data); return S_OK; }
	HRESULT WINAPI Compare(IAIMPString* S, int* CompareResult, BOOL IgnoreCase)
	{
		if (!S || !CompareResult) return E_POINTER;
		*CompareResult = IgnoreCase ? _wcsicmp(GetData(), S->GetData()) : wcscmp(GetData(), S->GetData());
		return S_OK;
	}
	HRESULT WINAPI Compare2(WCHAR* Chars, int, int* CompareResult, BOOL IgnoreCase)
	{
		if (!CompareResult) return E_POINTER;
		*CompareResult = IgnoreCase ? _wcsicmp(GetData(), Chars ? Chars : L"") : wcscmp(GetData(), Chars ? Chars : L"");
		return S_OK;
	}
	HRESULT WINAPI Delete(int, int) { return E_NOTIMPL; }
	HRESULT WINAPI Find(IAIMPString*, int*, int, int) { return E_NOTIMPL; }
	HRESULT WINAPI Find2(WCHAR*, int, int*, int, int) { return E_NOTIMPL; }
	HRESULT WINAPI Insert(int, IAIMPString*) { return E_NOTIMPL; }
	HRESULT WINAPI Insert2(int, WCHAR*, int) { return E_NOTIMPL; }
	HRESULT WINAPI Replace(IAIMPString*, IAIMPString*, int) { return E_NOTIMPL; }
	HRESULT WINAPI Replace2(WCHAR*, int, WCHAR*, int, int) { return E_NOTIMPL; }
	HRESULT WINAPI SubString(int Index, int Count, IAIMPString** S)
	{
		if (!S || Index < 0 || Count < 0 || Index + Count > m_len) return E_INVALIDARG;
		CAimpString* t = new CAimpString();
		t->SetData(m_data + Index, Count);
		*S = t; return S_OK;
	}
};

class CAimpErrorInfo : public IAIMPErrorInfo
{
	LONG m_ref;
public:
	CAimpErrorInfo() : m_ref(1) {}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IAIMPErrorInfo) { *ppv = this; AddRef(); return S_OK; }
		*ppv = NULL; return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	ULONG STDMETHODCALLTYPE Release() { LONG r = InterlockedDecrement(&m_ref); if (r == 0) delete this; return (ULONG)r; }
	HRESULT WINAPI GetInfo(int*, IAIMPString**, IAIMPString**) { return E_NOTIMPL; }
	HRESULT WINAPI GetInfoFormatted(IAIMPString**) { return E_NOTIMPL; }
	void WINAPI SetInfo(int, IAIMPString*, IAIMPString*) {}
};

class CAimpCore : public IAIMPCore
{
	LONG m_ref;
public:
	IAIMPExtensionAudioDecoderOld* m_extOld;
	CAimpCore() : m_ref(1), m_extOld(NULL) {}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IAIMPCore) { *ppv = (IAIMPCore*)this; AddRef(); return S_OK; }
		*ppv = NULL; return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	ULONG STDMETHODCALLTYPE Release() { LONG r = InterlockedDecrement(&m_ref); if (r == 0) delete this; return (ULONG)r; }
	HRESULT WINAPI CreateObject(REFIID iid, void** Obj)
	{
		if (!Obj) return E_POINTER;
		if (iid == IID_IAIMPString) { *Obj = new CAimpString(); return S_OK; }
		if (iid == IID_IAIMPErrorInfo) { *Obj = new CAimpErrorInfo(); return S_OK; }
		*Obj = NULL; return E_NOINTERFACE;
	}
	HRESULT WINAPI GetPath(int, IAIMPString** Value)
	{
		if (!Value) return E_POINTER;
		*Value = new CAimpString(L".");
		return S_OK;
	}
	HRESULT WINAPI RegisterExtension(REFIID ServiceIID, IUnknown* Extension)
	{
		if (!Extension) return E_INVALIDARG;
		if (ServiceIID == IID_IAIMPServiceAudioDecoders || ServiceIID == IID_IAIMPExtensionAudioDecoderOld) {
			IAIMPExtensionAudioDecoderOld* old = NULL;
			if (SUCCEEDED(Extension->QueryInterface(IID_IAIMPExtensionAudioDecoderOld, (void**)&old)) && old) {
				if (m_extOld) m_extOld->Release();
				m_extOld = old;
				return S_OK;
			}
			IAIMPExtensionAudioDecoder* neu = NULL;
			if (SUCCEEDED(Extension->QueryInterface(IID_IAIMPExtensionAudioDecoder, (void**)&neu)) && neu) {
				neu->Release(); // stream-based; Old preferred for file path host
			}
		}
		return S_OK;
	}
	HRESULT WINAPI RegisterService(IUnknown*) { return S_OK; }
	HRESULT WINAPI UnregisterExtension(IUnknown* Extension)
	{
		if (Extension && m_extOld == Extension) { m_extOld->Release(); m_extOld = NULL; }
		return S_OK;
	}
};

static CAimpCore* g_aimpCore = NULL;

static void AimpUnloadPlugin()
{
	if (g_aimpDec) { g_aimpDec->Release(); g_aimpDec = NULL; }
	g_aimpExtOld = NULL;
	if (g_aimpPlugin) {
		g_aimpPlugin->Finalize();
		g_aimpPlugin->Release();
		g_aimpPlugin = NULL;
	}
	if (g_aimpCore) {
		if (g_aimpCore->m_extOld) { g_aimpCore->m_extOld->Release(); g_aimpCore->m_extOld = NULL; }
		g_aimpCore->Release();
		g_aimpCore = NULL;
	}
	if (g_aimpDll) { FreeLibrary(g_aimpDll); g_aimpDll = NULL; }
	g_aimpOpen = 0;
}

static int AimpLoadPlugin(const wchar_t* dllPath)
{
	AimpUnloadPlugin();
	HMODULE h = LoadLibraryExW(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) return 0;
	TAIMPPluginGetHeaderProc getHdr = (TAIMPPluginGetHeaderProc)GetProcAddress(h, "AIMPPluginGetHeader");
	if (!getHdr) { FreeLibrary(h); return 0; }
	IAIMPPlugin* plug = NULL;
	if (FAILED(getHdr(&plug)) || !plug) { FreeLibrary(h); return 0; }
	DWORD cat = plug->InfoGetCategories();
	if ((cat & AIMP_PLUGIN_CATEGORY_DECODERS) == 0) {
		plug->Release();
		FreeLibrary(h);
		return 0;
	}
	g_aimpCore = new CAimpCore();
	if (FAILED(plug->Initialize(g_aimpCore))) {
		plug->Release();
		g_aimpCore->Release();
		g_aimpCore = NULL;
		FreeLibrary(h);
		return 0;
	}
	g_aimpDll = h;
	g_aimpPlugin = plug;
	g_aimpExtOld = g_aimpCore->m_extOld;
	return g_aimpExtOld ? 1 : 0;
}

int PluginAimp_TryEnum(const wchar_t* dllPath, int is64)
{
	if (!dllPath || !dllPath[0] || kpicnt >= 149) return 0;
	if (is64) {
		plugkind[kpicnt] = PLUGKIND_AIMP;
		kpiarch[kpicnt] = 64;
		kpif[kpicnt] = dllPath;
		ext[kpicnt][0] = L"";
		ext[kpicnt][299] = L"";
		kvar[kpicnt][0] = 0;
		kpicnt++;
		return 1;
	}
	if (!AimpLoadPlugin(dllPath)) return 0;
	plugkind[kpicnt] = PLUGKIND_AIMP;
	kpiarch[kpicnt] = 32;
	kpif[kpicnt] = dllPath;
	// 拡張子はプラグイン名・説明から推定不可なことが多い → 空（plugsaimp で CreateDecoder 試行）
	ext[kpicnt][0] = L"";
	ext[kpicnt][299] = L"";
	kvar[kpicnt][0] = 0;
	PWCHAR nm = g_aimpPlugin->InfoGet(AIMP_PLUGIN_INFO_NAME);
	(void)nm;
	AimpUnloadPlugin();
	kpicnt++;
	return 1;
}

int PluginAimp_Open(const wchar_t* dllPath, const wchar_t* mediaPath)
{
	PluginAimp_Close();
	if (!AimpLoadPlugin(dllPath) || !g_aimpExtOld) {
		AimpUnloadPlugin();
		return 0;
	}
	CAimpString* fn = new CAimpString(mediaPath);
	CAimpErrorInfo* err = new CAimpErrorInfo();
	IAIMPAudioDecoder* dec = NULL;
	HRESULT hr = g_aimpExtOld->CreateDecoder(fn, AIMP_DECODER_FLAGS_FORCE_CREATE_INSTANCE, err, &dec);
	fn->Release();
	err->Release();
	if (FAILED(hr) || !dec) {
		AimpUnloadPlugin();
		return 0;
	}
	int sr = 0, ch = 0, fmt = 0;
	if (!dec->GetStreamInfo(&sr, &ch, &fmt)) {
		dec->Release();
		AimpUnloadPlugin();
		return 0;
	}
	g_aimpDec = dec;
	g_aimpRate = sr > 0 ? sr : 44100;
	g_aimpCh = ch > 0 ? ch : 2;
	if (fmt == AIMP_DECODER_SAMPLEFORMAT_08BIT) g_aimpBits = 8;
	else if (fmt == AIMP_DECODER_SAMPLEFORMAT_16BIT) g_aimpBits = 16;
	else if (fmt == AIMP_DECODER_SAMPLEFORMAT_24BIT) g_aimpBits = 24;
	else if (fmt == AIMP_DECODER_SAMPLEFORMAT_32BIT || fmt == AIMP_DECODER_SAMPLEFORMAT_32BITFLOAT) g_aimpBits = 32;
	else g_aimpBits = 16;
	g_aimpSize = dec->GetSize();
	g_aimpOpen = 1;
	return 1;
}

void PluginAimp_Close()
{
	if (g_aimpDec) { g_aimpDec->Release(); g_aimpDec = NULL; }
	AimpUnloadPlugin();
}

int PluginAimp_SeekBytes(INT64 pos)
{
	if (!g_aimpDec || !g_aimpOpen) return 0;
	return g_aimpDec->SetPosition(pos) ? 1 : 0;
}

int PluginAimp_IsOpen() { return g_aimpOpen; }
int PluginAimp_SampleRate() { return g_aimpRate; }
int PluginAimp_Channels() { return g_aimpCh; }
int PluginAimp_Bits() { return g_aimpBits; }
INT64 PluginAimp_SizeBytes() { return g_aimpSize; }

int PluginAimp_Read(BYTE* dst, int bytesWanted)
{
	if (!dst || bytesWanted <= 0 || !g_aimpDec || !g_aimpOpen) return 0;
	int n = g_aimpDec->Read(dst, bytesWanted);
	return n > 0 ? n : 0;
}

int readaimp(BYTE* bw, int cnt)
{
	return PluginAimp_Read(bw, cnt);
}
