#pragma once
// WASAPI マイク(eCapture) / ループバック元再生端末(eRender) の共通選択・同期。
// 真実は savedata.mic_device / loop_device（*_cur は補助）。

class CCustomComboBox;
class CCustomPopupMenu;

enum { AUDIODEV_MAX = 32 };

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
