#include "stdafx.h"
#include "gpu/GpuDx11.h"
#include "resource.h"
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <vector>
#include <string.h>
#include <stdio.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifndef GPU_RELEASE
#define GPU_RELEASE(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)
#endif

struct GpuInstRect {
	float xywh[4];
	float color[4];
	float uvRect[4];
	float extra[4];
};

struct GpuPianoRow {
	float xywh[4];
	UINT litBits[4];
	float colW[4];
	float colB[4];
	float colLitW[4];
	float colLitB[4];
	float colGap[4];
	UINT ext[4];
};

struct GpuFrameCB {
	float Screen[2];
	float InvScreen[2];
	float Time;
	float FadeGain;
	UINT PianoPass;
	UINT PianoRows;
	UINT Reserve[32];
};

static ID3D11Device* s_dev;
static ID3D11DeviceContext* s_ctx;
static ID3D11VertexShader* s_vsRect;
static ID3D11PixelShader* s_psRect;
static ID3D11VertexShader* s_vsPiano;
static ID3D11ComputeShader* s_csHex;
static ID3D11ComputeShader* s_csGlyph;
static ID3D11Buffer* s_cbFrame;
static ID3D11Buffer* s_cbHex;
static ID3D11Buffer* s_cbGlyph;
static ID3D11Texture2D* s_atlas;
static ID3D11ShaderResourceView* s_atlasSrv;
static ID3D11SamplerState* s_samp;
static ID3D11BlendState* s_blend;
static ID3D11BlendState* s_blendForceA;
static ID3D11RasterizerState* s_rs;
static ID3D11DepthStencilState* s_dsOff;
static IDXGIFactory2* s_fact;
static ATOM s_wndClass;
static GpuMonSurf* s_capture;
static int s_started;
static GpuInstRect s_cpuRects[GPU_MAX_RECT];
static GpuPianoRow s_cpuPiano[GPU_MAX_PIANO_ROW];

static void CrTo4(COLORREF c, float* o)
{
	o[0] = GetRValue(c) / 255.f;
	o[1] = GetGValue(c) / 255.f;
	o[2] = GetBValue(c) / 255.f;
	o[3] = 1.f;
}

