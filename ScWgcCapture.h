#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Windows.Graphics.Capture で HWND を top-down BGRA に取得する。
// 他窓やキャプチャUIが前面でも対象ウィンドウ本体を取れる（OBS と同系統）。
// dstW/dstH は偶数推奨。失敗時は FALSE（呼び出し側で GDI フォールバック）。
BOOL ScWgcCaptureWindowBgra(HWND hwnd, BYTE* dstBgra, int dstW, int dstH, int dstStride);

// 全セッション解放（ダイアログ終了時など）
void ScWgcShutdown(void);

#ifdef __cplusplus
}
#endif
