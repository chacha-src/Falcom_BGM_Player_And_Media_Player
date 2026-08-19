// XMPlay input plugin host (XMPIN pull Process)
#include "stdafx.h"
#include "PluginXmplay.h"
#include "PluginKinds.h"
#include "third_party/xmplay/xmpin.h"

extern CString kpif[];
extern CString ext[][300];
extern BYTE kvar[][300];
extern BYTE kpiarch[];
extern BYTE plugkind[];
extern BOOL kpichk[];
extern int kpicnt;

static HMODULE g_xmpDll = NULL;
static XMPIN* g_xmpIn = NULL;
static int g_xmpOpen = 0;
static int g_xmpRate = 44100;
static int g_xmpCh = 2;
static int g_xmpBits = 16;
static float g_xmpLen = 0.f;
static float g_xmpFloatBuf[8192 * 8];
static BYTE g_xmpPcmPending[8192 * 8 * 2];
static int g_xmpPendingBytes = 0;
static int g_xmpPendingOff = 0;
static int g_xmpEof = 0;

static XMPFUNC_IN g_xmpFuncIn;
static XMPFUNC_MISC g_xmpFuncMisc;
static XMPFUNC_FILE g_xmpFuncFile;

static void WINAPI XmpSetLength(float length, BOOL)
{
	if (length >= 0.f) g_xmpLen = length;
}
static void WINAPI XmpSetGain(DWORD, float) {}
static BOOL WINAPI XmpUpdateTitle(const char*) { return FALSE; }
static BOOL WINAPI XmpGetLooping() { return FALSE; }

static DWORD WINAPI XmpMiscGetVersion() { return 0x03080000; }
static HWND WINAPI XmpMiscGetWindow() { return NULL; }
static void* WINAPI XmpMiscAlloc(DWORD len) { return malloc(len); }
static void* WINAPI XmpMiscReAlloc(void* mem, DWORD len) { return realloc(mem, len); }
static void WINAPI XmpMiscFree(void* mem) { free(mem); }
static BOOL WINAPI XmpMiscCheckCancel() { return FALSE; }
static DWORD WINAPI XmpMiscGetConfig(DWORD) { return 0; }
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

