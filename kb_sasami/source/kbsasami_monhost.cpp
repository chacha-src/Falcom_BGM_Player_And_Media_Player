/* raira=0 かつ kbsasami.fmmidimonitor=1 の FM/MIDI モニタ。本体と同じ CMidiMonitorDlg をこのホストで出す。
   オプション自体はプラグインが読む。ここは MON_SHOW が来たときだけ窓を出す。
   VstMidiEngine はこのプロジェクトに既にある。 */
#include "stdafx.h"
#include "resource.h"
#include "kbsasami_monhost.h"
#include <limits.h>
#include "kbsasami_monshm.h"
#include "CMidiMonitorDlg.h"
#include "CFmMonitorDlg.h"
#include "CCustomPopupMenu.h"
#include "VstMidiEngine.h"

COggDlg* og = nullptr;
CPlayList* pl = nullptr;
CString filen;
CString fnn;
int mode = -3;
int tempo = 0;
int pitch = 0;
int playy = 1;
int wavbit_sample_Hz = 44100;
int plf = 0;
int playf = 0;
int ps = 0;
int plcnt = 0;
int g_openDecoderMode = INT_MIN;
__int64 playb = 0;
CVstHostDlg* g_vstHostDlg = nullptr;
CMediaPlayerDlg* mp = nullptr;

int PlMidDiskGet(LPCTSTR, int*, int*, int*, int* force)
{
	if (force) *force = 0;
	return -1;
}
void PlMidForceSet(LPCTSTR, int) {}
void PlMidNotifyMarkViews() {}
__int64 OggGetCemuLiveHeardFrames() { return 0; }
void OggPersistSaveDatNow() {}
BOOL OggPrepareResumeBeforePlayback(LPCTSTR) { return FALSE; }
void OpenVstHostModeless(CWnd*) {}
void KpiV5SyncKbsasamiOptions(int) {}

class CImageBase;
CImageBase* playbase = nullptr;
CImageBase* renderbase = nullptr;
CImageBase* folderbase = nullptr;
int g_oggSubUiRestoring = 0;
volatile LONG g_appExiting = 0;

CString SongParams_BuildTipExtraForRow(int) { return CString(); }

CWnd* CCC_GetActiveMainWindow()
{
	if (og && ::IsWindow(og->GetSafeHwnd())) return og;
	return nullptr;
}
void CCC_NotifyAeroSettingChanged() {}
void OfflineHelpOpen(HWND, int) {}
void CEmuCatalogGetExeArcdataPath(wchar_t* out, int n)
{
	if (out && n > 0) out[0] = 0;
}
void MpPersistSavedataQuick() {}
void PlRefreshMidiPlayModes() {}
void CEmuRequestMidiEngineReplay() {}
void COgg_DropPlaybackUiPostedMsg(UINT) {}
void COgg_KickTimerp() {}

void MmBindVstActiveSlot()
{
	VstMidiSetIoSlot(0);
}

void FmMonGeomPersistOpen(int open)
{
	struct { char magic[4]; int version; int isOpen; int x, y, w, h; } g = {};
	g.magic[0] = 'F'; g.magic[1] = 'M'; g.magic[2] = 'M'; g.magic[3] = 'G';
	g.version = 1;
	g.isOpen = open ? 1 : 0;
	g.x = savedata.midimonx;
	g.y = savedata.midimony;
	g.w = savedata.midimonw;
	g.h = savedata.midimonh;
	if (g.w < 200 || g.h < 160) {
		g.x = savedata.fmmonx;
		g.y = savedata.fmmony;
		g.w = savedata.fmmonw;
		g.h = savedata.fmmonh;
	}
	wchar_t path[MAX_PATH] = {};
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	_snwprintf_s(path, _TRUNCATE, L"%sogg_kbsasami\\fmmon_geom.dat", tmp);
	wchar_t dir[MAX_PATH];
	wcsncpy_s(dir, path, _TRUNCATE);
	wchar_t* sl = wcsrchr(dir, L'\\');
	if (sl) { *sl = 0; CreateDirectoryW(dir, NULL); }
	HANDLE hf = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hf == INVALID_HANDLE_VALUE) return;
	DWORD wr = 0;
	WriteFile(hf, &g, sizeof(g), &wr, NULL);
	CloseHandle(hf);
}

static COggDlg g_og;
static CMidiMonitorDlg* g_mm = nullptr;
static CFmMonitorDlg* g_fm = nullptr;
static HANDLE g_map = NULL;
static KbsMonShm* g_shm = nullptr;
static int g_monFm = 0;
static int g_hold = 0;
static wchar_t g_path[520];

class CKbsHostApp : public CWinApp {
public:
	BOOL InitInstance() override { return TRUE; }
};
CKbsHostApp g_kbsHostApp;

int COggDlg::MidiMonitorIsVisible() const
{
	return (g_mm && ::IsWindow(g_mm->GetSafeHwnd()) && g_mm->IsWindowVisible()) ? 1 : 0;
}

double OggGetGdiPlaybackTimeSec()
{
	const int sr = wavbit_sample_Hz > 0 ? wavbit_sample_Hz : 44100;
	if (playb < 0) return 0;
	return (double)playb / (double)sr;
}

__int64 OggGetHeardPcmFrames()
{
	return playb > 0 ? playb : 0;
}

void RequestPlaybackRestart(HWND) {}

