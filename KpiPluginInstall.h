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

// 起動時サイレント: Plugins.zip（KPI / ym2608 rom・リズム WAV）＋公式 DLL。
// WinFMP=c60、PDZFZ8XWin=aosoft。PMDWin は c60 ではなく Plugins.zip（fmmon 付き）。
// リズム資産は欠落時のみ Plugins.zip 同梱分を配置（fmpmd / kbsasami）。
// Plugins\Kobarin\fmpmd を自動作成。旧 fmpmd.kpi は無効化。
// KPI 読み込みダイアログ表示中に呼ぶ（plug 前）。
BOOL KpiInstall_SilentUpdateFmpmd(LPCTSTR exeDir);

// FM/MIDI モニタ対応 KPI（fmmidi/s98/vgm/msx/gme）:
// Plugins に既にあるものだけ、同梱バンドル (.ogg_kpi_fmmon) から上書き更新。
// 無い場合は新規インストールしない。
BOOL KpiInstall_SilentUpdateFmMonKpis(LPCTSTR exeDir);
