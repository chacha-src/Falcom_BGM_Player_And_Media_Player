#pragma once

#include "resource.h"

class CMediaPlayerDlg;

#ifndef WM_MP_TRANSPORT_CMD
#define WM_MP_TRANSPORT_CMD (WM_APP + 67)
#endif

void MpAddonsShutdownAll();
void CloseMpDjPadIfOpen();
void CloseMpAlarmDlgIfOpen();
void CloseMpMirrorDlgIfOpen();
void CloseMpRemoteDlgIfOpen();
void CloseMpSsVizIfOpen();

void OpenMpDjPadModeless(CWnd* parent);
void OpenMpAlarmDlgModeless(CWnd* parent);
void OpenMpMirrorDlgModeless(CWnd* parent);
void OpenMpRemoteDlgModeless(CWnd* parent);
void OpenMpSsVizModeless(CWnd* parent);

void MpMirrorWritePcm(const BYTE* pcm, int bytes);
void MpMirrorShutdown();

void MpBpmOnTimerTick();
void MpBpmDetectFromPeaks();
BOOL MpBpmIsMeasuring();

// PC音ループバックをツール側から一時確保。MIDI録り/BPM計測などが共有。
void MpPcAudioRetain();
void MpPcAudioRelease();
void MpPcAudioMarkUserOwned(); // ユーザーが明示的にPC音ONにした

void MpRemoteEnsureRunning(HWND notifyHwnd);
void MpRemoteStop();

void MpMidiInSetActive(BOOL on, HWND notifyHwnd);
void MpMidiInShutdown();

void MpAlarmTick(CMediaPlayerDlg* mp);
void MpAlarmEnsureTimer(CMediaPlayerDlg* mp);

LRESULT MpAddonsOnTransportCmd(CMediaPlayerDlg* mp, WPARAM wParam, LPARAM lParam);

void MpOnBpmDetect(CMediaPlayerDlg* mp);
void MpOnVideoExtract(CMediaPlayerDlg* mp);
void MpOnVideoReplaceAudio(CMediaPlayerDlg* mp);
void MpOnGameCapturePreset(CMediaPlayerDlg* mp);
BOOL MpMidiInIsActive();
void MpOnMidiInToggle(CMediaPlayerDlg* mp);
