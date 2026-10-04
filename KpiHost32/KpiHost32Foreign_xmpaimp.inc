// XMPlay / AIMP を ogghost32 内でホストする。MFC 無し。KpiHost32Foreign.cpp から include。
// PluginXmplay.cpp / PluginAimp.cpp と同じ手順（faceproc 全部返す、XMPFILE、AIMP Core 登録）。

enum { FXMP_MAX_FLOATS = 8192 * 8 };

struct XmpFileObj
{
	HANDLE h;
	BYTE* mem;
	DWORD memLen;
	DWORD memPos;
	char name[MAX_PATH * 2];
};

static ForeignSession* g_xmpCur = nullptr;

static XMPFUNC_IN g_fxIn;
static XMPFUNC_MISC g_fxMisc;
static XMPFUNC_FILE g_fxFile;
static XMPFUNC_REGISTRY g_fxReg;
static XMPFUNC_TEXT g_fxText;
static XMPFUNC_STATUS g_fxStatus;
static int g_fxTables = 0;

static void WINAPI FxSetLength(float length, BOOL)
{
	if (g_xmpCur && length >= 0.f) g_xmpCur->xmpLen = length;
}
static void WINAPI FxSetGain(DWORD, float) {}
static BOOL WINAPI FxUpdateTitle(const char*) { return FALSE; }
static BOOL WINAPI FxGetLooping() { return FALSE; }
static DWORD WINAPI FxMiscGetVersion() { return 0x03080400; }
static HWND WINAPI FxMiscGetWindow() { return NULL; }
static void* WINAPI FxMiscAlloc(DWORD len) { return malloc(len ? len : 1); }
static void* WINAPI FxMiscReAlloc(void* mem, DWORD len) { return realloc(mem, len ? len : 1); }
static void WINAPI FxMiscFree(void* mem) { free(mem); }
static BOOL WINAPI FxMiscCheckCancel() { return FALSE; }
static DWORD WINAPI FxMiscGetConfig(DWORD option)
{
	if (option == XMPCONFIG_OUTPUT && g_xmpCur)
		return (DWORD)(DWORD_PTR)&g_xmpCur->xmpFmt;
	return 0;
}
static const char* WINAPI FxMiscGetSkinConfig(const char*) { return NULL; }
static void WINAPI FxMiscShowBubble(const char*, DWORD) {}
static void WINAPI FxMiscRefreshInfo(DWORD) {}
static char* WINAPI FxMiscGetInfoText(DWORD) { return NULL; }
static char* WINAPI FxMiscFormatInfoText(char* buf, const char*, const char*) { return buf; }
static char* WINAPI FxMiscGetTag(const char*) { return NULL; }
static BOOL WINAPI FxMiscRegisterShortcut(const XMPSHORTCUT*) { return FALSE; }
static BOOL WINAPI FxMiscPerformShortcut(DWORD) { return FALSE; }
static const XMPCUE* WINAPI FxMiscGetCue(DWORD) { return NULL; }
static BOOL WINAPI FxMiscDDE(const char*) { return FALSE; }
static DWORD WINAPI FxRegGet(const char*, const char*, void*, DWORD) { return 0; }
static DWORD WINAPI FxRegGetString(const char*, const char*, char* data, DWORD size)
{
	if (data && size) data[0] = 0;
	return 0;
}
static BOOL WINAPI FxRegGetInt(const char*, const char*, int*) { return FALSE; }
static BOOL WINAPI FxRegSet(const char*, const char*, const void*, DWORD) { return TRUE; }
static BOOL WINAPI FxRegSetString(const char*, const char*, const char*) { return TRUE; }
static BOOL WINAPI FxRegSetInt(const char*, const char*, const int*) { return TRUE; }

