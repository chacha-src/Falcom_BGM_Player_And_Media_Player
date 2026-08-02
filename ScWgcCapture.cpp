// ScWgcCapture.cpp
// Windows.Graphics.Capture (HWND)
// プール=ウィンドウ実サイズ → GPU シェーダでレイヤへ縮小 → Compose 時に小さく Map
// FrameArrived では Map しない（コールバックでの GPU 同期待ちを避ける）

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#endif

#include <windows.h>
#include <d3d11.h>
#include <d3d10_1.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <unknwn.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ScWgcCapture.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowsapp.lib")

namespace {

using winrt::com_ptr;
using winrt::Windows::Foundation::Metadata::ApiInformation;
using winrt::Windows::Graphics::SizeInt32;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

struct WgcSession {
	HWND hwnd = nullptr;
	GraphicsCaptureItem item{ nullptr };
	Direct3D11CaptureFramePool pool{ nullptr };
	GraphicsCaptureSession session{ nullptr };
	winrt::event_token arrivedToken{};
	winrt::event_token closedToken{};
	com_ptr<ID3D11Texture2D> gpuInput;
	com_ptr<ID3D11Texture2D> gpuScaled;
	com_ptr<ID3D11ShaderResourceView> srv;
	com_ptr<ID3D11RenderTargetView> rtv;
	com_ptr<ID3D11Texture2D> staging;
	std::vector<BYTE> bgra;
	int contentW = 0;
	int contentH = 0;
	int poolW = 0;
	int poolH = 0;
	int scaledW = 0;
	int scaledH = 0;
	std::atomic<int> wantW{ 0 };
	std::atomic<int> wantH{ 0 };
	std::atomic<unsigned> gpuGen{ 0 };
	unsigned cpuGen = 0;
	DWORD lastPoolResizeTick = 0;
	bool hasFrame = false;
	bool gpuReady = false;
	bool closed = false;
	HANDLE frameEvent = nullptr;
	std::mutex frameMtx;
};

static std::mutex g_mtx;
static std::mutex g_ctxMtx;
static com_ptr<ID3D11Device> g_d3d;
static com_ptr<ID3D11DeviceContext> g_ctx;
static IDirect3DDevice g_winrtDevice{ nullptr };
static std::unordered_map<HWND, std::unique_ptr<WgcSession>> g_sessions;
static bool g_apartmentReady = false;

static com_ptr<ID3D11VertexShader> g_vs;
static com_ptr<ID3D11PixelShader> g_ps;
static com_ptr<ID3D11SamplerState> g_samp;
static com_ptr<ID3D11RasterizerState> g_rs;
static com_ptr<ID3D11BlendState> g_bs;
static BOOL g_shaderReady = FALSE;

static void EnsureApartment()
{
	if (g_apartmentReady) return;
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
	} catch (...) {
	}
	g_apartmentReady = true;
}

static BOOL EnsureShaders()
{
	if (g_shaderReady) return TRUE;
	if (!g_d3d) return FALSE;

	// UV を [0,1] に収めたフルスクリーン三角形（CLAMP 依存を減らす）
	static const char kHlsl[] =
		"Texture2D tex0 : register(t0);\n"
		"SamplerState samp0 : register(s0);\n"
		"struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
		"VSOut VSMain(uint vid : SV_VertexID) {\n"
		"  VSOut o;\n"
		"  float2 pos = float2((vid == 1) ? 3.0 : -1.0, (vid == 2) ? 3.0 : -1.0);\n"
		"  o.pos = float4(pos, 0.0, 1.0);\n"
		"  o.uv = float2((pos.x + 1.0) * 0.5, 1.0 - (pos.y + 1.0) * 0.5);\n"
		"  return o;\n"
		"}\n"
		"float4 PSMain(VSOut i) : SV_Target {\n"
		"  return tex0.SampleLevel(samp0, saturate(i.uv), 0);\n"
		"}\n";

	com_ptr<ID3DBlob> vsBlob, psBlob, err;
	if (FAILED(D3DCompile(kHlsl, sizeof(kHlsl) - 1, nullptr, nullptr, nullptr,
		"VSMain", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, vsBlob.put(), err.put())) || !vsBlob)
		return FALSE;
	if (FAILED(D3DCompile(kHlsl, sizeof(kHlsl) - 1, nullptr, nullptr, nullptr,
		"PSMain", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, psBlob.put(), err.put())) || !psBlob)
		return FALSE;
	if (FAILED(g_d3d->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, g_vs.put())))
		return FALSE;
	if (FAILED(g_d3d->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, g_ps.put())))
		return FALSE;

	D3D11_SAMPLER_DESC sd = {};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(g_d3d->CreateSamplerState(&sd, g_samp.put())))
		return FALSE;

	D3D11_RASTERIZER_DESC rd = {};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_NONE;
	if (FAILED(g_d3d->CreateRasterizerState(&rd, g_rs.put())))
		return FALSE;

	D3D11_BLEND_DESC bd = {};
	bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(g_d3d->CreateBlendState(&bd, g_bs.put())))
		return FALSE;

	g_shaderReady = TRUE;
	return TRUE;
}