static LRESULT CALLBACK GpuHostProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_ERASEBKGND) return 1;
	if (m == WM_PAINT) {
		PAINTSTRUCT ps;
		BeginPaint(h, &ps);
		EndPaint(h, &ps);
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

static void RegisterHostClass()
{
	if (s_wndClass) return;
	WNDCLASSW wc = {};
	wc.lpfnWndProc = GpuHostProc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"OggGpuMonHost";
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	s_wndClass = RegisterClassW(&wc);
}

static HRESULT CompileHlslFile(const wchar_t* path, const char* entry, const char* profile, ID3DBlob** out)
{
	ID3DBlob* err = NULL;
	HRESULT hr = D3DCompileFromFile(path, NULL, NULL, entry, profile,
		D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
	if (FAILED(hr)) {
		GPU_RELEASE(err);
		hr = D3DCompileFromFile(path, NULL, NULL, entry, profile,
			D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, out, &err);
	}
	GPU_RELEASE(err);
	return hr;
}

static HRESULT LoadCsoFile(const wchar_t* path, ID3DBlob** out)
{
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
	DWORD sz = GetFileSize(h, NULL);
	if (!sz || sz == INVALID_FILE_SIZE) { CloseHandle(h); return E_FAIL; }
	ID3DBlob* b = NULL;
	HRESULT hr = D3DCreateBlob(sz, &b);
	if (FAILED(hr) || !b) { CloseHandle(h); return hr; }
	DWORD rd = 0;
	if (!ReadFile(h, b->GetBufferPointer(), sz, &rd, NULL) || rd != sz) {
		CloseHandle(h); GPU_RELEASE(b); return E_FAIL;
	}
	CloseHandle(h);
	*out = b;
	return S_OK;
}

static HRESULT LoadCsoResource(int id, ID3DBlob** out)
{
	HMODULE mod = GetModuleHandleW(NULL);
	HRSRC r = FindResourceW(mod, MAKEINTRESOURCEW(id), RT_RCDATA);
	if (!r) return E_FAIL;
	HGLOBAL g = LoadResource(mod, r);
	if (!g) return E_FAIL;
	DWORD sz = SizeofResource(mod, r);
	const void* p = LockResource(g);
	if (!p || !sz) return E_FAIL;
	ID3DBlob* b = NULL;
	HRESULT hr = D3DCreateBlob(sz, &b);
	if (FAILED(hr) || !b) return hr;
	memcpy(b->GetBufferPointer(), p, sz);
	*out = b;
	return S_OK;
}

static void ShaderPaths(const wchar_t* set, const char* entry, wchar_t* cso, size_t csoN, wchar_t* hlsl, size_t hlslN)
{
	wchar_t base[MAX_PATH] = {};
	GetModuleFileNameW(NULL, base, MAX_PATH);
	wchar_t* slash = wcsrchr(base, L'\\');
	if (slash) slash[1] = 0;
	wchar_t we[32];
	MultiByteToWideChar(CP_UTF8, 0, entry, -1, we, 32);
	_snwprintf_s(cso, csoN, _TRUNCATE, L"%sshaders\\cso\\%s_%s.cso", base, set, we);
	if (wcscmp(set, L"gpu") == 0) {
		if (!strcmp(entry, "VS_Rect") || !strcmp(entry, "PS_Rect"))
			_snwprintf_s(hlsl, hlslN, _TRUNCATE, L"%sshaders\\gpu_rect.hlsl", base);
		else if (!strcmp(entry, "VS_PianoKey"))
			_snwprintf_s(hlsl, hlslN, _TRUNCATE, L"%sshaders\\gpu_piano.hlsl", base);
		else
			_snwprintf_s(hlsl, hlslN, _TRUNCATE, L"%sshaders\\gpu_hex.hlsl", base);
	} else if (wcscmp(set, L"s3m") == 0)
		_snwprintf_s(hlsl, hlslN, _TRUNCATE, L"%sshaders\\s3m.hlsl", base);
	else
		_snwprintf_s(hlsl, hlslN, _TRUNCATE, L"%sshaders\\s3r.hlsl", base);
}

HRESULT GpuTryLoadCso(const wchar_t* set, const char* entry, const char* profile,
	const char* hlslFallback, SIZE_T hlslBytes, void** outBlob)
{
	if (!set || !entry || !profile || !outBlob) return E_INVALIDARG;
	*outBlob = NULL;
	ID3DBlob* blob = NULL;
	int rid = 0;
	if (wcscmp(set, L"gpu") == 0) {
		if (!strcmp(entry, "VS_Rect")) rid = IDR_CSO_GPU_VS_RECT;
		else if (!strcmp(entry, "PS_Rect")) rid = IDR_CSO_GPU_PS_RECT;
		else if (!strcmp(entry, "VS_PianoKey")) rid = IDR_CSO_GPU_VS_PIANO;
		else if (!strcmp(entry, "CS_Hex")) rid = IDR_CSO_GPU_CS_HEX;
		else if (!strcmp(entry, "CS_HexGlyph")) rid = IDR_CSO_GPU_CS_GLYPH;
	}
	if (rid && SUCCEEDED(LoadCsoResource(rid, &blob))) { *outBlob = blob; return S_OK; }
	wchar_t cso[MAX_PATH], hlsl[MAX_PATH];
	ShaderPaths(set, entry, cso, MAX_PATH, hlsl, MAX_PATH);
	if (SUCCEEDED(LoadCsoFile(cso, &blob))) { *outBlob = blob; return S_OK; }
	if (GetFileAttributesW(hlsl) != INVALID_FILE_ATTRIBUTES) {
		if (SUCCEEDED(CompileHlslFile(hlsl, entry, profile, &blob))) { *outBlob = blob; return S_OK; }
	}
	if (hlslFallback && hlslBytes) {
		ID3DBlob* err = NULL;
		HRESULT hr = D3DCompile(hlslFallback, hlslBytes, NULL, NULL, NULL, entry, profile,
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err);
		GPU_RELEASE(err);
		if (SUCCEEDED(hr)) { *outBlob = blob; return S_OK; }
	}
	return E_FAIL;
}

static int BuildAtlas()
{
	const int w = GPU_ATLAS_W, h = GPU_ATLAS_H, cell = GPU_ATLAS_CELL;
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = NULL;
	HDC screen = GetDC(NULL);
	HDC dc = CreateCompatibleDC(screen);
	HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
	if (!bmp || !bits) {
		if (dc) DeleteDC(dc);
		ReleaseDC(NULL, screen);
		return 0;
	}
	HGDIOBJ oldb = SelectObject(dc, bmp);
	RECT rc = { 0, 0, w, h };
	HBRUSH br = CreateSolidBrush(RGB(0, 0, 0));
	FillRect(dc, &rc, br);
	DeleteObject(br);
	HFONT font = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas");
	HGDIOBJ oldf = SelectObject(dc, font);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 255, 255));
	const wchar_t* glyphs = L"0123456789ABCDEF- ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	for (int i = 0; glyphs[i]; i++) {
		int col = i % GPU_ATLAS_COLS;
		int row = i / GPU_ATLAS_COLS;
		RECT cr = { col * cell, row * cell, (col + 1) * cell, (row + 1) * cell };
		wchar_t t[2] = { glyphs[i], 0 };
		DrawTextW(dc, t, 1, &cr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	SelectObject(dc, oldf);
	DeleteObject(font);
	SelectObject(dc, oldb);
	DeleteDC(dc);
	ReleaseDC(NULL, screen);
	/* GDI はアルファを書かない。0 のままだとグリフが消える */
	{
		unsigned* px = (unsigned*)bits;
		const int n = w * h;
		for (int i = 0; i < n; i++) {
			const unsigned v = px[i];
			if (v & 0x00FFFFFFu)
				px[i] = v | 0xFF000000u;
			else
				px[i] = 0;
		}
	}

	D3D11_TEXTURE2D_DESC td = {};
	td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd = {};
	sd.pSysMem = bits;
	sd.SysMemPitch = w * 4;
	HRESULT hr = s_dev->CreateTexture2D(&td, &sd, &s_atlas);
	DeleteObject(bmp);
	if (FAILED(hr)) return 0;
	if (FAILED(s_dev->CreateShaderResourceView(s_atlas, NULL, &s_atlasSrv))) return 0;
	return 1;
}

static int CreateShaders()
{
	ID3DBlob* b = NULL;
	if (FAILED(GpuTryLoadCso(L"gpu", "VS_Rect", "vs_5_0", NULL, 0, (void**)&b)) || !b) return 0;
	if (FAILED(s_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &s_vsRect))) {
		GPU_RELEASE(b); return 0;
	}
	GPU_RELEASE(b);
	if (FAILED(GpuTryLoadCso(L"gpu", "PS_Rect", "ps_5_0", NULL, 0, (void**)&b)) || !b) return 0;
	if (FAILED(s_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &s_psRect))) {
		GPU_RELEASE(b); return 0;
	}
	GPU_RELEASE(b);
	if (FAILED(GpuTryLoadCso(L"gpu", "VS_PianoKey", "vs_5_0", NULL, 0, (void**)&b)) || !b) return 0;
	if (FAILED(s_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &s_vsPiano))) {
		GPU_RELEASE(b); return 0;
	}
	GPU_RELEASE(b);
	if (FAILED(GpuTryLoadCso(L"gpu", "CS_Hex", "cs_5_0", NULL, 0, (void**)&b)) || !b) return 0;
	if (FAILED(s_dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &s_csHex))) {
		GPU_RELEASE(b); return 0;
	}
	GPU_RELEASE(b);
	if (FAILED(GpuTryLoadCso(L"gpu", "CS_HexGlyph", "cs_5_0", NULL, 0, (void**)&b)) || !b) return 0;
	if (FAILED(s_dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &s_csGlyph))) {
		GPU_RELEASE(b); return 0;
	}
	GPU_RELEASE(b);
	return 1;
}