static char* FxDupA(const char* s, int len)
{
	if (!s) return NULL;
	if (len < 0) len = (int)strlen(s);
	char* r = (char*)malloc((size_t)len + 1);
	if (!r) return NULL;
	memcpy(r, s, (size_t)len);
	r[len] = 0;
	return r;
}
static char* WINAPI FxTextAnsi(const char* text, int len)
{
	if (!text) return NULL;
	int wl = MultiByteToWideChar(CP_ACP, 0, text, len, NULL, 0);
	if (wl <= 0) return FxDupA(text, len);
	wchar_t* w = (wchar_t*)malloc(((size_t)wl + 1) * sizeof(wchar_t));
	if (!w) return NULL;
	MultiByteToWideChar(CP_ACP, 0, text, len, w, wl);
	w[wl] = 0;
	int ul = WideCharToMultiByte(CP_UTF8, 0, w, wl, NULL, 0, NULL, NULL);
	char* r = (char*)malloc((size_t)(ul > 0 ? ul : 0) + 1);
	if (r) {
		if (ul > 0) WideCharToMultiByte(CP_UTF8, 0, w, wl, r, ul, NULL, NULL);
		r[ul > 0 ? ul : 0] = 0;
	}
	free(w);
	return r;
}
static char* WINAPI FxTextUnicode(const WCHAR* text, int len)
{
	if (!text) return NULL;
	int ul = WideCharToMultiByte(CP_UTF8, 0, text, len, NULL, 0, NULL, NULL);
	char* r = (char*)malloc((size_t)(ul > 0 ? ul : 0) + 1);
	if (!r) return NULL;
	if (ul > 0) WideCharToMultiByte(CP_UTF8, 0, text, len, r, ul, NULL, NULL);
	r[ul > 0 ? ul : 0] = 0;
	return r;
}
static char* WINAPI FxTextUtf8(const char* text, int len) { return FxDupA(text, len); }

