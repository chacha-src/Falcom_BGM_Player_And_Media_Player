// Soft3DTextD2D.cpp — Direct2D/DirectWrite text bake for Soft3D race/maze HUD
#include "stdafx.h"
#include "Soft3DTextD2D.h"

#include <new>
#include <math.h>
#include <string.h>
#include <wincodec.h>
#include <d2d1.h>
#include <dwrite.h>

#ifdef _MSC_VER
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#endif

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)
#endif

static ID2D1Factory* s_d2d = NULL;
static IDWriteFactory* s_dwrite = NULL;
static IWICImagingFactory* s_wic = NULL;

// 実際に使えるフォント面（起動時に1回解決）
static wchar_t s_faceName[64] = L"Segoe UI";
static int s_faceReady = 0;

// 使う文だけ TextFormat を配列キャッシュ（毎 Draw で Create しない）
enum { S3T_FMT_CACHE = 48 };
struct Soft3DFmtSlot {
	IDWriteTextFormat* fmt;
	int pxQ;   // 量子化した px（0.5 刻み×2）
	int bold;
	int align;
	int vCenter;
};
static Soft3DFmtSlot s_fmtCache[S3T_FMT_CACHE];
static int s_fmtN = 0;

struct Soft3DTextD2DCanvas {
	IWICBitmap* bitmap;
	ID2D1RenderTarget* rt;
	int w, h;
	IWICBitmapLock* lock;
	const BYTE* bits;
	UINT stride;
};

static void Soft3DTextD2D_ClearFmtCache()
{
	for (int i = 0; i < s_fmtN; i++) SAFE_RELEASE(s_fmtCache[i].fmt);
	s_fmtN = 0;
	memset(s_fmtCache, 0, sizeof(s_fmtCache));
}

static void Soft3DTextD2D_ResolveFace()
{
	if (s_faceReady || !s_dwrite) return;
	static const wchar_t* kFaces[] = {
		L"Yu Gothic UI", L"Meiryo UI", L"Segoe UI", L"MS UI Gothic"
	};
	IDWriteFontCollection* col = NULL;
	if (FAILED(s_dwrite->GetSystemFontCollection(&col, FALSE)) || !col) {
		wcscpy_s(s_faceName, L"Segoe UI");
		s_faceReady = 1;
		return;
	}
	for (int i = 0; i < 4; i++) {
		UINT idx = 0; BOOL exists = FALSE;
		if (SUCCEEDED(col->FindFamilyName(kFaces[i], &idx, &exists)) && exists) {
			wcscpy_s(s_faceName, kFaces[i]);
			break;
		}
	}
	col->Release();
	s_faceReady = 1;
}

BOOL Soft3DTextD2D_Ensure()
{
	HRESULT hr = S_OK;
	if (!s_d2d) {
		hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &s_d2d);
		if (FAILED(hr) || !s_d2d) return FALSE;
	}
	if (!s_dwrite) {
		hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&s_dwrite);
		if (FAILED(hr) || !s_dwrite) return FALSE;
	}
	if (!s_wic) {
		hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_wic));
		if (FAILED(hr) || !s_wic) {
			CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
			hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_wic));
			if (FAILED(hr) || !s_wic) return FALSE;
		}
	}
	Soft3DTextD2D_ResolveFace();
	return TRUE;
}

void Soft3DTextD2D_Shutdown()
{
	Soft3DTextD2D_ClearFmtCache();
	s_faceReady = 0;
	SAFE_RELEASE(s_wic);
	SAFE_RELEASE(s_dwrite);
	SAFE_RELEASE(s_d2d);
}

static int Soft3DTextD2D_QuantPx(float fontPx)
{
	// 0.5px 単位に丸めてキャッシュ衝突を抑える
	int q = (int)floorf(fontPx * 2.f + 0.5f);
	if (q < 16) q = 16; // 8px 下限
	if (q > 400) q = 400; // 200px 上限
	return q;
}

