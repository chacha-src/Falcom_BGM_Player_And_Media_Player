// ScWgcCapture.cpp
// Windows.Graphics.Capture (HWND / HMONITOR)
// プール=実サイズ → GPU シェーダで出力サイズへ縮小 → Compose 時に小さく Map
// FrameArrived では Map しない（コールバックでの GPU 同期待ちを避ける）
// キャプチャテクスチャへ直接 SRV を張り、中間 Copy を省略（可能な場合）

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
#include <cmath>
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

enum class WgcTargetKind { Window, Monitor };

struct WgcSession {
	WgcTargetKind kind = WgcTargetKind::Window;
	HWND hwnd = nullptr;
	HMONITOR monitor = nullptr;
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
	std::atomic<int> cropX{ 0 };
	std::atomic<int> cropY{ 0 };
	std::atomic<int> cropW{ 0 }; // 0=全体
	std::atomic<int> cropH{ 0 };
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
static std::unordered_map<HWND, std::unique_ptr<WgcSession>> g_winSessions;
static std::unordered_map<HMONITOR, std::unique_ptr<WgcSession>> g_monSessions;
static bool g_apartmentReady = false;

static com_ptr<ID3D11VertexShader> g_vs;
static com_ptr<ID3D11PixelShader> g_ps;
static com_ptr<ID3D11SamplerState> g_sampLinear;
static com_ptr<ID3D11SamplerState> g_sampPoint;
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

