#pragma once

// オフラインヘルプ (oggYSEDbgm_uni_avx2.chm)
// F1 / キャプション「本」ボタン / 不足時の配置確保

#define OFFLINE_HELP_CHM_NAME _T("oggYSEDbgm_uni_avx2.chm")

// exe と同じフォルダの CHM フルパスを返す（存在しなくてもパスは組み立てる）
CString OfflineHelpGetChmPath();

// CHM が無ければ %TEMP% 展開済みからコピー、それでも無ければ ZIP を裏で取得して配置
// 既に取得中なら何もしない。UI スレッドから呼んでよい（裏スレッド起動のみ）
void OfflineHelpEnsureAvailable();

// CHM を開く。無ければ Ensure を起動して案内し、あれば HtmlHelp / ShellExecute
// preferredEn: TRUE=英語トピック優先, FALSE=日本語, -1=OS UI 言語で自動
void OfflineHelpOpen(HWND hwndOwner, int preferredEn = -1);