static IDWriteTextFormat* Soft3DTextD2D_GetFormat(float fontPx, int bold, int align, int vCenter)
{
	if (!s_dwrite) return NULL;
	Soft3DTextD2D_ResolveFace();
	const int pxQ = Soft3DTextD2D_QuantPx(fontPx);
	bold = bold ? 1 : 0;
	align = (align < 0) ? 0 : ((align > 2) ? 2 : align);
	vCenter = vCenter ? 1 : 0;

	for (int i = 0; i < s_fmtN; i++) {
		Soft3DFmtSlot& s = s_fmtCache[i];
		if (s.fmt && s.pxQ == pxQ && s.bold == bold && s.align == align && s.vCenter == vCenter)
			return s.fmt;
	}

	IDWriteTextFormat* fmt = NULL;
	const float px = (float)pxQ * 0.5f;
	HRESULT hr = s_dwrite->CreateTextFormat(
		s_faceName, NULL,
		bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_SEMI_BOLD,
		DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
		px, L"ja-jp", &fmt);
	if (FAILED(hr) || !fmt) {
		hr = s_dwrite->CreateTextFormat(
			L"Segoe UI", NULL,
			bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_SEMI_BOLD,
			DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			px, L"en-us", &fmt);
		if (FAILED(hr) || !fmt) return NULL;
	}
	DWRITE_TEXT_ALIGNMENT ha = DWRITE_TEXT_ALIGNMENT_LEADING;
	if (align == 1) ha = DWRITE_TEXT_ALIGNMENT_CENTER;
	else if (align == 2) ha = DWRITE_TEXT_ALIGNMENT_TRAILING;
	fmt->SetTextAlignment(ha);
	fmt->SetParagraphAlignment(vCenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
	fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

	if (s_fmtN < S3T_FMT_CACHE) {
		Soft3DFmtSlot& s = s_fmtCache[s_fmtN++];
		s.fmt = fmt;
		s.pxQ = pxQ; s.bold = bold; s.align = align; s.vCenter = vCenter;
		return fmt; // キャッシュが所有（呼び出し側は Release しない）
	}
	// 満杯時は一時フォーマット（呼び出し側で解放が必要だが、満杯は稀）
	// → 満杯なら最古を捨てて差し替え
	SAFE_RELEASE(s_fmtCache[0].fmt);
	for (int i = 1; i < S3T_FMT_CACHE; i++) s_fmtCache[i - 1] = s_fmtCache[i];
	s_fmtCache[S3T_FMT_CACHE - 1].fmt = fmt;
	s_fmtCache[S3T_FMT_CACHE - 1].pxQ = pxQ;
	s_fmtCache[S3T_FMT_CACHE - 1].bold = bold;
	s_fmtCache[S3T_FMT_CACHE - 1].align = align;
	s_fmtCache[S3T_FMT_CACHE - 1].vCenter = vCenter;
	s_fmtN = S3T_FMT_CACHE;
	return fmt;
}

Soft3DTextD2DCanvas* Soft3DTextD2D_Begin(int w, int h)
{
	if (w < 8 || h < 8) return NULL;
	if (!Soft3DTextD2D_Ensure()) return NULL;
	Soft3DTextD2DCanvas* c = new (std::nothrow) Soft3DTextD2DCanvas();
	if (!c) return NULL;
	c->bitmap = NULL; c->rt = NULL; c->w = w; c->h = h;
	c->lock = NULL; c->bits = NULL; c->stride = 0;
	HRESULT hr = s_wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &c->bitmap);
	if (FAILED(hr) || !c->bitmap) { delete c; return NULL; }
	D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_DEFAULT,
		D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
	hr = s_d2d->CreateWicBitmapRenderTarget(c->bitmap, props, &c->rt);
	if (FAILED(hr) || !c->rt) { SAFE_RELEASE(c->bitmap); delete c; return NULL; }
	c->rt->BeginDraw();
	c->rt->Clear(D2D1::ColorF(0, 0, 0, 0));
	c->rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	c->rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
	return c;
}

void Soft3DTextD2D_FillRect(Soft3DTextD2DCanvas* c, float x, float y, float w, float h, BYTE a, BYTE r, BYTE g, BYTE b)
{
	if (!c || !c->rt) return;
	ID2D1SolidColorBrush* br = NULL;
	if (FAILED(c->rt->CreateSolidColorBrush(D2D1::ColorF(r / 255.f, g / 255.f, b / 255.f, a / 255.f), &br)) || !br) return;
	c->rt->FillRectangle(D2D1::RectF(x, y, x + w, y + h), br);
	br->Release();
}

void Soft3DTextD2D_FillEllipse(Soft3DTextD2DCanvas* c, float cx, float cy, float rw, float rh, BYTE a, BYTE r, BYTE g, BYTE b)
{
	if (!c || !c->rt) return;
	ID2D1SolidColorBrush* br = NULL;
	if (FAILED(c->rt->CreateSolidColorBrush(D2D1::ColorF(r / 255.f, g / 255.f, b / 255.f, a / 255.f), &br)) || !br) return;
	c->rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rw, rh), br);
	br->Release();
}

