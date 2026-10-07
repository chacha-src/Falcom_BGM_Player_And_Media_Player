#pragma once
/* kbsasami_host が本体の CMidiMonitorDlg / CFmMonitorDlg をコンパイルするときの差。
   描画は同じ cpp。再生位置・パス・プレイヤー窓だけここ。 */
#include "CCustomControl.h"

class CFmMonitorDlg;
class CPianoRoll;

class CPianoRollStub : public CWnd {
public:
	int IsPcAudioScoring() const { return 0; }
	void CopyActiveKeyLevels(BYTE*) const {}
};

class COggDlg : public CWnd {
public:
	CFmMonitorDlg* m_FmMonitorDlg = nullptr;
	CCustomSliderCtrl m_dsval;
	CPianoRollStub* m_PianoRollDlg = nullptr;
	int MidiMonitorIsVisible() const;
};

class CPlayList {
public:
	struct Row { wchar_t fol[1024]; wchar_t name[1024]; };
	Row* pc;
	int playcnt;
};

class CMediaPlayerDlg : public CWnd {
public:
	void SyncPushToggleButtons() {}
};
extern CMediaPlayerDlg* mp;

#ifndef WM_OGG_TOGGLE_SUBUI
#define WM_OGG_TOGGLE_SUBUI (WM_APP + 102)
#endif

int PlMidDiskGet(LPCTSTR fol, int* ch32, int* mapKind, int* sysMode, int* mapForce);
void PlMidForceSet(LPCTSTR fol, int mapForce);
void PlMidNotifyMarkViews();
__int64 OggGetCemuLiveHeardFrames();
void OggPersistSaveDatNow();
BOOL OggPrepareResumeBeforePlayback(LPCTSTR mediaPath);
void OpenVstHostModeless(CWnd* parent);
void KpiV5SyncKbsasamiOptions(int midPlayPrefer);

extern COggDlg* og;
extern CPlayList* pl;
extern CString filen;
extern CString fnn;
extern int mode;
extern int tempo;
extern int pitch;
extern int playy;
extern int wavbit_sample_Hz;
extern int plf;
extern int playf;
extern int ps;
extern int plcnt;
extern int g_openDecoderMode;
extern __int64 playb;

class CVstHostDlg : public CWnd {
public:
	void PartPluginName(int, wchar_t* out, int n) const
	{
		if (out && n > 0) out[0] = 0;
	}
};
extern CVstHostDlg* g_vstHostDlg;

double OggGetGdiPlaybackTimeSec();
__int64 OggGetHeardPcmFrames();
void RequestPlaybackRestart(HWND hwnd = NULL);

LPCTSTR DatArc_StageDir();
void DatArc_InvalidateLeaf(LPCTSTR leaf);
BOOL DatArc_Commit(LPCTSTR leaf);
CString DatArc_Path(LPCTSTR leaf);

void PcHwMidiInRestoreFromSave();
void PcHwMidiInAppendToMenu(class CCustomPopupMenu* parent);
int PcHwMidiInHandleCmd(UINT cmd, CWnd* owner);
int PcHwMidiInStealShorts(BYTE* ports, DWORD* msgs, int maxN);
int PcHwMidiInStealSysex(int* port, BYTE* dst, int dstMax);

void KbsHostMonStartup();
void KbsHostMonShow();
void KbsHostMonHide();
void KbsHostMonQuit();
void KbsHostMonRequestQuit();
int KbsHostMonHolding();
void KbsHostMonPull();
/* 1=FM(.fpy)。0=MIDI。kpi が曲を開いたときの指定。 */
int KbsHostMonIsFm();
