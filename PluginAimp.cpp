// AIMP decoder plugin host (AIMPPluginGetHeader + IAIMPExtensionAudioDecoder / ...Old)
//
// AIMP SDK の実装に合わせた点:
//  - デコーダ系プラグインは Initialize(Core) の中で Core->RegisterExtension() を呼び、
//    再生用に IAIMPExtensionAudioDecoder(ストリーム版) か IAIMPExtensionAudioDecoderOld(パス版)、
//    対応拡張子用に IAIMPExtensionFileFormat を登録してくる。
//    拡張子は IAIMPExtensionFileFormat::GetExtList() ("*.mp3;*.mp2;") からしか取れない。
//  - 新しめのプラグインは Old を持たずストリーム版だけを登録するので、
//    IAIMPStream 実装(ファイル)を用意して両方に対応する。
//  - 32bit float 出力は本体の再生経路が整数 PCM 前提なので int32 へ変換して渡す(バイト数不変)。
#include "stdafx.h"
#include "PluginAimp.h"
#include "PluginKinds.h"
#include <float.h>

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
static IAIMPStream* g_aimpStream = NULL;
static int g_aimpOpen = 0;
static int g_aimpRate = 44100;
static int g_aimpCh = 2;
static int g_aimpBits = 16;
static int g_aimpFloat = 0;   // デコーダのネイティブが 32bit float
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

// ストリーム版 CreateDecoder(IAIMPStream*) 用のローカルファイルストリーム
class CAimpFileStream : public IAIMPStream
{
	LONG m_ref;
	HANDLE m_h;
public:
	explicit CAimpFileStream(const wchar_t* path) : m_ref(1)
	{
		m_h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	}
	~CAimpFileStream() { if (m_h != INVALID_HANDLE_VALUE) CloseHandle(m_h); }
	BOOL IsValid() const { return m_h != INVALID_HANDLE_VALUE; }
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IAIMPStream) { *ppv = (IAIMPStream*)this; AddRef(); return S_OK; }
		*ppv = NULL; return E_NOINTERFACE;
	}
	ULONG STDMETHODCALLTYPE AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
	ULONG STDMETHODCALLTYPE Release() { LONG r = InterlockedDecrement(&m_ref); if (r == 0) delete this; return (ULONG)r; }
	INT64 WINAPI GetSize()
	{
		LARGE_INTEGER li;
		if (!GetFileSizeEx(m_h, &li)) return 0;
		return li.QuadPart;
	}
	HRESULT WINAPI SetSize(const INT64) { return E_NOTIMPL; }
	INT64 WINAPI GetPosition()
	{
		LARGE_INTEGER z; z.QuadPart = 0;
		LARGE_INTEGER cur; cur.QuadPart = 0;
		if (!SetFilePointerEx(m_h, z, &cur, FILE_CURRENT)) return 0;
		return cur.QuadPart;
	}
	HRESULT WINAPI Seek(const INT64 Offset, int Mode)
	{
		DWORD m = FILE_BEGIN;
		if (Mode == AIMP_STREAM_SEEKMODE_FROM_CURRENT) m = FILE_CURRENT;
		else if (Mode == AIMP_STREAM_SEEKMODE_FROM_END) m = FILE_END;
		LARGE_INTEGER li; li.QuadPart = Offset;
		return SetFilePointerEx(m_h, li, NULL, m) ? S_OK : E_FAIL;
	}
	int WINAPI Read(unsigned char* Buffer, unsigned int Count)
	{
		DWORD rd = 0;
		if (!ReadFile(m_h, Buffer, Count, &rd, NULL)) return -1;
		return (int)rd;
	}
	HRESULT WINAPI Write(unsigned char*, unsigned int, unsigned int*) { return E_NOTIMPL; }
};