void Soft3DTextD2D_FillTriangle(Soft3DTextD2DCanvas* c, float x0, float y0, float x1, float y1, float x2, float y2, BYTE a, BYTE r, BYTE g, BYTE b)
{
	if (!c || !c->rt || !s_d2d) return;
	ID2D1PathGeometry* geo = NULL;
	if (FAILED(s_d2d->CreatePathGeometry(&geo)) || !geo) return;
	ID2D1GeometrySink* sink = NULL;
	if (SUCCEEDED(geo->Open(&sink)) && sink) {
		sink->BeginFigure(D2D1::Point2F(x0, y0), D2D1_FIGURE_BEGIN_FILLED);
		D2D1_POINT_2F pts[2] = { D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2) };
		sink->AddLines(pts, 2);
		sink->EndFigure(D2D1_FIGURE_END_CLOSED);
		sink->Close();
		sink->Release();
	}
	ID2D1SolidColorBrush* br = NULL;
	if (SUCCEEDED(c->rt->CreateSolidColorBrush(D2D1::ColorF(r / 255.f, g / 255.f, b / 255.f, a / 255.f), &br)) && br) {
		c->rt->FillGeometry(geo, br);
		br->Release();
	}
	geo->Release();
}

void Soft3DTextD2D_DrawText(Soft3DTextD2DCanvas* c, const wchar_t* text, float x, float y, float w, float h,
	float fontPx, int bold, int align, int vCenter, BYTE a, BYTE r, BYTE g, BYTE b)
{
	if (!c || !c->rt || !text || !text[0]) return;
	IDWriteTextFormat* fmt = Soft3DTextD2D_GetFormat(fontPx, bold, align, vCenter);
	if (!fmt) return;
	ID2D1SolidColorBrush* br = NULL;
	if (SUCCEEDED(c->rt->CreateSolidColorBrush(D2D1::ColorF(r / 255.f, g / 255.f, b / 255.f, a / 255.f), &br)) && br) {
		c->rt->DrawText(text, (UINT32)wcslen(text), fmt, D2D1::RectF(x, y, x + w, y + h), br,
			D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
		br->Release();
	}
	// fmt はキャッシュ所有 — Release しない
}

void Soft3DTextD2D_DrawTextShadow(Soft3DTextD2DCanvas* c, const wchar_t* text, float x, float y, float w, float h,
	float fontPx, int bold, int align, int vCenter,
	BYTE fa, BYTE fr, BYTE fg, BYTE fb, float shadowDx, float shadowDy, BYTE sa, BYTE sr, BYTE sg, BYTE sb)
{
	Soft3DTextD2D_DrawText(c, text, x + shadowDx, y + shadowDy, w, h, fontPx, bold, align, vCenter, sa, sr, sg, sb);
	Soft3DTextD2D_DrawText(c, text, x, y, w, h, fontPx, bold, align, vCenter, fa, fr, fg, fb);
}

BOOL Soft3DTextD2D_End(Soft3DTextD2DCanvas* c, const BYTE** outBits, UINT* outStride)
{
	if (!c || !c->rt || !c->bitmap) return FALSE;
	HRESULT hr = c->rt->EndDraw();
	SAFE_RELEASE(c->rt);
	if (FAILED(hr)) return FALSE;
	WICRect rc = { 0, 0, c->w, c->h };
	hr = c->bitmap->Lock(&rc, WICBitmapLockRead, &c->lock);
	if (FAILED(hr) || !c->lock) return FALSE;
	UINT cb = 0;
	BYTE* ptr = NULL;
	hr = c->lock->GetDataPointer(&cb, &ptr);
	if (FAILED(hr) || !ptr) { SAFE_RELEASE(c->lock); return FALSE; }
	hr = c->lock->GetStride(&c->stride);
	if (FAILED(hr)) { SAFE_RELEASE(c->lock); return FALSE; }
	c->bits = ptr;
	if (outBits) *outBits = c->bits;
	if (outStride) *outStride = c->stride;
	return TRUE;
}

void Soft3DTextD2D_Release(Soft3DTextD2DCanvas* c)
{
	if (!c) return;
	SAFE_RELEASE(c->lock);
	SAFE_RELEASE(c->rt);
	SAFE_RELEASE(c->bitmap);
	delete c;
}