int GpuDx11_Startup(void)
{
	if (s_started && s_dev) return 1;
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D3D_FEATURE_LEVEL req = D3D_FEATURE_LEVEL_11_0, got = (D3D_FEATURE_LEVEL)0;
	HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, &req, 1,
		D3D11_SDK_VERSION, &s_dev, &got, &s_ctx);
	if (FAILED(hr))
		hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, flags, &req, 1,
			D3D11_SDK_VERSION, &s_dev, &got, &s_ctx);
	if (FAILED(hr) || got < D3D_FEATURE_LEVEL_11_0) return 0;
	IDXGIDevice* xd = NULL;
	IDXGIAdapter* xa = NULL;
	if (FAILED(s_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd)) || FAILED(xd->GetAdapter(&xa))) {
		GPU_RELEASE(xd); return 0;
	}
	xa->GetParent(__uuidof(IDXGIFactory2), (void**)&s_fact);
	GPU_RELEASE(xa); GPU_RELEASE(xd);
	if (!s_fact) return 0;
	if (!CreateShaders()) return 0;
	if (!BuildAtlas()) return 0;

	D3D11_BUFFER_DESC cbd = {};
	cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbd.Usage = D3D11_USAGE_DYNAMIC;
	cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbd.ByteWidth = sizeof(GpuFrameCB);
	if (FAILED(s_dev->CreateBuffer(&cbd, NULL, &s_cbFrame))) return 0;
	cbd.ByteWidth = 256;
	if (FAILED(s_dev->CreateBuffer(&cbd, NULL, &s_cbHex))) return 0;
	if (FAILED(s_dev->CreateBuffer(&cbd, NULL, &s_cbGlyph))) return 0;

	D3D11_SAMPLER_DESC smp = {};
	smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	s_dev->CreateSamplerState(&smp, &s_samp);

	D3D11_BLEND_DESC bd = {};
	bd.RenderTarget[0].BlendEnable = TRUE;
	bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	s_dev->CreateBlendState(&bd, &s_blend);

	bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO;
	bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	s_dev->CreateBlendState(&bd, &s_blendForceA);

	D3D11_RASTERIZER_DESC rd = {};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_NONE;
	rd.ScissorEnable = FALSE;
	s_dev->CreateRasterizerState(&rd, &s_rs);

	D3D11_DEPTH_STENCIL_DESC dd = {};
	dd.DepthEnable = FALSE;
	s_dev->CreateDepthStencilState(&dd, &s_dsOff);

	RegisterHostClass();
	s_started = 1;
	return 1;
}

