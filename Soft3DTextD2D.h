#pragma once
// Soft3D 共有: GDI+ DrawString の代わりに Direct2D / DirectWrite で BGRA へ焼く
// （レース／迷路の HUD・トーストが timerp 上で UI を塞がないように）

#include <windows.h>

struct Soft3DTextD2DCanvas;

BOOL Soft3DTextD2D_Ensure();
void Soft3DTextD2D_Shutdown();

// Begin: 透明クリアした w×h BGRA キャンバス。End まで描画。
Soft3DTextD2DCanvas* Soft3DTextD2D_Begin(int w, int h);
void Soft3DTextD2D_FillRect(Soft3DTextD2DCanvas* c, float x, float y, float w, float h, BYTE a, BYTE r, BYTE g, BYTE b);
void Soft3DTextD2D_FillEllipse(Soft3DTextD2DCanvas* c, float cx, float cy, float rw, float rh, BYTE a, BYTE r, BYTE g, BYTE b);
void Soft3DTextD2D_FillTriangle(Soft3DTextD2DCanvas* c, float x0, float y0, float x1, float y1, float x2, float y2, BYTE a, BYTE r, BYTE g, BYTE b);
// align: 0=near 1=center 2=far（水平）。vCenter=TRUE で垂直中央。
// wrap: 0=折り返しなし（既定） 1=枠内で折り返し（改行・単語／CJK 境界。収まらなければ非常折り）。
void Soft3DTextD2D_DrawText(Soft3DTextD2DCanvas* c, const wchar_t* text, float x, float y, float w, float h,
	float fontPx, int bold, int align, int vCenter, BYTE a, BYTE r, BYTE g, BYTE b, int wrap = 0);
void Soft3DTextD2D_DrawTextShadow(Soft3DTextD2DCanvas* c, const wchar_t* text, float x, float y, float w, float h,
	float fontPx, int bold, int align, int vCenter,
	BYTE fa, BYTE fr, BYTE fg, BYTE fb, float shadowDx, float shadowDy, BYTE sa, BYTE sr, BYTE sg, BYTE sb, int wrap = 0);
// End: bits は top-down BGRA、stride=w*4。呼び出し側がコピー後に End 必須。
BOOL Soft3DTextD2D_End(Soft3DTextD2DCanvas* c, const BYTE** outBits, UINT* outStride);
void Soft3DTextD2D_Release(Soft3DTextD2DCanvas* c);