static XMPFILE WINAPI XmpFileOpen(const char* filename)
{
	HANDLE h = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return NULL;
	return (XMPFILE)h;
}
static XMPFILE WINAPI XmpFileOpenMemory(const void*, DWORD) { return NULL; }
static void WINAPI XmpFileClose(XMPFILE file)
{
	if (file) CloseHandle((HANDLE)file);
}
static DWORD WINAPI XmpFileGetType(XMPFILE) { return XMPFILE_TYPE_FILE; }
static DWORD WINAPI XmpFileGetSize(XMPFILE file)
{
	if (!file) return 0;
	return GetFileSize((HANDLE)file, NULL);
}
static const char* WINAPI XmpFileGetFilename(XMPFILE) { return NULL; }
static const void* WINAPI XmpFileGetMemory(XMPFILE) { return NULL; }
static DWORD WINAPI XmpFileRead(XMPFILE file, void* buf, DWORD len)
{
	if (!file || !buf) return 0;
	DWORD rd = 0;
	ReadFile((HANDLE)file, buf, len, &rd, NULL);
	return rd;
}
static BOOL WINAPI XmpFileSeek(XMPFILE file, DWORD pos)
{
	if (!file) return FALSE;
	return SetFilePointer((HANDLE)file, (LONG)pos, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
}
static DWORD WINAPI XmpFileTell(XMPFILE file)
{
	if (!file) return 0;
	return SetFilePointer((HANDLE)file, 0, NULL, FILE_CURRENT);
}
static void WINAPI XmpFileNetSetRate(XMPFILE, DWORD) {}
static BOOL WINAPI XmpFileNetIsActive(XMPFILE) { return FALSE; }
static BOOL WINAPI XmpFileNetPreBuf(XMPFILE) { return FALSE; }
static DWORD WINAPI XmpFileNetAvailable(XMPFILE) { return 0; }
static char* WINAPI XmpFileArchiveList(XMPFILE) { return NULL; }
static XMPFILE WINAPI XmpFileArchiveExtract(XMPFILE, const char*, DWORD) { return NULL; }

static void* WINAPI XmpFaceProc(DWORD face)
{
	if (face == XMPFUNC_IN_FACE) return &g_xmpFuncIn;
	if (face == XMPFUNC_MISC_FACE) return &g_xmpFuncMisc;
	if (face == XMPFUNC_FILE_FACE) return &g_xmpFuncFile;
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

static void XmpParseExts(const char* exts)
{
	ext[kpicnt][0] = L"";
	if (!exts) return;
	// "description\0ext1/ext2/etc"
	const char* p = exts;
	if (*p) p += strlen(p) + 1;
	if (!*p) return;
	CStringA listA(p);
	int ei = 0;
	int start = 0;
	for (;;) {
		int slash = listA.Find('/', start);
		CStringA tok = (slash < 0) ? listA.Mid(start) : listA.Mid(start, slash - start);
		if (!tok.IsEmpty() && ei < 298) {
			CString e(tok);
			e.MakeLower();
			if (e[0] != L'.') e = L"." + e;
			ext[kpicnt][ei] = e;
			kvar[kpicnt][ei] = 0;
			ei++;
		}
		if (slash < 0) break;
		start = slash + 1;
	}
	ext[kpicnt][ei] = L"";
	ext[kpicnt][299] = L"";
}

typedef XMPIN* (WINAPI* pfn_XMPIN_GetInterface)(UINT32 face, InterfaceProc faceproc);

int PluginXmplay_TryEnum(const wchar_t* dllPath, int is64)
{
	if (!dllPath || !dllPath[0] || kpicnt >= 149) return 0;
	if (is64) {
		plugkind[kpicnt] = PLUGKIND_XMPLAY;
		kpiarch[kpicnt] = 64;
		kpif[kpicnt] = dllPath;
		ext[kpicnt][0] = L"";
		ext[kpicnt][299] = L"";
		kvar[kpicnt][0] = 0;
		kpicnt++;
		return 1;
	}
	XmpInitHostTables();
	HMODULE h = LoadLibraryExW(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!h) return 0;
	pfn_XMPIN_GetInterface getIf = (pfn_XMPIN_GetInterface)GetProcAddress(h, "XMPIN_GetInterface");
	if (!getIf) {
		FreeLibrary(h);
		return 0;
	}
	XMPIN* in = getIf(XMPIN_FACE, XmpFaceProc);
	if (!in) {
		FreeLibrary(h);
		return 0;
	}
	plugkind[kpicnt] = PLUGKIND_XMPLAY;
	kpiarch[kpicnt] = 32;
	kpif[kpicnt] = dllPath;
	XmpParseExts(in->exts);
	FreeLibrary(h);
	kpicnt++;
	return 1;
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
	WideCharToMultiByte(CP_UTF8, 0, mediaPath, -1, pathA, (int)sizeof(pathA), NULL, NULL);
	g_xmpDll = h;
	g_xmpIn = in;
	g_xmpLen = 0.f;
	g_xmpPendingBytes = 0;
	g_xmpPendingOff = 0;
	g_xmpEof = 0;
	DWORD orc = in->Open(pathA, NULL);
	if (!orc) {
		PluginXmplay_Close();
		return 0;
	}
	XMPFORMAT form = {};
	form.rate = 44100;
	form.chan = 2;
	form.res = 2;
	if (in->SetFormat) in->SetFormat(&form);
	g_xmpRate = form.rate > 0 ? (int)form.rate : 44100;
	g_xmpCh = form.chan > 0 ? (int)form.chan : 2;
	g_xmpBits = 16;
	g_xmpOpen = 1;
	return 1;
}

void PluginXmplay_Close()
{
	if (g_xmpIn && g_xmpIn->Close) g_xmpIn->Close();
	g_xmpIn = NULL;
	if (g_xmpDll) {
		FreeLibrary(g_xmpDll);
		g_xmpDll = NULL;
	}
	g_xmpOpen = 0;
	g_xmpPendingBytes = 0;
	g_xmpPendingOff = 0;
	g_xmpEof = 0;
}

int PluginXmplay_SeekSec(double sec)
{
	if (!g_xmpIn || !g_xmpOpen || !g_xmpIn->SetPosition) return 0;
	g_xmpIn->SetPosition((DWORD)(sec * 1000.0));
	g_xmpPendingBytes = 0;
	g_xmpPendingOff = 0;
	g_xmpEof = 0;
	return 1;
}

int PluginXmplay_IsOpen() { return g_xmpOpen; }
int PluginXmplay_SampleRate() { return g_xmpRate; }
int PluginXmplay_Channels() { return g_xmpCh; }
int PluginXmplay_Bits() { return g_xmpBits; }
double PluginXmplay_LengthSec() { return (double)g_xmpLen; }

static void XmpFloatToS16(const float* src, int samples, BYTE* dst)
{
	for (int i = 0; i < samples; ++i) {
		float f = src[i];
		if (f > 1.f) f = 1.f;
		if (f < -1.f) f = -1.f;
		short s = (short)(f * 32767.f);
		dst[i * 2] = (BYTE)(s & 0xff);
		dst[i * 2 + 1] = (BYTE)((s >> 8) & 0xff);
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
		const int maxFrames = 4096;
		DWORD gotFrames = g_xmpIn->Process(g_xmpFloatBuf, maxFrames);
		if (gotFrames == 0) {
			g_xmpEof = 1;
			break;
		}
		int samples = (int)gotFrames * g_xmpCh;
		if (samples > (int)(sizeof(g_xmpPcmPending) / 2))
			samples = (int)(sizeof(g_xmpPcmPending) / 2);
		XmpFloatToS16(g_xmpFloatBuf, samples, g_xmpPcmPending);
		g_xmpPendingBytes = samples * 2;
		g_xmpPendingOff = 0;
	}
	return got;
}

int readxmplay(BYTE* bw, int cnt)
{
	return PluginXmplay_Read(bw, cnt);
}