void GpuDx11_Shutdown(void)
{
	s_capture = NULL;
	GPU_RELEASE(s_dsOff);
	GPU_RELEASE(s_rs);
	GPU_RELEASE(s_blendForceA);
	GPU_RELEASE(s_blend);
	GPU_RELEASE(s_samp);
	GPU_RELEASE(s_atlasSrv);
	GPU_RELEASE(s_atlas);
	GPU_RELEASE(s_cbGlyph);
	GPU_RELEASE(s_cbHex);
	GPU_RELEASE(s_cbFrame);
	GPU_RELEASE(s_csGlyph);
	GPU_RELEASE(s_csHex);
	GPU_RELEASE(s_vsPiano);
	GPU_RELEASE(s_psRect);
	GPU_RELEASE(s_vsRect);
	GPU_RELEASE(s_fact);
	GPU_RELEASE(s_ctx);
	GPU_RELEASE(s_dev);
	s_started = 0;
}

int GpuDx11_Ready(void) { return (s_dev && s_ctx && s_vsRect && s_psRect) ? 1 : 0; }
ID3D11Device* GpuDx11_Device(void) { return s_dev; }
ID3D11DeviceContext* GpuDx11_Context(void) { return s_ctx; }

static void ReleaseSurfGpu(GpuMonSurf* s)
{
	if (!s) return;
	GPU_RELEASE(*(ID3D11ShaderResourceView**)&s->hexOutSrv);
	GPU_RELEASE(*(ID3D11UnorderedAccessView**)&s->hexOutUav);
	GPU_RELEASE(*(ID3D11Buffer**)&s->hexOut);
	GPU_RELEASE(*(ID3D11ShaderResourceView**)&s->hexDumpSrv);
	GPU_RELEASE(*(ID3D11Buffer**)&s->hexDump);
	GPU_RELEASE(*(ID3D11ShaderResourceView**)&s->pianoSrv);
	GPU_RELEASE(*(ID3D11Buffer**)&s->pianoSB);
	GPU_RELEASE(*(ID3D11ShaderResourceView**)&s->rectSrv);
	GPU_RELEASE(*(ID3D11Buffer**)&s->rectSB);
	GPU_RELEASE(*(ID3D11RenderTargetView**)&s->rtv);
	GPU_RELEASE(*(ID3D11Texture2D**)&s->bb);
	GPU_RELEASE(*(IDXGISwapChain1**)&s->sc);
	s->ready = 0;
}

void GpuMonSurf_Release(GpuMonSurf* s)
{
	if (!s) return;
	if (s_capture == s) s_capture = NULL;
	ReleaseSurfGpu(s);
	if (s->child && IsWindow(s->child))
		DestroyWindow(s->child);
	s->child = NULL;
	s->parent = NULL;
	memset(s, 0, sizeof(*s));
}

static int CreateSurfBuffers(GpuMonSurf* s)
{
	D3D11_BUFFER_DESC bd = {};
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.ByteWidth = sizeof(GpuInstRect) * GPU_MAX_RECT;
	bd.StructureByteStride = sizeof(GpuInstRect);
	if (FAILED(s_dev->CreateBuffer(&bd, NULL, (ID3D11Buffer**)&s->rectSB))) return 0;
	D3D11_SHADER_RESOURCE_VIEW_DESC svd = {};
	svd.Format = DXGI_FORMAT_UNKNOWN;
	svd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	svd.Buffer.NumElements = GPU_MAX_RECT;
	if (FAILED(s_dev->CreateShaderResourceView((ID3D11Buffer*)s->rectSB, &svd, (ID3D11ShaderResourceView**)&s->rectSrv)))
		return 0;

	bd.ByteWidth = sizeof(GpuPianoRow) * GPU_MAX_PIANO_ROW;
	bd.StructureByteStride = sizeof(GpuPianoRow);
	if (FAILED(s_dev->CreateBuffer(&bd, NULL, (ID3D11Buffer**)&s->pianoSB))) return 0;
	svd.Buffer.NumElements = GPU_MAX_PIANO_ROW;
	if (FAILED(s_dev->CreateShaderResourceView((ID3D11Buffer*)s->pianoSB, &svd, (ID3D11ShaderResourceView**)&s->pianoSrv)))
		return 0;

	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.CPUAccessFlags = 0;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	bd.ByteWidth = sizeof(GpuInstRect) * (GPU_HEX_CELLS * 3);
	bd.StructureByteStride = sizeof(GpuInstRect);
	if (FAILED(s_dev->CreateBuffer(&bd, NULL, (ID3D11Buffer**)&s->hexOut))) return 0;
	D3D11_UNORDERED_ACCESS_VIEW_DESC uvd = {};
	uvd.Format = DXGI_FORMAT_UNKNOWN;
	uvd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uvd.Buffer.NumElements = GPU_HEX_CELLS * 3;
	if (FAILED(s_dev->CreateUnorderedAccessView((ID3D11Buffer*)s->hexOut, &uvd, (ID3D11UnorderedAccessView**)&s->hexOutUav)))
		return 0;
	svd.Buffer.NumElements = GPU_HEX_CELLS * 3;
	if (FAILED(s_dev->CreateShaderResourceView((ID3D11Buffer*)s->hexOut, &svd, (ID3D11ShaderResourceView**)&s->hexOutSrv)))
		return 0;

	D3D11_BUFFER_DESC raw = {};
	raw.ByteWidth = 0xB00;
	raw.Usage = D3D11_USAGE_DYNAMIC;
	raw.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	raw.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	raw.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	if (FAILED(s_dev->CreateBuffer(&raw, NULL, (ID3D11Buffer**)&s->hexDump))) return 0;
	D3D11_SHADER_RESOURCE_VIEW_DESC rawv = {};
	rawv.Format = DXGI_FORMAT_R32_TYPELESS;
	rawv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	rawv.BufferEx.NumElements = 0xB00 / 4;
	rawv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	if (FAILED(s_dev->CreateShaderResourceView((ID3D11Buffer*)s->hexDump, &rawv,
		(ID3D11ShaderResourceView**)&s->hexDumpSrv)))
		return 0;
	return 1;
}