static BOOL EnsureD3D()
{
	if (g_winrtDevice && g_shaderReady) return TRUE;
	EnsureApartment();

	if (!g_d3d) {
		const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
		HRESULT hr = D3D11CreateDevice(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
			nullptr, 0, D3D11_SDK_VERSION, g_d3d.put(), &fl, g_ctx.put());
		if (FAILED(hr)) {
			hr = D3D11CreateDevice(
				nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
				nullptr, 0, D3D11_SDK_VERSION, g_d3d.put(), &fl, g_ctx.put());
		}
		if (FAILED(hr) || !g_d3d || !g_ctx) return FALSE;

		com_ptr<ID3D10Multithread> mt;
		if (SUCCEEDED(g_d3d->QueryInterface(__uuidof(ID3D10Multithread), mt.put_void())) && mt)
			mt->SetMultithreadProtected(TRUE);

		com_ptr<IDXGIDevice> dxgi;
		hr = g_d3d->QueryInterface(__uuidof(IDXGIDevice), dxgi.put_void());
		if (FAILED(hr) || !dxgi) return FALSE;

		com_ptr<::IInspectable> insp;
		hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), insp.put());
		if (FAILED(hr) || !insp) return FALSE;
		g_winrtDevice = insp.as<IDirect3DDevice>();
		if (!g_winrtDevice) return FALSE;
	}
	return EnsureShaders();
}

static GraphicsCaptureItem CreateItemForWindow(HWND hwnd)
{
	auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
	GraphicsCaptureItem item{ nullptr };
	winrt::check_hresult(factory->CreateForWindow(
		hwnd, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
	return item;
}

static com_ptr<ID3D11Texture2D> TextureFromSurface(
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const& surface)
{
	auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
	com_ptr<ID3D11Texture2D> tex;
	winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(tex.put())));
	return tex;
}

static int ScEven2(int v)
{
	if (v < 2) return 2;
	return v & ~1;
}

static BOOL RecreatePool_NoLock(WgcSession& s, int w, int h)
{
	w = ScEven2(w);
	h = ScEven2(h);
	if (!s.pool || !g_winrtDevice) return FALSE;
	if (s.poolW == w && s.poolH == h) return TRUE;
	try {
		s.pool.Recreate(g_winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, SizeInt32{ w, h });
		s.poolW = w;
		s.poolH = h;
		s.staging = nullptr;
		s.gpuScaled = nullptr;
		s.gpuInput = nullptr;
		s.srv = nullptr;
		s.rtv = nullptr;
		s.gpuReady = false;
		s.hasFrame = false;
		if (s.frameEvent) ResetEvent(s.frameEvent);
		return TRUE;
	} catch (...) {
		return FALSE;
	}
}

static BOOL EnsureTex(com_ptr<ID3D11Texture2D>& tex, int w, int h, UINT bind,
	com_ptr<ID3D11ShaderResourceView>* clearSrv,
	com_ptr<ID3D11RenderTargetView>* clearRtv)
{
	if (tex) {
		D3D11_TEXTURE2D_DESC d = {};
		tex->GetDesc(&d);
		if ((int)d.Width == w && (int)d.Height == h)
			return TRUE;
		tex = nullptr;
		if (clearSrv) *clearSrv = nullptr;
		if (clearRtv) *clearRtv = nullptr;
	}
	D3D11_TEXTURE2D_DESC nd = {};
	nd.Width = (UINT)w;
	nd.Height = (UINT)h;
	nd.MipLevels = 1;
	nd.ArraySize = 1;
	nd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	nd.SampleDesc.Count = 1;
	nd.Usage = D3D11_USAGE_DEFAULT;
	nd.BindFlags = bind;
	return SUCCEEDED(g_d3d->CreateTexture2D(&nd, nullptr, tex.put()));
}