class CAimpCore : public IAIMPCore
{
	LONG m_ref;
public:
	IAIMPExtensionAudioDecoderOld* m_extOld;
	IAIMPExtensionAudioDecoder* m_extNew;
	CString m_exts;          // "*.mp3;*.mp2;" を連結したもの
	CString m_pluginDir;
	CAimpCore() : m_ref(1), m_extOld(NULL), m_extNew(NULL) {}
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
		*Value = new CAimpString(m_pluginDir.IsEmpty() ? L"." : (const wchar_t*)m_pluginDir);
		return S_OK;
	}
	HRESULT WINAPI RegisterExtension(REFIID, IUnknown* Extension)
	{
		// ServiceIID で振り分けず、来たものを片端から QI する（登録先 IID はプラグイン依存）
		if (!Extension) return E_INVALIDARG;
		IAIMPExtensionFileFormat* fmt = NULL;
		if (SUCCEEDED(Extension->QueryInterface(IID_IAIMPExtensionFileFormat, (void**)&fmt)) && fmt) {
			IAIMPString* s = NULL;
			if (SUCCEEDED(fmt->GetExtList(&s)) && s) {
				if (!m_exts.IsEmpty()) m_exts += L";";
				m_exts += s->GetData();
				s->Release();
			}
			fmt->Release();
		}
		if (!m_extOld) {
			IAIMPExtensionAudioDecoderOld* old = NULL;
			if (SUCCEEDED(Extension->QueryInterface(IID_IAIMPExtensionAudioDecoderOld, (void**)&old)) && old)
				m_extOld = old;
		}
		if (!m_extNew) {
			IAIMPExtensionAudioDecoder* neu = NULL;
			if (SUCCEEDED(Extension->QueryInterface(IID_IAIMPExtensionAudioDecoder, (void**)&neu)) && neu)
				m_extNew = neu;
		}
		return S_OK;
	}
	HRESULT WINAPI RegisterService(IUnknown*) { return S_OK; }
	HRESULT WINAPI UnregisterExtension(IUnknown* Extension)
	{
		if (!Extension) return S_OK;
		if (m_extOld == Extension) { m_extOld->Release(); m_extOld = NULL; }
		if (m_extNew == Extension) { m_extNew->Release(); m_extNew = NULL; }
		return S_OK;
	}
};

static CAimpCore* g_aimpCore = NULL;

static void AimpUnloadPlugin()
{
	if (g_aimpDec) { g_aimpDec->Release(); g_aimpDec = NULL; }
	if (g_aimpStream) { g_aimpStream->Release(); g_aimpStream = NULL; }
	if (g_aimpPlugin) {
		g_aimpPlugin->Finalize();
		g_aimpPlugin->Release();
		g_aimpPlugin = NULL;
	}
	if (g_aimpCore) {
		if (g_aimpCore->m_extOld) { g_aimpCore->m_extOld->Release(); g_aimpCore->m_extOld = NULL; }
		if (g_aimpCore->m_extNew) { g_aimpCore->m_extNew->Release(); g_aimpCore->m_extNew = NULL; }
		g_aimpCore->Release();
		g_aimpCore = NULL;
	}
	if (g_aimpDll) { FreeLibrary(g_aimpDll); g_aimpDll = NULL; }
	g_aimpOpen = 0;
	g_aimpFloat = 0;
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
	if ((plug->InfoGetCategories() & AIMP_PLUGIN_CATEGORY_DECODERS) == 0) {
		plug->Release();
		FreeLibrary(h);
		return 0;
	}
	g_aimpCore = new CAimpCore();
	CString dir(dllPath);
	int sl = dir.ReverseFind(L'\\');
	if (sl > 0) dir = dir.Left(sl);
	g_aimpCore->m_pluginDir = dir;
	if (FAILED(plug->Initialize(g_aimpCore))) {
		plug->Release();
		g_aimpCore->Release();
		g_aimpCore = NULL;
		FreeLibrary(h);
		return 0;
	}
	g_aimpDll = h;
	g_aimpPlugin = plug;
	return (g_aimpCore->m_extOld || g_aimpCore->m_extNew) ? 1 : 0;
}

// "*.mp3;*.mp2;" → ext[kpicnt][]
static void AimpParseExts(const CString& list)
{
	ext[kpicnt][0] = L"";
	ext[kpicnt][299] = L"";
	int ei = 0;
	int start = 0;
	for (;;) {
		int sc = list.Find(L';', start);
		CString tok = (sc < 0) ? list.Mid(start) : list.Mid(start, sc - start);
		tok.Trim();
		tok.MakeLower();
		int st = 0;
		while (st < tok.GetLength() && (tok[st] == L'*' || tok[st] == L'.')) st++;
		tok = tok.Mid(st);
		if (!tok.IsEmpty() && ei < 298) {
			tok = L"." + tok;
			int dup = 0;
			for (int k = 0; k < ei; k++) {
				if (ext[kpicnt][k] == tok) { dup = 1; break; }
			}
			if (!dup) {
				ext[kpicnt][ei] = tok;
				kvar[kpicnt][ei] = 0;
				ei++;
			}
		}
		if (sc < 0) break;
		start = sc + 1;
	}
	ext[kpicnt][ei] = L"";
}

