// XMPlay input plugin host (XMPIN pull Process)
//
// xmpin.h / xmpfunc.h と実プラグイン(xmp-vgmstream 等)の実装に合わせた点:
//  - faceproc は MISC(0) / REGISTRY(1) / FILE(2) / TEXT(3) / STATUS(4) / IN(11) を全部返す。
//    どれか1つでも NULL を返すと XMPIN_GetInterface 内で参照して落ちる／NULL を返すプラグインが多い。
//  - Open(filename, file) の file は XMPFILE。XMPIN_FLAG_NOXMPFILE 以外は必ず開いて渡す。
//    戻り値 0=失敗 / 1=成功 / 2=成功(ホストが file を閉じてよい)。
//  - Process(buf, count) の count は「float の個数」であってフレーム数でもバイト数でもない。
//    戻り値も float の個数。ここを frame と誤ると倍のサンプルを拾って無音／雑音になる。
//  - 文字列は UTF-8。
//  - SetFormat はプラグインが XMPFORMAT を埋める（ホストが指定するのではない）。
#include "stdafx.h"
#include "PluginXmplay.h"
#include "PluginKinds.h"
#include "third_party/xmplay/xmpin.h"
#include <float.h>

extern CString kpif[];
extern CString ext[][300];
extern BYTE kvar[][300];
extern BYTE kpiarch[];
extern BYTE plugkind[];
extern BOOL kpichk[];
extern int kpicnt;

enum { XMP_MAX_FLOATS = 8192 * 8 };

struct XmpFileObj
{
	HANDLE h;
	BYTE* mem;
	DWORD memLen;
	DWORD memPos;
	char name[MAX_PATH * 2];
};

static HMODULE g_xmpDll = NULL;
static XMPIN* g_xmpIn = NULL;
static int g_xmpOpen = 0;
static int g_xmpRate = 44100;
static int g_xmpCh = 2;
static int g_xmpBits = 16;
static float g_xmpLen = 0.f;
static float g_xmpFloatBuf[XMP_MAX_FLOATS];
static BYTE g_xmpPcmPending[XMP_MAX_FLOATS * 2];
static int g_xmpPendingBytes = 0;
static int g_xmpPendingOff = 0;
static int g_xmpEof = 0;
static XmpFileObj* g_xmpCurFile = NULL;   // Open に渡した XMPFILE（戻り値2ならここで閉じる）
static __int64 g_xmpPlayedFloats = 0;
static XMPFORMAT g_xmpFmt = { 44100, 2, 2 };

static XMPFUNC_IN g_xmpFuncIn;
static XMPFUNC_MISC g_xmpFuncMisc;
static XMPFUNC_FILE g_xmpFuncFile;
static XMPFUNC_REGISTRY g_xmpFuncReg;
static XMPFUNC_TEXT g_xmpFuncText;
static XMPFUNC_STATUS g_xmpFuncStatus;

// ---- XMPFUNC_IN ----
static void WINAPI XmpSetLength(float length, BOOL)
{
	if (length >= 0.f) g_xmpLen = length;
}
static void WINAPI XmpSetGain(DWORD, float) {}
static BOOL WINAPI XmpUpdateTitle(const char*) { return FALSE; }
static BOOL WINAPI XmpGetLooping() { return FALSE; }

// ---- XMPFUNC_MISC ----
static DWORD WINAPI XmpMiscGetVersion() { return 0x03080400; } // 3.8.4
static HWND WINAPI XmpMiscGetWindow() { return NULL; }
static void* WINAPI XmpMiscAlloc(DWORD len) { return malloc(len ? len : 1); }
static void* WINAPI XmpMiscReAlloc(void* mem, DWORD len) { return realloc(mem, len ? len : 1); }
static void WINAPI XmpMiscFree(void* mem) { free(mem); }
static BOOL WINAPI XmpMiscCheckCancel() { return FALSE; }
static DWORD WINAPI XmpMiscGetConfig(DWORD option)
{
	if (option == XMPCONFIG_OUTPUT) return (DWORD)(DWORD_PTR)&g_xmpFmt;
	return 0;
}
static const char* WINAPI XmpMiscGetSkinConfig(const char*) { return NULL; }
static void WINAPI XmpMiscShowBubble(const char*, DWORD) {}
static void WINAPI XmpMiscRefreshInfo(DWORD) {}
static char* WINAPI XmpMiscGetInfoText(DWORD) { return NULL; }
static char* WINAPI XmpMiscFormatInfoText(char* buf, const char*, const char*) { return buf; }
static char* WINAPI XmpMiscGetTag(const char*) { return NULL; }
static BOOL WINAPI XmpMiscRegisterShortcut(const XMPSHORTCUT*) { return FALSE; }
static BOOL WINAPI XmpMiscPerformShortcut(DWORD) { return FALSE; }
static const XMPCUE* WINAPI XmpMiscGetCue(DWORD) { return NULL; }
static BOOL WINAPI XmpMiscDDE(const char*) { return FALSE; }