	// UV を [0,1] に収めたフルスクリーン三角形。SampleLevel(lod=0) でミップ不要。
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
	sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.MaxLOD = D3D11_FLOAT32_MAX;
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	if (FAILED(g_d3d->CreateSamplerState(&sd, g_sampLinear.put())))
		return FALSE;
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	if (FAILED(g_d3d->CreateSamplerState(&sd, g_sampPoint.put())))
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

static GraphicsCaptureItem CreateItemForMonitor(HMONITOR mon)
{
	auto factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
	GraphicsCaptureItem item{ nullptr };
	winrt::check_hresult(factory->CreateForMonitor(
		mon, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
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

static BOOL ScaleBgraCropTo(
	const BYTE* src, int srcW, int srcH,
	int cropX, int cropY, int cropW, int cropH,
	BYTE* dst, int dstW, int dstH, int dstStride);
static BOOL ScaleBgraTo(
	const BYTE* src, int srcW, int srcH,
	BYTE* dst, int dstW, int dstH, int dstStride)
{
	return ScaleBgraCropTo(src, srcW, srcH, 0, 0, srcW, srcH, dst, dstW, dstH, dstStride);
}

static BOOL ScaleBgraCropTo(
	const BYTE* src, int srcW, int srcH,
	int cropX, int cropY, int cropW, int cropH,
	BYTE* dst, int dstW, int dstH, int dstStride)
{
	if (!src || !dst || srcW < 2 || srcH < 2 || dstW < 2 || dstH < 2)
		return FALSE;
	if (cropW <= 0 || cropH <= 0) {
		cropX = 0; cropY = 0; cropW = srcW; cropH = srcH;
	}
	if (cropX < 0) cropX = 0;
	if (cropY < 0) cropY = 0;
	if (cropX + cropW > srcW) cropW = srcW - cropX;
	if (cropY + cropH > srcH) cropH = srcH - cropY;
	if (cropW < 2 || cropH < 2) return FALSE;
	const int srcStride = srcW * 4;
	if (cropW == dstW && cropH == dstH) {
		for (int y = 0; y < dstH; ++y) {
			memcpy(dst + (size_t)y * (size_t)dstStride,
				src + (size_t)(cropY + y) * (size_t)srcStride + (size_t)cropX * 4,
				(size_t)dstW * 4);
		}
		return TRUE;
	}
	for (int y = 0; y < dstH; ++y) {
		const int sy = cropY + (int)(((__int64)y * cropH) / dstH);
		const BYTE* srow = src + (size_t)sy * (size_t)srcStride;
		BYTE* drow = dst + (size_t)y * (size_t)dstStride;
		for (int x = 0; x < dstW; ++x) {
			const int sx = cropX + (int)(((__int64)x * cropW) / dstW);
			const BYTE* p = srow + (size_t)sx * 4;
			BYTE* q = drow + (size_t)x * 4;
			q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = 255;
		}
	}
	return TRUE;
}

static BOOL DrawScaleToRtv(ID3D11ShaderResourceView* srcSrv, ID3D11RenderTargetView* rtv,
	int dstW, int dstH, BOOL usePoint)
{
	if (!srcSrv || !rtv || !g_ctx || !g_vs || !g_ps) return FALSE;
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
	g_ctx->PSSetShaderResources(0, 1, &srcSrv);
	ID3D11SamplerState* samp = (usePoint ? g_sampPoint : g_sampLinear).get();
	g_ctx->PSSetSamplers(0, 1, &samp);
	g_ctx->IASetInputLayout(nullptr);
	g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_ctx->Draw(3, 0);

	ID3D11ShaderResourceView* nullSrv = nullptr;
	g_ctx->PSSetShaderResources(0, 1, &nullSrv);
	ID3D11RenderTargetView* nullRtv = nullptr;
	g_ctx->OMSetRenderTargets(1, &nullRtv, nullptr);
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

	if (!EnsureTex(s.gpuScaled, dstW, dstH,
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, nullptr, &s.rtv))
		return FALSE;
	if (!s.rtv && FAILED(g_d3d->CreateRenderTargetView(s.gpuScaled.get(), nullptr, s.rtv.put())))
		return FALSE;

	// キャプチャ面へ直接 SRV（可能な場合は中間 Copy を省略）
	com_ptr<ID3D11ShaderResourceView> directSrv;
	ID3D11ShaderResourceView* drawSrv = nullptr;
	if (SUCCEEDED(g_d3d->CreateShaderResourceView(src, nullptr, directSrv.put())) && directSrv) {
		drawSrv = directSrv.get();
	} else {
		if (!EnsureTex(s.gpuInput, srcW, srcH,
			D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, &s.srv, nullptr))
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
		drawSrv = s.srv.get();
	}

	// 整数倍縮小ならポイント、それ以外はバイリニア
	const BOOL integerScale =
		(srcW % dstW == 0) && (srcH % dstH == 0) &&
		(srcW / dstW >= 2 || srcH / dstH >= 2);
	if (!DrawScaleToRtv(drawSrv, s.rtv.get(), dstW, dstH, integerScale))
		return FALSE;

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
		return TRUE;

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

static void DestroyWinSession_NoLock(HWND hwnd)
{
	auto it = g_winSessions.find(hwnd);
	if (it == g_winSessions.end()) return;
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
	g_winSessions.erase(it);
}

static void DestroyMonSession_NoLock(HMONITOR mon)
{
	auto it = g_monSessions.find(mon);
	if (it == g_monSessions.end()) return;
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
	g_monSessions.erase(it);
}

static BOOL StartSessionCommon(WgcSession& s)
{
	SizeInt32 itemSize = s.item.Size();
	const int poolW = ScEven2((int)itemSize.Width);
	const int poolH = ScEven2((int)itemSize.Height);
	if (poolW < 2 || poolH < 2)
		return FALSE;

	s.poolW = poolW;
	s.poolH = poolH;
	s.pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
		g_winrtDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, SizeInt32{ poolW, poolH });

	WgcSession* raw = &s;
	s.arrivedToken = s.pool.FrameArrived(
		[raw](Direct3D11CaptureFramePool const& sender, winrt::Windows::Foundation::IInspectable const&) {
			OnFrameArrived(raw, sender);
		});

	s.session = s.pool.CreateCaptureSession(s.item);
	if (ApiInformation::IsPropertyPresent(
		L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled"))
		s.session.IsCursorCaptureEnabled(false);
	if (ApiInformation::IsPropertyPresent(
		L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired"))
		s.session.IsBorderRequired(false);
	s.session.StartCapture();
	return TRUE;
}

static WgcSession* EnsureWindowSession(HWND hwnd)
{
	if (!hwnd || !IsWindow(hwnd) || IsIconic(hwnd)) return nullptr;
	if (!EnsureD3D()) return nullptr;

	std::lock_guard<std::mutex> lock(g_mtx);
	auto it = g_winSessions.find(hwnd);
	if (it != g_winSessions.end()) {
		WgcSession* s = it->second.get();
		if (s->closed || !IsWindow(hwnd))
			DestroyWinSession_NoLock(hwnd);
		else
			return s;
	}

	try {
		auto s = std::make_unique<WgcSession>();
		s->kind = WgcTargetKind::Window;
		s->hwnd = hwnd;
		s->frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		s->item = CreateItemForWindow(hwnd);
		s->closedToken = s->item.Closed(
			[hwnd](GraphicsCaptureItem const&, winrt::Windows::Foundation::IInspectable const&) {
				std::lock_guard<std::mutex> lk(g_mtx);
				DestroyWinSession_NoLock(hwnd);
			});
		if (!StartSessionCommon(*s))
			return nullptr;
		WgcSession* out = s.get();
		g_winSessions.emplace(hwnd, std::move(s));
		return out;
	} catch (...) {
		return nullptr;
	}
}

static WgcSession* EnsureMonitorSession(HMONITOR mon)
{
	if (!mon) return nullptr;
	if (!EnsureD3D()) return nullptr;

	std::lock_guard<std::mutex> lock(g_mtx);
	auto it = g_monSessions.find(mon);
	if (it != g_monSessions.end()) {
		WgcSession* s = it->second.get();
		if (s->closed)
			DestroyMonSession_NoLock(mon);
		else
			return s;
	}

	try {
		auto s = std::make_unique<WgcSession>();
		s->kind = WgcTargetKind::Monitor;
		s->monitor = mon;
		s->frameEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		s->item = CreateItemForMonitor(mon);
		s->closedToken = s->item.Closed(
			[mon](GraphicsCaptureItem const&, winrt::Windows::Foundation::IInspectable const&) {
				std::lock_guard<std::mutex> lk(g_mtx);
				DestroyMonSession_NoLock(mon);
			});
		if (!StartSessionCommon(*s))
			return nullptr;
		WgcSession* out = s.get();
		g_monSessions.emplace(mon, std::move(s));
		return out;
	} catch (...) {
		return nullptr;
	}
}

static BOOL CaptureSessionToBgra(WgcSession* s, BYTE* dstBgra, int dstW, int dstH, int dstStride,
	int cropX, int cropY, int cropW, int cropH)
{
	if (!s || !dstBgra) return FALSE;
	dstW &= ~1;
	dstH &= ~1;
	if (dstW < 2 || dstH < 2 || dstStride < dstW * 4) return FALSE;

	// 切り出し時はまずウィンドウ全体を GPU 縮小してから CPU で矩形抽出（UV CB なしで確実）
	const BOOL useCrop = (cropW > 1 && cropH > 1);
	if (useCrop) {
		// 全体を適度な解像度で取り、切り出し精度を保つ
		int fullW = ScEven2(s->poolW > 0 ? s->poolW : dstW);
		int fullH = ScEven2(s->poolH > 0 ? s->poolH : dstH);
		if (fullW > 1920) {
			fullH = ScEven2((int)(((__int64)fullH * 1920) / fullW));
			fullW = 1920;
		}
		s->wantW.store(fullW);
		s->wantH.store(fullH);
	} else {
		s->wantW.store(dstW);
		s->wantH.store(dstH);
	}

	// プレビュー用: 溜まっているフレームを1枚だけ引く（長い Wait は FPS を半減させるのでしない）
	if (s->pool && !s->closed) {
		try {
			Direct3D11CaptureFrame frame = s->pool.TryGetNextFrame();
			if (frame) OnGpuFrame(*s, frame);
		} catch (...) {
		}
	}

	if (!s->gpuReady && s->frameEvent)
		WaitForSingleObject(s->frameEvent, 100);

	if (!ReadbackScaledToCpu(*s))
		return FALSE;

	std::lock_guard<std::mutex> lock(s->frameMtx);
	if (!s->hasFrame || s->bgra.empty() || s->contentW < 2 || s->contentH < 2)
		return FALSE;
	if (useCrop) {
		// crop はウィンドウ実寸基準 → content サイズへスケール
		const int pw = s->poolW > 0 ? s->poolW : s->contentW;
		const int ph = s->poolH > 0 ? s->poolH : s->contentH;
		int sx = (int)(((__int64)cropX * s->contentW) / (pw > 0 ? pw : 1));
		int sy = (int)(((__int64)cropY * s->contentH) / (ph > 0 ? ph : 1));
		int sw = (int)(((__int64)cropW * s->contentW) / (pw > 0 ? pw : 1));
		int sh = (int)(((__int64)cropH * s->contentH) / (ph > 0 ? ph : 1));
		return ScaleBgraCropTo(
			s->bgra.data(), s->contentW, s->contentH,
			sx, sy, sw, sh,
			dstBgra, dstW, dstH, dstStride);
	}
	return ScaleBgraTo(
		s->bgra.data(), s->contentW, s->contentH,
		dstBgra, dstW, dstH, dstStride);
}

} // namespace

BOOL ScWgcCaptureWindowBgra(HWND hwnd, BYTE* dstBgra, int dstW, int dstH, int dstStride)
{
	return ScWgcCaptureWindowBgraCrop(hwnd, dstBgra, dstW, dstH, dstStride, 0, 0, 0, 0);
}

BOOL ScWgcCaptureWindowBgraCrop(HWND hwnd, BYTE* dstBgra, int dstW, int dstH, int dstStride,
	int cropX, int cropY, int cropW, int cropH)
{
	if (!hwnd || !dstBgra || !IsWindow(hwnd)) return FALSE;
	WgcSession* s = EnsureWindowSession(hwnd);
	if (!s) return FALSE;
	return CaptureSessionToBgra(s, dstBgra, dstW, dstH, dstStride, cropX, cropY, cropW, cropH);
}

BOOL ScWgcCaptureMonitorBgra(HMONITOR mon, BYTE* dstBgra, int dstW, int dstH, int dstStride)
{
	if (!mon || !dstBgra) return FALSE;
	WgcSession* s = EnsureMonitorSession(mon);
	if (!s) return FALSE;
	return CaptureSessionToBgra(s, dstBgra, dstW, dstH, dstStride, 0, 0, 0, 0);
}

static void ScFxCpuGray(BYTE* bgra, int w, int h, int stride)
{
	for (int y = 0; y < h; ++y) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		for (int x = 0; x < w; ++x) {
			BYTE* p = row + (size_t)x * 4;
			const int g = (p[0] * 29 + p[1] * 150 + p[2] * 77) >> 8;
			p[0] = p[1] = p[2] = (BYTE)g;
		}
	}
}

static void ScFxCpuSepia(BYTE* bgra, int w, int h, int stride)
{
	for (int y = 0; y < h; ++y) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		for (int x = 0; x < w; ++x) {
			BYTE* p = row + (size_t)x * 4;
			const int B = p[0], G = p[1], R = p[2];
			int r = (R * 393 + G * 769 + B * 189) >> 10;
			int g = (R * 349 + G * 686 + B * 168) >> 10;
			int b = (R * 272 + G * 534 + B * 131) >> 10;
			if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
			p[2] = (BYTE)r; p[1] = (BYTE)g; p[0] = (BYTE)b;
		}
	}
}

static void ScFxCpuVignette(BYTE* bgra, int w, int h, int stride)
{
	const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
	const float maxd = sqrtf(cx * cx + cy * cy);
	for (int y = 0; y < h; ++y) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		for (int x = 0; x < w; ++x) {
			const float dx = (x - cx) / maxd, dy = (y - cy) / maxd;
			float v = 1.f - (dx * dx + dy * dy);
			if (v < 0.15f) v = 0.15f;
			if (v > 1.f) v = 1.f;
			BYTE* p = row + (size_t)x * 4;
			p[0] = (BYTE)(p[0] * v); p[1] = (BYTE)(p[1] * v); p[2] = (BYTE)(p[2] * v);
		}
	}
}

static void ScFxCpuMirror(BYTE* bgra, int w, int h, int stride)
{
	for (int y = 0; y < h; ++y) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		for (int x = 0; x < w / 2; ++x) {
			BYTE* a = row + (size_t)x * 4;
			BYTE* b = row + (size_t)(w - 1 - x) * 4;
			BYTE t0 = a[0], t1 = a[1], t2 = a[2], t3 = a[3];
			a[0] = b[0]; a[1] = b[1]; a[2] = b[2]; a[3] = b[3];
			b[0] = t0; b[1] = t1; b[2] = t2; b[3] = t3;
		}
	}
}

static void ScFxCpuSharpen(BYTE* bgra, int w, int h, int stride)
{
	// 4K では重いので薄い 3x3 のみ（GPU失敗時の保険）
	if ((size_t)w * (size_t)h > (size_t)1920 * 1200) return;
	std::vector<BYTE> tmp((size_t)stride * (size_t)h);
	memcpy(tmp.data(), bgra, (size_t)stride * (size_t)h);
	for (int y = 1; y < h - 1; ++y) {
		BYTE* drow = bgra + (size_t)y * (size_t)stride;
		for (int x = 1; x < w - 1; ++x) {
			const BYTE* c = tmp.data() + (size_t)y * (size_t)stride + (size_t)x * 4;
			const BYTE* u = c - stride, *dn = c + stride, *l = c - 4, *r = c + 4;
			for (int k = 0; k < 3; ++k) {
				int v = (int)c[k] * 5 - (int)u[k] - (int)dn[k] - (int)l[k] - (int)r[k];
				if (v < 0) v = 0; if (v > 255) v = 255;
				drow[(size_t)x * 4 + k] = (BYTE)v;
			}
		}
	}
}

static void ScFxCpuTint(BYTE* bgra, int w, int h, int stride, int addR, int addG, int addB, int sat)
{
	for (int y = 0; y < h; ++y) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		for (int x = 0; x < w; ++x) {
			BYTE* p = row + (size_t)x * 4;
			int B = p[0] + addB, G = p[1] + addG, R = p[2] + addR;
			if (sat != 0) {
				const int g = (B * 29 + G * 150 + R * 77) >> 8;
				R = g + ((R - g) * sat) / 100;
				G = g + ((G - g) * sat) / 100;
				B = g + ((B - g) * sat) / 100;
			}
			if (R < 0) R = 0; if (R > 255) R = 255;
			if (G < 0) G = 0; if (G > 255) G = 255;
			if (B < 0) B = 0; if (B > 255) B = 255;
			p[2] = (BYTE)R; p[1] = (BYTE)G; p[0] = (BYTE)B;
		}
	}
}

static void ScFxCpuPoster(BYTE* bgra, int w, int h, int stride)
{
	for (int y = 0; y < h; ++y) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		for (int x = 0; x < w; ++x) {
			BYTE* p = row + (size_t)x * 4;
			p[0] = (BYTE)((p[0] >> 5) << 5);
			p[1] = (BYTE)((p[1] >> 5) << 5);
			p[2] = (BYTE)((p[2] >> 5) << 5);
		}
	}
}

static void ScFxCpuScanline(BYTE* bgra, int w, int h, int stride)
{
	for (int y = 0; y < h; y += 2) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		for (int x = 0; x < w; ++x) {
			BYTE* p = row + (size_t)x * 4;
			p[0] = (BYTE)((p[0] * 55) >> 6);
			p[1] = (BYTE)((p[1] * 55) >> 6);
			p[2] = (BYTE)((p[2] * 55) >> 6);
		}
	}
}