int PluginAimp_TryEnum(const wchar_t* dllPath, int is64)
{
	if (!dllPath || !dllPath[0] || kpicnt >= 149) return 0;
	if (is64) {
		// x64 は KpiHost64 側に AIMP 再生系が無いので台帳に載せない
		return 0;
	}
	int ok = 0;
	try {
		if (AimpLoadPlugin(dllPath)) {
			plugkind[kpicnt] = PLUGKIND_AIMP;
			kpiarch[kpicnt] = 32;
			kpif[kpicnt] = dllPath;
			AimpParseExts(g_aimpCore ? g_aimpCore->m_exts : CString());
			// 拡張子を公開しないプラグインは plugsaimp でマッチできないので載せない
			if (ext[kpicnt][0] != L"") {
				kpichk[kpicnt] = TRUE;
				kpicnt++;
				ok = 1;
			}
		}
	}
	catch (...) {
		ok = 0;
	}
	AimpUnloadPlugin();
	return ok;
}

int PluginAimp_Open(const wchar_t* dllPath, const wchar_t* mediaPath)
{
	PluginAimp_Close();
	if (!AimpLoadPlugin(dllPath)) {
		AimpUnloadPlugin();
		return 0;
	}
	IAIMPAudioDecoder* dec = NULL;
	CAimpErrorInfo* err = new CAimpErrorInfo();
	if (g_aimpCore->m_extOld) {
		CAimpString* fn = new CAimpString(mediaPath);
		if (FAILED(g_aimpCore->m_extOld->CreateDecoder(fn, 0, err, &dec)))
			dec = NULL;
		fn->Release();
	}
	if (!dec && g_aimpCore->m_extNew) {
		CAimpFileStream* st = new CAimpFileStream(mediaPath);
		if (st->IsValid()) {
			if (FAILED(g_aimpCore->m_extNew->CreateDecoder(st, 0, err, &dec)))
				dec = NULL;
		}
		if (dec) g_aimpStream = st; // デコーダが読み続けるので再生中は保持
		else st->Release();
	}
	err->Release();
	if (!dec) {
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
	g_aimpFloat = 0;
	if (fmt == AIMP_DECODER_SAMPLEFORMAT_08BIT) g_aimpBits = 8;
	else if (fmt == AIMP_DECODER_SAMPLEFORMAT_16BIT) g_aimpBits = 16;
	else if (fmt == AIMP_DECODER_SAMPLEFORMAT_24BIT) g_aimpBits = 24;
	else if (fmt == AIMP_DECODER_SAMPLEFORMAT_32BIT) g_aimpBits = 32;
	else if (fmt == AIMP_DECODER_SAMPLEFORMAT_32BITFLOAT) { g_aimpBits = 32; g_aimpFloat = 1; }
	else g_aimpBits = 16;
	g_aimpSize = dec->GetSize();
	g_aimpOpen = 1;
	return 1;
}

void PluginAimp_Close()
{
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
	int n = 0;
	try { n = g_aimpDec->Read(dst, bytesWanted); }
	catch (...) { n = 0; }
	if (n <= 0) return 0;
	if (g_aimpFloat) {
		// 32bit float → 32bit int。KPI の QuietBoost(GetFloatToInt16Scale) は掛けない（正規化 float 向け）。
		// バイト数は同じなのでその場で置換。長さ/Seek はデコーダの float バイト基準のまま。
		float* f = (float*)dst;
		int* i32 = (int*)dst;
		const int cntS = n / 4;
		for (int i = 0; i < cntS; i++) {
			double v = (double)f[i];
			if (!_finite(v)) v = 0.0;
			double s = v * 2147483647.0;
			if (s > 2147483647.0) s = 2147483647.0;
			if (s < -2147483648.0) s = -2147483648.0;
			i32[i] = (int)(s >= 0.0 ? (s + 0.5) : (s - 0.5));
		}
	}
	return n;
}

int readaimp(BYTE* bw, int cnt)
{
	return PluginAimp_Read(bw, cnt);
}