int GpuMonSurf_Ensure(GpuMonSurf* s, HWND parent, int x, int y, unsigned w, unsigned h)
{
	if (!s || !parent || !GpuDx11_Ready()) return 0;
	if (w < 8) w = 8;
	if (h < 8) h = 8;
	if (!s->child) {
		s->child = CreateWindowExW(0, L"OggGpuMonHost", L"",
			WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
			x, y, (int)w, (int)h, parent, NULL, GetModuleHandleW(NULL), NULL);
		if (!s->child) return 0;
		s->parent = parent;
		s->capH = y;
	} else if (s->parent != parent || s->w != w || s->h != h || s->capH != y) {
		/* 同じ位置での MoveWindow(TRUE) は親のアクリル帯まで Invalidate し、
		   無演奏時にキャプション文字だけが点滅する。Present はこちらで行う。 */
		::MoveWindow(s->child, x, y, (int)w, (int)h, FALSE);
		s->parent = parent;
		s->capH = y;
	}
	if (s->ready && s->w == w && s->h == h && s->sc) return 1;
	ReleaseSurfGpu(s);
	DXGI_SWAP_CHAIN_DESC1 sd = {};
	sd.Width = w; sd.Height = h;
	sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	sd.SampleDesc.Count = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = 2;
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
	sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
	HRESULT hr = s_fact->CreateSwapChainForHwnd(s_dev, s->child, &sd, NULL, NULL, (IDXGISwapChain1**)&s->sc);
	if (FAILED(hr)) return 0;
	IDXGISwapChain1* sc = (IDXGISwapChain1*)s->sc;
	if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&s->bb))) return 0;
	if (FAILED(s_dev->CreateRenderTargetView((ID3D11Texture2D*)s->bb, NULL, (ID3D11RenderTargetView**)&s->rtv)))
		return 0;
	if (!CreateSurfBuffers(s)) return 0;
	s->w = w; s->h = h; s->ready = 1; s->rectN = 0; s->pianoN = 0;
	return 1;
}