static BOOL EnsureStaging(WgcSession& s, int w, int h)
{
	if (s.staging) {
		D3D11_TEXTURE2D_DESC sd = {};
		s.staging->GetDesc(&sd);
		if ((int)sd.Width == w && (int)sd.Height == h)
			return TRUE;
		s.staging = nullptr;
	}
	D3D11_TEXTURE2D_DESC nd = {};
	nd.Width = (UINT)w;
	nd.Height = (UINT)h;
	nd.MipLevels = 1;
	nd.ArraySize = 1;
	nd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	nd.SampleDesc.Count = 1;
	nd.Usage = D3D11_USAGE_STAGING;
	nd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	return SUCCEEDED(g_d3d->CreateTexture2D(&nd, nullptr, s.staging.put()));
}

static BOOL ScaleBgraTo(
	const BYTE* src, int srcW, int srcH,
	BYTE* dst, int dstW, int dstH, int dstStride)
{
	if (!src || !dst || srcW < 2 || srcH < 2 || dstW < 2 || dstH < 2)
		return FALSE;
	const int srcStride = srcW * 4;
	if (srcW == dstW && srcH == dstH) {
		for (int y = 0; y < dstH; ++y)
			memcpy(dst + (size_t)y * (size_t)dstStride, src + (size_t)y * (size_t)srcStride, (size_t)dstW * 4);
		return TRUE;
	}
	for (int y = 0; y < dstH; ++y) {
		const int sy = (int)(((__int64)y * srcH) / dstH);
		const BYTE* srow = src + (size_t)sy * (size_t)srcStride;
		BYTE* drow = dst + (size_t)y * (size_t)dstStride;
		for (int x = 0; x < dstW; ++x) {
			const int sx = (int)(((__int64)x * srcW) / dstW);
			const BYTE* p = srow + (size_t)sx * 4;
			BYTE* q = drow + (size_t)x * 4;
			q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = 255;
		}
	}
	return TRUE;
}

// FrameArrived 用: GPU 縮小まで（Map しない）
static BOOL GpuScaleOnly(WgcSession& s, ID3D11Texture2D* src, int srcW, int srcH, int dstW, int dstH)
{
	if (!src || !g_d3d || !g_ctx || !EnsureShaders()) return FALSE;
	dstW = ScEven2(dstW);
	dstH = ScEven2(dstH);
	srcW = ScEven2(srcW);
	srcH = ScEven2(srcH);
	if (dstW < 2 || dstH < 2 || srcW < 2 || srcH < 2) return FALSE;

	std::lock_guard<std::mutex> ctxLock(g_ctxMtx);

	if (dstW == srcW && dstH == srcH) {
		if (!EnsureTex(s.gpuScaled, dstW, dstH,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, nullptr, &s.rtv))
			return FALSE;
		g_ctx->CopyResource(s.gpuScaled.get(), src);
		s.scaledW = dstW;
		s.scaledH = dstH;
		s.gpuReady = true;
		s.gpuGen.fetch_add(1);
		if (s.frameEvent) SetEvent(s.frameEvent);
		return TRUE;
	}

	if (!EnsureTex(s.gpuInput, srcW, srcH,
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &s.srv, nullptr))
		return FALSE;
	if (!EnsureTex(s.gpuScaled, dstW, dstH,
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, nullptr, &s.rtv))
		return FALSE;

	{
		D3D11_BOX box = {};
		box.right = (UINT)srcW;
		box.bottom = (UINT)srcH;
		box.back = 1;
		g_ctx->CopySubresourceRegion(s.gpuInput.get(), 0, 0, 0, 0, src, 0, &box);
	}

	if (!s.srv && FAILED(g_d3d->CreateShaderResourceView(s.gpuInput.get(), nullptr, s.srv.put())))
		return FALSE;
	if (!s.rtv && FAILED(g_d3d->CreateRenderTargetView(s.gpuScaled.get(), nullptr, s.rtv.put())))
		return FALSE;

	ID3D11RenderTargetView* rtv = s.rtv.get();
	const float clear[4] = { 0, 0, 0, 1 };
	g_ctx->ClearRenderTargetView(rtv, clear);
	g_ctx->OMSetRenderTargets(1, &rtv, nullptr);

	D3D11_VIEWPORT vp = {};
	vp.Width = (float)dstW;
	vp.Height = (float)dstH;
	vp.MinDepth = 0.f;
	vp.MaxDepth = 1.f;
	g_ctx->RSSetViewports(1, &vp);
	g_ctx->RSSetState(g_rs.get());

	float blendFactor[4] = { 0, 0, 0, 0 };
	g_ctx->OMSetBlendState(g_bs.get(), blendFactor, 0xffffffff);
	g_ctx->OMSetDepthStencilState(nullptr, 0);

	g_ctx->VSSetShader(g_vs.get(), nullptr, 0);
	g_ctx->PSSetShader(g_ps.get(), nullptr, 0);
	ID3D11ShaderResourceView* srv = s.srv.get();
	g_ctx->PSSetShaderResources(0, 1, &srv);
	ID3D11SamplerState* samp = g_samp.get();
	g_ctx->PSSetSamplers(0, 1, &samp);
	g_ctx->IASetInputLayout(nullptr);
	g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_ctx->Draw(3, 0);

	ID3D11ShaderResourceView* nullSrv = nullptr;
	g_ctx->PSSetShaderResources(0, 1, &nullSrv);
	ID3D11RenderTargetView* nullRtv = nullptr;
	g_ctx->OMSetRenderTargets(1, &nullRtv, nullptr);

	s.scaledW = dstW;
	s.scaledH = dstH;
	s.gpuReady = true;
	s.gpuGen.fetch_add(1);
	if (s.frameEvent) SetEvent(s.frameEvent);
	return TRUE;
}