LPCTSTR DatArc_StageDir() { return NULL; }
void DatArc_InvalidateLeaf(LPCTSTR) {}
BOOL DatArc_Commit(LPCTSTR) { return FALSE; }
CString DatArc_Path(LPCTSTR) { return CString(); }

void PcHwMidiInRestoreFromSave() {}
void PcHwMidiInAppendToMenu(CCustomPopupMenu*) {}
int PcHwMidiInHandleCmd(UINT, CWnd*) { return 0; }
int PcHwMidiInStealShorts(BYTE*, DWORD*, int) { return 0; }
int PcHwMidiInStealSysex(int*, BYTE*, int) { return 0; }
int CEmuMidiLiveActive(void) { return 0; }

static void OpenShm()
{
	if (g_shm) return;
	g_map = OpenFileMappingW(FILE_MAP_READ, FALSE, KBSMON_MAP_NAME);
	if (!g_map) return;
	g_shm = (KbsMonShm*)MapViewOfFile(g_map, FILE_MAP_READ, 0, 0, sizeof(KbsMonShm));
	if (!g_shm) {
		CloseHandle(g_map);
		g_map = NULL;
	}
}

static __int64 s_lastHeard = 0;
static int s_haveHeard = 0;

void KbsHostMonPull()
{
	OpenShm();
	if (!g_shm || g_shm->magic != KBSMON_MAGIC) return;
	KbsMonShm c{};
	for (int i = 0; i < 8; ++i) {
		const long a = g_shm->seq;
		if (a & 1) continue;
		MemoryBarrier();
		c = *g_shm;
		MemoryBarrier();
		if (g_shm->seq == a) break;
	}
	if (c.magic != KBSMON_MAGIC) return;
	if (c.path[0] && wcscmp(g_path, c.path) != 0) {
		wcsncpy_s(g_path, c.path, _TRUNCATE);
		filen = c.path;
		s_haveHeard = 0;
	}
	if (!c.show || !c.playf) {
		playf = 0;
		playy = 0;
		return;
	}
	playf = 1;
	playy = 1;
	/* playSample はデコード終端。スピーカは lag だけ後ろで、Render が空いても壁時計で進む。 */
	__int64 heard = c.playSample - c.lagSamples;
	if (heard < 0) heard = 0;
	if (c.qpcFreq > 0 && c.qpc > 0 && c.sampleRate > 0) {
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		__int64 dt = now.QuadPart - c.qpc;
		if (dt < 0) dt = 0;
		heard += dt * (__int64)c.sampleRate / c.qpcFreq;
	}
	if (heard > c.playSample) heard = c.playSample;
	const int sr = (c.sampleRate >= 8000) ? c.sampleRate : 44100;
	if (s_haveHeard && heard < s_lastHeard) {
		const __int64 back = s_lastHeard - heard;
		/* 200ms 級の lag 再計算で針が戻ると、前の音と次の音が交互になる。
		   曲頭シーク／ループだけ通す。 */
		if (back <= (__int64)sr / 2)
			heard = s_lastHeard;
	}
	s_lastHeard = heard;
	s_haveHeard = 1;
	playb = heard;
	if (c.sampleRate >= 8000) {
		wavbit_sample_Hz = c.sampleRate;
		savedata.samples = (DWORD)c.sampleRate;
	}
	g_monFm = c.fm ? 1 : 0;
	mode = -3;
}

int KbsHostMonIsFm()
{
	return g_monFm;
}

void KbsHostMonStartup()
{
	AfxWinInit(GetModuleHandleW(NULL), NULL, GetCommandLineW(), SW_HIDE);
	savedata.midimonx = -1;
	savedata.midimony = -1;
	savedata.fmmonx = -1;
	savedata.fmmony = -1;
	g_openDecoderMode = INT_MIN;
}

void KbsHostMonShow()
{
	g_hold = 1;
	KbsHostMonPull();
	og = &g_og;
	if (!g_fm)
		g_fm = new CFmMonitorDlg();
	g_og.m_FmMonitorDlg = g_fm;
	if (!g_mm)
		g_mm = new CMidiMonitorDlg();
	if (!::IsWindow(g_mm->GetSafeHwnd())) {
		if (!g_mm->Create(IDD_MIDIMONITOR, NULL))
			return;
	}
	g_mm->ShowWindow(SW_SHOWNOACTIVATE);
}

void KbsHostMonHide()
{
	g_hold = 0;
	if (g_mm && ::IsWindow(g_mm->GetSafeHwnd()))
		g_mm->ShowWindow(SW_HIDE);
}

int KbsHostMonHolding()
{
	if (InterlockedCompareExchange(&g_appExiting, 0, 0))
		return 0;
	return g_hold;
}

void KbsHostMonRequestQuit()
{
	InterlockedExchange(&g_appExiting, 1);
	g_hold = 0;
}

void KbsHostMonQuit()
{
	KbsHostMonRequestQuit();
	UiTickPump::ShutdownHub();
	g_hold = 0;
	if (g_fm)
		g_fm->DetachForDestroy();
	if (g_mm)
		g_mm->DetachForDestroy();
	if (g_mm) {
		if (::IsWindow(g_mm->GetSafeHwnd()))
			g_mm->DestroyWindow();
		delete g_mm;
		g_mm = NULL;
	}
	if (g_fm) {
		if (::IsWindow(g_fm->GetSafeHwnd()))
			g_fm->DestroyWindow();
		delete g_fm;
		g_fm = NULL;
	}
	g_og.m_FmMonitorDlg = NULL;
}