static void ScFxCpuWaveLike(BYTE* bgra, int w, int h, int stride, float timeSec, BOOL underwater)
{
	if ((size_t)w * (size_t)h > (size_t)1600 * 1000) {
		// 大解像度は色味だけ（変位はGPU任せ）
		if (underwater) ScFxCpuTint(bgra, w, h, stride, -20, 10, 35, 90);
		return;
	}
	std::vector<BYTE> tmp((size_t)stride * (size_t)h);
	memcpy(tmp.data(), bgra, (size_t)stride * (size_t)h);
	const float ampX = underwater ? 0.012f : 0.008f;
	const float ampY = underwater ? 0.010f : 0.006f;
	for (int y = 0; y < h; ++y) {
		BYTE* drow = bgra + (size_t)y * (size_t)stride;
		const float fy = (float)y / (float)((h > 1) ? h : 1);
		for (int x = 0; x < w; ++x) {
			const float fx = (float)x / (float)((w > 1) ? w : 1);
			float u = fx + sinf(fy * (underwater ? 25.f : 40.f) + timeSec * (underwater ? 2.f : 4.f)) * ampX;
			float v = fy + (underwater
				? sinf(fx * 20.f + timeSec * 1.5f)
				: cosf(fx * 30.f + timeSec * 3.f)) * ampY;
			int sx = (int)(u * (w - 1) + 0.5f);
			int sy = (int)(v * (h - 1) + 0.5f);
			if (sx < 0) sx = 0; if (sx >= w) sx = w - 1;
			if (sy < 0) sy = 0; if (sy >= h) sy = h - 1;
			const BYTE* s = tmp.data() + (size_t)sy * (size_t)stride + (size_t)sx * 4;
			BYTE* d = drow + (size_t)x * 4;
			d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
		}
	}
	if (underwater) ScFxCpuTint(bgra, w, h, stride, -20, 10, 35, 90);
}

static void ScFxCpuInvert(BYTE* bgra, int w, int h, int stride)
{
	for (int y = 0; y < h; ++y) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		for (int x = 0; x < w; ++x) {
			BYTE* p = row + (size_t)x * 4;
			p[0] = (BYTE)(255 - p[0]); p[1] = (BYTE)(255 - p[1]); p[2] = (BYTE)(255 - p[2]);
		}
	}
}

static void ScFxCpuSolarize(BYTE* bgra, int w, int h, int stride)
{
	for (int y = 0; y < h; ++y) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		for (int x = 0; x < w; ++x) {
			BYTE* p = row + (size_t)x * 4;
			for (int k = 0; k < 3; ++k)
				if (p[k] > 128) p[k] = (BYTE)(255 - p[k]);
		}
	}
}

static void ScFxCpuPixelate(BYTE* bgra, int w, int h, int stride)
{
	const int bs = 12;
	if (w < bs * 2 || h < bs * 2) return;
	std::vector<BYTE> tmp((size_t)stride * (size_t)h);
	memcpy(tmp.data(), bgra, (size_t)stride * (size_t)h);
	for (int by = 0; by < h; by += bs) {
		for (int bx = 0; bx < w; bx += bs) {
			int r = 0, g = 0, b = 0, n = 0;
			const int x1 = (bx + bs < w) ? bx + bs : w;
			const int y1 = (by + bs < h) ? by + bs : h;
			for (int y = by; y < y1; ++y) {
				const BYTE* row = tmp.data() + (size_t)y * (size_t)stride;
				for (int x = bx; x < x1; ++x) {
					const BYTE* p = row + (size_t)x * 4;
					b += p[0]; g += p[1]; r += p[2]; ++n;
				}
			}
			if (n <= 0) continue;
			const BYTE B = (BYTE)(b / n), G = (BYTE)(g / n), R = (BYTE)(r / n);
			for (int y = by; y < y1; ++y) {
				BYTE* row = bgra + (size_t)y * (size_t)stride;
				for (int x = bx; x < x1; ++x) {
					BYTE* p = row + (size_t)x * 4;
					p[0] = B; p[1] = G; p[2] = R;
				}
			}
		}
	}
}

static void ScFxCpuFlipV(BYTE* bgra, int w, int h, int stride)
{
	std::vector<BYTE> row((size_t)w * 4);
	for (int y = 0; y < h / 2; ++y) {
		BYTE* a = bgra + (size_t)y * (size_t)stride;
		BYTE* b = bgra + (size_t)(h - 1 - y) * (size_t)stride;
		memcpy(row.data(), a, (size_t)w * 4);
		memcpy(a, b, (size_t)w * 4);
		memcpy(b, row.data(), (size_t)w * 4);
	}
}

static void ScFxCpuNoise(BYTE* bgra, int w, int h, int stride, float timeSec)
{
	const unsigned seed = (unsigned)(timeSec * 1000.f) ^ 0xA5A5u;
	for (int y = 0; y < h; ++y) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		unsigned s = seed + (unsigned)y * 374761393u;
		for (int x = 0; x < w; ++x) {
			s = s * 1664525u + 1013904223u;
			const int n = (int)((s >> 24) & 31) - 15;
			BYTE* p = row + (size_t)x * 4;
			for (int k = 0; k < 3; ++k) {
				int v = (int)p[k] + n;
				if (v < 0) v = 0; if (v > 255) v = 255;
				p[k] = (BYTE)v;
			}
		}
	}
}

static void ScFxCpuContrast(BYTE* bgra, int w, int h, int stride, int amount /*100=identity*/)
{
	for (int y = 0; y < h; ++y) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		for (int x = 0; x < w; ++x) {
			BYTE* p = row + (size_t)x * 4;
			for (int k = 0; k < 3; ++k) {
				int v = (((int)p[k] - 128) * amount) / 100 + 128;
				if (v < 0) v = 0; if (v > 255) v = 255;
				p[k] = (BYTE)v;
			}
		}
	}
}

static void ScFxCpuBrightness(BYTE* bgra, int w, int h, int stride, int add)
{
	for (int y = 0; y < h; ++y) {
		BYTE* row = bgra + (size_t)y * (size_t)stride;
		for (int x = 0; x < w; ++x) {
			BYTE* p = row + (size_t)x * 4;
			for (int k = 0; k < 3; ++k) {
				int v = (int)p[k] + add;
				if (v < 0) v = 0; if (v > 255) v = 255;
				p[k] = (BYTE)v;
			}
		}
	}
}

static void ScFxCpuNightVision(BYTE* bgra, int w, int h, int stride, float timeSec)
{
	ScFxCpuGray(bgra, w, h, stride);
	ScFxCpuTint(bgra, w, h, stride, -40, 50, -40, 100);
	ScFxCpuVignette(bgra, w, h, stride);
	ScFxCpuNoise(bgra, w, h, stride, timeSec);
}