static XMPFILE WINAPI FxFileOpen(const char* filename)
{
	if (!filename) return NULL;
	wchar_t wpath[MAX_PATH * 2];
	if (MultiByteToWideChar(CP_UTF8, 0, filename, -1, wpath, MAX_PATH * 2) <= 0)
		return NULL;
	HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return NULL;
	XmpFileObj* f = (XmpFileObj*)calloc(1, sizeof(XmpFileObj));
	if (!f) { CloseHandle(h); return NULL; }
	f->h = h;
	strncpy_s(f->name, filename, _TRUNCATE);
	return (XMPFILE)f;
}
static XMPFILE WINAPI FxFileOpenMemory(const void* buf, DWORD len)
{
	if (!buf || !len) return NULL;
	XmpFileObj* f = (XmpFileObj*)calloc(1, sizeof(XmpFileObj));
	if (!f) return NULL;
	f->h = INVALID_HANDLE_VALUE;
	f->mem = (BYTE*)malloc(len);
	if (!f->mem) { free(f); return NULL; }
	memcpy(f->mem, buf, len);
	f->memLen = len;
	return (XMPFILE)f;
}
static void WINAPI FxFileClose(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	if (!f) return;
	if (f->h && f->h != INVALID_HANDLE_VALUE) CloseHandle(f->h);
	free(f->mem);
	free(f);
}
static DWORD WINAPI FxFileGetType(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	return (f && f->mem) ? XMPFILE_TYPE_MEMORY : XMPFILE_TYPE_FILE;
}
static DWORD WINAPI FxFileGetSize(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	if (!f) return 0;
	if (f->mem) return f->memLen;
	return GetFileSize(f->h, NULL);
}
static const char* WINAPI FxFileGetFilename(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	return (f && f->name[0]) ? f->name : NULL;
}
static const void* WINAPI FxFileGetMemory(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	return f ? f->mem : NULL;
}
static DWORD WINAPI FxFileRead(XMPFILE file, void* buf, DWORD len)
{
	XmpFileObj* f = (XmpFileObj*)file;
	if (!f || !buf || !len) return 0;
	if (f->mem) {
		DWORD left = (f->memPos < f->memLen) ? (f->memLen - f->memPos) : 0;
		if (len > left) len = left;
		memcpy(buf, f->mem + f->memPos, len);
		f->memPos += len;
		return len;
	}
	DWORD rd = 0;
	ReadFile(f->h, buf, len, &rd, NULL);
	return rd;
}
static BOOL WINAPI FxFileSeek(XMPFILE file, DWORD pos)
{
	XmpFileObj* f = (XmpFileObj*)file;
	if (!f) return FALSE;
	if (f->mem) {
		if (pos > f->memLen) return FALSE;
		f->memPos = pos;
		return TRUE;
	}
	return SetFilePointer(f->h, (LONG)pos, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
}
static DWORD WINAPI FxFileTell(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	if (!f) return 0;
	if (f->mem) return f->memPos;
	return SetFilePointer(f->h, 0, NULL, FILE_CURRENT);
}
static void WINAPI FxFileNetSetRate(XMPFILE, DWORD) {}
static BOOL WINAPI FxFileNetIsActive(XMPFILE) { return FALSE; }
static BOOL WINAPI FxFileNetPreBuf(XMPFILE) { return FALSE; }
static DWORD WINAPI FxFileNetAvailable(XMPFILE) { return 0; }
static char* WINAPI FxFileArchiveList(XMPFILE) { return NULL; }
static XMPFILE WINAPI FxFileArchiveExtract(XMPFILE, const char*, DWORD) { return NULL; }

static BOOL WINAPI FxStatusIsPlaying() { return (g_xmpCur && g_xmpCur->xmp) ? TRUE : FALSE; }
static double WINAPI FxStatusGetTime()
{
	if (!g_xmpCur || g_xmpCur->rate <= 0 || g_xmpCur->ch <= 0) return 0.0;
	return (double)g_xmpCur->xmpPlayed / (double)(g_xmpCur->rate * g_xmpCur->ch);
}
static QWORD WINAPI FxStatusGetWritten()
{
	if (!g_xmpCur || g_xmpCur->ch <= 0) return 0;
	return (QWORD)(g_xmpCur->xmpPlayed / g_xmpCur->ch);
}
static DWORD WINAPI FxStatusGetLatency() { return 0; }
static const XMPFORMAT* WINAPI FxStatusGetFormat(BOOL)
{
	return g_xmpCur ? &g_xmpCur->xmpFmt : NULL;
}

static void* WINAPI FxFaceProc(DWORD face)
{
	switch (face) {
	case XMPFUNC_MISC_FACE:     return &g_fxMisc;
	case XMPFUNC_REGISTRY_FACE: return &g_fxReg;
	case XMPFUNC_FILE_FACE:     return &g_fxFile;
	case XMPFUNC_TEXT_FACE:     return &g_fxText;
	case XMPFUNC_STATUS_FACE:   return &g_fxStatus;
	case XMPFUNC_IN_FACE:       return &g_fxIn;
	}
	return NULL;
}

static void FxInitTables()
{
	if (g_fxTables) return;
	g_fxTables = 1;
	ZeroMemory(&g_fxIn, sizeof(g_fxIn));
	g_fxIn.SetLength = FxSetLength;
	g_fxIn.SetGain = FxSetGain;
	g_fxIn.UpdateTitle = FxUpdateTitle;
	g_fxIn.GetLooping = FxGetLooping;
	ZeroMemory(&g_fxMisc, sizeof(g_fxMisc));
	g_fxMisc.GetVersion = FxMiscGetVersion;
	g_fxMisc.GetWindow = FxMiscGetWindow;
	g_fxMisc.Alloc = FxMiscAlloc;
	g_fxMisc.ReAlloc = FxMiscReAlloc;
	g_fxMisc.Free = FxMiscFree;
	g_fxMisc.CheckCancel = FxMiscCheckCancel;
	g_fxMisc.GetConfig = FxMiscGetConfig;
	g_fxMisc.GetSkinConfig = FxMiscGetSkinConfig;
	g_fxMisc.ShowBubble = FxMiscShowBubble;
	g_fxMisc.RefreshInfo = FxMiscRefreshInfo;
	g_fxMisc.GetInfoText = FxMiscGetInfoText;
	g_fxMisc.FormatInfoText = FxMiscFormatInfoText;
	g_fxMisc.GetTag = FxMiscGetTag;
	g_fxMisc.RegisterShortcut = FxMiscRegisterShortcut;
	g_fxMisc.PerformShortcut = FxMiscPerformShortcut;
	g_fxMisc.GetCue = FxMiscGetCue;
	g_fxMisc.DDE = FxMiscDDE;
	ZeroMemory(&g_fxReg, sizeof(g_fxReg));
	g_fxReg.Get = FxRegGet;
	g_fxReg.GetString = FxRegGetString;
	g_fxReg.GetInt = FxRegGetInt;
	g_fxReg.Set = FxRegSet;
	g_fxReg.SetString = FxRegSetString;
	g_fxReg.SetInt = FxRegSetInt;
	ZeroMemory(&g_fxText, sizeof(g_fxText));
	g_fxText.Ansi = FxTextAnsi;
	g_fxText.Unicode = FxTextUnicode;
	g_fxText.Utf8 = FxTextUtf8;
	ZeroMemory(&g_fxStatus, sizeof(g_fxStatus));
	g_fxStatus.IsPlaying = FxStatusIsPlaying;
	g_fxStatus.GetTime = FxStatusGetTime;
	g_fxStatus.GetWritten = FxStatusGetWritten;
	g_fxStatus.GetLatency = FxStatusGetLatency;
	g_fxStatus.GetFormat = FxStatusGetFormat;
	ZeroMemory(&g_fxFile, sizeof(g_fxFile));
	g_fxFile.Open = FxFileOpen;
	g_fxFile.OpenMemory = FxFileOpenMemory;
	g_fxFile.Close = FxFileClose;
	g_fxFile.GetType = FxFileGetType;
	g_fxFile.GetSize = FxFileGetSize;
	g_fxFile.GetFilename = FxFileGetFilename;
	g_fxFile.GetMemory = FxFileGetMemory;
	g_fxFile.Read = FxFileRead;
	g_fxFile.Seek = FxFileSeek;
	g_fxFile.Tell = FxFileTell;
	g_fxFile.NetSetRate = FxFileNetSetRate;
	g_fxFile.NetIsActive = FxFileNetIsActive;
	g_fxFile.NetPreBuf = FxFileNetPreBuf;
	g_fxFile.NetAvailable = FxFileNetAvailable;
	g_fxFile.ArchiveList = FxFileArchiveList;
	g_fxFile.ArchiveExtract = FxFileArchiveExtract;
}

static void AppendDotExtA(std::wstring& out, const char* tok, size_t n)
{
	while (n && (*tok == ' ' || *tok == '\t' || *tok == '*' || *tok == '.')) { tok++; n--; }
	while (n && (tok[n - 1] == ' ' || tok[n - 1] == '\t')) n--;
	if (!n) return;
	if (!out.empty()) out += L'/';
	out += L'.';
	for (size_t i = 0; i < n; i++)
		out += (wchar_t)tolower((unsigned char)tok[i]);
}

static std::wstring ParseXmpExts(const char* exts)
{
	std::wstring out;
	if (!exts) return out;
	const char* p = exts;
	if (*p) p += strlen(p) + 1;
	if (!*p) return out;
	const char* start = p;
	for (;;) {
		const char* sl = strchr(start, '/');
		size_t n = sl ? (size_t)(sl - start) : strlen(start);
		AppendDotExtA(out, start, n);
		if (!sl) break;
		start = sl + 1;
	}
	return out;
}

typedef XMPIN* (WINAPI* pfn_XMPIN_GetInterface)(UINT32 face, InterfaceProc faceproc);

static uint32_t ForeignXmpListExts(HMODULE h, std::wstring& outExts)
{
	FxInitTables();
	pfn_XMPIN_GetInterface getIf = (pfn_XMPIN_GetInterface)GetProcAddress(h, "XMPIN_GetInterface");
	if (!getIf) return KPIHOST32_STATUS_FAIL;
	XMPIN* in = NULL;
	try { in = getIf(XMPIN_FACE, FxFaceProc); }
	catch (...) { in = NULL; }
	if (!in || !in->Open || !in->Process) return KPIHOST32_STATUS_FAIL;
	outExts = ParseXmpExts(in->exts);
	return outExts.empty() ? KPIHOST32_STATUS_FAIL : KPIHOST32_STATUS_OK;
}

static void ForeignXmpCloseSession(ForeignSession* s)
{
	if (!s) return;
	if (s->xmp && s->xmp->Close) {
		try { s->xmp->Close(); }
		catch (...) {}
	}
	s->xmp = nullptr;
	if (s->xmpFile) {
		FxFileClose((XMPFILE)s->xmpFile);
		s->xmpFile = nullptr;
	}
	if (g_xmpCur == s) g_xmpCur = nullptr;
}

static uint32_t ForeignXmpOpen(ForeignSession* s, const std::wstring& media)
{
	FxInitTables();
	pfn_XMPIN_GetInterface getIf = (pfn_XMPIN_GetInterface)GetProcAddress(s->dll, "XMPIN_GetInterface");
	if (!getIf) return KPIHOST32_STATUS_FAIL;
	XMPIN* in = NULL;
	try { in = getIf(XMPIN_FACE, FxFaceProc); }
	catch (...) { in = NULL; }
	if (!in || !in->Open || !in->Process) return KPIHOST32_STATUS_FAIL;
	char pathA[MAX_PATH * 2];
	pathA[0] = 0;
	WideCharToMultiByte(CP_UTF8, 0, media.c_str(), -1, pathA, (int)sizeof(pathA), NULL, NULL);
	s->xmpLen = 0.f;
	s->pendN = 0;
	s->pendOff = 0;
	s->eof = 0;
	s->xmpPlayed = 0;
	g_xmpCur = s;
	XmpFileObj* file = NULL;
	if (!(in->flags & XMPIN_FLAG_NOXMPFILE))
		file = (XmpFileObj*)FxFileOpen(pathA);
	if (in->CheckFile && !in->CheckFile(pathA, (XMPFILE)file)) {
		if (file) FxFileClose((XMPFILE)file);
		g_xmpCur = nullptr;
		return KPIHOST32_STATUS_FAIL;
	}
	if (file) FxFileSeek((XMPFILE)file, 0);
	DWORD orc = 0;
	try { orc = in->Open(pathA, (XMPFILE)file); }
	catch (...) { orc = 0; }
	if (orc == 0) {
		if (file) FxFileClose((XMPFILE)file);
		g_xmpCur = nullptr;
		return KPIHOST32_STATUS_FAIL;
	}
	if (orc == 2) {
		if (file) FxFileClose((XMPFILE)file);
		file = NULL;
	}
	s->xmp = in;
	s->xmpFile = file;
	XMPFORMAT form;
	ZeroMemory(&form, sizeof(form));
	if (in->SetFormat) in->SetFormat(&form);
	s->rate = (form.rate > 0) ? (int)form.rate : 44100;
	s->ch = (form.chan > 0) ? (int)form.chan : 2;
	if (s->ch > 8) s->ch = 8;
	s->bits = 16;
	s->xmpFmt.rate = (DWORD)s->rate;
	s->xmpFmt.chan = (DWORD)s->ch;
	s->xmpFmt.res = 2;
	s->playing = 1;
	return KPIHOST32_STATUS_OK;
}

static void FxFloatToS16(const float* src, int count, BYTE* dst)
{
	for (int i = 0; i < count; ++i) {
		double v = (double)src[i];
		if (!_finite(v)) v = 0.0;
		double sv = v * 32767.0;
		if (sv > 32767.0) sv = 32767.0;
		else if (sv < -32768.0) sv = -32768.0;
		short out = (short)(sv >= 0.0 ? (sv + 0.5) : (sv - 0.5));
		dst[i * 2] = (BYTE)(out & 0xff);
		dst[i * 2 + 1] = (BYTE)((out >> 8) & 0xff);
	}
}

static uint32_t ForeignXmpRender(ForeignSession* s, uint32_t want, uint8_t* dest, uint32_t destCap, uint32_t& gotBytes, uint32_t& eof)
{
	gotBytes = 0;
	eof = 0;
	if (!s || !s->xmp || !s->xmp->Process) return KPIHOST32_STATUS_FAIL;
	if (want > destCap) want = destCap;
	g_xmpCur = s;
	uint32_t got = 0;
	while (got < want) {
		if (s->pendOff < s->pendN) {
			uint32_t take = (uint32_t)(s->pendN - s->pendOff);
			if (take > want - got) take = want - got;
			memcpy(dest + got, s->pending + s->pendOff, take);
			s->pendOff += take;
			got += take;
			continue;
		}
		if (s->eof) break;
		int wantFloats = (int)((want - got) / 2);
		if (wantFloats > FXMP_MAX_FLOATS) wantFloats = FXMP_MAX_FLOATS;
		if (s->ch > 0) wantFloats -= wantFloats % s->ch;
		if (wantFloats <= 0) wantFloats = s->ch > 0 ? s->ch : 1;
		DWORD n = 0;
		try { n = s->xmp->Process(s->fbuf, (DWORD)wantFloats); }
		catch (...) { n = 0; }
		if (n == 0 || n > (DWORD)wantFloats) {
			s->eof = 1;
			break;
		}
		s->pendN = (uint32_t)n * 2u;
		FxFloatToS16(s->fbuf, (int)n, s->pending);
		s->pendOff = 0;
		s->xmpPlayed += (__int64)n;
	}
	gotBytes = got;
	if (got == 0 && s->eof) eof = 1;
	return KPIHOST32_STATUS_OK;
}

static uint32_t ForeignXmpSeek(ForeignSession* s, uint64_t posSample)
{
	if (!s || !s->xmp || !s->xmp->SetPosition || s->rate <= 0) return KPIHOST32_STATUS_FAIL;
	g_xmpCur = s;
	double gran = 0.001;
	if (s->xmp->GetGranularity) {
		double g = s->xmp->GetGranularity();
		if (g > 0.0) gran = g;
	}
	double sec = (double)posSample / (double)s->rate;
	s->xmp->SetPosition((DWORD)(sec / gran));
	s->pendN = 0;
	s->pendOff = 0;
	s->eof = 0;
	s->xmpPlayed = (__int64)(sec * s->rate * s->ch);
	return KPIHOST32_STATUS_OK;
}

// ---- AIMP (PluginAimp.cpp と同じ Core / Stream / Decoder) ----

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
	std::wstring m_exts;
	std::wstring m_pluginDir;
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
		*Value = new CAimpString(m_pluginDir.empty() ? L"." : m_pluginDir.c_str());
		return S_OK;
	}
	HRESULT WINAPI RegisterExtension(REFIID, IUnknown* Extension)
	{
		if (!Extension) return E_INVALIDARG;
		IAIMPExtensionFileFormat* fmt = NULL;
		if (SUCCEEDED(Extension->QueryInterface(IID_IAIMPExtensionFileFormat, (void**)&fmt)) && fmt) {
			IAIMPString* str = NULL;
			if (SUCCEEDED(fmt->GetExtList(&str)) && str) {
				if (!m_exts.empty()) m_exts += L";";
				m_exts += str->GetData() ? str->GetData() : L"";
				str->Release();
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

static std::wstring ParseAimpExts(const std::wstring& list)
{
	std::wstring out;
	size_t start = 0;
	while (start <= list.size()) {
		size_t sc = list.find(L';', start);
		std::wstring tok = (sc == std::wstring::npos) ? list.substr(start) : list.substr(start, sc - start);
		size_t b = 0;
		while (b < tok.size() && (tok[b] == L' ' || tok[b] == L'\t' || tok[b] == L'*' || tok[b] == L'.')) b++;
		size_t e = tok.size();
		while (e > b && (tok[e - 1] == L' ' || tok[e - 1] == L'\t')) e--;
		if (e > b) {
			if (!out.empty()) out += L'/';
			out += L'.';
			for (size_t i = b; i < e; i++) {
				wchar_t c = tok[i];
				if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
				out += c;
			}
		}
		if (sc == std::wstring::npos) break;
		start = sc + 1;
	}
	return out;
}

static void ForeignAimpCloseSession(ForeignSession* s)
{
	if (!s) return;
	if (s->aimpDec) { s->aimpDec->Release(); s->aimpDec = nullptr; }
	if (s->aimpStream) { s->aimpStream->Release(); s->aimpStream = nullptr; }
	if (s->aimpPlug) {
		s->aimpPlug->Finalize();
		s->aimpPlug->Release();
		s->aimpPlug = nullptr;
	}
	if (s->aimpCore) {
		if (s->aimpCore->m_extOld) { s->aimpCore->m_extOld->Release(); s->aimpCore->m_extOld = NULL; }
		if (s->aimpCore->m_extNew) { s->aimpCore->m_extNew->Release(); s->aimpCore->m_extNew = NULL; }
		s->aimpCore->Release();
		s->aimpCore = nullptr;
	}
	s->aimpExtOld = nullptr;
	s->aimpExtNew = nullptr;
	s->aimpFloat = 0;
	s->aimpSize = 0;
}

static int ForeignAimpInitPlugin(ForeignSession* s, const std::wstring& dllPath)
{
	if (!s->dll) return 0;
	TAIMPPluginGetHeaderProc getHdr = (TAIMPPluginGetHeaderProc)GetProcAddress(s->dll, "AIMPPluginGetHeader");
	if (!getHdr) return 0;
	IAIMPPlugin* plug = NULL;
	if (FAILED(getHdr(&plug)) || !plug) return 0;
	if ((plug->InfoGetCategories() & AIMP_PLUGIN_CATEGORY_DECODERS) == 0) {
		plug->Release();
		return 0;
	}
	CAimpCore* core = new CAimpCore();
	size_t slash = dllPath.find_last_of(L"\\/");
	core->m_pluginDir = (slash == std::wstring::npos) ? L"." : dllPath.substr(0, slash);
	if (FAILED(plug->Initialize(core))) {
		plug->Release();
		core->Release();
		return 0;
	}
	s->aimpPlug = plug;
	s->aimpCore = core;
	s->aimpExtOld = core->m_extOld;
	s->aimpExtNew = core->m_extNew;
	s->aimpExts = core->m_exts;
	return (core->m_extOld || core->m_extNew) ? 1 : 0;
}

static uint32_t ForeignAimpListExts(HMODULE h, const std::wstring& path, std::wstring& outExts)
{
	ForeignSession probe;
	probe.dll = h;
	if (!ForeignAimpInitPlugin(&probe, path)) {
		ForeignAimpCloseSession(&probe);
		probe.dll = NULL;
		return KPIHOST32_STATUS_FAIL;
	}
	outExts = ParseAimpExts(probe.aimpExts);
	ForeignAimpCloseSession(&probe);
	probe.dll = NULL;
	return outExts.empty() ? KPIHOST32_STATUS_FAIL : KPIHOST32_STATUS_OK;
}

static uint32_t ForeignAimpOpen(ForeignSession* s, const std::wstring& dll, const std::wstring& media)
{
	if (!ForeignAimpInitPlugin(s, dll)) {
		ForeignAimpCloseSession(s);
		return KPIHOST32_STATUS_FAIL;
	}
	IAIMPAudioDecoder* dec = NULL;
	CAimpErrorInfo* err = new CAimpErrorInfo();
	if (s->aimpCore && s->aimpCore->m_extOld) {
		CAimpString* fn = new CAimpString(media.c_str());
		if (FAILED(s->aimpCore->m_extOld->CreateDecoder(fn, 0, err, &dec)))
			dec = NULL;
		fn->Release();
	}
	if (!dec && s->aimpCore && s->aimpCore->m_extNew) {
		CAimpFileStream* st = new CAimpFileStream(media.c_str());
		if (st->IsValid()) {
			if (FAILED(s->aimpCore->m_extNew->CreateDecoder(st, 0, err, &dec)))
				dec = NULL;
		}
		if (dec) s->aimpStream = st;
		else st->Release();
	}
	err->Release();
	if (!dec) {
		ForeignAimpCloseSession(s);
		return KPIHOST32_STATUS_FAIL;
	}
	int sr = 0, ch = 0, fmt = 0;
	if (!dec->GetStreamInfo(&sr, &ch, &fmt)) {
		dec->Release();
		ForeignAimpCloseSession(s);
		return KPIHOST32_STATUS_FAIL;
	}
	s->aimpDec = dec;
	s->rate = sr > 0 ? sr : 44100;
	s->ch = ch > 0 ? ch : 2;
	s->aimpFloat = 0;
	if (fmt == AIMP_DECODER_SAMPLEFORMAT_08BIT) s->bits = 8;
	else if (fmt == AIMP_DECODER_SAMPLEFORMAT_16BIT) s->bits = 16;
	else if (fmt == AIMP_DECODER_SAMPLEFORMAT_24BIT) s->bits = 24;
	else if (fmt == AIMP_DECODER_SAMPLEFORMAT_32BIT) s->bits = 32;
	else if (fmt == AIMP_DECODER_SAMPLEFORMAT_32BITFLOAT) { s->bits = 32; s->aimpFloat = 1; }
	else s->bits = 16;
	s->aimpSize = dec->GetSize();
	s->playing = 1;
	return KPIHOST32_STATUS_OK;
}

static uint32_t ForeignAimpRender(ForeignSession* s, uint32_t want, uint8_t* dest, uint32_t destCap, uint32_t& gotBytes, uint32_t& eof)
{
	gotBytes = 0;
	eof = 0;
	if (!s || !s->aimpDec) return KPIHOST32_STATUS_FAIL;
	if (want > destCap) want = destCap;
	int n = 0;
	try { n = s->aimpDec->Read(dest, want); }
	catch (...) { n = 0; }
	if (n <= 0) {
		eof = 1;
		return KPIHOST32_STATUS_OK;
	}
	if (s->aimpFloat) {
		float* f = (float*)dest;
		int* i32 = (int*)dest;
		const int cntS = n / 4;
		for (int i = 0; i < cntS; i++) {
			double v = (double)f[i];
			if (!_finite(v)) v = 0.0;
			double sv = v * 2147483647.0;
			if (sv > 2147483647.0) sv = 2147483647.0;
			if (sv < -2147483648.0) sv = -2147483648.0;
			i32[i] = (int)(sv >= 0.0 ? (sv + 0.5) : (sv - 0.5));
		}
	}
	gotBytes = (uint32_t)n;
	return KPIHOST32_STATUS_OK;
}

static uint32_t ForeignAimpSeek(ForeignSession* s, uint64_t posSample)
{
	if (!s || !s->aimpDec || s->ch <= 0) return KPIHOST32_STATUS_FAIL;
	int bpf = (s->bits / 8) * s->ch;
	if (bpf <= 0) return KPIHOST32_STATUS_FAIL;
	INT64 pos = (INT64)posSample * bpf;
	return s->aimpDec->SetPosition(pos) ? KPIHOST32_STATUS_OK : KPIHOST32_STATUS_FAIL;
}