// Compose スレッド用: 縮小済み GPU テクスチャを CPU へ
static BOOL ReadbackScaledToCpu(WgcSession& s)
{
	if (!s.gpuReady || !s.gpuScaled || s.scaledW < 2 || s.scaledH < 2)
		return FALSE;
	const unsigned gen = s.gpuGen.load();
	if (s.hasFrame && s.cpuGen == gen)
		return TRUE; // 同じ GPU フレームなら再利用

	std::lock_guard<std::mutex> ctxLock(g_ctxMtx);
	const int w = s.scaledW;
	const int h = s.scaledH;
	if (!EnsureStaging(s, w, h))
		return FALSE;
	g_ctx->CopyResource(s.staging.get(), s.gpuScaled.get());

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(g_ctx->Map(s.staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
		return FALSE;
	const int stride = w * 4;
	{
		std::lock_guard<std::mutex> lock(s.frameMtx);
		s.bgra.resize((size_t)stride * (size_t)h);
		const BYTE* srcRow = (const BYTE*)mapped.pData;
		BYTE* dstRow = s.bgra.data();
		for (int y = 0; y < h; ++y) {
			memcpy(dstRow, srcRow, (size_t)stride);
			srcRow += mapped.RowPitch;
			dstRow += stride;
		}
		s.contentW = w;
		s.contentH = h;
		s.hasFrame = true;
		s.cpuGen = gen;
	}
	g_ctx->Unmap(s.staging.get(), 0);
	return TRUE;
}

static void OnGpuFrame(WgcSession& s, Direct3D11CaptureFrame const& frame)
{
	SizeInt32 cs = frame.ContentSize();
	const int nativeW = ScEven2((int)cs.Width);
	const int nativeH = ScEven2((int)cs.Height);
	if (nativeW >= 2 && nativeH >= 2) {
		const int adw = nativeW > s.poolW ? nativeW - s.poolW : s.poolW - nativeW;
		const int adh = nativeH > s.poolH ? nativeH - s.poolH : s.poolH - nativeH;
		if (adw > 2 || adh > 2) {
			const DWORD now = GetTickCount();
			if (now - s.lastPoolResizeTick >= 300) {
				std::lock_guard<std::mutex> glock(g_mtx);
				if (!s.closed && RecreatePool_NoLock(s, nativeW, nativeH))
					s.lastPoolResizeTick = now;
			}
			return;
		}
	}

	auto surface = frame.Surface();
	com_ptr<ID3D11Texture2D> src = TextureFromSurface(surface);
	if (!src) return;

	D3D11_TEXTURE2D_DESC desc = {};
	src->GetDesc(&desc);
	int srcW = ScEven2((int)desc.Width);
	int srcH = ScEven2((int)desc.Height);
	if (nativeW >= 2 && nativeW <= srcW) srcW = nativeW;
	if (nativeH >= 2 && nativeH <= srcH) srcH = nativeH;
	if (srcW < 2 || srcH < 2) return;

	int dstW = ScEven2(s.wantW.load());
	int dstH = ScEven2(s.wantH.load());
	if (dstW < 2 || dstH < 2) {
		dstW = srcW;
		dstH = srcH;
		if (dstW > 1920) {
			dstH = ScEven2((int)(((__int64)dstH * 1920) / dstW));
			dstW = 1920;
		}
	}

	GpuScaleOnly(s, src.get(), srcW, srcH, dstW, dstH);
}

static void OnFrameArrived(WgcSession* s, Direct3D11CaptureFramePool const& sender)
{
	if (!s || s->closed) return;
	Direct3D11CaptureFrame frame = sender.TryGetNextFrame();
	if (!frame) return;
	try {
		OnGpuFrame(*s, frame);
	} catch (...) {
	}
}

static void DestroySession_NoLock(HWND hwnd)
{
	auto it = g_sessions.find(hwnd);
	if (it == g_sessions.end()) return;
	WgcSession* s = it->second.get();
	s->closed = true;
	try {
		if (s->pool && s->arrivedToken.value)
			s->pool.FrameArrived(s->arrivedToken);
		if (s->item && s->closedToken.value)
			s->item.Closed(s->closedToken);
		if (s->session) s->session.Close();
		if (s->pool) s->pool.Close();
	} catch (...) {
	}
	if (s->frameEvent) {
		CloseHandle(s->frameEvent);
		s->frameEvent = nullptr;
	}
	g_sessions.erase(it);
}

static WgcSession* EnsureSession(HWND hwnd)
{
	if (!hwnd || !IsWindow(hwnd) || IsIconic(hwnd)) return nullptr;
	if (!EnsureD3D()) return nullptr;

	std::lock_guard<std::mutex> lock(g_mtx);
	auto it = g_sessions.find(hwnd);
	if (it != g_sessions.end()) {
		WgcSession* s = it->second.get();
		if (s->closed || !IsWindow(hwnd))
			DestroySession_NoLock(hwnd);
		else
			return s;
	}

	try {
		auto s = std::make_unique<WgcSession>();
		s->hwnd = hwnd;
		s->frameEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		s->item = CreateItemForWindow(hwnd);
		SizeInt32 itemSize = s->item.Size();
		const int poolW = ScEven2((int)itemSize.Width);
		const int poolH = ScEven2((int)itemSize.Height);
		if (poolW < 2 || poolH < 2)
			return nullptr;

		s->poolW = poolW;
		s->poolH = poolH;
		s->pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
			g_winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, SizeInt32{ poolW, poolH });

		WgcSession* raw = s.get();
		s->arrivedToken = s->pool.FrameArrived(
			[raw](Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&) {
				OnFrameArrived(raw, sender);
			});
		s->closedToken = s->item.Closed(
			[hwnd](GraphicsCaptureItem const&, winrt::Windows::Foundation::IInspectable const&) {
				std::lock_guard<std::mutex> lk(g_mtx);
				DestroySession_NoLock(hwnd);
			});

		s->session = s->pool.CreateCaptureSession(s->item);
		if (ApiInformation::IsPropertyPresent(
			L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled"))
			s->session.IsCursorCaptureEnabled(false);
		if (ApiInformation::IsPropertyPresent(
			L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired"))
			s->session.IsBorderRequired(false);
		s->session.StartCapture();

		WgcSession* out = s.get();
		g_sessions.emplace(hwnd, std::move(s));
		return out;
	} catch (...) {
		return nullptr;
	}
}

} // namespace

BOOL ScWgcCaptureWindowBgra(HWND hwnd, BYTE* dstBgra, int dstW, int dstH, int dstStride)
{
	if (!hwnd || !dstBgra || !IsWindow(hwnd)) return FALSE;
	dstW &= ~1;
	dstH &= ~1;
	if (dstW < 2 || dstH < 2 || dstStride < dstW * 4) return FALSE;

	WgcSession* s = EnsureSession(hwnd);
	if (!s) return FALSE;

	s->wantW.store(dstW);
	s->wantH.store(dstH);

	if (!s->gpuReady && s->frameEvent)
		WaitForSingleObject(s->frameEvent, 250);

	if (!ReadbackScaledToCpu(*s))
		return FALSE;

	std::lock_guard<std::mutex> lock(s->frameMtx);
	if (!s->hasFrame || s->bgra.empty() || s->contentW < 2 || s->contentH < 2)
		return FALSE;
	return ScaleBgraTo(
		s->bgra.data(), s->contentW, s->contentH,
		dstBgra, dstW, dstH, dstStride);
}

void ScWgcShutdown(void)
{
	std::lock_guard<std::mutex> lock(g_mtx);
	std::vector<HWND> keys;
	keys.reserve(g_sessions.size());
	for (auto& kv : g_sessions)
		keys.push_back(kv.first);
	for (HWND h : keys)
		DestroySession_NoLock(h);
	g_vs = nullptr;
	g_ps = nullptr;
	g_samp = nullptr;
	g_rs = nullptr;
	g_bs = nullptr;
	g_shaderReady = FALSE;
	g_winrtDevice = nullptr;
	g_ctx = nullptr;
	g_d3d = nullptr;
}