static void UploadFrameCB(GpuMonSurf* s, UINT pianoPass)
{
	D3D11_MAPPED_SUBRESOURCE map = {};
	if (FAILED(s_ctx->Map(s_cbFrame, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return;
	GpuFrameCB cb = {};
	cb.Screen[0] = (float)s->w; cb.Screen[1] = (float)s->h;
	cb.InvScreen[0] = 1.f / (float)s->w; cb.InvScreen[1] = 1.f / (float)s->h;
	cb.Time = (float)(GetTickCount64() & 0xffffff) * 0.001f;
	cb.FadeGain = 1.f;
	cb.PianoPass = pianoPass;
	cb.PianoRows = (UINT)s->pianoN;
	memcpy(map.pData, &cb, sizeof(cb));
	s_ctx->Unmap(s_cbFrame, 0);
}

static void BindDraw(GpuMonSurf* s)
{
	ID3D11DeviceContext* ctx = s_ctx;
	ID3D11RenderTargetView* rtv = (ID3D11RenderTargetView*)s->rtv;
	ctx->OMSetRenderTargets(1, &rtv, NULL);
	D3D11_VIEWPORT vp = {};
	vp.Width = (float)s->w; vp.Height = (float)s->h; vp.MaxDepth = 1.f;
	ctx->RSSetViewports(1, &vp);
	ctx->RSSetState(s_rs);
	ctx->OMSetDepthStencilState(s_dsOff, 0);
	float blendF[4] = { 0, 0, 0, 0 };
	ctx->OMSetBlendState(s_blend, blendF, 0xffffffff);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->IASetInputLayout(NULL);
	UploadFrameCB(s, 0);
	ctx->VSSetConstantBuffers(0, 1, &s_cbFrame);
	ctx->PSSetConstantBuffers(0, 1, &s_cbFrame);
	ctx->PSSetShaderResources(1, 1, &s_atlasSrv);
	ctx->PSSetSamplers(0, 1, &s_samp);
	ctx->PSSetShader(s_psRect, NULL, 0);
}

int GpuMonSurf_Begin(GpuMonSurf* s, COLORREF bg)
{
	if (!s || !s->ready) return 0;
	s->rectN = 0; s->pianoN = 0;
	BindDraw(s);
	float c[4] = { GetRValue(bg) / 255.f, GetGValue(bg) / 255.f, GetBValue(bg) / 255.f, 1.f };
	s_ctx->ClearRenderTargetView((ID3D11RenderTargetView*)s->rtv, c);
	return 1;
}

void GpuMonSurf_AddRect(GpuMonSurf* s, float x, float y, float w, float h, COLORREF c, float glow)
{
	if (!s || s->rectN >= GPU_MAX_RECT) return;
	GpuInstRect& r = s_cpuRects[s->rectN++];
	r.xywh[0] = x; r.xywh[1] = y; r.xywh[2] = w; r.xywh[3] = h;
	CrTo4(c, r.color);
	r.uvRect[0] = r.uvRect[1] = r.uvRect[2] = r.uvRect[3] = -1.f;
	r.extra[0] = 0; r.extra[1] = glow; r.extra[2] = r.extra[3] = 0;
}

void GpuMonSurf_AddGlyph(GpuMonSurf* s, float x, float y, float w, float h, unsigned glyph, COLORREF c)
{
	if (!s || s->rectN >= GPU_MAX_RECT) return;
	GpuInstRect& r = s_cpuRects[s->rectN++];
	r.xywh[0] = x; r.xywh[1] = y; r.xywh[2] = w; r.xywh[3] = h;
	CrTo4(c, r.color);
	unsigned col = glyph & 15, row = glyph >> 4;
	r.uvRect[0] = (col * 32.f) / 512.f;
	r.uvRect[1] = (row * 32.f) / 256.f;
	r.uvRect[2] = r.uvRect[0] + 32.f / 512.f;
	r.uvRect[3] = r.uvRect[1] + 32.f / 256.f;
	r.extra[0] = 1; r.extra[1] = r.extra[2] = r.extra[3] = 0;
}

void GpuMonSurf_AddPianoRow(GpuMonSurf* s, const RECT* rc, const uint32_t litBits[4],
	COLORREF w, COLORREF b, COLORREF lw, COLORREF lb)
{
	if (!s || !rc || s->pianoN >= GPU_MAX_PIANO_ROW) return;
	GpuPianoRow& p = s_cpuPiano[s->pianoN++];
	p.xywh[0] = (float)rc->left; p.xywh[1] = (float)rc->top;
	p.xywh[2] = (float)(rc->right - rc->left); p.xywh[3] = (float)(rc->bottom - rc->top);
	p.litBits[0] = litBits[0]; p.litBits[1] = litBits[1];
	p.litBits[2] = litBits[2]; p.litBits[3] = litBits[3];
	CrTo4(w, p.colW); CrTo4(b, p.colB); CrTo4(lw, p.colLitW); CrTo4(lb, p.colLitB);
	p.colGap[0] = 12 / 255.f; p.colGap[1] = 14 / 255.f; p.colGap[2] = 18 / 255.f; p.colGap[3] = 1.f;
	p.ext[0] = p.ext[1] = p.ext[2] = p.ext[3] = 0;
	GpuMonSurf_AddRect(s, p.xywh[0], p.xywh[1], p.xywh[2], p.xywh[3], RGB(12, 14, 18), 0);
}

int GpuMonSurf_FlushRects(GpuMonSurf* s)
{
	if (!s || !s->ready || s->rectN <= 0) return 0;
	D3D11_MAPPED_SUBRESOURCE map = {};
	if (FAILED(s_ctx->Map((ID3D11Buffer*)s->rectSB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return 0;
	memcpy(map.pData, s_cpuRects, sizeof(GpuInstRect) * s->rectN);
	s_ctx->Unmap((ID3D11Buffer*)s->rectSB, 0);
	ID3D11ShaderResourceView* srv = (ID3D11ShaderResourceView*)s->rectSrv;
	s_ctx->VSSetShader(s_vsRect, NULL, 0);
	s_ctx->VSSetShaderResources(0, 1, &srv);
	s_ctx->DrawInstanced(6, s->rectN, 0, 0);
	ID3D11ShaderResourceView* none = NULL;
	s_ctx->VSSetShaderResources(0, 1, &none);
	return 1;
}

int GpuMonSurf_FlushPianos(GpuMonSurf* s)
{
	if (!s || !s->ready || s->pianoN <= 0 || !s_vsPiano) return 0;
	D3D11_MAPPED_SUBRESOURCE map = {};
	if (FAILED(s_ctx->Map((ID3D11Buffer*)s->pianoSB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return 0;
	memcpy(map.pData, s_cpuPiano, sizeof(GpuPianoRow) * s->pianoN);
	s_ctx->Unmap((ID3D11Buffer*)s->pianoSB, 0);
	ID3D11ShaderResourceView* srv = (ID3D11ShaderResourceView*)s->pianoSrv;
	s_ctx->VSSetShader(s_vsPiano, NULL, 0);
	s_ctx->VSSetShaderResources(0, 1, &srv);
	UploadFrameCB(s, 0);
	s_ctx->DrawInstanced(6, (UINT)s->pianoN * 52, 0, 0);
	UploadFrameCB(s, 1);
	s_ctx->DrawInstanced(6, (UINT)s->pianoN * 36, 0, 0);
	ID3D11ShaderResourceView* none = NULL;
	s_ctx->VSSetShaderResources(0, 1, &none);
	return 1;
}

int GpuMonSurf_HexExpand(GpuMonSurf* s, const void* dump512, const BYTE* fade, const BYTE* touched,
	int bankBase, int rows, float x, float y, float cellW, float cellH, int gapExtra, int haveDump)
{
	if (!s || !s->ready || !s_csHex) return 0;
	if (rows < 1) rows = 1;
	if (rows > 16) rows = 16;
	D3D11_MAPPED_SUBRESOURCE map = {};
	if (SUCCEEDED(s_ctx->Map((ID3D11Buffer*)s->hexDump, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
		memset(map.pData, 0, 0xB00);
		if (dump512) memcpy(map.pData, dump512, 0x300);
		if (fade) memcpy((BYTE*)map.pData + 0x500, fade, 0x300);
		if (touched) memcpy((BYTE*)map.pData + 0x800, touched, 0x300);
		s_ctx->Unmap((ID3D11Buffer*)s->hexDump, 0);
	}
	struct HexCB {
		float Origin[4];
		UINT BankBase, RowCount, GapExtra, HaveDump;
		UINT Ext[32];
	} hcb = {};
	hcb.Origin[0] = x; hcb.Origin[1] = y; hcb.Origin[2] = cellW; hcb.Origin[3] = cellH;
	hcb.BankBase = (UINT)bankBase;
	hcb.RowCount = (UINT)rows;
	hcb.GapExtra = (UINT)gapExtra;
	hcb.HaveDump = haveDump ? 1u : 0u;
	if (SUCCEEDED(s_ctx->Map(s_cbHex, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
		memcpy(map.pData, &hcb, sizeof(hcb));
		s_ctx->Unmap(s_cbHex, 0);
	}
	ID3D11ShaderResourceView* dumpSrv = (ID3D11ShaderResourceView*)s->hexDumpSrv;
	ID3D11UnorderedAccessView* uav = (ID3D11UnorderedAccessView*)s->hexOutUav;
	s_ctx->CSSetShader(s_csHex, NULL, 0);
	s_ctx->CSSetConstantBuffers(1, 1, &s_cbHex);
	s_ctx->CSSetShaderResources(0, 1, &dumpSrv);
	s_ctx->CSSetUnorderedAccessViews(0, 1, &uav, NULL);
	s_ctx->Dispatch(1, 1, 1);
	if (s_csGlyph) {
		struct GlyphCB {
			float Origin[4];
			UINT BankBase, RowCount, GapExtra, HaveDump, OutBase, Pad[3];
			UINT Ext[28];
		} gcb = {};
		gcb.Origin[0] = x; gcb.Origin[1] = y; gcb.Origin[2] = cellW; gcb.Origin[3] = cellH;
		gcb.BankBase = (UINT)bankBase;
		gcb.RowCount = (UINT)rows;
		gcb.GapExtra = (UINT)gapExtra;
		gcb.HaveDump = haveDump ? 1u : 0u;
		gcb.OutBase = (UINT)rows * 16u;
		if (SUCCEEDED(s_ctx->Map(s_cbGlyph, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
			memcpy(map.pData, &gcb, sizeof(gcb));
			s_ctx->Unmap(s_cbGlyph, 0);
		}
		s_ctx->CSSetShader(s_csGlyph, NULL, 0);
		s_ctx->CSSetConstantBuffers(2, 1, &s_cbGlyph);
		s_ctx->Dispatch(1, 1, 1);
	}
	ID3D11UnorderedAccessView* noneU = NULL;
	ID3D11ShaderResourceView* noneS = NULL;
	s_ctx->CSSetUnorderedAccessViews(0, 1, &noneU, NULL);
	s_ctx->CSSetShaderResources(0, 1, &noneS);
	s_ctx->CSSetShader(NULL, NULL, 0);

	BindDraw(s);
	ID3D11ShaderResourceView* hexSrv = (ID3D11ShaderResourceView*)s->hexOutSrv;
	s_ctx->VSSetShader(s_vsRect, NULL, 0);
	s_ctx->VSSetShaderResources(0, 1, &hexSrv);
	UINT n = (UINT)rows * 16u;
	s_ctx->DrawInstanced(6, n * 3, 0, 0);
	s_ctx->VSSetShaderResources(0, 1, &noneS);
	return 1;
}

HDC GpuMonSurf_GetDC(GpuMonSurf* s)
{
	if (!s || !s->ready || !s->bb) return NULL;
	s_ctx->OMSetRenderTargets(0, NULL, NULL);
	IDXGISurface1* surf = NULL;
	if (FAILED(((ID3D11Texture2D*)s->bb)->QueryInterface(__uuidof(IDXGISurface1), (void**)&surf)))
		return NULL;
	HDC hdc = NULL;
	HRESULT hr = surf->GetDC(FALSE, &hdc);
	GPU_RELEASE(surf);
	if (FAILED(hr)) return NULL;
	s->gdiLock = 1;
	return hdc;
}

void GpuMonSurf_ReleaseDC(GpuMonSurf* s)
{
	if (!s || !s->gdiLock || !s->bb) return;
	IDXGISurface1* surf = NULL;
	if (SUCCEEDED(((ID3D11Texture2D*)s->bb)->QueryInterface(__uuidof(IDXGISurface1), (void**)&surf))) {
		surf->ReleaseDC(NULL);
		GPU_RELEASE(surf);
	}
	s->gdiLock = 0;
	BindDraw(s);
}

int GpuMonSurf_ForceOpaque(GpuMonSurf* s)
{
	/* DXGI+GDI は RGB だけ書いて A=0。DWM がパネル/文字を完全透過にする */
	if (!s || !s->ready || !s_blendForceA) return 0;
	BindDraw(s);
	float bf[4] = { 0, 0, 0, 0 };
	s_ctx->OMSetBlendState(s_blendForceA, bf, 0xffffffff);
	D3D11_MAPPED_SUBRESOURCE map = {};
	if (SUCCEEDED(s_ctx->Map((ID3D11Buffer*)s->rectSB, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
		GpuInstRect r = {};
		r.xywh[2] = (float)s->w;
		r.xywh[3] = (float)s->h;
		r.color[3] = 1.f;
		r.uvRect[0] = r.uvRect[1] = r.uvRect[2] = r.uvRect[3] = -1.f;
		memcpy(map.pData, &r, sizeof(r));
		s_ctx->Unmap((ID3D11Buffer*)s->rectSB, 0);
	}
	ID3D11ShaderResourceView* srv = (ID3D11ShaderResourceView*)s->rectSrv;
	s_ctx->VSSetShader(s_vsRect, NULL, 0);
	s_ctx->VSSetShaderResources(0, 1, &srv);
	s_ctx->DrawInstanced(6, 1, 0, 0);
	ID3D11ShaderResourceView* none = NULL;
	s_ctx->VSSetShaderResources(0, 1, &none);
	s_ctx->OMSetBlendState(s_blend, bf, 0xffffffff);
	return 1;
}

int GpuMonSurf_Present(GpuMonSurf* s)
{
	if (!s || !s->sc) return 0;
	if (s->gdiLock) GpuMonSurf_ReleaseDC(s);
	HRESULT hr = ((IDXGISwapChain1*)s->sc)->Present(0, 0);
	return SUCCEEDED(hr) ? 1 : 0;
}

void GpuMon_CaptureBegin(GpuMonSurf* s) { s_capture = s; }
void GpuMon_CaptureEnd(void) { s_capture = NULL; }
int GpuMon_CaptureActive(void) { return s_capture ? 1 : 0; }

int GpuMon_CapturePiano(const RECT* rc, int midiNote, int lit, COLORREF gap,
	COLORREF keyW, COLORREF keyB, COLORREF litW, COLORREF litB)
{
	if (!s_capture || !rc) return 0;
	uint32_t bits[4] = {};
	if (lit && midiNote >= 21 && midiNote <= 108) {
		int b = midiNote - 21;
		bits[b >> 5] |= (1u << (b & 31));
	}
	(void)gap;
	GpuMonSurf_AddPianoRow(s_capture, rc, bits, keyW, keyB, litW, litB);
	return 1;
}

int GpuMon_CapturePianoMask(const RECT* rc, const uint32_t litBits[4],
	COLORREF keyW, COLORREF keyB, COLORREF litW, COLORREF litB)
{
	if (!s_capture || !rc || !litBits) return 0;
	GpuMonSurf_AddPianoRow(s_capture, rc, litBits, keyW, keyB, litW, litB);
	return 1;
}