static void ScFxCpuOne(BYTE* bgra, int w, int h, int stride, int effect, float timeSec)
{
	switch (effect) {
	case SC_FX_BLUR_SOFT:
	case SC_FX_BLUR_STRONG:
	case SC_FX_BLUR_MEGA:
	case SC_FX_MOTION_BLUR:
	case SC_FX_RADIAL_BLUR:
	case SC_FX_ZOOM_BLUR:
	case SC_FX_GODRAYS:
	case SC_FX_BLOOM:
		// 重量系は GPU 任せ（失敗時は素通し）
		break;
	case SC_FX_GRAY: ScFxCpuGray(bgra, w, h, stride); break;
	case SC_FX_SEPIA: ScFxCpuSepia(bgra, w, h, stride); break;
	case SC_FX_VIGNETTE: ScFxCpuVignette(bgra, w, h, stride); break;
	case SC_FX_SHARPEN: ScFxCpuSharpen(bgra, w, h, stride); break;
	case SC_FX_MIRROR: ScFxCpuMirror(bgra, w, h, stride); break;
	case SC_FX_WAVE: ScFxCpuWaveLike(bgra, w, h, stride, timeSec, FALSE); break;
	case SC_FX_UNDERWATER: ScFxCpuWaveLike(bgra, w, h, stride, timeSec, TRUE); break;
	case SC_FX_DUSK: ScFxCpuTint(bgra, w, h, stride, 40, 10, -25, 110); break;
	case SC_FX_COOL: ScFxCpuTint(bgra, w, h, stride, -15, 5, 35, 100); break;
	case SC_FX_WARM: ScFxCpuTint(bgra, w, h, stride, 35, 15, -10, 105); break;
	case SC_FX_POSTER: ScFxCpuPoster(bgra, w, h, stride); break;
	case SC_FX_SCANLINE: ScFxCpuScanline(bgra, w, h, stride); break;
	case SC_FX_EDGE: ScFxCpuSharpen(bgra, w, h, stride); break;
	case SC_FX_INVERT: ScFxCpuInvert(bgra, w, h, stride); break;
	case SC_FX_SOLARIZE: ScFxCpuSolarize(bgra, w, h, stride); break;
	case SC_FX_PIXELATE: ScFxCpuPixelate(bgra, w, h, stride); break;
	case SC_FX_FLIP_V: ScFxCpuFlipV(bgra, w, h, stride); break;
	case SC_FX_NOISE: ScFxCpuNoise(bgra, w, h, stride, timeSec); break;
	case SC_FX_NEON:
		ScFxCpuSharpen(bgra, w, h, stride);
		ScFxCpuTint(bgra, w, h, stride, -20, 40, 60, 140);
		break;
	case SC_FX_NIGHTVISION: ScFxCpuNightVision(bgra, w, h, stride, timeSec); break;
	case SC_FX_COMIC:
		ScFxCpuPoster(bgra, w, h, stride);
		ScFxCpuContrast(bgra, w, h, stride, 140);
		break;
	case SC_FX_RETRO:
		ScFxCpuSepia(bgra, w, h, stride);
		ScFxCpuScanline(bgra, w, h, stride);
		ScFxCpuVignette(bgra, w, h, stride);
		break;
	case SC_FX_FISHEYE:
		ScFxCpuWaveLike(bgra, w, h, stride, 0.f, FALSE); // 簡易: 変位系はGPU
		break;
	case SC_FX_HUE_SHIFT: ScFxCpuTint(bgra, w, h, stride, 25, -10, -15, 115); break;
	case SC_FX_CONTRAST: ScFxCpuContrast(bgra, w, h, stride, 145); break;
	case SC_FX_BRIGHTNESS: ScFxCpuBrightness(bgra, w, h, stride, 28); break;
	case SC_FX_SATURATE: ScFxCpuTint(bgra, w, h, stride, 0, 0, 0, 160); break;
	case SC_FX_DESAT: ScFxCpuTint(bgra, w, h, stride, 0, 0, 0, 45); break;
	case SC_FX_THRESHOLD: {
		for (int y = 0; y < h; ++y) {
			BYTE* row = bgra + (size_t)y * (size_t)stride;
			for (int x = 0; x < w; ++x) {
				BYTE* p = row + (size_t)x * 4;
				const int g = (p[0] * 29 + p[1] * 150 + p[2] * 77) >> 8;
				const BYTE v = (g > 128) ? 255 : 0;
				p[0] = p[1] = p[2] = v;
			}
		}
		break;
	}
	case SC_FX_RED_CAST: ScFxCpuTint(bgra, w, h, stride, 45, -15, -20, 110); break;
	case SC_FX_GREEN_CAST: ScFxCpuTint(bgra, w, h, stride, -20, 40, -15, 110); break;
	case SC_FX_BLUE_CAST: ScFxCpuTint(bgra, w, h, stride, -20, -10, 50, 110); break;
	case SC_FX_CYAN: ScFxCpuTint(bgra, w, h, stride, -25, 20, 35, 105); break;
	case SC_FX_MAGENTA: ScFxCpuTint(bgra, w, h, stride, 35, -20, 30, 110); break;
	case SC_FX_YELLOW: ScFxCpuTint(bgra, w, h, stride, 30, 25, -30, 110); break;
	case SC_FX_TEAL_ORANGE: ScFxCpuTint(bgra, w, h, stride, 25, 5, -20, 120); break;
	case SC_FX_FADE:
		ScFxCpuBrightness(bgra, w, h, stride, 18);
		ScFxCpuTint(bgra, w, h, stride, 8, 8, 8, 70);
		break;
	case SC_FX_SPOTLIGHT: {
		const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
		const float maxd = sqrtf(cx * cx + cy * cy);
		for (int y = 0; y < h; ++y) {
			BYTE* row = bgra + (size_t)y * (size_t)stride;
			for (int x = 0; x < w; ++x) {
				const float dx = (x - cx) / maxd, dy = (y - cy) / maxd;
				float v = 1.15f - (dx * dx + dy * dy) * 1.4f;
				if (v < 0.25f) v = 0.25f;
				if (v > 1.25f) v = 1.25f;
				BYTE* p = row + (size_t)x * 4;
				for (int k = 0; k < 3; ++k) {
					int t = (int)(p[k] * v);
					if (t > 255) t = 255;
					p[k] = (BYTE)t;
				}
			}
		}
		break;
	}
	case SC_FX_BARS_H:
		for (int y = 0; y < h; y += 4) {
			BYTE* row = bgra + (size_t)y * (size_t)stride;
			for (int x = 0; x < w; ++x) {
				BYTE* p = row + (size_t)x * 4;
				p[0] = (BYTE)((p[0] * 50) >> 6);
				p[1] = (BYTE)((p[1] * 50) >> 6);
				p[2] = (BYTE)((p[2] * 50) >> 6);
			}
		}
		break;
	case SC_FX_BARS_V:
		for (int y = 0; y < h; ++y) {
			BYTE* row = bgra + (size_t)y * (size_t)stride;
			for (int x = 0; x < w; x += 4) {
				BYTE* p = row + (size_t)x * 4;
				p[0] = (BYTE)((p[0] * 50) >> 6);
				p[1] = (BYTE)((p[1] * 50) >> 6);
				p[2] = (BYTE)((p[2] * 50) >> 6);
			}
		}
		break;
	case SC_FX_CHROMA:
		ScFxCpuTint(bgra, w, h, stride, 15, 0, -15, 120);
		break;
	case SC_FX_EMBOSS: ScFxCpuSharpen(bgra, w, h, stride); break;
	case SC_FX_LIFT: ScFxCpuBrightness(bgra, w, h, stride, 20); ScFxCpuContrast(bgra, w, h, stride, 90); break;
	case SC_FX_MONO_BLUE:
		ScFxCpuGray(bgra, w, h, stride);
		ScFxCpuTint(bgra, w, h, stride, -15, 5, 40, 100);
		break;
	case SC_FX_MONO_GREEN:
		ScFxCpuGray(bgra, w, h, stride);
		ScFxCpuTint(bgra, w, h, stride, -20, 35, -15, 100);
		break;
	case SC_FX_QUAD: ScFxCpuMirror(bgra, w, h, stride); ScFxCpuFlipV(bgra, w, h, stride); break;
	case SC_FX_DUO_PURPLE: ScFxCpuTint(bgra, w, h, stride, 30, -15, 35, 115); break;
	case SC_FX_SWIRL:
	case SC_FX_RIPPLE:
	case SC_FX_VORTEX:
	case SC_FX_HEAT_HAZE:
	case SC_FX_KALEIDO:
	case SC_FX_DISPLACE:
		ScFxCpuWaveLike(bgra, w, h, stride, timeSec, FALSE);
		break;
	case SC_FX_GLITCH:
		ScFxCpuTint(bgra, w, h, stride, 20, 0, -20, 130);
		ScFxCpuNoise(bgra, w, h, stride, timeSec);
		break;
	case SC_FX_CRT_CURVE:
		ScFxCpuScanline(bgra, w, h, stride);
		ScFxCpuVignette(bgra, w, h, stride);
		break;
	case SC_FX_OIL:
		ScFxCpuPoster(bgra, w, h, stride);
		ScFxCpuSharpen(bgra, w, h, stride);
		break;
	case SC_FX_WATERCOLOR:
	case SC_FX_DREAM:
		ScFxCpuBrightness(bgra, w, h, stride, 16);
		ScFxCpuTint(bgra, w, h, stride, 8, 6, 4, 85);
		break;
	case SC_FX_PENCIL:
		ScFxCpuGray(bgra, w, h, stride);
		ScFxCpuSharpen(bgra, w, h, stride);
		ScFxCpuInvert(bgra, w, h, stride);
		break;
	case SC_FX_INTERLACE:
		ScFxCpuScanline(bgra, w, h, stride);
		break;
	case SC_FX_CHROMA_HEAVY:
		ScFxCpuTint(bgra, w, h, stride, 25, -5, -25, 140);
		break;
	case SC_FX_FOG:
		ScFxCpuBrightness(bgra, w, h, stride, 22);
		ScFxCpuTint(bgra, w, h, stride, 10, 12, 16, 55);
		ScFxCpuVignette(bgra, w, h, stride);
		break;
	case SC_FX_SHARPEN_HEAVY:
		ScFxCpuSharpen(bgra, w, h, stride);
		ScFxCpuContrast(bgra, w, h, stride, 130);
		break;
	default: break;
	}
}

