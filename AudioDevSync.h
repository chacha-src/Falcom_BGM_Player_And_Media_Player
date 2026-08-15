#pragma once
// WASAPI マイク(eCapture) / ループバック元再生端末(eRender) の共通選択・同期。
// 真実は savedata.mic_device / loop_device（*_cur は補助）。
// 端末の挿抜は IMMNotificationClient → WM_AUDIODEV_CHANGED で UI スレッドへ。

class CCustomComboBox;
class CCustomPopupMenu;

enum { AUDIODEV_MAX = 32 };

#ifndef WM_AUDIODEV_CHANGED
#define WM_AUDIODEV_CHANGED (WM_APP + 731)
#endif

// --- マイク ---
void AudioMicDevRefresh();
int AudioMicDevCount();
LPCTSTR AudioMicDevId(int i);
CString AudioMicDevName(int i);
int AudioMicDevCurSel(); // savedata から
void AudioMicDevFillCombo(CCustomComboBox& cb);
void AudioMicDevApplySel(int sel); // savedata 更新 + キャプチャ再起動 + 全UI同期
void AudioMicDevApplyFromCombo(CCustomComboBox& cb);
void AudioMicDevSyncComboSel(CCustomComboBox& cb); // 選択だけ合わせる（再列挙なし）
void AudioMicDevSyncAllUi();
void AudioMicDevRegisterCombo(CCustomComboBox* cb);
void AudioMicDevUnregisterCombo(CCustomComboBox* cb);
void AudioMicDevAppendMenu(CCustomPopupMenu& menu); // 「マイク端末」サブメニュー
BOOL AudioMicDevHandleMenuCmd(UINT cmd);

// --- ループバック（システム音の再生端末）---
void AudioLoopDevRefresh();
int AudioLoopDevCount();
LPCTSTR AudioLoopDevId(int i);
CString AudioLoopDevName(int i);
int AudioLoopDevCurSel();
void AudioLoopDevFillCombo(CCustomComboBox& cb);
void AudioLoopDevApplySel(int sel);
void AudioLoopDevApplyFromCombo(CCustomComboBox& cb);
void AudioLoopDevSyncComboSel(CCustomComboBox& cb);
void AudioLoopDevSyncAllUi();
void AudioLoopDevRegisterCombo(CCustomComboBox* cb);
void AudioLoopDevUnregisterCombo(CCustomComboBox* cb);
void AudioLoopDevAppendMenu(CCustomPopupMenu& menu);
BOOL AudioLoopDevHandleMenuCmd(UINT cmd);

// --- 挿抜監視 / 再構築 ---
// hwndUi: WM_AUDIODEV_CHANGED を受ける UI スレッド窓（通常は COggDlg）
void AudioDevWatchEnsure(HWND hwndUi);
void AudioDevWatchShutdown();
// 再列挙して登録済みコンボを全部作り直し。独自 Fill の窓へも WM_AUDIODEV_CHANGED を配送。
void AudioDevRebuildAll();
void AudioDevRegisterNotifyHwnd(HWND h);
void AudioDevUnregisterNotifyHwnd(HWND h);
CString AudioDevRescanButtonLabel();
void AudioDevApplyRescanButton(CWnd* btn);
