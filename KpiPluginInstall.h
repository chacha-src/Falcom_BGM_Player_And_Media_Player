#pragma once

// KPI プラグイン ZIP 取得・展開（exe 隣へ plugins\ を展開）
// URL: https://ppp.oohara.jp/download/Plugins.zip

typedef void (*KpiInstallProgressFn)(int percent, void* ctx);

// exeDir に Plugins.zip を展開（中の plugins フォルダごと）。成功で TRUE。
BOOL KpiInstall_DownloadAndExtract(LPCTSTR exeDir, KpiInstallProgressFn progress, void* ctx, CString& errOut);

// 起動時サイレント: 所持の有無に関わらずチェックし、無い／サーバが新しければ
// kbsasami.zip を取得して exeDir\Plugins\kbsasami\（x86）と x64\ を更新。
// ローカルが最新ならスキップ。失敗は無言。plug() 前に呼べば DLL ロック無し。
BOOL KpiInstall_SilentUpdateKbsasami(LPCTSTR exeDir);
