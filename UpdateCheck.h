#pragma once

// 更新チェック用
#define WM_APP_UPDATE_AVAILABLE  (WM_APP + 99)
#define WM_APP_UPDATE_PEEK       (WM_APP + 100)
#define WM_APP_UPDATE_PREFS      (WM_APP + 101)

// メイン画面を作る前に一度だけ同期確認し、更新があれば更新・再起動する
void RunStartupUpdateCheck();

/* exe 隣に d3dcompiler_47.dll が無いとき更新 ZIP からサイレント取得→配置→再起動（成功時は exit） */
void EnsureD3dCompilerAvailable();

// メイン画面表示後の定期更新チェックスレッドを開始
void StartUpdateCheckThread(HWND hNotifyWnd);

// 更新を実行（ダウンロード→展開→再起動）、成功時はプロセス終了
// 戻り値: true=更新実行して終了, false=失敗またはキャンセル
bool DoUpdateAndRestart();

// 「いいえ」で更新を見送ったサーバー版タイムスタンプ（再起動まで再通知しない）
void UpdateCheckDismissVersion(__int64 serverModified);
void UpdateCheckBeginPrompt();
void UpdateCheckEndPrompt(bool dismissedNo, __int64 serverModified);

// 配布 ZIP が実行中 exe より新しいか（ダイアログは出さない）
bool UpdateCheckIsNewerAvailable(__int64* outServerModified);

// 更新確認ダイアログ（はい/いいえ＋次回から尋ねない）。戻り値 IDYES / IDNO
int UpdateCheckAskUser(CWnd* parent, __int64 serverModified);