// ---- XMPFUNC_REGISTRY ----
// 設定は持たない。既定値で動くように「未設定」を返す。
static DWORD WINAPI XmpRegGet(const char*, const char*, void*, DWORD) { return 0; }
static DWORD WINAPI XmpRegGetString(const char*, const char*, char* data, DWORD size)
{
	if (data && size) data[0] = 0;
	return 0;
}
static BOOL WINAPI XmpRegGetInt(const char*, const char*, int*) { return FALSE; }
static BOOL WINAPI XmpRegSet(const char*, const char*, const void*, DWORD) { return TRUE; }
static BOOL WINAPI XmpRegSetString(const char*, const char*, const char*) { return TRUE; }
static BOOL WINAPI XmpRegSetInt(const char*, const char*, const int*) { return TRUE; }

// ---- XMPFUNC_TEXT（戻り値は Free で解放される前提の新規確保） ----
static char* XmpDupA(const char* s, int len)
{
	if (!s) return NULL;
	if (len < 0) len = (int)strlen(s);
	char* r = (char*)malloc((size_t)len + 1);
	if (!r) return NULL;
	memcpy(r, s, (size_t)len);
	r[len] = 0;
	return r;
}
static char* WINAPI XmpTextAnsi(const char* text, int len)
{
	// ANSI → UTF-8
	if (!text) return NULL;
	int wl = MultiByteToWideChar(CP_ACP, 0, text, len, NULL, 0);
	if (wl <= 0) return XmpDupA(text, len);
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
static char* WINAPI XmpTextUnicode(const WCHAR* text, int len)
{
	if (!text) return NULL;
	int ul = WideCharToMultiByte(CP_UTF8, 0, text, len, NULL, 0, NULL, NULL);
	char* r = (char*)malloc((size_t)(ul > 0 ? ul : 0) + 1);
	if (!r) return NULL;
	if (ul > 0) WideCharToMultiByte(CP_UTF8, 0, text, len, r, ul, NULL, NULL);
	r[ul > 0 ? ul : 0] = 0;
	return r;
}
static char* WINAPI XmpTextUtf8(const char* text, int len) { return XmpDupA(text, len); }

// ---- XMPFUNC_FILE ----
static XMPFILE WINAPI XmpFileOpen(const char* filename)
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
static XMPFILE WINAPI XmpFileOpenMemory(const void* buf, DWORD len)
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
static void WINAPI XmpFileClose(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	if (!f) return;
	if (f->h && f->h != INVALID_HANDLE_VALUE) CloseHandle(f->h);
	free(f->mem);
	free(f);
}
static DWORD WINAPI XmpFileGetType(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	return (f && f->mem) ? XMPFILE_TYPE_MEMORY : XMPFILE_TYPE_FILE;
}
static DWORD WINAPI XmpFileGetSize(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	if (!f) return 0;
	if (f->mem) return f->memLen;
	return GetFileSize(f->h, NULL);
}
static const char* WINAPI XmpFileGetFilename(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	return (f && f->name[0]) ? f->name : NULL;
}
static const void* WINAPI XmpFileGetMemory(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	return f ? f->mem : NULL;
}
static DWORD WINAPI XmpFileRead(XMPFILE file, void* buf, DWORD len)
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
static BOOL WINAPI XmpFileSeek(XMPFILE file, DWORD pos)
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
static DWORD WINAPI XmpFileTell(XMPFILE file)
{
	XmpFileObj* f = (XmpFileObj*)file;
	if (!f) return 0;
	if (f->mem) return f->memPos;
	return SetFilePointer(f->h, 0, NULL, FILE_CURRENT);
}
static void WINAPI XmpFileNetSetRate(XMPFILE, DWORD) {}
static BOOL WINAPI XmpFileNetIsActive(XMPFILE) { return FALSE; }
static BOOL WINAPI XmpFileNetPreBuf(XMPFILE) { return FALSE; }
static DWORD WINAPI XmpFileNetAvailable(XMPFILE) { return 0; }
static char* WINAPI XmpFileArchiveList(XMPFILE) { return NULL; }
static XMPFILE WINAPI XmpFileArchiveExtract(XMPFILE, const char*, DWORD) { return NULL; }

// ---- XMPFUNC_STATUS ----
static BOOL WINAPI XmpStatusIsPlaying() { return g_xmpOpen ? TRUE : FALSE; }
static double WINAPI XmpStatusGetTime()
{
	if (g_xmpRate <= 0 || g_xmpCh <= 0) return 0.0;
	return (double)g_xmpPlayedFloats / (double)(g_xmpRate * g_xmpCh);
}
static QWORD WINAPI XmpStatusGetWritten()
{
	return (g_xmpCh > 0) ? (QWORD)(g_xmpPlayedFloats / g_xmpCh) : 0;
}
static DWORD WINAPI XmpStatusGetLatency() { return 0; }
static const XMPFORMAT* WINAPI XmpStatusGetFormat(BOOL) { return &g_xmpFmt; }

static void* WINAPI XmpFaceProc(DWORD face)
{
	switch (face) {
	case XMPFUNC_MISC_FACE:     return &g_xmpFuncMisc;
	case XMPFUNC_REGISTRY_FACE: return &g_xmpFuncReg;
	case XMPFUNC_FILE_FACE:     return &g_xmpFuncFile;
	case XMPFUNC_TEXT_FACE:     return &g_xmpFuncText;
	case XMPFUNC_STATUS_FACE:   return &g_xmpFuncStatus;
	case XMPFUNC_IN_FACE:       return &g_xmpFuncIn;
	}
	return NULL;
}

static void XmpInitHostTables()
{
	ZeroMemory(&g_xmpFuncIn, sizeof(g_xmpFuncIn));
	g_xmpFuncIn.SetLength = XmpSetLength;
	g_xmpFuncIn.SetGain = XmpSetGain;
	g_xmpFuncIn.UpdateTitle = XmpUpdateTitle;
	g_xmpFuncIn.GetLooping = XmpGetLooping;

	ZeroMemory(&g_xmpFuncMisc, sizeof(g_xmpFuncMisc));
	g_xmpFuncMisc.GetVersion = XmpMiscGetVersion;
	g_xmpFuncMisc.GetWindow = XmpMiscGetWindow;
	g_xmpFuncMisc.Alloc = XmpMiscAlloc;
	g_xmpFuncMisc.ReAlloc = XmpMiscReAlloc;
	g_xmpFuncMisc.Free = XmpMiscFree;
	g_xmpFuncMisc.CheckCancel = XmpMiscCheckCancel;
	g_xmpFuncMisc.GetConfig = XmpMiscGetConfig;
	g_xmpFuncMisc.GetSkinConfig = XmpMiscGetSkinConfig;
	g_xmpFuncMisc.ShowBubble = XmpMiscShowBubble;
	g_xmpFuncMisc.RefreshInfo = XmpMiscRefreshInfo;
	g_xmpFuncMisc.GetInfoText = XmpMiscGetInfoText;
	g_xmpFuncMisc.FormatInfoText = XmpMiscFormatInfoText;
	g_xmpFuncMisc.GetTag = XmpMiscGetTag;
	g_xmpFuncMisc.RegisterShortcut = XmpMiscRegisterShortcut;
	g_xmpFuncMisc.PerformShortcut = XmpMiscPerformShortcut;
	g_xmpFuncMisc.GetCue = XmpMiscGetCue;
	g_xmpFuncMisc.DDE = XmpMiscDDE;

	ZeroMemory(&g_xmpFuncReg, sizeof(g_xmpFuncReg));
	g_xmpFuncReg.Get = XmpRegGet;
	g_xmpFuncReg.GetString = XmpRegGetString;
	g_xmpFuncReg.GetInt = XmpRegGetInt;
	g_xmpFuncReg.Set = XmpRegSet;
	g_xmpFuncReg.SetString = XmpRegSetString;
	g_xmpFuncReg.SetInt = XmpRegSetInt;

	ZeroMemory(&g_xmpFuncText, sizeof(g_xmpFuncText));
	g_xmpFuncText.Ansi = XmpTextAnsi;
	g_xmpFuncText.Unicode = XmpTextUnicode;
	g_xmpFuncText.Utf8 = XmpTextUtf8;

	ZeroMemory(&g_xmpFuncStatus, sizeof(g_xmpFuncStatus));
	g_xmpFuncStatus.IsPlaying = XmpStatusIsPlaying;
	g_xmpFuncStatus.GetTime = XmpStatusGetTime;
	g_xmpFuncStatus.GetWritten = XmpStatusGetWritten;
	g_xmpFuncStatus.GetLatency = XmpStatusGetLatency;
	g_xmpFuncStatus.GetFormat = XmpStatusGetFormat;

	ZeroMemory(&g_xmpFuncFile, sizeof(g_xmpFuncFile));
	g_xmpFuncFile.Open = XmpFileOpen;
	g_xmpFuncFile.OpenMemory = XmpFileOpenMemory;
	g_xmpFuncFile.Close = XmpFileClose;
	g_xmpFuncFile.GetType = XmpFileGetType;
	g_xmpFuncFile.GetSize = XmpFileGetSize;
	g_xmpFuncFile.GetFilename = XmpFileGetFilename;
	g_xmpFuncFile.GetMemory = XmpFileGetMemory;
	g_xmpFuncFile.Read = XmpFileRead;
	g_xmpFuncFile.Seek = XmpFileSeek;
	g_xmpFuncFile.Tell = XmpFileTell;
	g_xmpFuncFile.NetSetRate = XmpFileNetSetRate;
	g_xmpFuncFile.NetIsActive = XmpFileNetIsActive;
	g_xmpFuncFile.NetPreBuf = XmpFileNetPreBuf;
	g_xmpFuncFile.NetAvailable = XmpFileNetAvailable;
	g_xmpFuncFile.ArchiveList = XmpFileArchiveList;
	g_xmpFuncFile.ArchiveExtract = XmpFileArchiveExtract;
}

// exts は "説明\0ext1/ext2/etc"
static void XmpParseExts(const char* exts)
{
	ext[kpicnt][0] = L"";
	ext[kpicnt][299] = L"";
	if (!exts) return;
	const char* p = exts;
	if (*p) p += strlen(p) + 1;
	if (!*p) return;
	CStringA listA(p);
	int ei = 0;
	int start = 0;
	for (;;) {
		int slash = listA.Find('/', start);
		CStringA tokA = (slash < 0) ? listA.Mid(start) : listA.Mid(start, slash - start);
		tokA.Trim();
		if (!tokA.IsEmpty() && ei < 298) {
			CString e(tokA);
			e.MakeLower();
			int st = 0;
			while (st < e.GetLength() && (e[st] == L'*' || e[st] == L'.')) st++;
			e = e.Mid(st);
			if (!e.IsEmpty()) {
				e = L"." + e;
				ext[kpicnt][ei] = e;
				kvar[kpicnt][ei] = 0;
				ei++;
			}
		}
		if (slash < 0) break;
		start = slash + 1;
	}
	ext[kpicnt][ei] = L"";
}

typedef XMPIN* (WINAPI* pfn_XMPIN_GetInterface)(UINT32 face, InterfaceProc faceproc);

int PluginXmplay_TryEnum(const wchar_t* dllPath, int is64)
{
	if (!dllPath || !dllPath[0] || kpicnt >= 149) return 0;
	if (is64) {
		// x64 は KpiHost64 側に XMPlay 再生系が無いので台帳に載せない（載せると -2 に落ちるだけ）
		return 0;
	}
	XmpInitHostTables();
	HMODULE h = LoadLibraryExW(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) return 0;
	pfn_XMPIN_GetInterface getIf = (pfn_XMPIN_GetInterface)GetProcAddress(h, "XMPIN_GetInterface");
	if (!getIf) {
		FreeLibrary(h);
		return 0;
	}
	int ok = 0;
	try {
		XMPIN* in = getIf(XMPIN_FACE, XmpFaceProc);
		if (in && in->Open && in->Process) {
			plugkind[kpicnt] = PLUGKIND_XMPLAY;
			kpiarch[kpicnt] = 32;
			kpif[kpicnt] = dllPath;
			XmpParseExts(in->exts);
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
	FreeLibrary(h);
	return ok;
}

int PluginXmplay_Open(const wchar_t* dllPath, const wchar_t* mediaPath)
{
	PluginXmplay_Close();
	XmpInitHostTables();
	HMODULE h = LoadLibraryExW(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) return 0;
	pfn_XMPIN_GetInterface getIf = (pfn_XMPIN_GetInterface)GetProcAddress(h, "XMPIN_GetInterface");
	if (!getIf) {
		FreeLibrary(h);
		return 0;
	}
	XMPIN* in = getIf(XMPIN_FACE, XmpFaceProc);
	if (!in || !in->Open || !in->Process) {
		FreeLibrary(h);
		return 0;
	}
	char pathA[MAX_PATH * 2];
	pathA[0] = 0;
	WideCharToMultiByte(CP_UTF8, 0, mediaPath, -1, pathA, (int)sizeof(pathA), NULL, NULL);

	g_xmpLen = 0.f;
	g_xmpPendingBytes = 0;
	g_xmpPendingOff = 0;
	g_xmpEof = 0;
	g_xmpPlayedFloats = 0;

	// XMPIN_FLAG_NOXMPFILE 以外は XMPFILE を開いて渡す。NULL を渡すと大半のプラグインが失敗する。
	// Open が通るまで g_xmpIn は握らない（Open 前に Close() を呼ぶと落ちるプラグインがある）。
	XmpFileObj* file = NULL;
	if (!(in->flags & XMPIN_FLAG_NOXMPFILE))
		file = (XmpFileObj*)XmpFileOpen(pathA);
	if (in->CheckFile && !in->CheckFile(pathA, (XMPFILE)file)) {
		if (file) XmpFileClose((XMPFILE)file);
		FreeLibrary(h);
		return 0;
	}
	if (file) XmpFileSeek((XMPFILE)file, 0);

	DWORD orc = 0;
	try { orc = in->Open(pathA, (XMPFILE)file); }
	catch (...) { orc = 0; }
	if (orc == 0) {
		if (file) XmpFileClose((XMPFILE)file);
		FreeLibrary(h);
		return 0;
	}
	if (orc == 2) {
		// 2 = 成功かつホストがファイルを閉じてよい
		if (file) XmpFileClose((XMPFILE)file);
		file = NULL;
	}
	g_xmpDll = h;
	g_xmpIn = in;
	g_xmpCurFile = file;

	// SetFormat はプラグインが自分のネイティブ形式を書き込む
	XMPFORMAT form;
	ZeroMemory(&form, sizeof(form));
	if (in->SetFormat) in->SetFormat(&form);
	g_xmpRate = (form.rate > 0) ? (int)form.rate : 44100;
	g_xmpCh = (form.chan > 0) ? (int)form.chan : 2;
	if (g_xmpCh > 8) g_xmpCh = 8;
	g_xmpBits = 16; // Process は常に float。ホストへは 16bit PCM で渡す
	g_xmpFmt.rate = (DWORD)g_xmpRate;
	g_xmpFmt.chan = (DWORD)g_xmpCh;
	g_xmpFmt.res = 2;
	g_xmpOpen = 1;
	return 1;
}

void PluginXmplay_Close()
{
	if (g_xmpIn && g_xmpIn->Close) {
		try { g_xmpIn->Close(); }
		catch (...) {}
	}
	g_xmpIn = NULL;
	if (g_xmpCurFile) {
		XmpFileClose((XMPFILE)g_xmpCurFile);
		g_xmpCurFile = NULL;
	}
	if (g_xmpDll) {
		FreeLibrary(g_xmpDll);
		g_xmpDll = NULL;
	}
	g_xmpOpen = 0;
	g_xmpPendingBytes = 0;
	g_xmpPendingOff = 0;
	g_xmpEof = 0;
	g_xmpPlayedFloats = 0;
}

int PluginXmplay_SeekSec(double sec)
{
	if (!g_xmpIn || !g_xmpOpen || !g_xmpIn->SetPosition) return 0;
	// 既定の粒度はミリ秒（GetGranularity があればそれが1単位の秒数）
	double gran = 0.001;
	if (g_xmpIn->GetGranularity) {
		double g = g_xmpIn->GetGranularity();
		if (g > 0.0) gran = g;
	}
	g_xmpIn->SetPosition((DWORD)(sec / gran));
	g_xmpPendingBytes = 0;
	g_xmpPendingOff = 0;
	g_xmpEof = 0;
	g_xmpPlayedFloats = (__int64)(sec * g_xmpRate * g_xmpCh);
	return 1;
}

int PluginXmplay_IsOpen() { return g_xmpOpen; }
int PluginXmplay_SampleRate() { return g_xmpRate; }
int PluginXmplay_Channels() { return g_xmpCh; }
int PluginXmplay_Bits() { return g_xmpBits; }
double PluginXmplay_LengthSec() { return (double)g_xmpLen; }

static void XmpFloatToS16(const float* src, int count, BYTE* dst)
{
	// XMPlay Process は正規化 float。KPI の GetFloatToInt16Scale(底上げ) は使わない。
	// （底上げは PSF 系の極端に小さい振幅向け。vgmstream 等に掛けるとピークで割れる）
	for (int i = 0; i < count; ++i) {
		double v = (double)src[i];
		if (!_finite(v)) v = 0.0;
		double s = v * 32767.0;
		if (s > 32767.0) s = 32767.0;
		else if (s < -32768.0) s = -32768.0;
		short out = (short)(s >= 0.0 ? (s + 0.5) : (s - 0.5));
		dst[i * 2] = (BYTE)(out & 0xff);
		dst[i * 2 + 1] = (BYTE)((out >> 8) & 0xff);
	}
}

int PluginXmplay_Read(BYTE* dst, int bytesWanted)
{
	if (!dst || bytesWanted <= 0 || !g_xmpOpen || !g_xmpIn) return 0;
	int got = 0;
	while (got < bytesWanted) {
		if (g_xmpPendingOff < g_xmpPendingBytes) {
			int take = g_xmpPendingBytes - g_xmpPendingOff;
			if (take > bytesWanted - got) take = bytesWanted - got;
			memcpy(dst + got, g_xmpPcmPending + g_xmpPendingOff, take);
			g_xmpPendingOff += take;
			got += take;
			continue;
		}
		if (g_xmpEof) break;
		// count は float の個数。int16 出力なので必要バイト数の 1/2。
		int wantFloats = (bytesWanted - got) / 2;
		if (wantFloats > XMP_MAX_FLOATS) wantFloats = XMP_MAX_FLOATS;
		wantFloats -= wantFloats % g_xmpCh; // チャンネル境界に揃える
		if (wantFloats <= 0) wantFloats = g_xmpCh;
		DWORD n = 0;
		try { n = g_xmpIn->Process(g_xmpFloatBuf, (DWORD)wantFloats); }
		catch (...) { n = 0; }
		if (n == 0 || n > (DWORD)wantFloats) {
			g_xmpEof = 1;
			break;
		}
		XmpFloatToS16(g_xmpFloatBuf, (int)n, g_xmpPcmPending);
		g_xmpPendingBytes = (int)n * 2;
		g_xmpPendingOff = 0;
		g_xmpPlayedFloats += (__int64)n;
	}
	return got;
}

int readxmplay(BYTE* bw, int cnt)
{
	return PluginXmplay_Read(bw, cnt);
}