struct ScFxGpuCache {
	int w = 0, h = 0;
	com_ptr<ID3D11Texture2D> texA;
	com_ptr<ID3D11Texture2D> texB;
	com_ptr<ID3D11Texture2D> staging;
	com_ptr<ID3D11ShaderResourceView> srvA;
	com_ptr<ID3D11ShaderResourceView> srvB;
	com_ptr<ID3D11RenderTargetView> rtvA;
	com_ptr<ID3D11RenderTargetView> rtvB;
	com_ptr<ID3D11Buffer> cb;
	com_ptr<ID3D11PixelShader> psBlur;
	com_ptr<ID3D11PixelShader> psColor;
	int compiled = 0;
};

static ScFxGpuCache& FxCache()
{
	static ScFxGpuCache c;
	return c;
}

static void ScFxReleaseGpuCache()
{
	ScFxGpuCache& c = FxCache();
	c.texA = nullptr; c.texB = nullptr; c.staging = nullptr;
	c.srvA = nullptr; c.srvB = nullptr; c.rtvA = nullptr; c.rtvB = nullptr;
	c.cb = nullptr; c.psBlur = nullptr; c.psColor = nullptr;
	c.w = 0; c.h = 0; c.compiled = 0;
}

static BOOL ScFxEnsureShaders(ScFxGpuCache& c)
{
	enum { kColorShaderVer = 3 };
	static int s_colorVer = 0;
	if (c.compiled && s_colorVer == kColorShaderVer)
		return (c.psBlur && c.psColor) ? TRUE : FALSE;
	c.psBlur = nullptr;
	c.psColor = nullptr;
	c.compiled = 1;
	s_colorVer = kColorShaderVer;
	static const char kBlur[] =
		"Texture2D tex0 : register(t0); SamplerState samp0 : register(s0);\n"
		"cbuffer CB : register(b0) { float2 texel; float2 dir; float radius; float3 pad; };\n"
		"float4 PSMain(float4 p:SV_Position, float2 uv:TEXCOORD0):SV_Target{\n"
		"  int R=(int)radius; if(R<1)R=1; if(R>8)R=8;\n"
		"  float4 a=0; float wsum=0;\n"
		"  [loop] for(int i=-R;i<=R;++i){\n"
		"    float wt=1.0-(abs((float)i)/(float)(R+1));\n"
		"    a+=tex0.SampleLevel(samp0,saturate(uv+dir*texel*(float)i),0)*wt; wsum+=wt;\n"
		"  } return a/max(wsum,1e-3);\n"
		"}\n";
	static const char kColor[] =
		"Texture2D tex0 : register(t0); SamplerState samp0 : register(s0);\n"
		"cbuffer CB : register(b0) { float mode; float time; float2 texel; };\n"
		"float hash21(float2 p){ return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }\n"
		"float4 PSMain(float4 p:SV_Position, float2 uv:TEXCOORD0):SV_Target{\n"
		"  float2 u=uv;\n"
		"  if(mode>6.5 && mode<7.5) u.x=1-u.x;\n"
		"  else if(mode>18.5 && mode<19.5) u.y=1-u.y;\n"
		"  else if(mode>7.5 && mode<8.5){\n"
		"    u.x+=sin(u.y*40.0+time*4.0)*0.008; u.y+=cos(u.x*30.0+time*3.0)*0.006;\n"
		"  } else if(mode>8.5 && mode<9.5){\n"
		"    u.x+=sin(u.y*25.0+time*2.0)*0.012; u.y+=sin(u.x*20.0+time*1.5)*0.010;\n"
		"  } else if(mode>17.5 && mode<18.5){\n"
		"    float2 bs=float2(texel.x*14.0,texel.y*14.0); u=floor(u/bs)*bs+bs*0.5;\n"
		"  } else if(mode>25.5 && mode<26.5){\n"
		"    float2 d=u-0.5; float r=length(d); float nr=pow(saturate(r*1.35),0.72);\n"
		"    if(r>1e-4) u=0.5+d*(nr/r); }\n"
		"  else if(mode>48.5 && mode<49.5){\n"
		"    float2 q=u; if(q.x>0.5)q.x=1-q.x; if(q.y>0.5)q.y=1-q.y; u=saturate(q*2.0); }\n"
		"  else if(mode>54.5 && mode<55.5){\n"
		"    float2 d=u-0.5; float a=atan2(d.y,d.x)+length(d)*3.2; float r=length(d);\n"
		"    u=saturate(0.5+float2(cos(a),sin(a))*r); }\n"
		"  else if(mode>55.5 && mode<56.5){\n"
		"    float2 d=u-0.5; float r=length(d);\n"
		"    u+=normalize(d+1e-5)*sin(r*40.0-time*6.0)*0.018; }\n"
		"  else if(mode>56.5 && mode<57.5){\n"
		"    float2 d=u-0.5; float a=atan2(d.y,d.x)+6.0*(0.7-length(d)); float r=length(d);\n"
		"    u=saturate(0.5+float2(cos(a),sin(a))*r); }\n"
		"  else if(mode>57.5 && mode<58.5){\n"
		"    u.x+=sin(u.y*30.0+time*5.0)*0.01; u.y+=cos(u.x*28.0+time*4.0)*0.008; }\n"
		"  else if(mode>58.5 && mode<59.5){\n"
		"    float band=floor(u.y*18.0); u.x+=sin(band*12.0+time*8.0)*0.035*(fmod(band,2.0)*2.0-1.0); }\n"
		"  else if(mode>59.5 && mode<60.5){\n"
		"    float2 d=u-0.5; float r=length(d); float nr=r+(r*r)*0.55; if(r>1e-4)u=0.5+d*(nr/r); }\n"
		"  else if(mode>60.5 && mode<61.5){\n"
		"    float2 d=u-0.5; float a=atan2(d.y,d.x); float r=length(d);\n"
		"    a=fmod(a+3.14159,1.0472)-0.5236; u=saturate(0.5+float2(cos(a),sin(a))*r); }\n"
		"  else if(mode>66.5 && mode<67.5){\n"
		"    float n=hash21(u*float2(90.0,70.0)+time)-0.5; u+=float2(n,n*0.7)*0.03; }\n"
		"  else if(mode>67.5 && mode<68.5){\n"
		"    if(fmod(floor(u.y/(texel.y*2.0+1e-6)),2.0)>0.5) u.x+=0.012; }\n"
		"  float4 c=tex0.SampleLevel(samp0,saturate(u),0);\n"
		"  if(mode>2.5 && mode<3.5){ float g=dot(c.rgb,float3(0.114,0.587,0.299)); c.rgb=g; }\n"
		"  else if(mode>3.5 && mode<4.5){\n"
		"    float3x3 m=float3x3(0.393,0.769,0.189, 0.349,0.686,0.168, 0.272,0.534,0.131);\n"
		"    c.rgb=saturate(mul(c.rgb,m)); }\n"
		"  else if(mode>4.5 && mode<5.5){\n"
		"    float2 d=u-0.5; float v=saturate(1-dot(d,d)*2.2); c.rgb*=max(v,0.15); }\n"
		"  else if(mode>5.5 && mode<6.5){\n"
		"    float4 up=tex0.SampleLevel(samp0,saturate(u+float2(0,-texel.y)),0);\n"
		"    float4 dn=tex0.SampleLevel(samp0,saturate(u+float2(0, texel.y)),0);\n"
		"    float4 lf=tex0.SampleLevel(samp0,saturate(u+float2(-texel.x,0)),0);\n"
		"    float4 rt=tex0.SampleLevel(samp0,saturate(u+float2( texel.x,0)),0);\n"
		"    c.rgb=saturate(c.rgb*5 - up.rgb - dn.rgb - lf.rgb - rt.rgb); }\n"
		"  else if(mode>8.5 && mode<9.5){\n"
		"    c.rgb=saturate(c.rgb*float3(0.75,1.05,1.25)+float3(-0.02,0.02,0.06)); }\n"
		"  else if(mode>9.5 && mode<10.5){\n"
		"    c.rgb=saturate(c.rgb*float3(1.25,0.95,0.70)+float3(0.06,0.02,-0.04));\n"
		"    float2 d=u-0.5; float v=saturate(1-dot(d,d)*1.6); c.rgb*=max(v,0.35); }\n"
		"  else if(mode>10.5 && mode<11.5){ c.rgb=saturate(c.rgb*float3(0.85,0.95,1.20)); }\n"
		"  else if(mode>11.5 && mode<12.5){ c.rgb=saturate(c.rgb*float3(1.20,1.05,0.85)); }\n"
		"  else if(mode>12.5 && mode<13.5){ c.rgb=floor(c.rgb*8.0+0.5)/8.0; }\n"
		"  else if(mode>13.5 && mode<14.5){\n"
		"    float s=frac(u.y/(texel.y*2.0+1e-6)); if(s<0.5) c.rgb*=0.72; }\n"
		"  else if(mode>14.5 && mode<15.5){\n"
		"    float4 up=tex0.SampleLevel(samp0,saturate(u+float2(0,-texel.y)),0);\n"
		"    float4 dn=tex0.SampleLevel(samp0,saturate(u+float2(0, texel.y)),0);\n"
		"    float4 lf=tex0.SampleLevel(samp0,saturate(u+float2(-texel.x,0)),0);\n"
		"    float4 rt=tex0.SampleLevel(samp0,saturate(u+float2( texel.x,0)),0);\n"
		"    float3 gx=rt.rgb-lf.rgb; float3 gy=dn.rgb-up.rgb;\n"
		"    c.rgb=saturate(length(gx)+length(gy)); }\n"
		"  else if(mode>15.5 && mode<16.5){ c.rgb=1.0-c.rgb; }\n"
		"  else if(mode>16.5 && mode<17.5){\n"
		"    c.rgb=lerp(c.rgb,1.0-c.rgb,step(0.5,c.rgb)); }\n"
		"  else if(mode>19.5 && mode<20.5){\n"
		"    float n=hash21(u*float2(640.0,360.0)+float2(time,time))-0.5; c.rgb=saturate(c.rgb+n*0.18); }\n"
		"  else if(mode>20.5 && mode<21.5){\n"
		"    float4 b=0; b+=tex0.SampleLevel(samp0,saturate(u+float2(texel.x*4,0)),0);\n"
		"    b+=tex0.SampleLevel(samp0,saturate(u+float2(-texel.x*4,0)),0);\n"
		"    b+=tex0.SampleLevel(samp0,saturate(u+float2(0,texel.y*4)),0);\n"
		"    b+=tex0.SampleLevel(samp0,saturate(u+float2(0,-texel.y*4)),0); b*=0.25;\n"
		"    c.rgb=saturate(c.rgb+max(b.rgb-0.55,0)*0.9); }\n"
		"  else if(mode>21.5 && mode<22.5){\n"
		"    float4 up=tex0.SampleLevel(samp0,saturate(u+float2(0,-texel.y)),0);\n"
		"    float4 dn=tex0.SampleLevel(samp0,saturate(u+float2(0, texel.y)),0);\n"
		"    float4 lf=tex0.SampleLevel(samp0,saturate(u+float2(-texel.x,0)),0);\n"
		"    float4 rt=tex0.SampleLevel(samp0,saturate(u+float2( texel.x,0)),0);\n"
		"    float e=saturate(length(rt.rgb-lf.rgb)+length(dn.rgb-up.rgb));\n"
		"    c.rgb=saturate(c.rgb*0.35+float3(0.1,0.9,1.0)*e); }\n"
		"  else if(mode>22.5 && mode<23.5){\n"
		"    float g=dot(c.rgb,float3(0.114,0.587,0.299));\n"
		"    float n=hash21(u+float2(time,time))-0.5; c.rgb=saturate(float3(0.05,g*1.25+n*0.1,0.08));\n"
		"    float2 d=u-0.5; float v=saturate(1-dot(d,d)*2.4); c.rgb*=max(v,0.12); }\n"
		"  else if(mode>23.5 && mode<24.5){\n"
		"    float4 up=tex0.SampleLevel(samp0,saturate(u+float2(0,-texel.y)),0);\n"
		"    float4 dn=tex0.SampleLevel(samp0,saturate(u+float2(0, texel.y)),0);\n"
		"    float4 lf=tex0.SampleLevel(samp0,saturate(u+float2(-texel.x,0)),0);\n"
		"    float4 rt=tex0.SampleLevel(samp0,saturate(u+float2( texel.x,0)),0);\n"
		"    float e=saturate(length(rt.rgb-lf.rgb)+length(dn.rgb-up.rgb));\n"
		"    c.rgb=floor(c.rgb*6.0+0.5)/6.0; c.rgb=saturate(c.rgb*(1.15)+e*0.55); }\n"
		"  else if(mode>24.5 && mode<25.5){\n"
		"    float3x3 m=float3x3(0.393,0.769,0.189, 0.349,0.686,0.168, 0.272,0.534,0.131);\n"
		"    c.rgb=saturate(mul(c.rgb,m));\n"
		"    float s=frac(u.y/(texel.y*2.0+1e-6)); if(s<0.5) c.rgb*=0.78;\n"
		"    float2 d=u-0.5; float v=saturate(1-dot(d,d)*1.8); c.rgb*=max(v,0.25); }\n"
		"  else if(mode>26.5 && mode<27.5){\n"
		"    float ang=0.55; float ca=cos(ang), sa=sin(ang);\n"
		"    float3 k=normalize(float3(1,1,1));\n"
		"    c.rgb=saturate(c.rgb*ca+cross(k,c.rgb)*sa+k*dot(k,c.rgb)*(1-ca)); }\n"
		"  else if(mode>27.5 && mode<28.5){ c.rgb=saturate((c.rgb-0.5)*1.45+0.5); }\n"
		"  else if(mode>28.5 && mode<29.5){ c.rgb=saturate(c.rgb+0.12); }\n"
		"  else if(mode>29.5 && mode<30.5){\n"
		"    float g=dot(c.rgb,float3(0.114,0.587,0.299)); c.rgb=saturate(lerp(float3(g,g,g),c.rgb,1.6)); }\n"
		"  else if(mode>30.5 && mode<31.5){\n"
		"    float g=dot(c.rgb,float3(0.114,0.587,0.299)); c.rgb=saturate(lerp(c.rgb,float3(g,g,g),0.65)); }\n"
		"  else if(mode>31.5 && mode<32.5){\n"
		"    float g=dot(c.rgb,float3(0.114,0.587,0.299)); c.rgb=(g>0.5)?1:0; }\n"
		"  else if(mode>32.5 && mode<33.5){ c.rgb=saturate(c.rgb*float3(1.35,0.85,0.80)+float3(0.05,0,0)); }\n"
		"  else if(mode>33.5 && mode<34.5){ c.rgb=saturate(c.rgb*float3(0.80,1.30,0.85)+float3(0,0.04,0)); }\n"
		"  else if(mode>34.5 && mode<35.5){ c.rgb=saturate(c.rgb*float3(0.80,0.90,1.35)+float3(0,0,0.05)); }\n"
		"  else if(mode>35.5 && mode<36.5){ c.rgb=saturate(c.rgb*float3(0.70,1.15,1.25)); }\n"
		"  else if(mode>36.5 && mode<37.5){ c.rgb=saturate(c.rgb*float3(1.25,0.75,1.20)); }\n"
		"  else if(mode>37.5 && mode<38.5){ c.rgb=saturate(c.rgb*float3(1.25,1.20,0.70)); }\n"
		"  else if(mode>38.5 && mode<39.5){\n"
		"    float g=dot(c.rgb,float3(0.114,0.587,0.299));\n"
		"    c.rgb=saturate(lerp(float3(0.05,0.35,0.40),float3(1.0,0.55,0.20),g)*0.55+c.rgb*0.55); }\n"
		"  else if(mode>39.5 && mode<40.5){\n"
		"    float g=dot(c.rgb,float3(0.114,0.587,0.299)); c.rgb=saturate(lerp(c.rgb,float3(0.92,0.90,0.88),0.45)+0.06); }\n"
		"  else if(mode>40.5 && mode<41.5){\n"
		"    float2 d=u-0.5; float v=1.2-dot(d,d)*1.8; c.rgb*=saturate(max(v,0.22)); }\n"
		"  else if(mode>41.5 && mode<42.5){\n"
		"    float s=frac(u.y/(texel.y*4.0+1e-6)); if(s<0.5) c.rgb*=0.62; }\n"
		"  else if(mode>42.5 && mode<43.5){\n"
		"    float s=frac(u.x/(texel.x*4.0+1e-6)); if(s<0.5) c.rgb*=0.62; }\n"
		"  else if(mode>43.5 && mode<44.5){\n"
		"    float r=tex0.SampleLevel(samp0,saturate(u+float2(texel.x*2,0)),0).r;\n"
		"    float b=tex0.SampleLevel(samp0,saturate(u-float2(texel.x*2,0)),0).b;\n"
		"    c.rgb=saturate(float3(r,c.g,b)); }\n"
		"  else if(mode>44.5 && mode<45.5){\n"
		"    float4 up=tex0.SampleLevel(samp0,saturate(u+float2(0,-texel.y)),0);\n"
		"    float4 lf=tex0.SampleLevel(samp0,saturate(u+float2(-texel.x,0)),0);\n"
		"    float e=dot(c.rgb-up.rgb,float3(0.3,0.3,0.3))+dot(c.rgb-lf.rgb,float3(0.3,0.3,0.3));\n"
		"    c.rgb=saturate(0.5+e*2.2); }\n"
		"  else if(mode>45.5 && mode<46.5){ c.rgb=saturate(c.rgb*0.92+0.10); }\n"
		"  else if(mode>46.5 && mode<47.5){\n"
		"    float g=dot(c.rgb,float3(0.114,0.587,0.299)); c.rgb=saturate(float3(g*0.75,g*0.9,g*1.25)); }\n"
		"  else if(mode>47.5 && mode<48.5){\n"
		"    float g=dot(c.rgb,float3(0.114,0.587,0.299)); c.rgb=saturate(float3(g*0.75,g*1.2,g*0.8)); }\n"
		"  else if(mode>49.5 && mode<50.5){\n"
		"    float g=dot(c.rgb,float3(0.114,0.587,0.299));\n"
		"    c.rgb=saturate(lerp(float3(0.25,0.08,0.40),float3(1.0,0.75,0.35),g)*0.5+c.rgb*0.55); }\n"
		"  else if(mode>51.5 && mode<52.5){\n"
		"    float4 a=0; [unroll] for(int i=1;i<=6;++i){\n"
		"      a+=tex0.SampleLevel(samp0,saturate(u+float2(texel.x*(float)i*2.5,0)),0);\n"
		"      a+=tex0.SampleLevel(samp0,saturate(u-float2(texel.x*(float)i*2.5,0)),0); }\n"
		"    c.rgb=saturate((c.rgb+a.rgb)/(1.0+12.0)); }\n"
		"  else if(mode>52.5 && mode<53.5){\n"
		"    float2 d=normalize(u-0.5+1e-5); float4 a=c;\n"
		"    [unroll] for(int i=1;i<=7;++i) a+=tex0.SampleLevel(samp0,saturate(u-d*texel*float(i)*3.0),0);\n"
		"    c.rgb=saturate(a.rgb/8.0); }\n"
		"  else if(mode>53.5 && mode<54.5){\n"
		"    float2 d=u-0.5; float4 a=c;\n"
		"    [unroll] for(int i=1;i<=8;++i){ float t=1.0-(float)i/9.0; a+=tex0.SampleLevel(samp0,saturate(0.5+d*t),0); }\n"
		"    c.rgb=saturate(a.rgb/9.0); }\n"
		"  else if(mode>58.5 && mode<59.5){\n"
		"    float r=tex0.SampleLevel(samp0,saturate(u+float2(0.02,0)),0).r;\n"
		"    float b=tex0.SampleLevel(samp0,saturate(u-float2(0.02,0)),0).b;\n"
		"    c.rgb=saturate(float3(r,c.g,b)); float n=hash21(u+time); if(n>0.97)c.rgb=1-c.rgb; }\n"
		"  else if(mode>59.5 && mode<60.5){\n"
		"    float s=frac(u.y/(texel.y*2.0+1e-6)); if(s<0.5)c.rgb*=0.7;\n"
		"    float2 d=u-0.5; c.rgb*=saturate(1.05-dot(d,d)*1.3); }\n"
		"  else if(mode>61.5 && mode<62.5){\n"
		"    float4 a=0; a+=tex0.SampleLevel(samp0,saturate(u+float2(texel.x*3,0)),0);\n"
		"    a+=tex0.SampleLevel(samp0,saturate(u+float2(-texel.x*3,0)),0);\n"
		"    a+=tex0.SampleLevel(samp0,saturate(u+float2(0,texel.y*3)),0);\n"
		"    a+=tex0.SampleLevel(samp0,saturate(u+float2(0,-texel.y*3)),0);\n"
		"    a+=tex0.SampleLevel(samp0,saturate(u+float2(texel.x*2,texel.y*2)),0);\n"
		"    a+=tex0.SampleLevel(samp0,saturate(u+float2(-texel.x*2,-texel.y*2)),0);\n"
		"    c.rgb=saturate((c.rgb*0.35+a.rgb*0.65/6.0)); c.rgb=floor(c.rgb*10.0+0.5)/10.0; }\n"
		"  else if(mode>62.5 && mode<63.5){\n"
		"    c.rgb=floor(c.rgb*6.0+0.5)/6.0; c.rgb=saturate(c.rgb*1.08+0.04); }\n"
		"  else if(mode>63.5 && mode<64.5){\n"
		"    float4 up=tex0.SampleLevel(samp0,saturate(u+float2(0,-texel.y)),0);\n"
		"    float4 dn=tex0.SampleLevel(samp0,saturate(u+float2(0, texel.y)),0);\n"
		"    float4 lf=tex0.SampleLevel(samp0,saturate(u+float2(-texel.x,0)),0);\n"
		"    float4 rt=tex0.SampleLevel(samp0,saturate(u+float2( texel.x,0)),0);\n"
		"    float e=saturate(length(rt.rgb-lf.rgb)+length(dn.rgb-up.rgb));\n"
		"    float g=dot(c.rgb,float3(0.114,0.587,0.299)); c.rgb=saturate(1.0-(e*1.4)+(g*0.15)); }\n"
		"  else if(mode>64.5 && mode<65.5){ c.rgb=saturate(c.rgb*0.85+0.12); }\n"
		"  else if(mode>65.5 && mode<66.5){\n"
		"    float2 d=normalize(u-0.5+1e-5); float4 a=0;\n"
		"    [unroll] for(int i=1;i<=10;++i){\n"
		"      float4 s=tex0.SampleLevel(samp0,saturate(u-d*texel*float(i)*4.0),0);\n"
		"      a+=s*max(dot(s.rgb,float3(0.3,0.3,0.3))-0.55,0); }\n"
		"    c.rgb=saturate(c.rgb+a.rgb*0.35); }\n"
		"  else if(mode>68.5 && mode<69.5){\n"
		"    float r=tex0.SampleLevel(samp0,saturate(u+float2(texel.x*5,0)),0).r;\n"
		"    float b=tex0.SampleLevel(samp0,saturate(u-float2(texel.x*5,texel.y*2)),0).b;\n"
		"    c.rgb=saturate(float3(r,c.g*0.95,b)); }\n"
		"  else if(mode>69.5 && mode<70.5){\n"
		"    float g=dot(c.rgb,float3(0.114,0.587,0.299));\n"
		"    c.rgb=saturate(lerp(c.rgb,float3(0.78,0.82,0.88),0.45)+0.05);\n"
		"    float2 d=u-0.5; c.rgb*=saturate(1.0-dot(d,d)*0.9); }\n"
		"  else if(mode>70.5 && mode<71.5){\n"
		"    float4 up=tex0.SampleLevel(samp0,saturate(u+float2(0,-texel.y)),0);\n"
		"    float4 dn=tex0.SampleLevel(samp0,saturate(u+float2(0, texel.y)),0);\n"
		"    float4 lf=tex0.SampleLevel(samp0,saturate(u+float2(-texel.x,0)),0);\n"
		"    float4 rt=tex0.SampleLevel(samp0,saturate(u+float2( texel.x,0)),0);\n"
		"    c.rgb=saturate(c.rgb*7 - up.rgb - dn.rgb - lf.rgb - rt.rgb); }\n"
		"  return c;\n"
		"}\n";
	com_ptr<ID3DBlob> blob, err;
	if (SUCCEEDED(D3DCompile(kBlur, sizeof(kBlur) - 1, nullptr, nullptr, nullptr,
		"PSMain", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.put(), err.put())) && blob)
		g_d3d->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, c.psBlur.put());
	blob = nullptr; err = nullptr;
	if (SUCCEEDED(D3DCompile(kColor, sizeof(kColor) - 1, nullptr, nullptr, nullptr,
		"PSMain", "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob.put(), err.put())) && blob)
		g_d3d->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, c.psColor.put());
	return (c.psBlur && c.psColor) ? TRUE : FALSE;
}

static BOOL ScFxEnsureTargets(ScFxGpuCache& c, int w, int h)
{
	if (c.w == w && c.h == h && c.texA && c.texB && c.staging && c.srvA && c.srvB && c.rtvA && c.rtvB && c.cb)
		return TRUE;
	c.texA = nullptr; c.texB = nullptr; c.staging = nullptr;
	c.srvA = nullptr; c.srvB = nullptr; c.rtvA = nullptr; c.rtvB = nullptr;
	c.cb = nullptr;
	c.w = 0; c.h = 0;

	D3D11_TEXTURE2D_DESC td = {};
	td.Width = (UINT)w; td.Height = (UINT)h; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	if (FAILED(g_d3d->CreateTexture2D(&td, nullptr, c.texA.put()))) return FALSE;
	if (FAILED(g_d3d->CreateTexture2D(&td, nullptr, c.texB.put()))) return FALSE;
	if (FAILED(g_d3d->CreateShaderResourceView(c.texA.get(), nullptr, c.srvA.put()))) return FALSE;
	if (FAILED(g_d3d->CreateShaderResourceView(c.texB.get(), nullptr, c.srvB.put()))) return FALSE;
	if (FAILED(g_d3d->CreateRenderTargetView(c.texA.get(), nullptr, c.rtvA.put()))) return FALSE;
	if (FAILED(g_d3d->CreateRenderTargetView(c.texB.get(), nullptr, c.rtvB.put()))) return FALSE;

	D3D11_TEXTURE2D_DESC sd = td;
	sd.Usage = D3D11_USAGE_STAGING;
	sd.BindFlags = 0;
	sd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
	if (FAILED(g_d3d->CreateTexture2D(&sd, nullptr, c.staging.put()))) return FALSE;

	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = 32;
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(g_d3d->CreateBuffer(&bd, nullptr, c.cb.put()))) return FALSE;

	c.w = w; c.h = h;
	return TRUE;
}

