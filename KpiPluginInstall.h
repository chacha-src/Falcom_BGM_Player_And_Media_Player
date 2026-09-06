#pragma once

// KPI プラグイン ZIP 取得・展開（exe 隣へ plugins\ を展開）
// URL: https://ppp.oohara.jp/download/Plugins.zip

typedef void (*KpiInstallProgressFn)(int percent, void* ctx);

// exeDir に Plugins.zip を展開（中の plugins フォルダごと）。成功で TRUE。
BOOL KpiInstall_DownloadAndExtract(LPCTSTR exeDir, KpiInstallProgressFn progress, void* ctx, CString& errOut);

// 起動時サイレント: Plugins.zip から kbsasami 等を展開（専用 kbsasami.zip は使わない）。
// DL は Plugins 無し、または ZIP が exe より新しいときのみ（Fmpmd と共有・1回だけ）。
// 失敗は無言。plug() 前に呼べば DLL ロック無し。
BOOL KpiInstall_SilentUpdateKbsasami(LPCTSTR exeDir);

// 起動時サイレント: Plugins.zip（KPI / リズム / 依存 DLL・BIN）＋必要なら公式 DLL。
// DL 条件は Kbsasami と同じ（Plugins 無し or ZIP>exe）。展開は日付マージ。
// WinFMP=c60、PDZFZ8XWin=aosoft。PMDWin は Plugins.zip（fmmon 付き）。
// Plugins\Kobarin\fmpmd を自動作成。旧 fmpmd.kpi は無効化。
// KPI 読み込みダイアログ表示中に呼ぶ（plug 前）。
BOOL KpiInstall_SilentUpdateFmpmd(LPCTSTR exeDir);

// FM/MIDI モニタ dump 対応 KPI（所持分のみ .ogg_kpi_fmmon から上書き）:
//   既存: fmmidi / s98 / vgm / msx / gme
//   keys-only 等: kbmdx / kbfmoplmidi / kbsc68
//   NSF→SPC→PSF系: kbnsfplug / kbsnesapu / kbpsf / kbpsf2(+viopsf2/kbzlib)
//   keys-only: kbsid / kbgsf(+viogsf) / kbncsf
//   対応中の枠: kbum / kbmod / kbpxtone / kb2sf|kbvio2sf / kbqsf /
//               kbusf / kbssf / kbdsf / kbsap / kbwsr / kbnezplug / kbgym
// Plugins および x64\Plugins。バンドルは直下＝従来(x64 多め)、
// x86 専用は Plugins\.ogg_kpi_fmmon\x86\、x64 専用は \x64\ を優先。
// 無いものは新規インストールしない。付属は同フォルダの .kpi があるときだけ。
BOOL KpiInstall_SilentUpdateFmMonKpis(LPCTSTR exeDir);

// KPI/DLL がモニタ dump 改造版か（同名ストックと区別）。実ファイルを見て判定。
// outFmMon=[FMmon] レジスタ系 / outMidMon=[MIDmon] keys-only(MIDIノート)系。
void KpiPlugin_ProbeMonitorCaps(LPCTSTR path, BOOL* outFmMon, BOOL* outMidMon);
