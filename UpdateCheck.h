#pragma once

// 更新チェック用
#define WM_APP_UPDATE_AVAILABLE  (WM_APP + 99)

// 更新チェックスレッドを開始（OnInitDialog等から呼ぶ）
void StartUpdateCheckThread(HWND hNotifyWnd);

// 更新を実行（ダウンロード→展開→再起動）、成功時はプロセス終了
// 戻り値: true=更新実行して終了, false=失敗またはキャンセル
bool DoUpdateAndRestart();