static void ScFxDrawPass(ScFxGpuCache& c, ID3D11ShaderResourceView* srv, ID3D11RenderTargetView* rtv,
	ID3D11PixelShader* ps, const float* cbData /* 8 floats */)
{
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (SUCCEEDED(g_ctx->Map(c.cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		memcpy(mapped.pData, cbData, 32);
		g_ctx->Unmap(c.cb.get(), 0);
	}
	ID3D11RenderTargetView* prtv = rtv;
	g_ctx->OMSetRenderTargets(1, &prtv, nullptr);
	D3D11_VIEWPORT vp = {};
	vp.Width = (float)c.w; vp.Height = (float)c.h; vp.MaxDepth = 1.f;
	g_ctx->RSSetViewports(1, &vp);
	g_ctx->RSSetState(g_rs.get());
	float bf[4] = {};
	g_ctx->OMSetBlendState(g_bs.get(), bf, 0xffffffff);
	g_ctx->VSSetShader(g_vs.get(), nullptr, 0);
	g_ctx->PSSetShader(ps, nullptr, 0);
	ID3D11ShaderResourceView* psrv = srv;
	g_ctx->PSSetShaderResources(0, 1, &psrv);
	ID3D11SamplerState* samp = g_sampLinear.get();
	g_ctx->PSSetSamplers(0, 1, &samp);
	ID3D11Buffer* pcb = c.cb.get();
	g_ctx->PSSetConstantBuffers(0, 1, &pcb);
	g_ctx->IASetInputLayout(nullptr);
	g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_ctx->Draw(3, 0);
	ID3D11ShaderResourceView* nullSrv = nullptr;
	g_ctx->PSSetShaderResources(0, 1, &nullSrv);
	ID3D11RenderTargetView* nullRtv = nullptr;
	g_ctx->OMSetRenderTargets(1, &nullRtv, nullptr);
}

static BOOL ScFxGpuApplyChainLocked(BYTE* bgra, int w, int h, int stride,
	const int* effects, int n, float timeSec)
{
	ScFxGpuCache& c = FxCache();
	if (!ScFxEnsureShaders(c) || !ScFxEnsureTargets(c, w, h))
		return FALSE;

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(g_ctx->Map(c.staging.get(), 0, D3D11_MAP_WRITE, 0, &mapped)))
		return FALSE;
	for (int y = 0; y < h; ++y)
		memcpy((BYTE*)mapped.pData + (size_t)y * mapped.RowPitch, bgra + (size_t)y * (size_t)stride, (size_t)w * 4);
	g_ctx->Unmap(c.staging.get(), 0);
	g_ctx->CopyResource(c.texA.get(), c.staging.get());

	BOOL srcIsA = TRUE;
	for (int i = 0; i < n; ++i) {
		const int fx = effects[i];
		if (fx <= SC_FX_NONE || fx >= SC_FX_COUNT) continue;

		ID3D11ShaderResourceView* srv = srcIsA ? c.srvA.get() : c.srvB.get();
		ID3D11RenderTargetView* rtv = srcIsA ? c.rtvB.get() : c.rtvA.get();

		if (fx == SC_FX_BLUR_SOFT || fx == SC_FX_BLUR_STRONG || fx == SC_FX_BLUR_MEGA
			|| fx == SC_FX_DREAM || fx == SC_FX_WATERCOLOR) {
			float radius = 2.f;
			if (fx == SC_FX_BLUR_STRONG || fx == SC_FX_DREAM) radius = 4.f;
			if (fx == SC_FX_BLUR_MEGA) radius = 6.f;
			float cbH[8] = { 1.f / (float)w, 1.f / (float)h, 1.f, 0.f, radius, 0, 0, 0 };
			float cbV[8] = { 1.f / (float)w, 1.f / (float)h, 0.f, 1.f, radius, 0, 0, 0 };
			ScFxDrawPass(c, srv, rtv, c.psBlur.get(), cbH);
			ID3D11ShaderResourceView* srv2 = srcIsA ? c.srvB.get() : c.srvA.get();
			ID3D11RenderTargetView* rtv2 = srcIsA ? c.rtvA.get() : c.rtvB.get();
			ScFxDrawPass(c, srv2, rtv2, c.psBlur.get(), cbV);
			if (fx == SC_FX_DREAM || fx == SC_FX_WATERCOLOR) {
				ID3D11ShaderResourceView* srv3 = srcIsA ? c.srvA.get() : c.srvB.get();
				ID3D11RenderTargetView* rtv3 = srcIsA ? c.rtvB.get() : c.rtvA.get();
				float cb[8] = { (float)fx, timeSec, 1.f / (float)w, 1.f / (float)h, 0, 0, 0, 0 };
				ScFxDrawPass(c, srv3, rtv3, c.psColor.get(), cb);
				srcIsA = !srcIsA;
			}
			continue;
		}

		float cb[8] = { (float)fx, timeSec, 1.f / (float)w, 1.f / (float)h, 0, 0, 0, 0 };
		ScFxDrawPass(c, srv, rtv, c.psColor.get(), cb);
		srcIsA = !srcIsA;
	}

	ID3D11Texture2D* finalTex = srcIsA ? c.texA.get() : c.texB.get();
	g_ctx->CopyResource(c.staging.get(), finalTex);
	if (FAILED(g_ctx->Map(c.staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
		return FALSE;
	for (int y = 0; y < h; ++y)
		memcpy(bgra + (size_t)y * (size_t)stride, (BYTE*)mapped.pData + (size_t)y * mapped.RowPitch, (size_t)w * 4);
	g_ctx->Unmap(c.staging.get(), 0);
	return TRUE;
}

BOOL ScGpuApplyEffectChain(BYTE* bgra, int w, int h, int stride,
	const int* effects, int n, float timeSec)
{
	if (!bgra || !effects || w < 2 || h < 2 || stride < w * 4 || n <= 0)
		return FALSE;
	if (n > SC_FX_CHAIN_MAX) n = SC_FX_CHAIN_MAX;
	w &= ~1; h &= ~1;

	int cleaned[SC_FX_CHAIN_MAX];
	int cn = 0;
	for (int i = 0; i < n && cn < SC_FX_CHAIN_MAX; ++i) {
		const int fx = effects[i];
		if (fx > SC_FX_NONE && fx < SC_FX_COUNT)
			cleaned[cn++] = fx;
	}
	if (cn <= 0) return TRUE;

	if (EnsureD3D() && g_d3d && g_ctx && g_vs) {
		std::lock_guard<std::mutex> ctxLock(g_ctxMtx);
		if (ScFxGpuApplyChainLocked(bgra, w, h, stride, cleaned, cn, timeSec))
			return TRUE;
	}
	for (int i = 0; i < cn; ++i)
		ScFxCpuOne(bgra, w, h, stride, cleaned[i], timeSec);
	return TRUE;
}

BOOL ScGpuApplyEffect(BYTE* bgra, int w, int h, int stride, int effect)
{
	if (effect <= SC_FX_NONE || effect >= SC_FX_COUNT) return TRUE;
	const int one = effect;
	return ScGpuApplyEffectChain(bgra, w, h, stride, &one, 1, 0.f);
}

void ScWgcShutdown(void)
{
	{
		std::lock_guard<std::mutex> ctxLock(g_ctxMtx);
		ScFxReleaseGpuCache();
	}
	std::lock_guard<std::mutex> lock(g_mtx);
	std::vector<HWND> wins;
	wins.reserve(g_winSessions.size());
	for (auto& kv : g_winSessions)
		wins.push_back(kv.first);
	for (HWND h : wins)
		DestroyWinSession_NoLock(h);

	std::vector<HMONITOR> mons;
	mons.reserve(g_monSessions.size());
	for (auto& kv : g_monSessions)
		mons.push_back(kv.first);
	for (HMONITOR m : mons)
		DestroyMonSession_NoLock(m);

	g_vs = nullptr;
	g_ps = nullptr;
	g_sampLinear = nullptr;
	g_sampPoint = nullptr;
	g_rs = nullptr;
	g_bs = nullptr;
	g_shaderReady = FALSE;
	g_winrtDevice = nullptr;
	g_ctx = nullptr;
	g_d3d = nullptr;
}

void ScWgcReleaseSessions(void)
{
	// D3D は残し、キャプチャセッションだけ破棄（録画時の WGC 固着解除用）
	std::lock_guard<std::mutex> lock(g_mtx);
	std::vector<HWND> wins;
	wins.reserve(g_winSessions.size());
	for (auto& kv : g_winSessions)
		wins.push_back(kv.first);
	for (HWND h : wins)
		DestroyWinSession_NoLock(h);

	std::vector<HMONITOR> mons;
	mons.reserve(g_monSessions.size());
	for (auto& kv : g_monSessions)
		mons.push_back(kv.first);
	for (HMONITOR m : mons)
		DestroyMonSession_NoLock(m);
}
