#pragma once
/* アプリ起動時に作る D3D11。FM/MIDI モニタ専用。迷路/レースは触らない。 */
#ifndef GPU_DX11_H
#define GPU_DX11_H

#include <Windows.h>
#include <stdint.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

enum {
	GPU_ATLAS_W = 512,
	GPU_ATLAS_H = 256,
	GPU_ATLAS_CELL = 32,
	GPU_ATLAS_COLS = 16,
	GPU_MAX_RECT = 4096,
	GPU_MAX_PIANO_ROW = 64,
	GPU_HEX_CELLS = 256
};

struct GpuMonSurf {
	HWND child;
	HWND parent;
	unsigned w, h;
	int ready;
	int gdiLock;
	void* sc;      /* IDXGISwapChain1* */
	void* bb;      /* ID3D11Texture2D* */
	void* rtv;     /* ID3D11RenderTargetView* */
	void* rectSB;
	void* rectSrv;
	void* pianoSB;
	void* pianoSrv;
	void* hexDump;
	void* hexDumpSrv;
	void* hexOut;
	void* hexOutUav;
	void* hexOutSrv;
	int rectN;
	int pianoN;
	int capH;
	uint8_t reserve[64];
};

int GpuDx11_Startup(void);
void GpuDx11_Shutdown(void);
int GpuDx11_Ready(void);
ID3D11Device* GpuDx11_Device(void);
ID3D11DeviceContext* GpuDx11_Context(void);

/* set="gpu" のみ。迷路/レースは使わない */
HRESULT GpuTryLoadCso(const wchar_t* set, const char* entry, const char* profile,
	const char* hlslFallback, SIZE_T hlslBytes, void** outBlob);

int GpuMonSurf_Ensure(GpuMonSurf* s, HWND parent, int x, int y, unsigned w, unsigned h);
void GpuMonSurf_Release(GpuMonSurf* s);
int GpuMonSurf_Begin(GpuMonSurf* s, COLORREF bg);
void GpuMonSurf_AddRect(GpuMonSurf* s, float x, float y, float w, float h,
	COLORREF c, float glow);
void GpuMonSurf_AddGlyph(GpuMonSurf* s, float x, float y, float w, float h,
	unsigned glyph, COLORREF c);
void GpuMonSurf_AddPianoRow(GpuMonSurf* s, const RECT* rc, const uint32_t litBits[4],
	COLORREF w, COLORREF b, COLORREF lw, COLORREF lb);
int GpuMonSurf_FlushRects(GpuMonSurf* s);
int GpuMonSurf_FlushPianos(GpuMonSurf* s);
int GpuMonSurf_HexExpand(GpuMonSurf* s, const void* dump512, const BYTE* fade, const BYTE* touched,
	int bankBase, int rows, float x, float y, float cellW, float cellH, int gapExtra, int haveDump);
HDC GpuMonSurf_GetDC(GpuMonSurf* s);
void GpuMonSurf_ReleaseDC(GpuMonSurf* s);
int GpuMonSurf_ForceOpaque(GpuMonSurf* s);
int GpuMonSurf_Present(GpuMonSurf* s);

/* DrawPiano108 / DrawMiniKeys が GPU バッチへ流す */
void GpuMon_CaptureBegin(GpuMonSurf* s);
void GpuMon_CaptureEnd(void);
int GpuMon_CaptureActive(void);
int GpuMon_CapturePiano(const RECT* rc, int midiNote, int lit, COLORREF gap,
	COLORREF keyW, COLORREF keyB, COLORREF litW, COLORREF litB);
int GpuMon_CapturePianoMask(const RECT* rc, const uint32_t litBits[4],
	COLORREF keyW, COLORREF keyB, COLORREF litW, COLORREF litB);

#endif
