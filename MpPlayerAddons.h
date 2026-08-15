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
BOOL MpSsVizIsOpen();
BOOL IsMpDjPadOpen();
// アプリ終了前: 開いていれば mpDjPadwindow を残す（Destroy で落とさない）
void MpDjPadPrepareAppExit();

void OpenMpDjPadModeless(CWnd* parent);
void OpenMpAlarmDlgModeless(CWnd* parent);
void OpenMpMirrorDlgModeless(CWnd* parent);
void OpenMpRemoteDlgModeless(CWnd* parent);
void OpenMpSsVizModeless(CWnd* parent);

void MpSortPlaylistByKey(int key);
// 表示ソートは一時的。ファイルへ書く前に自然順へ戻し、読込後に再適用する。
void MpPlaylistNatOrdClear();
void MpPlaylistNatOrdOnLoaded(int n);
void MpPlaylistNatOrdNotifyAdded();
void MpPlaylistNatOrdNotifyInserted(int at, int n);
void MpPlaylistNatOrdNotifyRemovedAt(int idx); // 降順削除ループから1件ずつ
void MpPlaylistNatOrdNotifySwap(int i, int j);
void MpPlaylistRestoreNaturalOrder();
void MpPlaylistApplySavedSort();
BOOL MpPlaylistBuildNaturalIndex(int* outIdx, int n); // outIdx[nat]=visual
int MpPlaylistNaturalIndexOfVisual(int visual);

void MpMirrorWritePcm(const BYTE* pcm, int bytes);
void MpMirrorShutdown();
void MpMirrorOnFormatReady(); // UI/再生準備後: ミラーONなら初期化（オーディオスレッドから呼ばない）
void MpRemoteWritePcm(const BYTE* pcm, int bytes); // ローカルリモート AAC（接続中のみ）

void MpBpmOnTimerTick();
void MpBpmDetectFromPeaks();
BOOL MpBpmIsMeasuring();
void MpBpmNotifyPeak(float peak); // 互換用（拍推定には使わない）
void MpBpmNotifyPcm(const float* L, const float* R, int frames, int sampleRate); // 再生PCMから拍推定
void MpBpmApplyValue(int bpm); // 拍グリッドへ反映して保持
void MpOnBpmCandPick(int candIndex); // 0..2 候補を採用
void MpBpmEnsureCandList(); // 主値から 3/4・3/2 等を補完（メニュー用）

// PC音ループバックをツール側から一時確保。MIDI録り/BPM計測などが共有。
void MpPcAudioRetain();
void MpPcAudioRelease();
void MpPcAudioMarkUserOwned(); // ユーザーが明示的にPC音ONにした

void MpRemoteEnsureRunning(HWND notifyHwnd);
void MpRemoteStop();
void MpRemoteUiTick(CMediaPlayerDlg* mp); // UIスレッド: status 用 pos/lrc/index キャッシュ
void MpRemoteOpenInBrowser(); // 127.0.0.1 でリモコンページを開く（必要ならサーバ起動）

void MpMidiInSetActive(BOOL on, HWND notifyHwnd);
void MpMidiInShutdown();

void MpAlarmTick(CMediaPlayerDlg* mp);
void MpAlarmEnsureTimer(CMediaPlayerDlg* mp);

LRESULT MpAddonsOnTransportCmd(CMediaPlayerDlg* mp, WPARAM wParam, LPARAM lParam);

void MpOnBpmDetect(CMediaPlayerDlg* mp);
void MpOnVideoExtract(CMediaPlayerDlg* mp);
void MpOnVideoReplaceAudio(CMediaPlayerDlg* mp);
void MpOnGameCapturePreset(CMediaPlayerDlg* mp, UINT presetCmd);
BOOL MpMidiInIsActive();
void MpOnMidiInToggle(CMediaPlayerDlg* mp);
