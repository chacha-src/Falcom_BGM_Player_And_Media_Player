#pragma once

// KPI プラグイン ZIP 取得・展開（exe 隣へ plugins\ を展開）
// URL: https://ppp.oohara.jp/download/Plugins.zip

typedef void (*KpiInstallProgressFn)(int percent, void* ctx);

// exeDir に Plugins.zip を展開（中の plugins フォルダごと）。成功で TRUE。
BOOL KpiInstall_DownloadAndExtract(LPCTSTR exeDir, KpiInstallProgressFn progress, void* ctx, CString& errOut);

// 起動時サイレント: kbsasami.zip の Last-Modified とローカル比較。
// 無いか新しければ exeDir\Plugins\kbsasami\（x86）と x64\ を更新。失敗は無言。
// API 互換のまま差し替えるだけなので、plug() 前に呼べば再生への影響はない。
BOOL KpiInstall_SilentUpdateKbsasami(LPCTSTR exeDir);
