// OmakeTacticsRender.cpp — おまけタクティクス: DirectX11 描画

#include "stdafx.h"
#include "resource.h"
#include "OmakeTacticsRender.h"
#include "Soft3DTexRes.h"
#include "Soft3DTextD2D.h"
#include <d3dcompiler.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <algorithm>

#pragma comment(lib, "windowscodecs.lib")

#define TAC_REL(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)

namespace otac {

void GpuMesh::Release() { TAC_REL(vb); TAC_REL(ib); nIdx = 0; }

int Rig::Find(const char* n) const
{
	for (int i = 0; i < nJ; i++) if (!strcmp(names[i], n)) return i;
	return -1;
}
int Rig::FindClip(const char* n) const
{
	for (int i = 0; i < (int)clips.size(); i++) if (!strcmp(clips[i].name, n)) return i;
	return -1;
}

// ============================================================ 初期化
Renderer::Renderer()
{
	m_frame = 0;
	m_view = m_proj = m_vp = MIdent();
	m_eye = v3(0, 10, -10);
	m_time = 0;
	m_hwnd = NULL; m_wait = NULL; m_lost = FALSE; m_presented = FALSE;
	m_w = m_h = 0; m_quality = 1; m_msaa = 1; m_lastHr = S_OK; m_failStage = 0; m_assetsOk = FALSE; m_pass = PASS_MAIN;
	m_dev = NULL; m_ctx = NULL; m_swap = NULL; m_bbRtv = NULL; m_bb = NULL; m_msTex = NULL; m_msRtv = NULL; m_dsTex = NULL; m_dsv = NULL;
	m_shTex = NULL; m_shDsv = NULL; m_shSrv = NULL; m_shSize = 0;
	m_ptTex = NULL; m_ptRtv = NULL; m_srvPortrait = NULL; m_ptDs = NULL; m_ptDsv = NULL;
	m_vsS = m_vsK = m_vsH = m_vsP = m_vsQ = NULL;
	m_psM = m_psO = m_psOV = m_psH = m_psP = m_psSky = NULL;
	m_ilS = m_ilK = m_ilH = m_ilP = NULL;
	m_cbFrame = m_cbObj = m_cbBones = m_vbHud = m_vbDyn = NULL; m_vbDynBytes = 0;
	m_sLin = m_sCmp = m_sClamp = NULL;
	m_rsBack = m_rsFront = m_rsNone = m_rsShadow = m_rsHud = NULL;
	m_dsWrite = m_dsRead = m_dsOff = NULL;
	m_bsOpaque = m_bsAlpha = m_bsPremul = m_bsAdd = NULL;
	for (int i = 0; i < TEX_MAX; i++) { m_tex[i] = NULL; m_srv[i] = NULL; }
	m_texMini = NULL; m_srvMini = NULL;
	m_waterColor = RGBA8(60, 130, 190);
	memset(&m_fcb, 0, sizeof(m_fcb));
	m_lightVP = MIdent();
	m_hudSrv = NULL;
	m_inPortrait = FALSE; m_portraitSlot = 0;
}

Renderer::~Renderer() { Release(); }

void Renderer::ReleaseTargets()
{
	if (m_ctx) m_ctx->OMSetRenderTargets(0, NULL, NULL);
	if (m_msRtv == m_bbRtv) m_msRtv = NULL;
	TAC_REL(m_msRtv); TAC_REL(m_msTex);
	TAC_REL(m_bbRtv); TAC_REL(m_bb);
	TAC_REL(m_dsv); TAC_REL(m_dsTex);
}

void Renderer::Release()
{
	for (auto& kv : m_text) { TAC_REL(kv.second.srv); TAC_REL(kv.second.tex); }
	m_text.clear();
	m_terrain.Release(); m_water.Release(); m_lava.Release();
	for (auto& b : m_props) b.mesh.Release();
	m_props.clear();
	for (auto& m : models) for (auto& p : m.parts) p.Release();
	models.clear();
	for (auto& a : attaches) a.mesh.Release();
	attaches.clear();
	rigs.clear();
	statics.clear();
	for (auto& c : m_creatures) c.Release();
	m_creatures.clear();
	m_skins.clear();
	ReleaseAvatarCache();
	for (auto& c : m_gear) c.Release();
	m_gear.clear();
	m_assetsOk = FALSE;
	ReleaseTargets();
	TAC_REL(m_shSrv); TAC_REL(m_shDsv); TAC_REL(m_shTex);
	TAC_REL(m_srvPortrait); TAC_REL(m_ptRtv); TAC_REL(m_ptTex); TAC_REL(m_ptDsv); TAC_REL(m_ptDs);
	TAC_REL(m_vsS); TAC_REL(m_vsK); TAC_REL(m_vsH); TAC_REL(m_vsP); TAC_REL(m_vsQ);
	TAC_REL(m_psM); TAC_REL(m_psO); TAC_REL(m_psOV); TAC_REL(m_psH); TAC_REL(m_psP); TAC_REL(m_psSky);
	TAC_REL(m_ilS); TAC_REL(m_ilK); TAC_REL(m_ilH); TAC_REL(m_ilP);
	TAC_REL(m_cbFrame); TAC_REL(m_cbObj); TAC_REL(m_cbBones); TAC_REL(m_vbHud); TAC_REL(m_vbDyn);
	TAC_REL(m_sLin); TAC_REL(m_sCmp); TAC_REL(m_sClamp);
	TAC_REL(m_rsBack); TAC_REL(m_rsFront); TAC_REL(m_rsNone); TAC_REL(m_rsShadow); TAC_REL(m_rsHud);
	TAC_REL(m_dsWrite); TAC_REL(m_dsRead); TAC_REL(m_dsOff);
	TAC_REL(m_bsOpaque); TAC_REL(m_bsAlpha); TAC_REL(m_bsPremul); TAC_REL(m_bsAdd);
	for (int i = 0; i < TEX_MAX; i++) { TAC_REL(m_srv[i]); TAC_REL(m_tex[i]); }
	TAC_REL(m_srvMini); TAC_REL(m_texMini);
	if (m_wait) { CloseHandle(m_wait); m_wait = NULL; }
	TAC_REL(m_swap);
	if (m_ctx) { m_ctx->ClearState(); m_ctx->Flush(); }
	TAC_REL(m_ctx);
	TAC_REL(m_dev);
	m_lost = FALSE;
}

BOOL Renderer::Init(HWND hwnd, int quality)
{
	Release();
	m_presented = FALSE;
	m_hwnd = hwnd;
	m_quality = Clampi(quality, 0, 2);
	m_failStage = 1;
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D3D_FEATURE_LEVEL req = D3D_FEATURE_LEVEL_11_0, got = (D3D_FEATURE_LEVEL)0;
	HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, &req, 1, D3D11_SDK_VERSION, &m_dev, &got, &m_ctx);
	if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, flags, &req, 1, D3D11_SDK_VERSION, &m_dev, &got, &m_ctx);
	if (FAILED(hr) || !m_dev) { m_lastHr = hr; return FALSE; }
	m_failStage = 2;
	IDXGIDevice* xd = NULL; IDXGIAdapter* xa = NULL; IDXGIFactory2* f2 = NULL;
	if (FAILED(hr = m_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd)) || FAILED(hr = xd->GetAdapter(&xa))) {
		TAC_REL(xd); m_lastHr = hr; return FALSE;
	}
	xa->GetParent(__uuidof(IDXGIFactory2), (void**)&f2);
	RECT rc; ::GetClientRect(hwnd, &rc);
	m_w = max(8, (int)(rc.right - rc.left)); m_h = max(8, (int)(rc.bottom - rc.top));
	DXGI_SWAP_CHAIN_DESC1 sd = {};
	sd.Width = m_w; sd.Height = m_h; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Scaling = DXGI_SCALING_STRETCH; sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
	hr = f2 ? f2->CreateSwapChainForHwnd(m_dev, hwnd, &sd, NULL, NULL, &m_swap) : E_FAIL;
	if (FAILED(hr) && f2) { sd.Flags = 0; hr = f2->CreateSwapChainForHwnd(m_dev, hwnd, &sd, NULL, NULL, &m_swap); }
	if (FAILED(hr) && f2) { sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; sd.BufferCount = 1; sd.Flags = 0; hr = f2->CreateSwapChainForHwnd(m_dev, hwnd, &sd, NULL, NULL, &m_swap); }
	if (f2) f2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
	TAC_REL(f2); TAC_REL(xa); TAC_REL(xd);
	if (FAILED(hr) || !m_swap) { m_lastHr = hr; return FALSE; }
	if (sd.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
		IDXGISwapChain2* s2 = NULL;
		if (SUCCEEDED(m_swap->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&s2)) && s2) {
			s2->SetMaximumFrameLatency(1);
			m_wait = s2->GetFrameLatencyWaitableObject();
			s2->Release();
		}
	}
	m_failStage = 3;
	if (!CreateShaders()) return FALSE;
	m_failStage = 4;
	if (!CreateStates()) return FALSE;
	m_failStage = 5;
	if (!CreateProcTextures()) return FALSE;
	m_failStage = 6;
	if (!CreateTargets()) return FALSE;
	m_failStage = 0;
	return TRUE;
}

void Renderer::SetQuality(int q)
{
	q = Clampi(q, 0, 2);
	if (q == m_quality) return;
	m_quality = q;
	if (m_dev) { ReleaseTargets(); TAC_REL(m_shSrv); TAC_REL(m_shDsv); TAC_REL(m_shTex); CreateTargets(); }
}

BOOL Renderer::CreateTargets()
{
	if (!m_dev || !m_swap) return FALSE;
	HRESULT hr = m_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&m_bb);
	if (FAILED(hr)) { m_lastHr = hr; return FALSE; }
	if (FAILED(hr = m_dev->CreateRenderTargetView(m_bb, NULL, &m_bbRtv))) { m_lastHr = hr; return FALSE; }
	int want = m_quality >= 2 ? 8 : (m_quality == 1 ? 4 : 1);
	m_msaa = 1;
	for (int s = want; s >= 2; s /= 2) {
		UINT ql = 0;
		if (SUCCEEDED(m_dev->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, s, &ql)) && ql > 0) { m_msaa = s; break; }
	}
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = m_w; td.Height = m_h; td.MipLevels = 1; td.ArraySize = 1;
	td.SampleDesc.Count = m_msaa;
	if (m_msaa > 1) {
		td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.BindFlags = D3D11_BIND_RENDER_TARGET;
		if (FAILED(m_dev->CreateTexture2D(&td, NULL, &m_msTex)) || FAILED(m_dev->CreateRenderTargetView(m_msTex, NULL, &m_msRtv))) {
			TAC_REL(m_msTex); TAC_REL(m_msRtv); m_msaa = 1; td.SampleDesc.Count = 1;
		}
	}
	if (m_msaa == 1) m_msRtv = m_bbRtv;
	td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (FAILED(hr = m_dev->CreateTexture2D(&td, NULL, &m_dsTex)) || FAILED(hr = m_dev->CreateDepthStencilView(m_dsTex, NULL, &m_dsv))) { m_lastHr = hr; return FALSE; }
	if (!m_shTex) {
		int ss = m_quality >= 2 ? 4096 : (m_quality == 1 ? 2048 : 1024);
		for (; ss >= 512; ss /= 2) {
			D3D11_TEXTURE2D_DESC st = {};
			st.Width = st.Height = ss; st.MipLevels = 1; st.ArraySize = 1; st.SampleDesc.Count = 1;
			st.Format = DXGI_FORMAT_R24G8_TYPELESS; st.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
			if (FAILED(m_dev->CreateTexture2D(&st, NULL, &m_shTex))) continue;
			D3D11_DEPTH_STENCIL_VIEW_DESC dd = {}; dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			D3D11_SHADER_RESOURCE_VIEW_DESC sv = {}; sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
			if (SUCCEEDED(m_dev->CreateDepthStencilView(m_shTex, &dd, &m_shDsv)) && SUCCEEDED(m_dev->CreateShaderResourceView(m_shTex, &sv, &m_shSrv))) { m_shSize = ss; break; }
			TAC_REL(m_shDsv); TAC_REL(m_shSrv); TAC_REL(m_shTex);
		}
	}
	if (!m_ptTex) {
		D3D11_TEXTURE2D_DESC pt = {};
		pt.Width = PORTRAIT_PX * 6; pt.Height = PORTRAIT_PX * 4; pt.MipLevels = 1; pt.ArraySize = 1; pt.SampleDesc.Count = 1;
		pt.Format = DXGI_FORMAT_B8G8R8A8_UNORM; pt.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (SUCCEEDED(m_dev->CreateTexture2D(&pt, NULL, &m_ptTex))) {
			m_dev->CreateRenderTargetView(m_ptTex, NULL, &m_ptRtv);
			m_dev->CreateShaderResourceView(m_ptTex, NULL, &m_srvPortrait);
			if (m_ptRtv) { float c[4] = { 0, 0, 0, 0 }; m_ctx->ClearRenderTargetView(m_ptRtv, c); }
		}
		pt.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; pt.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		if (SUCCEEDED(m_dev->CreateTexture2D(&pt, NULL, &m_ptDs))) m_dev->CreateDepthStencilView(m_ptDs, NULL, &m_ptDsv);
	}
	return TRUE;
}

BOOL Renderer::Resize(int w, int h)
{
	if (!m_swap) return FALSE;
	w = max(8, w); h = max(8, h);
	if (w == m_w && h == m_h) return TRUE;
	ReleaseTargets();
	m_ctx->ClearState();
	m_ctx->Flush();
	DXGI_SWAP_CHAIN_DESC1 d = {};
	m_swap->GetDesc1(&d);
	HRESULT hr = m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, d.Flags);
	if (FAILED(hr)) { m_lastHr = hr; m_lost = TRUE; return FALSE; }
	m_w = w; m_h = h;
	return CreateTargets();
}

BOOL Renderer::RefreshSurface()
{
	if (!m_swap || !m_ctx) return FALSE;
	ReleaseTargets();
	m_ctx->ClearState();
	m_ctx->Flush();
	DXGI_SWAP_CHAIN_DESC1 d = {};
	m_swap->GetDesc1(&d);
	HRESULT hr = m_swap->ResizeBuffers(0, m_w, m_h, DXGI_FORMAT_UNKNOWN, d.Flags);
	m_presented = FALSE;
	if (FAILED(hr)) { m_lastHr = hr; m_lost = TRUE; return FALSE; }
	return CreateTargets();
}

BOOL Renderer::FrameReady()
{
	if (!m_wait || !m_presented) return TRUE;
	return WaitForSingleObject(m_wait, 0) == WAIT_OBJECT_0;
}

// ============================================================ シェーダ・状態
static HRESULT LoadBlob(const char* entry, const char* prof, int rid, ID3DBlob** out)
{
	return Soft3DLoadCso(L"tac", entry, prof, rid, L"omake_tactics.hlsl", (void**)out);
}

BOOL Renderer::CreateShaders()
{
	struct VSDef { const char* e; int rid; ID3D11VertexShader** vs; };
	struct PSDef { const char* e; int rid; ID3D11PixelShader** ps; };
	ID3DBlob* bS = NULL; ID3DBlob* bK = NULL; ID3DBlob* bH = NULL; ID3DBlob* bP = NULL; ID3DBlob* bQ = NULL;
	HRESULT hr;
	if (FAILED(hr = LoadBlob("VSS", "vs_5_0", IDR_CSO_TAC_VSS, &bS))) { m_lastHr = hr; return FALSE; }
	if (FAILED(hr = LoadBlob("VSK", "vs_5_0", IDR_CSO_TAC_VSK, &bK))) { TAC_REL(bS); m_lastHr = hr; return FALSE; }
	if (FAILED(hr = LoadBlob("VSH", "vs_5_0", IDR_CSO_TAC_VSH, &bH))) { TAC_REL(bS); TAC_REL(bK); m_lastHr = hr; return FALSE; }
	if (FAILED(hr = LoadBlob("VSP", "vs_5_0", IDR_CSO_TAC_VSP, &bP))) { TAC_REL(bS); TAC_REL(bK); TAC_REL(bH); m_lastHr = hr; return FALSE; }
	if (FAILED(hr = LoadBlob("VSQ", "vs_5_0", IDR_CSO_TAC_VSQ, &bQ))) { TAC_REL(bS); TAC_REL(bK); TAC_REL(bH); TAC_REL(bP); m_lastHr = hr; return FALSE; }
	BOOL ok = TRUE;
	ok = ok && SUCCEEDED(m_dev->CreateVertexShader(bS->GetBufferPointer(), bS->GetBufferSize(), NULL, &m_vsS));
	ok = ok && SUCCEEDED(m_dev->CreateVertexShader(bK->GetBufferPointer(), bK->GetBufferSize(), NULL, &m_vsK));
	ok = ok && SUCCEEDED(m_dev->CreateVertexShader(bH->GetBufferPointer(), bH->GetBufferSize(), NULL, &m_vsH));
	ok = ok && SUCCEEDED(m_dev->CreateVertexShader(bP->GetBufferPointer(), bP->GetBufferSize(), NULL, &m_vsP));
	ok = ok && SUCCEEDED(m_dev->CreateVertexShader(bQ->GetBufferPointer(), bQ->GetBufferSize(), NULL, &m_vsQ));
	if (ok) {
		D3D11_INPUT_ELEMENT_DESC ls[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		D3D11_INPUT_ELEMENT_DESC lk[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "BLENDWEIGHT", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		D3D11_INPUT_ELEMENT_DESC lh[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		D3D11_INPUT_ELEMENT_DESC lp[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		ok = ok && SUCCEEDED(m_dev->CreateInputLayout(ls, 4, bS->GetBufferPointer(), bS->GetBufferSize(), &m_ilS));
		ok = ok && SUCCEEDED(m_dev->CreateInputLayout(lk, 5, bK->GetBufferPointer(), bK->GetBufferSize(), &m_ilK));
		ok = ok && SUCCEEDED(m_dev->CreateInputLayout(lh, 3, bH->GetBufferPointer(), bH->GetBufferSize(), &m_ilH));
		ok = ok && SUCCEEDED(m_dev->CreateInputLayout(lp, 3, bP->GetBufferPointer(), bP->GetBufferSize(), &m_ilP));
	}
	TAC_REL(bS); TAC_REL(bK); TAC_REL(bH); TAC_REL(bP); TAC_REL(bQ);
	if (!ok) { m_lastHr = E_FAIL; return FALSE; }
	PSDef ps[] = {
		{ "PSM", IDR_CSO_TAC_PSM, &m_psM }, { "PSO", IDR_CSO_TAC_PSO, &m_psO }, { "PSOV", IDR_CSO_TAC_PSOV, &m_psOV },
		{ "PSH", IDR_CSO_TAC_PSH, &m_psH }, { "PSP", IDR_CSO_TAC_PSP, &m_psP }, { "PSSKY", IDR_CSO_TAC_PSSKY, &m_psSky },
	};
	for (auto& d : ps) {
		ID3DBlob* b = NULL;
		if (FAILED(hr = LoadBlob(d.e, "ps_5_0", d.rid, &b))) { m_lastHr = hr; return FALSE; }
		hr = m_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, d.ps);
		TAC_REL(b);
		if (FAILED(hr)) { m_lastHr = hr; return FALSE; }
	}
	(void)sizeof(VSDef);
	return TRUE;
}

BOOL Renderer::CreateStates()
{
	D3D11_BUFFER_DESC bd = {};
	bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.ByteWidth = (sizeof(FrameCB) + 15) & ~15;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbFrame))) return FALSE;
	bd.ByteWidth = 64 + 16 * 4;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbObj))) return FALSE;
	bd.ByteWidth = 64 * MAXJ;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbBones))) return FALSE;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.ByteWidth = sizeof(VtxH) * 6 * 4096;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_vbHud))) return FALSE;
	m_vbDynBytes = 4u * 1024u * 1024u;
	bd.ByteWidth = m_vbDynBytes;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_vbDyn))) return FALSE;

	D3D11_SAMPLER_DESC ss = {};
	ss.Filter = D3D11_FILTER_ANISOTROPIC; ss.MaxAnisotropy = 8;
	ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; ss.MaxLOD = D3D11_FLOAT32_MAX;
	m_dev->CreateSamplerState(&ss, &m_sLin);
	ss.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; ss.MaxAnisotropy = 1;
	ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	m_dev->CreateSamplerState(&ss, &m_sClamp);
	ss.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; ss.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
	ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
	ss.BorderColor[0] = ss.BorderColor[1] = ss.BorderColor[2] = ss.BorderColor[3] = 1.f;
	m_dev->CreateSamplerState(&ss, &m_sCmp);

	D3D11_RASTERIZER_DESC rs = {};
	rs.FillMode = D3D11_FILL_SOLID; rs.DepthClipEnable = TRUE; rs.MultisampleEnable = TRUE; rs.AntialiasedLineEnable = FALSE;
	rs.CullMode = D3D11_CULL_BACK; m_dev->CreateRasterizerState(&rs, &m_rsBack);
	rs.CullMode = D3D11_CULL_FRONT; m_dev->CreateRasterizerState(&rs, &m_rsFront);
	rs.CullMode = D3D11_CULL_NONE; m_dev->CreateRasterizerState(&rs, &m_rsNone);
	rs.MultisampleEnable = FALSE; rs.ScissorEnable = FALSE; m_dev->CreateRasterizerState(&rs, &m_rsHud);
	rs.CullMode = D3D11_CULL_NONE; rs.DepthBias = 1200; rs.SlopeScaledDepthBias = 2.0f; rs.DepthBiasClamp = 0.f;
	m_dev->CreateRasterizerState(&rs, &m_rsShadow);

	D3D11_DEPTH_STENCIL_DESC ds = {};
	ds.DepthEnable = TRUE; ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	m_dev->CreateDepthStencilState(&ds, &m_dsWrite);
	ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	m_dev->CreateDepthStencilState(&ds, &m_dsRead);
	ds.DepthEnable = FALSE;
	m_dev->CreateDepthStencilState(&ds, &m_dsOff);

	D3D11_BLEND_DESC bl = {};
	bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	m_dev->CreateBlendState(&bl, &m_bsOpaque);
	auto& t = bl.RenderTarget[0];
	t.BlendEnable = TRUE; t.BlendOp = t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	t.SrcBlend = D3D11_BLEND_SRC_ALPHA; t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; t.SrcBlendAlpha = D3D11_BLEND_ONE; t.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	m_dev->CreateBlendState(&bl, &m_bsAlpha);
	t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	m_dev->CreateBlendState(&bl, &m_bsPremul);
	t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_ONE; t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE;
	m_dev->CreateBlendState(&bl, &m_bsAdd);
	return m_sLin && m_sCmp && m_rsBack && m_dsWrite && m_bsAlpha && m_bsAdd;
}

// ============================================================ テクスチャ
static BOOL MakeTex(ID3D11Device* dev, ID3D11DeviceContext* ctx, const DWORD* px, int w, int h, BOOL mips, ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv)
{
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = w; td.Height = h; td.ArraySize = 1; td.SampleDesc.Count = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	if (mips) {
		td.MipLevels = 0; td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
		if (FAILED(dev->CreateTexture2D(&td, NULL, tex))) return FALSE;
		ctx->UpdateSubresource(*tex, 0, NULL, px, w * 4, 0);
		if (FAILED(dev->CreateShaderResourceView(*tex, NULL, srv))) return FALSE;
		ctx->GenerateMips(*srv);
	} else {
		td.MipLevels = 1; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA sd = { px, (UINT)w * 4, 0 };
		if (FAILED(dev->CreateTexture2D(&td, &sd, tex))) return FALSE;
		if (FAILED(dev->CreateShaderResourceView(*tex, NULL, srv))) return FALSE;
	}
	return TRUE;
}

static DWORD BGRA(int r, int g, int b, int a = 255)
{
	return (DWORD)(Clampi(b, 0, 255) | (Clampi(g, 0, 255) << 8) | (Clampi(r, 0, 255) << 16) | (Clampi(a, 0, 255) << 24));
}

static void HsvToRgb(float h, float s, float v, float& r, float& g, float& b)
{
	h = h - floorf(h);
	float i = floorf(h * 6), f = h * 6 - i;
	float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
	switch ((int)i % 6) {
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
}

DWORD Renderer::PaletteColor(int idx)
{
	idx = Clampi(idx, 0, 255);
	int row = idx / 16, col = idx % 16;
	if (row == 0) { int g = col * 255 / 15; return RGBA8(g, g, g); }
	static const float S[15] = { .85f, .85f, .85f, .85f, .85f, .6f, .6f, .6f, .6f, .35f, .35f, .35f, .35f, .15f, .15f };
	static const float V[15] = { .25f, .45f, .65f, .85f, 1.f, .35f, .6f, .85f, 1.f, .5f, .75f, .95f, 1.f, .7f, .95f };
	float r, g, b;
	HsvToRgb(col / 16.f, S[row - 1], V[row - 1], r, g, b);
	return RGBAf(r, g, b);
}

void Renderer::PaletteUV(int idx, float& u, float& v)
{
	idx = Clampi(idx, 0, 255);
	u = ((idx % 16) + .5f) / 16.f;
	v = ((idx / 16) + .5f) / 16.f;
}

static float TNoise(DWORD s, float x, float y)
{
	auto H = [s](int ix, int iy) {
		DWORD h = s * 0x9E3779B1u ^ (DWORD)ix * 0x85EBCA77u ^ (DWORD)iy * 0xC2B2AE3Du;
		h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
		return (h & 0xFFFF) / 65535.f;
	};
	int ix = (int)floorf(x), iy = (int)floorf(y);
	float fx = x - ix, fy = y - iy;
	fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
	return Lerpf(Lerpf(H(ix, iy), H(ix + 1, iy), fx), Lerpf(H(ix, iy + 1), H(ix + 1, iy + 1), fx), fy);
}
static float TFbm(DWORD s, float x, float y)
{
	float v = 0, a = .5f, n = 0;
	for (int i = 0; i < 4; i++) { v += TNoise(s + i * 77, x, y) * a; n += a; x *= 2.1f; y *= 2.1f; a *= .5f; }
	return v / n;
}

// 地形タイル（4x4, 128px）
enum {
	TL_GRASS, TL_FOREST, TL_DIRT, TL_ROCK, TL_SAND, TL_SNOW, TL_MUD, TL_COBBLE,
	TL_RUINS, TL_ASH, TL_CLIFF_DIRT, TL_CLIFF_ROCK, TL_BED, TL_LAVA, TL_PLANK, TL_CLIFF_SNOW
};

static void TileColor(int tile, int x, int y, float& r, float& g, float& b)
{
	const float fx = (float)x, fy = (float)y;
	float n = TFbm(tile * 131 + 7, fx / 18.f, fy / 18.f);
	float d = TFbm(tile * 57 + 3, fx / 5.f, fy / 5.f);
	switch (tile) {
	case TL_GRASS: {
		r = .36f + n * .12f; g = .58f + n * .16f; b = .25f + n * .06f;
		float bl = TNoise(91, fx * .9f, fy * .25f);
		if (bl > .72f) { r *= .82f; g *= .9f; b *= .8f; }
		if (TNoise(17, fx * .35f, fy * .35f) > .93f) { r = .95f; g = .9f; b = .45f; }
		break; }
	case TL_FOREST:
		r = .25f + n * .1f; g = .42f + n * .14f; b = .2f + n * .05f;
		if (d > .68f) { r = .45f + d * .1f; g = .33f; b = .18f; }
		break;
	case TL_DIRT:
		r = .55f + n * .12f; g = .42f + n * .1f; b = .28f + n * .06f;
		if (d > .75f) { r *= .8f; g *= .8f; b *= .8f; }
		if (TNoise(5, fx * .5f, fy * .5f) > .9f) { r = .7f; g = .66f; b = .6f; }
		break;
	case TL_ROCK: {
		float v = .45f + n * .2f;
		r = v * 1.02f; g = v; b = v * .97f;
		if (fabsf(TNoise(33, fx / 9.f, fy / 9.f) - .5f) < .03f) { r *= .6f; g *= .6f; b *= .6f; }
		break; }
	case TL_SAND: {
		float rip = sinf(fy * .42f + n * 9.f) * .5f + .5f;
		r = .86f + rip * .05f; g = .76f + rip * .05f; b = .52f + rip * .04f;
		break; }
	case TL_SNOW: {
		float v = .9f + n * .08f;
		r = v * .95f; g = v * .98f; b = v;
		if (TNoise(71, fx * .7f, fy * .7f) > .94f) { r = g = b = 1.f; }
		break; }
	case TL_MUD:
		r = .3f + n * .1f; g = .32f + n * .1f; b = .2f + n * .06f;
		if (d > .62f) { r = .22f; g = .3f; b = .24f; }
		break;
	case TL_COBBLE: {
		float cx = fx / 16.f + (((int)(fy / 16.f)) & 1) * .5f, cy = fy / 16.f;
		float ex = fabsf(cx - floorf(cx) - .5f), ey = fabsf(cy - floorf(cy) - .5f);
		float e = max(ex, ey);
		float v = .58f + TNoise(9, floorf(cx) * 3.f, floorf(cy) * 3.f) * .14f + n * .06f;
		if (e > .42f) v *= .55f;
		r = v * 1.03f; g = v * .98f; b = v * .9f;
		break; }
	case TL_RUINS: {
		float cx = fx / 32.f, cy = fy / 32.f;
		float ex = fabsf(cx - floorf(cx) - .5f), ey = fabsf(cy - floorf(cy) - .5f);
		float v = .62f + n * .12f;
		if (max(ex, ey) > .46f) v *= .5f;
		r = v; g = v * .97f; b = v * .9f;
		if (d > .72f) { r *= .7f; g *= .95f; b *= .65f; }
		break; }
	case TL_ASH:
		r = .3f + n * .1f; g = .27f + n * .09f; b = .26f + n * .08f;
		if (TNoise(41, fx * .4f, fy * .4f) > .93f) { r = .95f; g = .4f; b = .1f; }
		break;
	case TL_CLIFF_DIRT: {
		float s = sinf(fy * .3f + n * 4.f) * .5f + .5f;
		r = .45f + s * .1f + n * .06f; g = .33f + s * .08f; b = .22f + s * .05f;
		break; }
	case TL_CLIFF_ROCK: {
		float s = sinf(fy * .25f + n * 5.f) * .5f + .5f;
		float v = .38f + s * .12f + n * .08f;
		r = v; g = v * .98f; b = v * .95f;
		break; }
	case TL_BED:
		r = .3f + n * .1f; g = .36f + n * .1f; b = .32f + n * .08f;
		break;
	case TL_LAVA: {
		float c = fabsf(TFbm(55, fx / 14.f, fy / 14.f) - .5f);
		r = .22f + n * .08f; g = .1f; b = .08f;
		if (c < .05f) { r = 1.f; g = .55f; b = .12f; }
		break; }
	case TL_PLANK: {
		float p = fy / 21.f;
		float e = fabsf(p - floorf(p) - .5f);
		float v = .55f + TNoise(3, floorf(p) * 4.f, fx / 40.f) * .15f + TNoise(8, fx * .05f, fy * 1.2f) * .1f;
		r = v; g = v * .72f; b = v * .45f;
		if (e > .44f) { r *= .4f; g *= .4f; b *= .4f; }
		break; }
	default: {
		float t = fy / 128.f;
		float v = .4f + n * .1f;
		r = Lerpf(.95f, v, Smooth01(t * 2.5f)); g = Lerpf(.97f, v, Smooth01(t * 2.5f)); b = Lerpf(1.f, v * .95f, Smooth01(t * 2.5f));
		break; }
	}
}

BOOL Renderer::CreateProcTextures()
{
	{
		DWORD w[16];
		for (int i = 0; i < 16; i++) w[i] = 0xFFFFFFFF;
		if (!MakeTex(m_dev, m_ctx, w, 4, 4, FALSE, &m_tex[TEX_WHITE], &m_srv[TEX_WHITE])) return FALSE;
	}
	{
		std::vector<DWORD> p(64 * 64);
		for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
			DWORD c = PaletteColor((y / 4) * 16 + (x / 4));
			p[y * 64 + x] = BGRA(c & 255, (c >> 8) & 255, (c >> 16) & 255);
		}
		if (!MakeTex(m_dev, m_ctx, p.data(), 64, 64, FALSE, &m_tex[TEX_PAL], &m_srv[TEX_PAL])) return FALSE;
	}
	{
		const int S = 512, T = 128;
		std::vector<DWORD> p(S * S);
		for (int tile = 0; tile < 16; tile++) {
			int ox = (tile % 4) * T, oy = (tile / 4) * T;
			for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
				float r, g, b;
				TileColor(tile, x, y, r, g, b);
				p[(oy + y) * S + ox + x] = BGRA((int)(r * 255), (int)(g * 255), (int)(b * 255));
			}
		}
		if (!MakeTex(m_dev, m_ctx, p.data(), S, S, TRUE, &m_tex[TEX_ATLAS], &m_srv[TEX_ATLAS])) return FALSE;
	}
	return TRUE;
}

BOOL Renderer::LoadPngTex(const BYTE* data, UINT len, int slot)
{
	IWICImagingFactory* f = NULL;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
	if (FAILED(hr) || !f) return FALSE;
	IWICStream* st = NULL; IWICBitmapDecoder* dec = NULL; IWICBitmapFrameDecode* fr = NULL; IWICFormatConverter* cv = NULL;
	BOOL ok = FALSE;
	if (SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)data, len))
		&& SUCCEEDED(f->CreateDecoderFromStream(st, NULL, WICDecodeMetadataCacheOnLoad, &dec))
		&& SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&cv))
		&& SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
		UINT w = 0, h = 0;
		cv->GetSize(&w, &h);
		if (w > 0 && h > 0 && w <= 8192 && h <= 8192) {
			std::vector<DWORD> px((size_t)w * h);
			if (SUCCEEDED(cv->CopyPixels(NULL, w * 4, w * h * 4, (BYTE*)px.data())))
				ok = MakeTex(m_dev, m_ctx, px.data(), w, h, TRUE, &m_tex[slot], &m_srv[slot]);
		}
	}
	TAC_REL(cv); TAC_REL(fr); TAC_REL(dec); TAC_REL(st); TAC_REL(f);
	return ok;
}

// ============================================================ アセット
struct Reader {
	const BYTE* p; size_t n, o; bool bad;
	Reader(const BYTE* d, size_t len) : p(d), n(len), o(0), bad(false) {}
	bool Need(size_t k) { if (o + k > n) { bad = true; return false; } return true; }
	UINT U32() { if (!Need(4)) return 0; UINT v; memcpy(&v, p + o, 4); o += 4; return v; }
	int I32() { return (int)U32(); }
	float F32() { if (!Need(4)) return 0; float v; memcpy(&v, p + o, 4); o += 4; return v; }
	void Name(char* out, int len) { if (!Need(len)) { out[0] = 0; return; } memcpy(out, p + o, len); out[len - 1] = 0; o += len; }
	const BYTE* Bytes(size_t k) { if (!Need(k)) return NULL; const BYTE* r = p + o; o += k; return r; }
	void Align4() { o = (o + 3) & ~(size_t)3; }
};

static void FlipM(M4& m)
{
	m.m[2] = -m.m[2]; m.m[6] = -m.m[6]; m.m[14] = -m.m[14];
	m.m[8] = -m.m[8]; m.m[9] = -m.m[9]; m.m[11] = -m.m[11];
}

BOOL Renderer::MakeMesh(GpuMesh& out, const void* v, UINT stride, UINT nv, const WORD* idx, UINT ni, BOOL dynamic)
{
	out.Release();
	if (!nv || !ni) return FALSE;
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = stride * nv; bd.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
	D3D11_SUBRESOURCE_DATA sd = { v, 0, 0 };
	if (FAILED(m_dev->CreateBuffer(&bd, &sd, &out.vb))) return FALSE;
	bd.ByteWidth = ((ni * 2) + 3) & ~3u; bd.BindFlags = D3D11_BIND_INDEX_BUFFER; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.CPUAccessFlags = 0;
	std::vector<WORD> tmp(idx, idx + ni);
	tmp.resize(bd.ByteWidth / 2, 0);
	sd.pSysMem = tmp.data();
	if (FAILED(m_dev->CreateBuffer(&bd, &sd, &out.ib))) { out.Release(); return FALSE; }
	out.nIdx = ni; out.stride = stride; out.i32 = 0;
	return TRUE;
}

BOOL Renderer::MakeMesh32(GpuMesh& out, const void* v, UINT stride, UINT nv, const UINT* idx, UINT ni)
{
	out.Release();
	if (!nv || !ni) return FALSE;
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = stride * nv; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA sd = { v, 0, 0 };
	if (FAILED(m_dev->CreateBuffer(&bd, &sd, &out.vb))) return FALSE;
	bd.ByteWidth = ni * 4; bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	sd.pSysMem = idx;
	if (FAILED(m_dev->CreateBuffer(&bd, &sd, &out.ib))) { out.Release(); return FALSE; }
	out.nIdx = ni; out.stride = stride; out.i32 = 1;
	return TRUE;
}

static void ModelSetupJoints(Model& m, const Rig* r)
{
	m.headJ = m.chestJ = m.handR = m.handL = m.hipsJ = -1;
	if (!r) return;
	m.headJ = r->Find("head"); if (m.headJ < 0) m.headJ = r->Find("Head");
	m.chestJ = r->Find("chest"); if (m.chestJ < 0) m.chestJ = r->Find("Chest");
	m.handR = r->Find("handslot.r"); if (m.handR < 0) m.handR = r->Find("Hand.R");
	m.handL = r->Find("handslot.l"); if (m.handL < 0) m.handL = r->Find("Hand.L");
	m.hipsJ = r->Find("hips"); if (m.hipsJ < 0) m.hipsJ = r->Find("Hips");
}

BOOL Renderer::LoadAssets()
{
	if (m_assetsOk) return TRUE;
	HMODULE hm = AfxGetResourceHandle();
	HRSRC rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_ASSETS), RT_RCDATA);
	if (!rs) { hm = GetModuleHandle(NULL); rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_ASSETS), RT_RCDATA); }
	if (!rs) return FALSE;
	HGLOBAL hg = LoadResource(hm, rs);
	const BYTE* data = hg ? (const BYTE*)LockResource(hg) : NULL;
	DWORD size = SizeofResource(hm, rs);
	if (!data || size < 32 || memcmp(data, "TAC1", 4) != 0) return FALSE;
	Reader rd(data + 4, size - 4);
	UINT ver = rd.U32(), nTex = rd.U32(), nRig = rd.U32(), nMod = rd.U32(), nAtt = rd.U32(), nSt = rd.U32();
	if (ver != 1 || nTex > 12 || nRig > 8 || nMod > 32) return FALSE;
	for (UINT i = 0; i < nTex; i++) {
		UINT len = rd.U32();
		const BYTE* b = rd.Bytes(len);
		rd.Align4();
		if (!b || !LoadPngTex(b, len, (int)i)) return FALSE;
	}
	rigs.resize(nRig);
	for (UINT r = 0; r < nRig; r++) {
		Rig& g = rigs[r];
		g.nJ = (int)rd.U32();
		if (g.nJ <= 0 || g.nJ > MAXJ) return FALSE;
		for (int j = 0; j < g.nJ; j++) { rd.Name(g.names[j], 24); g.parent[j] = rd.I32(); }
		UINT nc = rd.U32();
		g.hips = 0;
		g.clips.resize(nc);
		for (UINT c = 0; c < nc; c++) {
			Clip& cl = g.clips[c];
			rd.Name(cl.name, 32);
			cl.nFrames = (int)rd.U32(); cl.fps = rd.F32(); g.hips = (int)rd.U32();
			if (cl.nFrames <= 0 || cl.nFrames > 4000) return FALSE;
			const short* q = (const short*)rd.Bytes((size_t)cl.nFrames * g.nJ * 8);
			const float* h = (const float*)rd.Bytes((size_t)cl.nFrames * 12);
			if (!q || !h) return FALSE;
			cl.q.resize((size_t)cl.nFrames * g.nJ);
			for (size_t k = 0; k < cl.q.size(); k++) {
				Q4 v = { -q[k * 4] / 32767.f, -q[k * 4 + 1] / 32767.f, q[k * 4 + 2] / 32767.f, q[k * 4 + 3] / 32767.f };
				float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
				if (l > 1e-6f) { v.x /= l; v.y /= l; v.z /= l; v.w /= l; }
				cl.q[k] = v;
			}
			cl.hips.resize(cl.nFrames);
			for (int f = 0; f < cl.nFrames; f++) cl.hips[f] = v3(h[f * 3], h[f * 3 + 1], -h[f * 3 + 2]);
		}
	}
	models.resize(nMod);
	for (UINT mi = 0; mi < nMod; mi++) {
		Model& m = models[mi];
		rd.Name(m.name, 32);
		m.rig = (int)rd.U32(); m.tex = (int)rd.U32();
		if (m.rig < 0 || m.rig >= (int)nRig) return FALSE;
		const Rig& g = rigs[m.rig];
		m.nJ = g.nJ;
		for (int j = 0; j < m.nJ; j++) {
			m.parent[j] = g.parent[j];
			float t[10];
			for (int k = 0; k < 10; k++) t[k] = rd.F32();
			m.restT[j] = v3(t[0], t[1], -t[2]);
			Q4 q = { -t[3], -t[4], t[5], t[6] };
			m.restR[j] = q;
			m.restS[j] = v3(t[7], t[8], t[9]);
		}
		for (int k = 0; k < 16; k++) m.top.m[k] = rd.F32();
		FlipM(m.top);
		for (int j = 0; j < m.nJ; j++) { for (int k = 0; k < 16; k++) m.invBind[j].m[k] = rd.F32(); FlipM(m.invBind[j]); }
		UINT np = rd.U32();
		m.headMin[0] = m.headMin[1] = m.headMin[2] = 1e9f;
		m.headMax[0] = m.headMax[1] = m.headMax[2] = -1e9f;
		ModelSetupJoints(m, &g);
		for (UINT p = 0; p < np; p++) {
			char pn[32]; rd.Name(pn, 32);
			UINT glow = rd.U32(), nv = rd.U32(), ni = rd.U32();
			const BYTE* vb = rd.Bytes((size_t)nv * 40);
			const WORD* ib = (const WORD*)rd.Bytes((size_t)ni * 2);
			rd.Align4();
			if (!vb || !ib) return FALSE;
			std::vector<VtxK> v(nv);
			memcpy(v.data(), vb, (size_t)nv * 40);
			for (auto& x : v) {
				x.p[2] = -x.p[2]; x.n[2] = -x.n[2];
				if (x.j[0] == m.headJ && x.w[0] > 200) {
					for (int k = 0; k < 3; k++) { m.headMin[k] = min(m.headMin[k], x.p[k]); m.headMax[k] = max(m.headMax[k], x.p[k]); }
				}
			}
			std::vector<WORD> idx(ib, ib + ni);
			for (UINT k = 0; k + 2 < ni; k += 3) std::swap(idx[k + 1], idx[k + 2]);
			GpuMesh gm;
			if (!MakeMesh(gm, v.data(), sizeof(VtxK), nv, idx.data(), ni)) return FALSE;
			gm.tex = m.tex; gm.glow = (int)glow;
			m.parts.push_back(gm);
		}
		// 頭部範囲をボーン基準へ
		if (m.headJ >= 0 && m.headMin[0] < 1e8f) {
			M4 bindHead = MInverse(m.invBind[m.headJ]);
			V3 o = v3(bindHead.m[12], bindHead.m[13], bindHead.m[14]);
			for (int k = 0; k < 3; k++) { m.headMin[k] -= (&o.x)[k]; m.headMax[k] -= (&o.x)[k]; }
		} else {
			m.headMin[0] = -.5f; m.headMin[1] = 0; m.headMin[2] = -.5f; m.headMax[0] = .5f; m.headMax[1] = 1.f; m.headMax[2] = .5f;
		}
	}
	attaches.resize(nAtt);
	for (UINT ai = 0; ai < nAtt; ai++) {
		Attach& a = attaches[ai];
		rd.Name(a.name, 32); rd.Name(a.joint, 24);
		a.tex = (int)rd.U32();
		UINT nv = rd.U32(), ni = rd.U32();
		const float* vb = (const float*)rd.Bytes((size_t)nv * 32);
		const WORD* ib = (const WORD*)rd.Bytes((size_t)ni * 2);
		rd.Align4();
		if (!vb || !ib) return FALSE;
		std::vector<VtxS> v(nv);
		for (UINT k = 0; k < nv; k++) {
			const float* s = vb + k * 8;
			VtxS& d = v[k];
			d.p[0] = s[0]; d.p[1] = s[1]; d.p[2] = -s[2];
			d.n[0] = s[3]; d.n[1] = s[4]; d.n[2] = -s[5];
			d.uv[0] = s[6]; d.uv[1] = s[7];
			d.c = 0xFFFFFFFF;
		}
		if (strstr(a.name, "Cape")) {
			// 法線は背中の外（+Z）を向き、正面の光が当たらず外側が黒くなる。光の来る -Z へ向ける。
			for (UINT k = 0; k < nv; k++) {
				v[k].n[0] = -v[k].n[0];
				v[k].n[1] = -v[k].n[1];
				v[k].n[2] = -v[k].n[2];
			}
		}
		std::vector<WORD> idx(ib, ib + ni);
		for (UINT k = 0; k + 2 < ni; k += 3) std::swap(idx[k + 1], idx[k + 2]);
		if (!MakeMesh(a.mesh, v.data(), sizeof(VtxS), nv, idx.data(), ni)) return FALSE;
		a.mesh.tex = a.tex;
	}
	statics.resize(nSt);
	for (UINT si = 0; si < nSt; si++) {
		StaticSrc& s = statics[si];
		rd.Name(s.name, 32);
		s.tex = (int)rd.U32();
		UINT nv = rd.U32(), ni = rd.U32();
		for (int k = 0; k < 6; k++) s.bb[k] = rd.F32();
		float z0 = -s.bb[5], z1 = -s.bb[2];
		s.bb[2] = z0; s.bb[5] = z1;
		const float* vb = (const float*)rd.Bytes((size_t)nv * 32);
		const WORD* ib = (const WORD*)rd.Bytes((size_t)ni * 2);
		rd.Align4();
		if (!vb || !ib) return FALSE;
		s.v.resize(nv);
		for (UINT k = 0; k < nv; k++) {
			const float* q = vb + k * 8;
			VtxS& d = s.v[k];
			d.p[0] = q[0]; d.p[1] = q[1]; d.p[2] = -q[2];
			d.n[0] = q[3]; d.n[1] = q[4]; d.n[2] = -q[5];
			d.uv[0] = q[6]; d.uv[1] = q[7];
			d.c = 0xFFFFFFFF;
		}
		s.i.assign(ib, ib + ni);
		for (UINT k = 0; k + 2 < ni; k += 3) std::swap(s.i[k + 1], s.i[k + 2]);
	}
	if (rd.bad) return FALSE;
	LoadCreaturePack();
	LoadGearPack();
	LoadFacePack();
	LoadAvatarPack();
	m_assetsOk = TRUE;
	return TRUE;
}

// packed=0 は頂点9 float。packed=1 は位置・法線 int16 と色バイト（TACP / TACQ）。
static BOOL ReadColorMesh(Reader& rd, int packed, std::vector<VtxS>& v, std::vector<WORD>& idx)
{
	UINT nv = rd.U32(), ni = rd.U32();
	if (!nv || !ni || nv > 200000 || ni > 400000) { rd.bad = true; return FALSE; }
	v.resize(nv);
	if (!packed) {
		const float* vb = (const float*)rd.Bytes((size_t)nv * 9 * 4);
		const WORD* ib = (const WORD*)rd.Bytes((size_t)ni * 2);
		rd.Align4();
		if (!vb || !ib) return FALSE;
		for (UINT k = 0; k < nv; k++) {
			const float* q = vb + k * 9;
			VtxS& d = v[k];
			d.p[0] = q[0]; d.p[1] = q[1]; d.p[2] = -q[2];
			d.n[0] = q[3]; d.n[1] = q[4]; d.n[2] = -q[5];
			d.uv[0] = 0; d.uv[1] = 0;
			int r = (int)(q[6] * 255.f), g = (int)(q[7] * 255.f), b = (int)(q[8] * 255.f);
			d.c = RGBA8(r, g, b);
		}
		idx.assign(ib, ib + ni);
	} else {
		float emin[3], emax[3], mid[3], half[3];
		for (int a = 0; a < 3; a++) emin[a] = rd.F32();
		for (int a = 0; a < 3; a++) emax[a] = rd.F32();
		for (int a = 0; a < 3; a++) {
			mid[a] = (emin[a] + emax[a]) * 0.5f;
			half[a] = (emax[a] - emin[a]) * 0.5f;
		}
		const short* ps = (const short*)rd.Bytes((size_t)nv * 6);
		const short* ns = (const short*)rd.Bytes((size_t)nv * 6);
		const BYTE* rgb = rd.Bytes((size_t)nv * 3);
		rd.Align4();
		const WORD* ib = (const WORD*)rd.Bytes((size_t)ni * 2);
		rd.Align4();
		if (!ps || !ns || !rgb || !ib) return FALSE;
		for (UINT k = 0; k < nv; k++) {
			VtxS& d = v[k];
			d.p[0] = mid[0] + (ps[k * 3] / 32767.f) * half[0];
			d.p[1] = mid[1] + (ps[k * 3 + 1] / 32767.f) * half[1];
			d.p[2] = -(mid[2] + (ps[k * 3 + 2] / 32767.f) * half[2]);
			d.n[0] = ns[k * 3] / 32767.f;
			d.n[1] = ns[k * 3 + 1] / 32767.f;
			d.n[2] = -(ns[k * 3 + 2] / 32767.f);
			d.uv[0] = 0; d.uv[1] = 0;
			d.c = RGBA8(rgb[k * 3], rgb[k * 3 + 1], rgb[k * 3 + 2]);
		}
		idx.assign(ib, ib + ni);
	}
	for (UINT k = 0; k + 2 < ni; k += 3) std::swap(idx[k + 1], idx[k + 2]);
	return TRUE;
}

void Renderer::LoadCreaturePack()
{
	for (auto& c : m_creatures) c.Release();
	m_creatures.clear();
	m_skins.clear();
	ReleaseAvatarCache();
	HMODULE hm = AfxGetResourceHandle();
	HRSRC rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_MON), RT_RCDATA);
	if (!rs) { hm = GetModuleHandle(NULL); rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_MON), RT_RCDATA); }
	if (!rs) return;
	HGLOBAL hg = LoadResource(hm, rs);
	const BYTE* data = hg ? (const BYTE*)LockResource(hg) : NULL;
	DWORD size = SizeofResource(hm, rs);
	if (!data || size < 8) return;
	if (memcmp(data, "TACK", 4) == 0) {
		Reader rd(data + 4, size - 4);
		UINT n = rd.U32();
		if (n > 256) return;
		m_creatures.reserve(n);
		m_skins.reserve(n);
		for (UINT i = 0; i < n; i++) {
			char name[32];
			rd.Name(name, 32);
			UINT nJ = rd.U32();
			if (nJ < 1 || nJ > MAXJ) return;
			Rig g;
			g.nJ = (int)nJ;
			g.hips = 0;
			for (UINT j = 0; j < nJ; j++) { rd.Name(g.names[j], 24); g.parent[j] = rd.I32(); }
			UINT nc = rd.U32();
			if (nc > 32) return;
			g.clips.resize(nc);
			for (UINT c = 0; c < nc; c++) {
				Clip& cl = g.clips[c];
				rd.Name(cl.name, 32);
				cl.nFrames = (int)rd.U32(); cl.fps = rd.F32(); g.hips = (int)rd.U32();
				if (cl.nFrames <= 0 || cl.nFrames > 64) return;
				const short* q = (const short*)rd.Bytes((size_t)cl.nFrames * nJ * 8);
				const float* h = (const float*)rd.Bytes((size_t)cl.nFrames * 12);
				if (!q || !h) return;
				cl.q.resize((size_t)cl.nFrames * nJ);
				for (size_t k = 0; k < cl.q.size(); k++) {
					Q4 v = { -q[k * 4] / 32767.f, -q[k * 4 + 1] / 32767.f, q[k * 4 + 2] / 32767.f, q[k * 4 + 3] / 32767.f };
					float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
					if (l > 1e-6f) { v.x /= l; v.y /= l; v.z /= l; v.w /= l; }
					cl.q[k] = v;
				}
				cl.hips.resize(cl.nFrames);
				for (int f = 0; f < cl.nFrames; f++) cl.hips[f] = v3(h[f * 3], h[f * 3 + 1], -h[f * 3 + 2]);
			}
			Model m;
			strncpy_s(m.name, name, 31);
			m.rig = (int)rigs.size();
			m.tex = TEX_WHITE;
			m.nJ = (int)nJ;
			for (UINT j = 0; j < nJ; j++) {
				m.parent[j] = g.parent[j];
				float t[10];
				for (int k = 0; k < 10; k++) t[k] = rd.F32();
				m.restT[j] = v3(t[0], t[1], -t[2]);
				Q4 qq = { -t[3], -t[4], t[5], t[6] };
				float l = sqrtf(qq.x * qq.x + qq.y * qq.y + qq.z * qq.z + qq.w * qq.w);
				if (l > 1e-6f) { qq.x /= l; qq.y /= l; qq.z /= l; qq.w /= l; }
				m.restR[j] = qq;
				m.restS[j] = v3(t[7], t[8], t[9]);
			}
			for (int k = 0; k < 16; k++) m.top.m[k] = rd.F32();
			FlipM(m.top);
			for (UINT j = 0; j < nJ; j++) { for (int k = 0; k < 16; k++) m.invBind[j].m[k] = rd.F32(); FlipM(m.invBind[j]); }
			UINT nv = rd.U32(), ni = rd.U32();
			if (!nv || !ni || nv > 70000 || ni > 200000) return;
			float emin[3], emax[3], mid[3], half[3];
			for (int a = 0; a < 3; a++) emin[a] = rd.F32();
			for (int a = 0; a < 3; a++) emax[a] = rd.F32();
			for (int a = 0; a < 3; a++) { mid[a] = (emin[a] + emax[a]) * 0.5f; half[a] = (emax[a] - emin[a]) * 0.5f; }
			const short* ps = (const short*)rd.Bytes((size_t)nv * 6);
			const short* ns = (const short*)rd.Bytes((size_t)nv * 6);
			const BYTE* rgb = rd.Bytes((size_t)nv * 3);
			const BYTE* jj = rd.Bytes((size_t)nv * 4);
			const BYTE* ww = rd.Bytes((size_t)nv * 4);
			rd.Align4();
			const WORD* ib = (const WORD*)rd.Bytes((size_t)ni * 2);
			rd.Align4();
			if (!ps || !ns || !rgb || !jj || !ww || !ib) return;
			CreatureSkin skin;
			skin.model = (int)models.size();
			skin.bind.resize(nv);
			std::vector<VtxS> poseV(nv);
			std::vector<WORD> idx(ib, ib + ni);
			for (UINT k = 0; k + 2 < ni; k += 3) std::swap(idx[k + 1], idx[k + 2]);
			for (UINT k = 0; k < nv; k++) {
				CSkinV& d = skin.bind[k];
				d.p[0] = mid[0] + (ps[k * 3] / 32767.f) * half[0];
				d.p[1] = mid[1] + (ps[k * 3 + 1] / 32767.f) * half[1];
				d.p[2] = -(mid[2] + (ps[k * 3 + 2] / 32767.f) * half[2]);
				d.n[0] = ns[k * 3] / 32767.f;
				d.n[1] = ns[k * 3 + 1] / 32767.f;
				d.n[2] = -(ns[k * 3 + 2] / 32767.f);
				d.c = RGBA8(rgb[k * 3], rgb[k * 3 + 1], rgb[k * 3 + 2]);
				for (int c = 0; c < 4; c++) { d.j[c] = jj[k * 4 + c]; d.w[c] = ww[k * 4 + c]; }
				VtxS& s = poseV[k];
				s.p[0] = d.p[0]; s.p[1] = d.p[1]; s.p[2] = d.p[2];
				s.n[0] = d.n[0]; s.n[1] = d.n[1]; s.n[2] = d.n[2];
				s.uv[0] = s.uv[1] = 0;
				s.c = d.c;
			}
			if (strncmp(name, "Hum", 3) == 0) {
				// ファイル上の法線は、Z 反転した面から取り直した内側向きが 6 割。
				// そのままカリングすると外側が消え、口腔と手の内側だけが見える。
				for (UINT k = 0; k < nv; k++) {
					skin.bind[k].n[0] = -skin.bind[k].n[0];
					skin.bind[k].n[1] = -skin.bind[k].n[1];
					skin.bind[k].n[2] = -skin.bind[k].n[2];
					poseV[k].n[0] = skin.bind[k].n[0];
					poseV[k].n[1] = skin.bind[k].n[1];
					poseV[k].n[2] = skin.bind[k].n[2];
				}
				for (UINT k = 0; k + 2 < ni; k += 3) std::swap(idx[k + 1], idx[k + 2]);
			}
			int headJ = -1;
			for (UINT j = 0; j < nJ; j++) if (strcmp(g.names[j], "Head") == 0) { headJ = (int)j; break; }
			if (strncmp(name, "Hum", 3) == 0 && headJ >= 0) {
				// 頭蓋は頭ボーンに全重みで乗る一色の塊（帽子・目は別色）。その色の頂点だけで測る。
				M4 bh = MInverse(m.invBind[headJ]);
				float ox = bh.m[12], oy = bh.m[13], oz = bh.m[14];
				std::vector<UINT> hv;
				std::map<DWORD, int> colN;
				for (UINT k = 0; k < nv; k++) {
					int best = 0, bw = -1;
					for (int c = 0; c < 4; c++) if ((int)skin.bind[k].w[c] > bw) { bw = skin.bind[k].w[c]; best = skin.bind[k].j[c]; }
					if (best != headJ || bw < 255) continue;
					hv.push_back(k);
					colN[skin.bind[k].c & 0x00FFFFFFu]++;
				}
				DWORD skinC = 0; int skinN = 0;
				for (auto& kv : colN) if (kv.second > skinN) { skinN = kv.second; skinC = kv.first; }
				std::vector<V3> sk;
				for (UINT k : hv) if ((skin.bind[k].c & 0x00FFFFFFu) == skinC)
					sk.push_back(v3(skin.bind[k].p[0] - ox, skin.bind[k].p[1] - oy, skin.bind[k].p[2] - oz));
				BOOL haveSk = FALSE;
				V3 skLo = v3(0, 0, 0), skHi = v3(0, 0, 0);
				if (sk.size() > 80) {
					V3 lo = sk[0], hi = sk[0];
					for (const V3& q : sk) {
						lo = v3(min(lo.x, q.x), min(lo.y, q.y), min(lo.z, q.z));
						hi = v3(max(hi.x, q.x), max(hi.y, q.y), max(hi.z, q.z));
					}
					haveSk = TRUE;
					skLo = v3(lo.x + ox - 0.01f, lo.y + oy - 0.01f, lo.z + oz - 0.01f);
					skHi = v3(hi.x + ox + 0.01f, hi.y + oy + 0.01f, hi.z + oz + 0.01f);
					// 頭頂と後頭に接する楕円体から始め、眉より上・顔より後ろの頭蓋がすべて入るまで広げる。
					float R = (hi.x - lo.x) * .5f;
					V3 cr = v3(R, R * 1.12f, min((hi.z - lo.z) * .5f, R * 1.25f));
					V3 cc = v3((lo.x + hi.x) * .5f, hi.y - cr.y, hi.z - cr.z);
					float s = 1.f;
					for (const V3& q : sk) {
						V3 d = v3((q.x - cc.x) / cr.x, (q.y - cc.y) / cr.y, (q.z - cc.z) / cr.z);
						if (d.y > -0.2f && d.z > -0.6f) s = max(s, Len(d));
					}
					cr = cr * (s * 1.04f);
					m.headMin[0] = cc.x - cr.x; m.headMin[1] = cc.y - cr.y; m.headMin[2] = cc.z - cr.z;
					m.headMax[0] = cc.x + cr.x; m.headMax[1] = cc.y + cr.y; m.headMax[2] = cc.z + cr.z;
				} else {
					m.headMin[0] = -0.10f; m.headMin[1] = 0.02f; m.headMin[2] = -0.01f;
					m.headMax[0] = 0.10f; m.headMax[1] = 0.14f; m.headMax[2] = 0.11f;
				}
				// 服の色替えは「その服の主な染め色」だけに掛ける。肌・顔・目・金具・革は元の色のまま。
				auto hsv = [](DWORD c, float& h, float& s, float& v) {
					float r = (c & 255) / 255.f, g = ((c >> 8) & 255) / 255.f, b = ((c >> 16) & 255) / 255.f;
					float mx = max(r, max(g, b)), mn = min(r, min(g, b)), dl = mx - mn;
					v = mx; s = mx > 1e-4f ? dl / mx : 0.f; h = 0.f;
					if (dl > 1e-5f) {
						if (mx == r) h = fmodf((g - b) / dl + 6.f, 6.f);
						else if (mx == g) h = (b - r) / dl + 2.f;
						else h = (r - g) / dl + 4.f;
						h *= 60.f;
					}
				};
				int sr = skinC & 255, sg = (skinC >> 8) & 255, sb = (skinC >> 16) & 255;
				std::vector<BYTE> dyeable(nv, 0);
				float hist[12] = {};
				for (UINT k = 0; k < nv; k++) {
					const CSkinV& b = skin.bind[k];
					int r = b.c & 255, gg = (b.c >> 8) & 255, bb = (b.c >> 16) & 255;
					if (abs(r - sr) < 16 && abs(gg - sg) < 16 && abs(bb - sb) < 16) continue;
					if (haveSk && b.p[0] > skLo.x && b.p[0] < skHi.x && b.p[1] > skLo.y && b.p[1] < skHi.y && b.p[2] > skLo.z && b.p[2] < skHi.z) continue;
					float h, s, v;
					hsv(b.c, h, s, v);
					if (s < 0.22f || v < 0.08f) continue;
					dyeable[k] = 1;
					hist[Clampi((int)(h / 30.f), 0, 11)] += 1.f;
				}
				// 0〜60°は革・金具・ブーツの土色。染め布の色が少しでもあればそちらを主とする。
				int earth = hist[1] > hist[0] ? 1 : 0, dyed = 2;
				for (int q = 3; q < 12; q++) if (hist[q] > hist[dyed]) dyed = q;
				int domBin = hist[dyed] >= hist[earth] * 0.25f && hist[dyed] > 0.f ? dyed : earth;
				float domH = domBin * 30.f + 15.f;
				skin.cloth.assign(nv, 0);
				for (UINT k = 0; k < nv; k++) if (dyeable[k]) {
					float h, s, v;
					hsv(skin.bind[k].c, h, s, v);
					float dh = fabsf(h - domH);
					if (dh > 180.f) dh = 360.f - dh;
					if (dh < 40.f) skin.cloth[k] = 1;
				}
			}
			GpuMesh gm;
			if (!MakeMesh(gm, poseV.data(), sizeof(VtxS), nv, idx.data(), ni)) return;
			gm.tex = TEX_WHITE;
			m_creatures.push_back(gm);
			ModelSetupJoints(m, &g);
			rigs.push_back(g);
			models.push_back(m);
			m_skins.push_back(std::move(skin));
		}
		return;
	}
	const int packed = memcmp(data, "TACP", 4) == 0;
	if (!packed && memcmp(data, "TACM", 4) != 0) return;
	Reader rd(data + 4, size - 4);
	UINT n = rd.U32();
	if (n > 256) return;
	m_creatures.reserve(n);
	for (UINT i = 0; i < n; i++) {
		char name[32];
		rd.Name(name, 32);
		std::vector<VtxS> v;
		std::vector<WORD> idx;
		if (!ReadColorMesh(rd, packed, v, idx)) break;
		GpuMesh gm;
		if (!MakeMesh(gm, v.data(), sizeof(VtxS), (UINT)v.size(), idx.data(), (UINT)idx.size())) continue;
		gm.tex = TEX_WHITE;
		m_creatures.push_back(gm);
	}
}

void Renderer::DrawCreature(int i, const M4& world, const DrawParams& dp)
{
	if (i < 0 || i >= (int)m_creatures.size()) return;
	DrawStatic(m_creatures[i], world, dp);
}

void Renderer::DrawSkinnedCreature(int i, const Pose& pose, const M4& world, const DrawParams& dp)
{
	int slot = -1;
	for (int k = 0; k < (int)m_skins.size(); k++) if (m_skins[k].model == i) { slot = k; break; }
	if (slot < 0 || m_skins[slot].bind.empty() || slot >= (int)m_creatures.size()) return;
	SkinAndDraw(m_skins[slot], m_creatures[slot], NULL, NULL, 0.f, pose, world, dp);
}

void Renderer::SkinAndDraw(const CreatureSkin& src, GpuMesh& base, const float* bx, const float* bn, float bt, const Pose& pose, const M4& world, const DrawParams& dp)
{
	std::vector<VtxS> v(src.bind.size());
	const float* bd = bt > 0.f ? bx : (bt < 0.f ? bn : NULL);
	const float ba = fabsf(bt);
	const BOOL dye = fabsf(dp.clothHue) > 1e-3f && src.cloth.size() == src.bind.size();
	float hc = cosf(dp.clothHue), hs = sinf(dp.clothHue);
	auto rotHue = [&](DWORD c) -> DWORD {
		// シェーダの HueRotate と同じ式（灰色軸まわりの回転）
		const float k = 0.57735f;
		float r = (c & 255) / 255.f, g = ((c >> 8) & 255) / 255.f, b = ((c >> 16) & 255) / 255.f;
		float kd = k * (r + g + b) * k * (1.f - hc);
		float cx = k * (b - g), cy = k * (r - b), cz = k * (g - r);
		float nr = r * hc + cx * hs + kd, ng = g * hc + cy * hs + kd, nb = b * hc + cz * hs + kd;
		auto q = [](float x) { int v = (int)(x * 255.f + .5f); return v < 0 ? 0 : (v > 255 ? 255 : v); };
		return (c & 0xFF000000u) | (DWORD)q(nr) | ((DWORD)q(ng) << 8) | ((DWORD)q(nb) << 16);
	};
	const FaceSet* fs = (dp.face > 0 && src.faceSet >= 0 && dp.face < m_faces[src.faceSet].nFace) ? &m_faces[src.faceSet] : NULL;
	for (size_t k = 0; k < src.bind.size(); k++) {
		CSkinV b = src.bind[k];
		if (bd) {
			b.p[0] += bd[k * 3] * ba;
			b.p[1] += bd[k * 3 + 1] * ba;
			b.p[2] += bd[k * 3 + 2] * ba;
		}
		if (fs && (int)k >= src.faceStart && (int)k < src.faceStart + src.faceN) {
			size_t q = (size_t)dp.face * fs->nv + (k - src.faceStart);
			const float* L = src.faceLin;
			const short* d = &fs->d[q * 3];
			float dx = d[0] * (0.25f / 32767.f), dy = d[1] * (0.25f / 32767.f), dz = d[2] * (0.25f / 32767.f);
			b.p[0] += L[0] * dx + L[1] * dy + L[2] * dz;
			b.p[1] += L[3] * dx + L[4] * dy + L[5] * dz;
			b.p[2] += L[6] * dx + L[7] * dy + L[8] * dz;
			const signed char* n = &fs->n[q * 4];
			float nx = n[0] / 127.f, ny = n[1] / 127.f, nz = n[2] / 127.f;
			b.n[0] = L[0] * nx + L[1] * ny + L[2] * nz;
			b.n[1] = L[3] * nx + L[4] * ny + L[5] * nz;
			b.n[2] = L[6] * nx + L[7] * ny + L[8] * nz;
			b.c = fs->c[q];
		}
		M4 acc;
		memset(acc.m, 0, sizeof(acc.m));
		for (int c = 0; c < 4; c++) {
			float w = b.w[c] / 255.f;
			int j = b.j[c];
			if (w <= 0.f || j < 0 || j >= pose.nJ) continue;
			for (int e = 0; e < 16; e++) acc.m[e] += pose.palette[j].m[e] * w;
		}
		V3 p = MXformPt(acc, v3(b.p[0], b.p[1], b.p[2]));
		V3 n = MXformDir(acc, v3(b.n[0], b.n[1], b.n[2]));
		float nl = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
		if (nl > 1e-6f) { n.x /= nl; n.y /= nl; n.z /= nl; }
		VtxS& d = v[k];
		d.p[0] = p.x; d.p[1] = p.y; d.p[2] = p.z;
		d.n[0] = n.x; d.n[1] = n.y; d.n[2] = n.z;
		d.uv[0] = d.uv[1] = 0;
		d.c = (dye && src.cloth[k]) ? rotHue(b.c) : b.c;
		if (dp.hairJoint >= 0) {
			int hw = 0;
			for (int c = 0; c < 4; c++) if (b.j[c] == dp.hairJoint && b.w[c] > hw) hw = b.w[c];
			if (hw > 10) d.c = dp.hairTint;
		}
	}
	if ((UINT)(v.size() * sizeof(VtxS)) > m_vbDynBytes || !m_vbDyn) {
		DrawStatic(base, world, dp);
		return;
	}
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(m_ctx->Map(m_vbDyn, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
	memcpy(ms.pData, v.data(), v.size() * sizeof(VtxS));
	m_ctx->Unmap(m_vbDyn, 0);
	ID3D11Buffer* saved = base.vb;
	base.vb = m_vbDyn;
	UINT savedStride = base.stride;
	base.stride = sizeof(VtxS);
	DrawStatic(base, world, dp);
	base.vb = saved;
	base.stride = savedStride;
}

void Renderer::LoadGearPack()
{
	for (auto& c : m_gear) c.Release();
	m_gear.clear();
	HMODULE hm = AfxGetResourceHandle();
	HRSRC rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_GEAR), RT_RCDATA);
	if (!rs) { hm = GetModuleHandle(NULL); rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_GEAR), RT_RCDATA); }
	if (!rs) return;
	HGLOBAL hg = LoadResource(hm, rs);
	const BYTE* data = hg ? (const BYTE*)LockResource(hg) : NULL;
	DWORD size = SizeofResource(hm, rs);
	if (!data || size < 8) return;
	const int packed = memcmp(data, "TACQ", 4) == 0;
	if (!packed && memcmp(data, "TACG", 4) != 0) return;
	Reader rd(data + 4, size - 4);
	UINT n = rd.U32();
	if (n > 256) return;
	m_gear.reserve(n);
	for (UINT i = 0; i < n; i++) {
		char name[32];
		rd.Name(name, 32);
		std::vector<VtxS> v;
		std::vector<WORD> idx;
		if (!ReadColorMesh(rd, packed, v, idx)) break;
		GpuMesh gm;
		if (!MakeMesh(gm, v.data(), sizeof(VtxS), (UINT)v.size(), idx.data(), (UINT)idx.size())) continue;
		gm.tex = TEX_WHITE;
		m_gear.push_back(gm);
	}
}

void Renderer::LoadFacePack()
{
	for (int g = 0; g < 2; g++) m_faces[g] = FaceSet();
	HMODULE hm = AfxGetResourceHandle();
	HRSRC rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_FACE), RT_RCDATA);
	if (!rs) { hm = GetModuleHandle(NULL); rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_FACE), RT_RCDATA); }
	if (!rs) return;
	HGLOBAL hg = LoadResource(hm, rs);
	const BYTE* data = hg ? (const BYTE*)LockResource(hg) : NULL;
	DWORD size = SizeofResource(hm, rs);
	if (!data || size < 8 || memcmp(data, "TACF", 4) != 0) return;
	Reader rd(data + 4, size - 4);
	UINT ng = rd.U32();
	if (ng != 2) return;
	for (UINT g = 0; g < ng; g++) {
		UINT nf = rd.U32(), nv = rd.U32();
		if (nf == 0 || nf > 32 || nv == 0 || nv > 20000) return;
		FaceSet& fs = m_faces[g];
		fs.d.resize((size_t)nf * nv * 3);
		fs.n.resize((size_t)nf * nv * 4);
		fs.c.resize((size_t)nf * nv);
		for (UINT f = 0; f < nf; f++) {
			const BYTE* d = rd.Bytes((size_t)nv * 6);
			const BYTE* n = rd.Bytes((size_t)nv * 4);
			const BYTE* c = rd.Bytes((size_t)nv * 4);
			if (!d || !n || !c) { fs = FaceSet(); return; }
			memcpy(&fs.d[(size_t)f * nv * 3], d, (size_t)nv * 6);
			memcpy(&fs.n[(size_t)f * nv * 4], n, (size_t)nv * 4);
			for (UINT k = 0; k < nv; k++) fs.c[(size_t)f * nv + k] = RGBA8(c[k * 4], c[k * 4 + 1], c[k * 4 + 2]);
		}
		fs.nFace = (int)nf;
		fs.nv = (int)nv;
	}
	UINT nm = rd.U32();
	if (nm > 64) return;
	for (UINT i = 0; i < nm; i++) {
		char name[32];
		rd.Name(name, 32);
		UINT g = rd.U32(), start = rd.U32(), nv = rd.U32();
		float lin[9];
		for (int k = 0; k < 9; k++) lin[k] = rd.F32();
		if (g > 1 || (int)nv != m_faces[g].nv) continue;
		for (CreatureSkin& s : m_skins) {
			if (s.model < 0 || s.model >= (int)models.size() || strcmp(models[s.model].name, name) != 0) continue;
			if ((size_t)start + nv != s.bind.size()) break;
			s.faceSet = (int)g;
			s.faceStart = (int)start;
			s.faceN = (int)nv;
			memcpy(s.faceLin, lin, sizeof(lin));
			break;
		}
	}
}

static bool ReadAvaMesh(Reader& rd, Renderer::AvaMesh& m)
{
	UINT nv = rd.U32();
	if (nv == 0 || nv > 65535) return false;
	float lo[3], hi[3];
	for (int a = 0; a < 3; a++) lo[a] = rd.F32();
	for (int a = 0; a < 3; a++) hi[a] = rd.F32();
	const BYTE* ps = rd.Bytes((size_t)nv * 6);
	const BYTE* ns = rd.Bytes((size_t)nv * 4);
	const BYTE* cs = rd.Bytes((size_t)nv * 4);
	const BYTE* js = rd.Bytes((size_t)nv * 4);
	const BYTE* ws = rd.Bytes((size_t)nv * 4);
	if (!ps || !ns || !cs || !js || !ws) return false;
	m.v.resize(nv);
	m.cloth.resize(nv);
	for (UINT k = 0; k < nv; k++) {
		Renderer::CSkinV& d = m.v[k];
		short q[3];
		memcpy(q, ps + k * 6, 6);
		for (int a = 0; a < 3; a++) {
			float mid = (lo[a] + hi[a]) * .5f, half = (hi[a] - lo[a]) * .5f;
			d.p[a] = mid + q[a] / 32767.f * half;
			d.n[a] = (signed char)ns[k * 4 + a] / 127.f;
		}
		d.c = RGBA8(cs[k * 4], cs[k * 4 + 1], cs[k * 4 + 2]);
		m.cloth[k] = cs[k * 4 + 3] ? 1 : 0;
		for (int c = 0; c < 4; c++) { d.j[c] = js[k * 4 + c]; d.w[c] = ws[k * 4 + c]; }
	}
	if (rd.U32()) {
		const BYTE* dx = rd.Bytes((size_t)nv * 6);
		const BYTE* dn = rd.Bytes((size_t)nv * 6);
		if (!dx || !dn) return false;
		m.dmax.resize((size_t)nv * 3);
		m.dmin.resize((size_t)nv * 3);
		for (size_t k = 0; k < (size_t)nv * 3; k++) {
			short a, b;
			memcpy(&a, dx + k * 2, 2);
			memcpy(&b, dn + k * 2, 2);
			m.dmax[k] = a * (0.1f / 32767.f);
			m.dmin[k] = b * (0.1f / 32767.f);
		}
	}
	return !rd.bad;
}

static bool ReadWords(Reader& rd, std::vector<WORD>& out, size_t n)
{
	const BYTE* p = rd.Bytes(n * 2);
	if (!p) return false;
	out.resize(n);
	if (n) memcpy(out.data(), p, n * 2);
	return true;
}

void Renderer::ReleaseAvatarCache()
{
	for (AvaCache& c : m_avaCache) c.gm.Release();
	m_avaCache.clear();
}

void Renderer::LoadAvatarPack()
{
	ReleaseAvatarCache();
	m_avaSeg.clear();
	m_avaCos.clear();
	m_avaModels.clear();
	for (int g = 0; g < 2; g++) m_avaBody[g] = AvaBody();
	HMODULE hm = AfxGetResourceHandle();
	HRSRC rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_AVA), RT_RCDATA);
	if (!rs) { hm = GetModuleHandle(NULL); rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_AVA), RT_RCDATA); }
	if (!rs) return;
	HGLOBAL hg = LoadResource(hm, rs);
	const BYTE* data = hg ? (const BYTE*)LockResource(hg) : NULL;
	DWORD size = SizeofResource(hm, rs);
	if (!data || size < 8 || memcmp(data, "TACV", 4) != 0) return;
	Reader rd(data + 4, size - 4);
	UINT ns = rd.U32();
	if (ns == 0 || ns > 32) return;
	for (UINT s = 0; s < ns; s++) {
		char nm[24];
		rd.Name(nm, 24);
		m_avaSeg.push_back(nm);
	}
	UINT nm = rd.U32();
	if (nm > 64) { m_avaSeg.clear(); return; }
	for (UINT i = 0; i < nm; i++) {
		AvaModel am;
		rd.Name(am.name, 32);
		am.gender = (int)rd.U32();
		const BYTE* p = rd.Bytes((size_t)ns * 12 * 4);
		if (!p || am.gender < 0 || am.gender > 1) { m_avaSeg.clear(); m_avaModels.clear(); return; }
		am.M.resize((size_t)ns * 12);
		memcpy(am.M.data(), p, (size_t)ns * 12 * 4);
		m_avaModels.push_back(am);
	}
	for (int g = 0; g < 2; g++) {
		AvaBody& b = m_avaBody[g];
		b.nFace = (int)rd.U32();
		bool ok = ReadAvaMesh(rd, b.m);
		UINT nb = ok ? rd.U32() : 0, ne = ok ? rd.U32() : 0;
		ok = ok && ReadWords(rd, b.bodyTri, (size_t)nb * 3) && ReadWords(rd, b.extraTri, (size_t)ne * 3);
		if (!ok || b.nFace < 0 || b.nFace > (int)b.m.v.size()) { m_avaSeg.clear(); m_avaModels.clear(); m_avaBody[0] = m_avaBody[1] = AvaBody(); return; }
	}
	UINT na = rd.U32();
	if (na > 256) return;
	for (UINT i = 0; i < na; i++) {
		AvaCos c;
		rd.Name(c.name, 32);
		c.gender = (int)rd.U32();
		if (c.gender < 0 || c.gender > 1 || !ReadAvaMesh(rd, c.m)) break;
		UINT nt = rd.U32();
		if (!ReadWords(rd, c.tri, (size_t)nt * 3)) break;
		size_t nk = (m_avaBody[c.gender].bodyTri.size() / 3 + 7) / 8;
		const BYTE* kp = rd.Bytes(nk);
		if (!kp) break;
		c.keep.assign(kp, kp + nk);
		if (m_avaBody[c.gender].m.v.size() + c.m.v.size() > 65535) continue;
		m_avaCos.push_back(std::move(c));
	}
}

int Renderer::AvatarCount(int gender) const
{
	int n = 0;
	for (const AvaCos& c : m_avaCos) if (c.gender == gender) n++;
	return n;
}

BOOL Renderer::DrawAvatar(int model, int avatar, int bust, const Pose& pose, const M4& world, const DrawParams& dp)
{
	if (model < 0 || model >= (int)models.size() || avatar <= 0 || m_avaSeg.empty()) return FALSE;
	const Model& md = models[model];
	const AvaModel* am = NULL;
	for (const AvaModel& x : m_avaModels) if (!strcmp(x.name, md.name)) { am = &x; break; }
	if (!am || md.rig < 0 || md.rig >= (int)rigs.size()) return FALSE;
	int ci = -1;
	for (int k = 0, n = 0; k < (int)m_avaCos.size(); k++) if (m_avaCos[k].gender == am->gender && ++n == avatar) { ci = k; break; }
	if (ci < 0) return FALSE;
	AvaCache* ac = NULL;
	for (AvaCache& x : m_avaCache) if (x.model == model && x.cos == ci) { ac = &x; break; }
	if (!ac) {
		const AvaBody& body = m_avaBody[am->gender];
		const AvaCos& cos = m_avaCos[ci];
		const Rig& rg = rigs[md.rig];
		const int ns = (int)m_avaSeg.size();
		std::vector<int> segJ(ns);
		for (int s = 0; s < ns; s++) {
			int j = rg.Find(m_avaSeg[s].c_str());
			segJ[s] = j >= 0 ? j : (md.hipsJ >= 0 ? md.hipsJ : 0);
		}
		AvaCache nc;
		nc.model = model;
		nc.cos = ci;
		CreatureSkin& sk = nc.skin;
		sk.model = model;
		const size_t nb = body.m.v.size(), nv = nb + cos.m.v.size();
		sk.bind.resize(nv);
		sk.cloth.resize(nv);
		const BOOL hasBust = !body.m.dmax.empty() && !cos.m.dmax.empty();
		if (hasBust) { nc.bx.resize(nv * 3); nc.bn.resize(nv * 3); }
		for (size_t k = 0; k < nv; k++) {
			const BOOL isBody = k < nb;
			const AvaMesh& src = isBody ? body.m : cos.m;
			const size_t q = isBody ? k : k - nb;
			const CSkinV& s = src.v[q];
			float A[12] = {};
			for (int c = 0; c < 4; c++) {
				float w = s.w[c] / 255.f;
				if (w <= 0.f || s.j[c] >= ns) continue;
				const float* Ms = &am->M[(size_t)s.j[c] * 12];
				for (int e = 0; e < 12; e++) A[e] += Ms[e] * w;
			}
			auto lin = [&](const float* x, float* o) {
				o[0] = A[0] * x[0] + A[1] * x[1] + A[2] * x[2];
				o[1] = A[4] * x[0] + A[5] * x[1] + A[6] * x[2];
				o[2] = A[8] * x[0] + A[9] * x[1] + A[10] * x[2];
			};
			CSkinV& d = sk.bind[k];
			lin(s.p, d.p);
			d.p[0] += A[3]; d.p[1] += A[7]; d.p[2] += A[11];
			lin(s.n, d.n);
			float nl = sqrtf(d.n[0] * d.n[0] + d.n[1] * d.n[1] + d.n[2] * d.n[2]);
			if (nl > 1e-6f) { d.n[0] /= nl; d.n[1] /= nl; d.n[2] /= nl; }
			d.c = s.c;
			for (int c = 0; c < 4; c++) {
				d.j[c] = (unsigned char)(s.j[c] < ns ? segJ[s.j[c]] : segJ[0]);
				d.w[c] = s.w[c];
			}
			sk.cloth[k] = src.cloth[q];
			if (hasBust) {
				lin(&src.dmax[q * 3], &nc.bx[k * 3]);
				lin(&src.dmin[q * 3], &nc.bn[k * 3]);
			}
		}
		std::vector<WORD> idx;
		idx.reserve(body.bodyTri.size() + body.extraTri.size() + cos.tri.size());
		for (size_t t = 0; t * 3 < body.bodyTri.size(); t++)
			if (t / 8 < cos.keep.size() && (cos.keep[t / 8] >> (t & 7)) & 1) idx.insert(idx.end(), &body.bodyTri[t * 3], &body.bodyTri[t * 3] + 3);
		idx.insert(idx.end(), body.extraTri.begin(), body.extraTri.end());
		for (WORD w : cos.tri) idx.push_back((WORD)(w + nb));
		for (const CreatureSkin& hs : m_skins) if (hs.model == model && hs.faceSet == am->gender && hs.faceN == body.nFace) {
			sk.faceSet = hs.faceSet;
			sk.faceStart = 0;
			sk.faceN = body.nFace;
			memcpy(sk.faceLin, hs.faceLin, sizeof(sk.faceLin));
			break;
		}
		std::vector<VtxS> pv(nv);
		for (size_t k = 0; k < nv; k++) {
			const CSkinV& b = sk.bind[k];
			VtxS& s = pv[k];
			s.p[0] = b.p[0]; s.p[1] = b.p[1]; s.p[2] = b.p[2];
			s.n[0] = b.n[0]; s.n[1] = b.n[1]; s.n[2] = b.n[2];
			s.uv[0] = s.uv[1] = 0;
			s.c = b.c;
		}
		if (!MakeMesh(nc.gm, pv.data(), sizeof(VtxS), (UINT)nv, idx.data(), (UINT)idx.size())) return FALSE;
		nc.gm.tex = TEX_WHITE;
		m_avaCache.push_back(std::move(nc));
		ac = &m_avaCache.back();
	}
	// 大きい側は MH の最大カップの 7 割まで。満量だと胸が尖り、胸元の布が反り返る。
	const float bt = Clampf((bust - 2) * (bust > 2 ? .35f : .5f), -1.f, 1.f);
	SkinAndDraw(ac->skin, ac->gm, ac->bx.empty() ? NULL : ac->bx.data(), ac->bn.empty() ? NULL : ac->bn.data(), bt, pose, world, dp);
	return TRUE;
}

void Renderer::DrawGear(int i, const M4& world, const DrawParams& dp)
{
	if (i < 0 || i >= (int)m_gear.size()) return;
	DrawStatic(m_gear[i], world, dp);
}

int Renderer::FindModel(const char* n) const { for (int i = 0; i < (int)models.size(); i++) if (!strcmp(models[i].name, n)) return i; return -1; }
int Renderer::FindAttach(const char* n) const { for (int i = 0; i < (int)attaches.size(); i++) if (!strcmp(attaches[i].name, n)) return i; return -1; }
int Renderer::FindStatic(const char* n) const { for (int i = 0; i < (int)statics.size(); i++) if (!strcmp(statics[i].name, n)) return i; return -1; }

int Renderer::AddProcModel(const Model& m, const std::vector<VtxK>& v, const std::vector<WORD>& idx)
{
	Model mm = m;
	mm.parts.clear();
	GpuMesh gm;
	if (!MakeMesh(gm, v.data(), sizeof(VtxK), (UINT)v.size(), idx.data(), (UINT)idx.size())) return -1;
	gm.tex = TEX_PAL;
	mm.parts.push_back(gm);
	models.push_back(mm);
	return (int)models.size() - 1;
}

// ============================================================ ポーズ
static Q4 SampleQ(const Clip& c, int nJ, int j, float t, int loop)
{
	float f = t * c.fps;
	int nf = c.nFrames;
	if (nf <= 1) return c.q[j];
	float span = (float)(nf - 1);
	if (loop) { f = fmodf(f, span); if (f < 0) f += span; }
	else f = Clampf(f, 0, span);
	int i0 = (int)f, i1 = min(nf - 1, i0 + 1);
	return QNlerp(c.q[(size_t)i0 * nJ + j], c.q[(size_t)i1 * nJ + j], f - i0);
}
static V3 SampleH(const Clip& c, float t, int loop)
{
	float f = t * c.fps;
	int nf = c.nFrames;
	if (nf <= 1) return c.hips[0];
	float span = (float)(nf - 1);
	if (loop) { f = fmodf(f, span); if (f < 0) f += span; }
	else f = Clampf(f, 0, span);
	int i0 = (int)f, i1 = min(nf - 1, i0 + 1);
	return Lerp(c.hips[i0], c.hips[i1], f - i0);
}

void Renderer::FinishPose(const Model& m, Pose& p, const PoseMods* mods) const
{
	p.nJ = m.nJ;
	const BOOL own = mods && mods->any;
	for (int j = 0; j < m.nJ; j++) {
		int pa = m.parent[j];
		p.global[j] = MMul(p.local[j], pa >= 0 ? p.global[pa] : m.top);
		if (own && (mods->own[j].x != 1.f || mods->own[j].y != 1.f || mods->own[j].z != 1.f))
			p.palette[j] = MMul(MMul(m.invBind[j], MScale(mods->own[j].x, mods->own[j].y, mods->own[j].z)), p.global[j]);
		else
			p.palette[j] = MMul(m.invBind[j], p.global[j]);
	}
}

void Renderer::EvalPose(const Model& m, int ca, float ta, int la, int cb, float tb, int lb, float blend, const PoseMods* mods, Pose& out) const
{
	const Rig* g = (m.rig >= 0 && m.rig < (int)rigs.size()) ? &rigs[m.rig] : NULL;
	const Clip* A = (g && ca >= 0 && ca < (int)g->clips.size()) ? &g->clips[ca] : NULL;
	const Clip* B = (g && cb >= 0 && cb < (int)g->clips.size()) ? &g->clips[cb] : NULL;
	blend = Clampf(blend, 0, 1);
	for (int j = 0; j < m.nJ; j++) {
		Q4 q = m.restR[j];
		V3 t = m.restT[j];
		if (A) q = SampleQ(*A, m.nJ, j, ta, la);
		if (B && blend < 1.f) q = QNlerp(SampleQ(*B, m.nJ, j, tb, lb), q, blend);
		if (g && j == g->hips) {
			if (A) t = SampleH(*A, ta, la);
			if (B && blend < 1.f) t = Lerp(SampleH(*B, tb, lb), t, blend);
			if (la && A && !A->hips.empty()) { t.x = A->hips[0].x; t.z = A->hips[0].z; }
		}
		V3 s = m.restS[j];
		if (mods && mods->any) { s.x *= mods->scale[j].x; s.y *= mods->scale[j].y; s.z *= mods->scale[j].z; }
		out.local[j] = MTRS(t, q, s);
	}
	FinishPose(m, out, mods);
}

void Renderer::EvalPoseLocal(const Model& m, const Q4* rot, const V3* trans, const PoseMods* mods, Pose& out) const
{
	for (int j = 0; j < m.nJ; j++) {
		Q4 q = rot ? QMul(m.restR[j], rot[j]) : m.restR[j];
		V3 t = m.restT[j];
		if (trans) t = t + trans[j];
		V3 s = m.restS[j];
		if (mods && mods->any) { s.x *= mods->scale[j].x; s.y *= mods->scale[j].y; s.z *= mods->scale[j].z; }
		out.local[j] = MTRS(t, q, s);
	}
	FinishPose(m, out, mods);
}

// ============================================================ フレーム
void Renderer::UpdateFrameCB()
{
	D3D11_MAPPED_SUBRESOURCE ms;
	if (SUCCEEDED(m_ctx->Map(m_cbFrame, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
		memcpy(ms.pData, &m_fcb, sizeof(m_fcb));
		m_ctx->Unmap(m_cbFrame, 0);
	}
}

void Renderer::BeginFrame()
{
	m_frame++;
	m_ctx->VSSetConstantBuffers(0, 1, &m_cbFrame);
	m_ctx->PSSetConstantBuffers(0, 1, &m_cbFrame);
	ID3D11Buffer* cb[2] = { m_cbObj, m_cbBones };
	m_ctx->VSSetConstantBuffers(1, 2, cb);
	m_ctx->PSSetConstantBuffers(1, 1, &m_cbObj);
	ID3D11SamplerState* ss[2] = { m_sLin, m_sCmp };
	m_ctx->PSSetSamplers(0, 2, ss);
	m_fcb.screen[0] = (float)m_w; m_fcb.screen[1] = (float)m_h; m_fcb.screen[2] = 1.f / m_w; m_fcb.screen[3] = 1.f / m_h;
}

void Renderer::SetCamera(const M4& view, const M4& proj, V3 eye, float time)
{
	m_view = view; m_proj = proj; m_vp = MMul(view, proj); m_eye = eye; m_time = time;
	m_fcb.vp = m_vp;
	m_fcb.eye[0] = eye.x; m_fcb.eye[1] = eye.y; m_fcb.eye[2] = eye.z; m_fcb.eye[3] = time;
}

void Renderer::SetLight(V3 dir, V3 focus, float radius)
{
	dir = Norm(dir);
	m_fcb.light[0] = dir.x; m_fcb.light[1] = dir.y; m_fcb.light[2] = dir.z; m_fcb.light[3] = m_shSrv ? 1.f : 0.f;
	V3 up = fabsf(dir.y) > .95f ? v3(0, 0, 1) : v3(0, 1, 0);
	M4 lv = MLookAtLH(v3(0, 0, 0), v3(-dir.x, -dir.y, -dir.z), up);
	// テクセル単位にスナップ（ちらつき防止）
	V3 fl = MXformPt(lv, focus);
	float texel = (radius * 2.f) / max(1, m_shSize);
	fl.x = floorf(fl.x / texel) * texel; fl.y = floorf(fl.y / texel) * texel;
	M4 inv = MInverse(lv);
	V3 snapped = MXformPt(inv, fl);
	V3 eye = snapped + dir * 60.f;
	M4 view = MLookAtLH(eye, snapped, up);
	M4 proj = MOrthoLH(radius * 2.f, radius * 2.f, 1.f, 140.f);
	m_lightVP = MMul(view, proj);
	m_fcb.lvp = m_lightVP;
	m_fcb.fog[3] = 1.f / max(1, m_shSize);
}

void Renderer::SetFog(float start, float end, float hf, DWORD c)
{
	m_fcb.fog[0] = start; m_fcb.fog[1] = end; m_fcb.fog[2] = hf;
	m_fcb.fogColor[0] = (c & 255) / 255.f; m_fcb.fogColor[1] = ((c >> 8) & 255) / 255.f; m_fcb.fogColor[2] = ((c >> 16) & 255) / 255.f; m_fcb.fogColor[3] = 1;
}

void Renderer::SetAmbient(DWORD sky, DWORD gnd, float strength, float hard)
{
	m_fcb.skyAmb[0] = (sky & 255) / 255.f; m_fcb.skyAmb[1] = ((sky >> 8) & 255) / 255.f; m_fcb.skyAmb[2] = ((sky >> 16) & 255) / 255.f; m_fcb.skyAmb[3] = strength;
	m_fcb.gndAmb[0] = (gnd & 255) / 255.f; m_fcb.gndAmb[1] = ((gnd >> 8) & 255) / 255.f; m_fcb.gndAmb[2] = ((gnd >> 16) & 255) / 255.f; m_fcb.gndAmb[3] = hard;
}

void Renderer::BeginShadowPass()
{
	m_pass = PASS_SHADOW;
	if (!m_shDsv) return;
	ID3D11ShaderResourceView* nul[2] = { NULL, NULL };
	m_ctx->PSSetShaderResources(0, 2, nul);
	m_ctx->OMSetRenderTargets(0, NULL, m_shDsv);
	m_ctx->ClearDepthStencilView(m_shDsv, D3D11_CLEAR_DEPTH, 1.f, 0);
	D3D11_VIEWPORT vp = { 0, 0, (float)m_shSize, (float)m_shSize, 0, 1 };
	m_ctx->RSSetViewports(1, &vp);
	m_ctx->RSSetState(m_rsShadow);
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
	M4 keep = m_fcb.vp;
	m_fcb.vp = m_lightVP;
	UpdateFrameCB();
	m_fcb.vp = keep;
}

void Renderer::BeginMainPass()
{
	m_pass = m_inPortrait ? PASS_PORTRAIT : PASS_MAIN;
	if (m_inPortrait) {
		m_ctx->OMSetRenderTargets(1, &m_ptRtv, m_ptDsv);
		int sx = (m_portraitSlot % 6) * PORTRAIT_PX, sy = (m_portraitSlot / 6) * PORTRAIT_PX;
		D3D11_VIEWPORT vp = { (float)sx, (float)sy, (float)PORTRAIT_PX, (float)PORTRAIT_PX, 0, 1 };
		m_ctx->RSSetViewports(1, &vp);
		m_ctx->ClearDepthStencilView(m_ptDsv, D3D11_CLEAR_DEPTH, 1.f, 0);
	} else {
		m_ctx->OMSetRenderTargets(1, &m_msRtv, m_dsv);
		float cc[4] = { .4f, .55f, .75f, 1 };
		m_ctx->ClearRenderTargetView(m_msRtv, cc);
		m_ctx->ClearDepthStencilView(m_dsv, D3D11_CLEAR_DEPTH, 1.f, 0);
		D3D11_VIEWPORT vp = { 0, 0, (float)m_w, (float)m_h, 0, 1 };
		m_ctx->RSSetViewports(1, &vp);
	}
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
	m_fcb.screen[0] = m_inPortrait ? (float)PORTRAIT_PX : (float)m_w;
	m_fcb.screen[1] = m_inPortrait ? (float)PORTRAIT_PX : (float)m_h;
	UpdateFrameCB();
	ID3D11ShaderResourceView* sh = (m_shSrv && m_fcb.light[3] > .5f) ? m_shSrv : NULL;
	m_ctx->PSSetShaderResources(1, 1, &sh);
}

void Renderer::ClearDepth()
{
	m_ctx->ClearDepthStencilView(m_inPortrait ? m_ptDsv : m_dsv, D3D11_CLEAR_DEPTH, 1.f, 0);
}

void Renderer::SetObj(const M4& world, const DrawParams& dp, float outlineW)
{
	struct { M4 w; float tint[4], params[4], extra[4], oc[4]; } o;
	o.w = world;
	o.tint[0] = (dp.tint & 255) / 255.f; o.tint[1] = ((dp.tint >> 8) & 255) / 255.f; o.tint[2] = ((dp.tint >> 16) & 255) / 255.f; o.tint[3] = ((dp.tint >> 24) & 255) / 255.f;
	o.params[0] = outlineW; o.params[1] = dp.mode; o.params[2] = dp.emissive; o.params[3] = dp.hue;
	o.extra[0] = dp.dissolve; o.extra[1] = dp.flash; o.extra[2] = dp.rim; o.extra[3] = dp.sway;
	o.oc[0] = (dp.outlineColor & 255) / 255.f; o.oc[1] = ((dp.outlineColor >> 8) & 255) / 255.f; o.oc[2] = ((dp.outlineColor >> 16) & 255) / 255.f; o.oc[3] = ((dp.outlineColor >> 24) & 255) / 255.f;
	D3D11_MAPPED_SUBRESOURCE ms;
	if (SUCCEEDED(m_ctx->Map(m_cbObj, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) { memcpy(ms.pData, &o, sizeof(o)); m_ctx->Unmap(m_cbObj, 0); }
}

void Renderer::BindForPass(BOOL skinned, BOOL outline)
{
	m_ctx->IASetInputLayout(skinned ? m_ilK : m_ilS);
	m_ctx->VSSetShader(skinned ? m_vsK : m_vsS, NULL, 0);
	m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	if (m_pass == PASS_SHADOW) {
		m_ctx->PSSetShader(NULL, NULL, 0);
		return;
	}
	m_ctx->PSSetShader(outline ? m_psO : m_psM, NULL, 0);
	m_ctx->RSSetState(outline ? m_rsFront : m_rsBack);
}

void Renderer::DrawSky(DWORD top, DWORD bottom, int lines, DWORD lineColor)
{
	DrawParams dp;
	dp.tint = top;
	dp.mode = (float)lines;
	dp.dissolve = (bottom & 255) / 255.f; dp.flash = ((bottom >> 8) & 255) / 255.f; dp.rim = ((bottom >> 16) & 255) / 255.f;
	dp.outlineColor = lineColor;
	SetObj(MIdent(), dp, 0);
	m_ctx->IASetInputLayout(NULL);
	m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_ctx->VSSetShader(m_vsQ, NULL, 0);
	m_ctx->PSSetShader(m_psSky, NULL, 0);
	m_ctx->RSSetState(m_rsNone);
	m_ctx->OMSetDepthStencilState(m_dsOff, 0);
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
	m_ctx->Draw(3, 0);
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
}

void Renderer::DrawStatic(const GpuMesh& m, const M4& world, const DrawParams& dp)
{
	if (!m.vb || !m.ib) return;
	UINT st = m.stride, off = 0;
	m_ctx->IASetVertexBuffers(0, 1, &m.vb, &st, &off);
	m_ctx->IASetIndexBuffer(m.ib, m.i32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
	if (m_pass == PASS_SHADOW) {
		if (((dp.tint >> 24) & 255) < 128 || dp.dissolve > .5f) return;
		SetObj(world, dp, 0);
		BindForPass(FALSE, FALSE);
		m_ctx->DrawIndexed(m.nIdx, 0, 0);
		return;
	}
	ID3D11ShaderResourceView* srv = SrvFor(m.tex);
	m_ctx->PSSetShaderResources(0, 1, &srv);
	BOOL alpha = ((dp.tint >> 24) & 255) < 255;
	m_ctx->OMSetBlendState(alpha ? m_bsAlpha : m_bsOpaque, NULL, 0xFFFFFFFF);
	if (dp.outline > 0 && !alpha) {
		SetObj(world, dp, dp.outline);
		BindForPass(FALSE, TRUE);
		if (dp.twoSided) m_ctx->RSSetState(m_rsNone);
		m_ctx->DrawIndexed(m.nIdx, 0, 0);
	}
	SetObj(world, dp, 0);
	BindForPass(FALSE, FALSE);
	if (dp.twoSided) m_ctx->RSSetState(m_rsNone);
	m_ctx->DrawIndexed(m.nIdx, 0, 0);
	if (alpha) m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
}

void Renderer::DrawSkinned(const Model& m, const Pose& p, const M4& world, const DrawParams& dp)
{
	D3D11_MAPPED_SUBRESOURCE ms;
	if (SUCCEEDED(m_ctx->Map(m_cbBones, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
		memcpy(ms.pData, p.palette, sizeof(M4) * min(p.nJ, (int)MAXJ));
		m_ctx->Unmap(m_cbBones, 0);
	}
	BOOL alpha = ((dp.tint >> 24) & 255) < 255;
	for (int pass = 0; pass < 2; pass++) {
		BOOL outline = (pass == 0);
		if (outline && (m_pass == PASS_SHADOW || dp.outline <= 0 || alpha)) continue;
		if (m_pass == PASS_SHADOW && (alpha || dp.dissolve > .5f)) return;
		if (m_pass != PASS_SHADOW) m_ctx->OMSetBlendState(alpha ? m_bsAlpha : m_bsOpaque, NULL, 0xFFFFFFFF);
		for (const auto& part : m.parts) {
			if (!part.vb) continue;
			DrawParams d = dp;
			if (part.glow) { d.emissive = max(d.emissive, 1.2f); if (outline) continue; }
			SetObj(world, d, outline ? dp.outline : 0);
			BindForPass(TRUE, outline);
			if (m_pass != PASS_SHADOW) {
				ID3D11ShaderResourceView* srv = SrvFor(part.tex);
				m_ctx->PSSetShaderResources(0, 1, &srv);
			}
			UINT st = part.stride, off = 0;
			m_ctx->IASetVertexBuffers(0, 1, &part.vb, &st, &off);
			m_ctx->IASetIndexBuffer(part.ib, DXGI_FORMAT_R16_UINT, 0);
			m_ctx->DrawIndexed(part.nIdx, 0, 0);
		}
	}
	if (alpha && m_pass != PASS_SHADOW) m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
}

void Renderer::DrawAttach(const Attach& a, const Pose& p, int joint, const M4& world, const DrawParams& dp, const M4* extra)
{
	if (joint < 0 || joint >= p.nJ) return;
	M4 w = MMul(p.global[joint], world);
	if (extra) w = MMul(*extra, w);
	DrawStatic(a.mesh, w, dp);
}

void Renderer::DrawTerrain()
{
	DrawParams dp;
	dp.mode = 3;
	dp.rim = 0;
	if (m_terrain.vb) DrawStatic(m_terrain, MIdent(), dp);
	dp.mode = 0;
	for (auto& b : m_props) {
		dp.hue = b.hue;
		dp.rim = .15f;
		dp.outline = m_pass == PASS_SHADOW ? 0 : .012f;
		DrawStatic(b.mesh, MIdent(), dp);
	}
	if (m_lava.vb) {
		DrawParams lp;
		lp.mode = 2;
		DrawStatic(m_lava, MIdent(), lp);
	}
}

void Renderer::DrawWater()
{
	if (!m_water.vb || m_pass == PASS_SHADOW) return;
	DrawParams dp;
	dp.mode = 1;
	dp.tint = (m_waterColor & 0xFFFFFF) | 0xF0000000;
	m_ctx->OMSetDepthStencilState(m_dsRead, 0);
	DrawStatic(m_water, MIdent(), dp);
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
}

void Renderer::DrawOverlayQuads(const VtxS* q, int n, int cursorStyle)
{
	if (!q || n <= 0 || m_pass == PASS_SHADOW) return;
	size_t bytes = (size_t)n * sizeof(VtxS);
	if (bytes > m_vbDynBytes) return;
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(m_ctx->Map(m_vbDyn, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
	memcpy(ms.pData, q, bytes);
	m_ctx->Unmap(m_vbDyn, 0);
	DrawParams dp;
	dp.mode = (float)cursorStyle;
	SetObj(MIdent(), dp, 0);
	UINT st = sizeof(VtxS), off = 0;
	m_ctx->IASetVertexBuffers(0, 1, &m_vbDyn, &st, &off);
	m_ctx->IASetInputLayout(m_ilS);
	m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_ctx->VSSetShader(m_vsS, NULL, 0);
	m_ctx->PSSetShader(m_psOV, NULL, 0);
	m_ctx->RSSetState(m_rsNone);
	m_ctx->OMSetDepthStencilState(m_dsRead, 0);
	m_ctx->OMSetBlendState(m_bsAlpha, NULL, 0xFFFFFFFF);
	m_ctx->Draw((UINT)n, 0);
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
}

void Renderer::DrawRibbon(const std::vector<VtxP>& v)
{
	if (v.empty() || m_pass == PASS_SHADOW) return;
	size_t bytes = v.size() * sizeof(VtxP);
	if (bytes > m_vbDynBytes) return;
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(m_ctx->Map(m_vbDyn, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
	memcpy(ms.pData, v.data(), bytes);
	m_ctx->Unmap(m_vbDyn, 0);
	UINT st = sizeof(VtxP), off = 0;
	m_ctx->IASetVertexBuffers(0, 1, &m_vbDyn, &st, &off);
	m_ctx->IASetInputLayout(m_ilP);
	m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_ctx->VSSetShader(m_vsP, NULL, 0);
	m_ctx->PSSetShader(m_psP, NULL, 0);
	m_ctx->RSSetState(m_rsNone);
	m_ctx->OMSetDepthStencilState(m_dsRead, 0);
	m_ctx->OMSetBlendState(m_bsPremul, NULL, 0xFFFFFFFF);
	m_ctx->Draw((UINT)v.size(), 0);
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
}

void Renderer::DrawParticles(const Particle* ps, int nPart)
{
	if (!ps || nPart <= 0 || m_pass == PASS_SHADOW) return;
	V3 right = Norm(v3(m_view.m[0], m_view.m[4], m_view.m[8]));
	V3 up = Norm(v3(m_view.m[1], m_view.m[5], m_view.m[9]));
	V3 fwd = Norm(v3(m_view.m[2], m_view.m[6], m_view.m[10]));
	enum { PV = 16384 };
	static VtxP buf[2][PV];
	int vn[2] = { 0, 0 };
	for (int pi = 0; pi < nPart; pi++) {
		const Particle& p = ps[pi];
		if (p.life <= 0) continue;
		float t = 1.f - p.life / max(1e-4f, p.max);
		float sz = Lerpf(p.size, p.size1, t);
		float a = p.a * Clampf(p.life / max(1e-4f, p.max) * 3.f, 0, 1) * Clampf(t * 8.f + .1f, 0, 1);
		V3 ax = right * sz, ay = up * sz;
		if (p.stretch > 0) {
			V3 vd = p.v;
			float sp = Len(vd);
			if (sp > 1e-3f) {
				vd = vd * (1.f / sp);
				V3 side = Norm(Cross(vd, fwd));
				ay = vd * (sz + sp * p.stretch);
				ax = side * sz * .5f;
			}
		}
		VtxP q[4];
		V3 c[4] = { p.p - ax - ay, p.p - ax + ay, p.p + ax + ay, p.p + ax - ay };
		float uv[4][2] = { { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };
		for (int k = 0; k < 4; k++) {
			q[k].p[0] = c[k].x; q[k].p[1] = c[k].y; q[k].p[2] = c[k].z;
			q[k].u = uv[k][0]; q[k].v = uv[k][1]; q[k].shape = (float)p.shape;
			q[k].c[0] = p.r; q[k].c[1] = p.g; q[k].c[2] = p.b; q[k].c[3] = a;
		}
		int bi = p.add ? 1 : 0;
		if (vn[bi] + 6 > PV) continue;
		buf[bi][vn[bi]++] = q[0]; buf[bi][vn[bi]++] = q[1]; buf[bi][vn[bi]++] = q[2];
		buf[bi][vn[bi]++] = q[0]; buf[bi][vn[bi]++] = q[2]; buf[bi][vn[bi]++] = q[3];
	}
	for (int b = 0; b < 2; b++) {
		if (vn[b] <= 0) continue;
		size_t bytes = min((size_t)vn[b] * sizeof(VtxP), (size_t)m_vbDynBytes);
		D3D11_MAPPED_SUBRESOURCE ms;
		if (FAILED(m_ctx->Map(m_vbDyn, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
		memcpy(ms.pData, buf[b], bytes);
		m_ctx->Unmap(m_vbDyn, 0);
		UINT st = sizeof(VtxP), off = 0;
		m_ctx->IASetVertexBuffers(0, 1, &m_vbDyn, &st, &off);
		m_ctx->IASetInputLayout(m_ilP);
		m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_ctx->VSSetShader(m_vsP, NULL, 0);
		m_ctx->PSSetShader(m_psP, NULL, 0);
		m_ctx->RSSetState(m_rsNone);
		m_ctx->OMSetDepthStencilState(m_dsRead, 0);
		m_ctx->OMSetBlendState(b ? m_bsAdd : m_bsPremul, NULL, 0xFFFFFFFF);
		m_ctx->Draw((UINT)(bytes / sizeof(VtxP)), 0);
	}
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
}

// ============================================================ 地形メッシュ
static int TopTile(int t, int theme)
{
	switch (t) {
	case T_PLAIN:
		switch (theme) {
		case TH_CANYON: return TL_DIRT;
		case TH_FOREST: return TL_FOREST;
		case TH_SNOW: return TL_SNOW;
		case TH_DESERT: return TL_SAND;
		case TH_VOLCANO: return TL_ASH;
		case TH_SWAMP: return TL_FOREST;
		default: return TL_GRASS;
		}
	case T_FOREST: return theme == TH_SNOW ? TL_SNOW : (theme == TH_DESERT ? TL_SAND : (theme == TH_VOLCANO ? TL_ASH : TL_FOREST));
	case T_HILL: return theme == TH_SNOW ? TL_SNOW : (theme == TH_VOLCANO ? TL_ASH : (theme == TH_CANYON || theme == TH_DESERT ? TL_DIRT : TL_GRASS));
	case T_MOUNTAIN: return theme == TH_SNOW ? TL_SNOW : TL_ROCK;
	case T_WATER: return TL_BED;
	case T_CHASM: return TL_ROCK;
	case T_LAVA: return TL_LAVA;
	case T_SAND: return TL_SAND;
	case T_SNOW: return TL_SNOW;
	case T_SWAMP: return TL_MUD;
	case T_ROAD: return theme == TH_DESERT || theme == TH_CANYON ? TL_DIRT : TL_COBBLE;
	case T_BRIDGE: return TL_PLANK;
	case T_RUINS: return TL_RUINS;
	case T_ROCK: return theme == TH_SNOW ? TL_SNOW : (theme == TH_RUINS ? TL_RUINS : TL_ROCK);
	}
	return TL_GRASS;
}

static int SideTile(int t, int theme)
{
	if (theme == TH_SNOW && t != T_WATER) return TL_CLIFF_SNOW;
	if (t == T_MOUNTAIN || t == T_ROCK || t == T_CHASM || theme == TH_VOLCANO || theme == TH_RUINS) return TL_CLIFF_ROCK;
	return TL_CLIFF_DIRT;
}

static void TileUV(int tile, float u, float v, float& ou, float& ov)
{
	const float inset = 6.f / 512.f, span = 128.f / 512.f;
	float x0 = (tile % 4) * span + inset, y0 = (tile / 4) * span + inset;
	float w = span - inset * 2;
	ou = x0 + u * w; ov = y0 + v * w;
}

static DWORD ThemeTint(int theme, float var)
{
	float r = 1, g = 1, b = 1;
	switch (theme) {
	case TH_SWAMP: r = .78f; g = .88f; b = .74f; break;
	case TH_VOLCANO: r = 1.0f; g = .86f; b = .82f; break;
	case TH_CANYON: r = 1.02f; g = .95f; b = .88f; break;
	case TH_DESERT: r = 1.02f; g = .98f; b = .92f; break;
	default: break;
	}
	return RGBAf(Clampf(r * var, 0, 1), Clampf(g * var, 0, 1), Clampf(b * var, 0, 1));
}

struct MeshBuild {
	std::vector<VtxS> v;
	std::vector<UINT> i;
	void Quad(V3 a, V3 b, V3 c, V3 d, V3 n, float u0, float v0, float u1, float v1, int tile, DWORD col)
	{
		UINT base = (UINT)v.size();
		V3 p[4] = { a, b, c, d };
		float uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
		for (int k = 0; k < 4; k++) {
			VtxS x;
			x.p[0] = p[k].x; x.p[1] = p[k].y; x.p[2] = p[k].z;
			x.n[0] = n.x; x.n[1] = n.y; x.n[2] = n.z;
			if (tile >= 0) TileUV(tile, uv[k][0], uv[k][1], x.uv[0], x.uv[1]);
			else { x.uv[0] = uv[k][0]; x.uv[1] = uv[k][1]; }
			x.c = col;
			v.push_back(x);
		}
		V3 cr = Cross(b - a, c - a);
		if (Dot(cr, n) >= 0) { i.push_back(base); i.push_back(base + 1); i.push_back(base + 2); i.push_back(base); i.push_back(base + 2); i.push_back(base + 3); }
		else { i.push_back(base); i.push_back(base + 2); i.push_back(base + 1); i.push_back(base); i.push_back(base + 3); i.push_back(base + 2); }
	}
	void Append(const StaticSrc& s, const M4& w, DWORD col, float swayH)
	{
		UINT base = (UINT)v.size();
		float h = max(.05f, s.bb[4] - s.bb[1]);
		for (const auto& src : s.v) {
			VtxS x = src;
			V3 p = MXformPt(w, v3(src.p[0], src.p[1], src.p[2]));
			V3 n = Norm(MXformDir(w, v3(src.n[0], src.n[1], src.n[2])));
			x.p[0] = p.x; x.p[1] = p.y; x.p[2] = p.z;
			x.n[0] = n.x; x.n[1] = n.y; x.n[2] = n.z;
			int a = 255;
			if (swayH > 0) {
				float k = Clampf((src.p[1] - s.bb[1]) / h, 0, 1);
				a = 255 - (int)(k * k * swayH * 63.f);
			}
			x.c = (col & 0x00FFFFFF) | ((DWORD)Clampi(a, 0, 255) << 24);
			v.push_back(x);
		}
		for (WORD k : s.i) i.push_back(base + k);
	}
};

static void AddBox(MeshBuild& mb, V3 c, V3 h, DWORD col, int tile)
{
	V3 p[8] = {
		v3(c.x - h.x, c.y - h.y, c.z - h.z), v3(c.x + h.x, c.y - h.y, c.z - h.z), v3(c.x + h.x, c.y + h.y, c.z - h.z), v3(c.x - h.x, c.y + h.y, c.z - h.z),
		v3(c.x - h.x, c.y - h.y, c.z + h.z), v3(c.x + h.x, c.y - h.y, c.z + h.z), v3(c.x + h.x, c.y + h.y, c.z + h.z), v3(c.x - h.x, c.y + h.y, c.z + h.z),
	};
	mb.Quad(p[0], p[1], p[2], p[3], v3(0, 0, -1), 0, 1, 1, 0, tile, col);
	mb.Quad(p[5], p[4], p[7], p[6], v3(0, 0, 1), 0, 1, 1, 0, tile, col);
	mb.Quad(p[4], p[0], p[3], p[7], v3(-1, 0, 0), 0, 1, 1, 0, tile, col);
	mb.Quad(p[1], p[5], p[6], p[2], v3(1, 0, 0), 0, 1, 1, 0, tile, col);
	mb.Quad(p[3], p[2], p[6], p[7], v3(0, 1, 0), 0, 0, 1, 1, tile, col);
}

BOOL Renderer::BuildTerrain(const Map& m)
{
	m_terrain.Release(); m_water.Release(); m_lava.Release();
	for (auto& b : m_props) b.mesh.Release();
	m_props.clear();
	MeshBuild tb, wb, lb;
	tb.v.reserve(80000); tb.i.reserve(120000);
	const float bottom = -3.2f;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		const Cell& c = m.At(x, z);
		if (c.t == T_VOID) continue;
		float y = m.TopY(x, z);
		float var = .92f + ((c.var * 37) % 100) / 100.f * .13f;
		int tile = TopTile(c.t, m.theme);
		DWORD col = ThemeTint(m.theme, var);
		if (c.t == T_CHASM) col = RGBAf(.35f, .32f, .33f);
		tb.Quad(v3((float)x, y, (float)z), v3((float)x, y, (float)z + 1), v3((float)x + 1, y, (float)z + 1), v3((float)x + 1, y, (float)z), v3(0, 1, 0),
			0, 0, 1, 1, tile, col);
		static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			float ny = Map::In(nx, nz) ? m.TopY(nx, nz) : bottom;
			if (ny >= y - 1e-3f) continue;
			int st = SideTile(c.t, m.theme);
			V3 n = v3((float)dx[k], 0, (float)dz[k]);
			float ex0, ez0, ex1, ez1;
			if (dx[k] == 1) { ex0 = x + 1.f; ez0 = (float)z; ex1 = x + 1.f; ez1 = z + 1.f; }
			else if (dx[k] == -1) { ex0 = (float)x; ez0 = z + 1.f; ex1 = (float)x; ez1 = (float)z; }
			else if (dz[k] == 1) { ex0 = x + 1.f; ez0 = z + 1.f; ex1 = (float)x; ez1 = z + 1.f; }
			else { ex0 = (float)x; ez0 = (float)z; ex1 = x + 1.f; ez1 = (float)z; }
			float yt = y;
			int seg = 0;
			while (yt > ny + 1e-3f && seg < 40) {
				float yb = max(ny, yt - kHStep * 2.f);
				float shade = .78f - seg * .04f;
				DWORD sc = ThemeTint(m.theme, Clampf(var * shade, .3f, 1.f));
				float v0 = 0, v1 = (yt - yb) / (kHStep * 2.f);
				tb.Quad(v3(ex0, yt, ez0), v3(ex1, yt, ez1), v3(ex1, yb, ez1), v3(ex0, yb, ez0), n, 0, v0, 1, v1, seg == 0 ? st : (st == TL_CLIFF_SNOW ? TL_CLIFF_ROCK : st), sc);
				yt = yb; seg++;
			}
		}
		if (c.t == T_WATER) {
			float wy = m.StandY(x, z) - .02f;
			wb.Quad(v3((float)x, wy, (float)z), v3((float)x, wy, z + 1.f), v3(x + 1.f, wy, z + 1.f), v3(x + 1.f, wy, (float)z), v3(0, 1, 0), 0, 0, 1, 1, -1, 0xFFFFFFFF);
		} else if (c.t == T_LAVA) {
			float ly = m.StandY(x, z) - .02f;
			lb.Quad(v3((float)x, ly, (float)z), v3((float)x, ly, z + 1.f), v3(x + 1.f, ly, z + 1.f), v3(x + 1.f, ly, (float)z), v3(0, 1, 0), 0, 0, 1, 1, -1, 0xFFFFFFFF);
		}
	}
	if (!MakeMesh32(m_terrain, tb.v.data(), sizeof(VtxS), (UINT)tb.v.size(), tb.i.data(), (UINT)tb.i.size())) return FALSE;
	m_terrain.tex = TEX_ATLAS;
	if (!wb.v.empty()) { MakeMesh32(m_water, wb.v.data(), sizeof(VtxS), (UINT)wb.v.size(), wb.i.data(), (UINT)wb.i.size()); m_water.tex = TEX_WHITE; }
	if (!lb.v.empty()) { MakeMesh32(m_lava, lb.v.data(), sizeof(VtxS), (UINT)lb.v.size(), lb.i.data(), (UINT)lb.i.size()); m_lava.tex = TEX_WHITE; }
	switch (m.theme) {
	case TH_SWAMP: m_waterColor = RGBA8(70, 100, 60); break;
	case TH_SNOW: m_waterColor = RGBA8(110, 160, 210); break;
	case TH_DESERT: m_waterColor = RGBA8(50, 150, 170); break;
	default: m_waterColor = RGBA8(55, 125, 190); break;
	}

	// 小物
	if (m_assetsOk) {
		MeshBuild nat, hal, pine, proc;
		const int sTreeA = FindStatic("tree_single_A"), sTreeB = FindStatic("tree_single_B");
		const int sTreesA = FindStatic("trees_A_medium"), sTreesB = FindStatic("trees_B_medium"), sTreesL = FindStatic("trees_B_large");
		const int sRockA = FindStatic("rock_single_A"), sRockC = FindStatic("rock_single_C"), sRockE = FindStatic("rock_single_E");
		const int sMtA = FindStatic("mountain_A"), sMtB = FindStatic("mountain_B"), sMtC = FindStatic("mountain_C"), sMtG = FindStatic("mountain_A_grass");
		const int sHillT = FindStatic("hills_A_trees"), sLily = FindStatic("waterlily_A"), sPlant = FindStatic("waterplant_A");
		const int sTent = FindStatic("tent"), sFlag = FindStatic("flag_red"), sBarrel = FindStatic("barrel"), sCrate = FindStatic("crate_A_big");
		const int sDeadL = FindStatic("tree_dead_large"), sDeadM = FindStatic("tree_dead_medium");
		const int sPineO = FindStatic("tree_pine_orange_large"), sPineY = FindStatic("tree_pine_yellow_medium");
		const int sGrave = FindStatic("gravestone"), sPump = FindStatic("pumpkin_orange_jackolantern"), sLant = FindStatic("lantern_standing");
		auto place = [&](MeshBuild& mb, int si, float cx, float cy, float cz, float sc, float rot, DWORD col, float sway) {
			if (si < 0) return;
			M4 w = MMul(MMul(MScale(sc, sc, sc), MRotY(rot)), MTrans(cx, cy, cz));
			mb.Append(statics[si], w, col, sway);
		};
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			const Cell& c = m.At(x, z);
			if (c.prop == PK_NONE) continue;
			float cx = x + .5f, cz = z + .5f, cy = m.TopY(x, z);
			float rot = (c.var % 6) * 1.0472f + ((c.var >> 3) & 7) * .05f;
			float jx = (((c.var * 13) % 9) - 4) * .03f, jz = (((c.var * 7) % 9) - 4) * .03f;
			DWORD tint = 0xFFFFFFFF;
			switch (c.prop) {
			case PK_TREE: {
				int si = (c.var & 1) ? sTreeA : sTreeB;
				place(nat, si, cx + jx, cy, cz + jz, 1.35f + (c.var % 5) * .08f, rot, tint, 1.f);
				if ((c.var & 6) == 2) place(nat, (c.var & 8) ? sTreeA : sTreeB, cx - .28f, cy, cz + .25f, 1.0f, rot * 1.7f, tint, 1.f);
				break; }
			case PK_TREES: {
				int si = (c.var % 3 == 0) ? sTreesA : ((c.var % 3 == 1) ? sTreesB : sTreesL);
				place(nat, si, cx, cy, cz, .56f, rot, tint, .8f);
				break; }
			case PK_HILLTREES: place(nat, sHillT, cx, cy, cz, .55f, rot, tint, .5f); break;
			case PK_PINE: {
				int si = (c.var & 1) ? sPineO : sPineY;
				float sc = si == sPineO ? .22f : .27f;
				place(pine, si, cx + jx, cy, cz + jz, sc + (c.var % 4) * .01f, rot, RGBAf(.92f, .98f, .95f), 1.f);
				break; }
			case PK_DEAD: {
				int si = (c.var & 1) ? sDeadL : sDeadM;
				place(hal, si, cx + jx, cy - .05f, cz + jz, si == sDeadL ? .3f : .34f, rot, m.theme == TH_DESERT ? RGBAf(1, .92f, .8f) : tint, .4f);
				break; }
			case PK_ROCK: {
				int si = (c.var % 3 == 0) ? sRockA : ((c.var % 3 == 1) ? sRockC : sRockE);
				DWORD rc = m.theme == TH_SNOW ? RGBAf(.95f, .97f, 1) : (m.theme == TH_VOLCANO ? RGBAf(.55f, .45f, .42f) : tint);
				place(nat, si, cx, cy, cz, si == sRockA ? 3.2f : 2.6f, rot, rc, 0);
				break; }
			case PK_MOUNT: {
				int si = (m.theme == TH_PLAINS || m.theme == TH_FOREST) && (c.var & 3) == 0 ? sMtG : ((c.var % 3 == 0) ? sMtA : (c.var % 3 == 1 ? sMtB : sMtC));
				DWORD mc = m.theme == TH_SNOW ? RGBAf(.92f, .95f, 1) : (m.theme == TH_VOLCANO ? RGBAf(.62f, .5f, .48f) : (m.theme == TH_DESERT ? RGBAf(1, .88f, .7f) : tint));
				place(nat, si, cx, cy - .05f, cz, .62f + (c.var % 4) * .03f, rot, mc, 0);
				break; }
			case PK_LILY:
				place(nat, (c.var & 1) ? sLily : sPlant, cx + jx * 3, m.StandY(x, z) - .01f, cz + jz * 3, 3.f, rot, tint, 0);
				break;
			case PK_PILLAR: {
				float hgt = .9f + (c.var % 5) * .18f;
				DWORD pc = RGBAf(.82f, .8f, .74f);
				AddBox(proc, v3(cx, cy + .06f, cz), v3(.36f, .06f, .36f), pc, TL_RUINS);
				AddBox(proc, v3(cx, cy + .12f + hgt * .5f, cz), v3(.24f, hgt * .5f, .24f), pc, TL_RUINS);
				if ((c.var & 3) != 0) AddBox(proc, v3(cx, cy + .12f + hgt + .06f, cz), v3(.32f, .06f, .32f), pc, TL_RUINS);
				break; }
			case PK_GRAVE: place(hal, sGrave, cx, cy, cz, .42f, rot, tint, 0); break;
			case PK_TENT: place(nat, sTent, cx, cy, cz, 1.9f, rot, tint, 0); break;
			case PK_FLAG: place(nat, sFlag, cx, cy, cz, 3.f, rot, tint, .6f); break;
			case PK_BARREL: place(nat, sBarrel, cx, cy, cz, 2.6f, rot, tint, 0); place(nat, sBarrel, cx + .25f, cy, cz - .2f, 2.2f, rot, tint, 0); break;
			case PK_CRATE: place(nat, sCrate, cx, cy, cz, 2.6f, rot, tint, 0); break;
			case PK_LANTERN: place(hal, sLant, cx + .3f, cy, cz + .3f, .6f, rot, tint, 0); break;
			case PK_PUMPKIN: place(hal, sPump, cx, cy, cz, .45f, rot, tint, 0); break;
			}
		}
		auto flush = [&](MeshBuild& mb, int tex, float hue) {
			if (mb.v.empty()) return;
			PropBatch pb;
			pb.hue = hue;
			if (MakeMesh32(pb.mesh, mb.v.data(), sizeof(VtxS), (UINT)mb.v.size(), mb.i.data(), (UINT)mb.i.size())) {
				pb.mesh.tex = tex;
				m_props.push_back(pb);
			}
		};
		int natTex = sTreeA >= 0 ? statics[sTreeA].tex : 5;
		int halTex = sDeadL >= 0 ? statics[sDeadL].tex : 6;
		flush(nat, natTex, 0);
		flush(hal, halTex, 0);
		flush(pine, halTex, 1.9f);
		flush(proc, TEX_ATLAS, 0);
	}
	BuildMinimap(m);
	return TRUE;
}

void Renderer::BuildMinimap(const Map& m)
{
	TAC_REL(m_srvMini); TAC_REL(m_texMini);
	std::vector<DWORD> px(MAPN * MAPN);
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		const Cell& c = m.At(x, z);
		int r = 100, g = 160, b = 80;
		switch (c.t) {
		case T_PLAIN:
			if (m.theme == TH_SNOW) { r = 230; g = 235; b = 245; }
			else if (m.theme == TH_DESERT) { r = 220; g = 195; b = 140; }
			else if (m.theme == TH_VOLCANO) { r = 85; g = 75; b = 72; }
			else if (m.theme == TH_CANYON) { r = 150; g = 115; b = 80; }
			else { r = 105; g = 165; b = 80; }
			break;
		case T_FOREST: r = 40; g = 100; b = 45; break;
		case T_HILL: r = 140; g = 160; b = 90; break;
		case T_MOUNTAIN: r = 120; g = 112; b = 105; break;
		case T_WATER: r = 60; g = 120; b = 200; break;
		case T_CHASM: r = 20; g = 18; b = 22; break;
		case T_LAVA: r = 240; g = 100; b = 30; break;
		case T_SAND: r = 225; g = 200; b = 140; break;
		case T_SNOW: r = 235; g = 240; b = 250; break;
		case T_SWAMP: r = 80; g = 90; b = 55; break;
		case T_ROAD: r = 185; g = 170; b = 140; break;
		case T_BRIDGE: r = 160; g = 110; b = 60; break;
		case T_RUINS: r = 170; g = 165; b = 150; break;
		case T_ROCK: r = 90; g = 88; b = 86; break;
		case T_VOID: r = 12; g = 14; b = 20; break;
		}
		float sh = .82f + c.h * .04f;
		if (c.gimm == GM_HEAL) { r = 80; g = 230; b = 120; sh = 1.f; }
		else if (c.gimm == GM_MIGHT) { r = 255; g = 200; b = 60; sh = 1.f; }
		else if (c.gimm == GM_WARP) { r = 80; g = 210; b = 255; sh = 1.f; }
		else if (c.gimm == GM_CHEST) { r = 230; g = 180; b = 50; sh = 1.f; }
		else if (c.t == T_DOOR) { r = 150; g = 100; b = 50; sh = 1.f; }
		px[(size_t)z * MAPN + x] = BGRA((int)(r * sh), (int)(g * sh), (int)(b * sh));
	}
	MakeTex(m_dev, m_ctx, px.data(), MAPN, MAPN, FALSE, &m_texMini, &m_srvMini);
}

// ============================================================ ポートレート
BOOL Renderer::BeginPortrait(int slot)
{
	if (!m_ptRtv || !m_ptDsv || slot < 0 || slot >= PORTRAIT_N) return FALSE;
	m_inPortrait = TRUE;
	m_portraitSlot = slot;
	return TRUE;
}

void Renderer::EndPortrait()
{
	m_inPortrait = FALSE;
	m_ctx->OMSetRenderTargets(1, &m_msRtv, m_dsv);
	D3D11_VIEWPORT vp = { 0, 0, (float)m_w, (float)m_h, 0, 1 };
	m_ctx->RSSetViewports(1, &vp);
	m_fcb.screen[0] = (float)m_w; m_fcb.screen[1] = (float)m_h;
}

// ============================================================ HUD
void Renderer::BeginHud()
{
	if (m_msaa > 1 && m_msTex && m_bb)
		m_ctx->ResolveSubresource(m_bb, 0, m_msTex, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
	m_ctx->OMSetRenderTargets(1, &m_bbRtv, NULL);
	D3D11_VIEWPORT vp = { 0, 0, (float)m_w, (float)m_h, 0, 1 };
	m_ctx->RSSetViewports(1, &vp);
	m_fcb.screen[0] = (float)m_w; m_fcb.screen[1] = (float)m_h; m_fcb.screen[2] = 1.f / m_w; m_fcb.screen[3] = 1.f / m_h;
	UpdateFrameCB();
	m_hud.clear();
	m_hudSrv = m_srv[TEX_WHITE];
}

void Renderer::FlushHud()
{
	if (m_hud.empty()) return;
	m_ctx->IASetInputLayout(m_ilH);
	m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_ctx->VSSetShader(m_vsH, NULL, 0);
	m_ctx->PSSetShader(m_psH, NULL, 0);
	m_ctx->RSSetState(m_rsHud);
	m_ctx->OMSetDepthStencilState(m_dsOff, 0);
	m_ctx->OMSetBlendState(m_bsPremul, NULL, 0xFFFFFFFF);
	m_ctx->PSSetSamplers(0, 1, &m_sClamp);
	m_ctx->PSSetShaderResources(0, 1, &m_hudSrv);
	const size_t maxV = 6 * 4096;
	size_t off = 0;
	while (off < (size_t)m_hud.size()) {
		size_t n = min(maxV, (size_t)m_hud.size() - off);
		D3D11_MAPPED_SUBRESOURCE ms;
		if (FAILED(m_ctx->Map(m_vbHud, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) break;
		memcpy(ms.pData, m_hud.data() + off, n * sizeof(VtxH));
		m_ctx->Unmap(m_vbHud, 0);
		UINT st = sizeof(VtxH), o = 0;
		m_ctx->IASetVertexBuffers(0, 1, &m_vbHud, &st, &o);
		m_ctx->Draw((UINT)n, 0);
		off += n;
	}
	m_hud.clear();
	ID3D11SamplerState* ss[2] = { m_sLin, m_sCmp };
	m_ctx->PSSetSamplers(0, 2, ss);
}

void Renderer::HudPush(float x, float y, float w, float h, float u0, float v0, float u1, float v1, DWORD c0, DWORD c1, ID3D11ShaderResourceView* srv)
{
	if (!srv) srv = m_srv[TEX_WHITE];
	if (srv != m_hudSrv) { FlushHud(); m_hudSrv = srv; }
	if (m_hud.size() + 6 > 24576) FlushHud();
	VtxH a = { x, y, u0, v0, c0 }, b = { x + w, y, u1, v0, c0 }, c = { x + w, y + h, u1, v1, c1 }, d = { x, y + h, u0, v1, c1 };
	m_hud.push_back(a); m_hud.push_back(b); m_hud.push_back(c);
	m_hud.push_back(a); m_hud.push_back(c); m_hud.push_back(d);
}

void Renderer::HudRect(float x, float y, float w, float h, DWORD c) { HudPush(x, y, w, h, 0, 0, 1, 1, c, c, NULL); }
void Renderer::HudRectGrad(float x, float y, float w, float h, DWORD t, DWORD b) { HudPush(x, y, w, h, 0, 0, 1, 1, t, b, NULL); }

void Renderer::HudFrame(float x, float y, float w, float h, float t, DWORD c)
{
	HudRect(x, y, w, t, c); HudRect(x, y + h - t, w, t, c);
	HudRect(x, y + t, t, h - 2 * t, c); HudRect(x + w - t, y + t, t, h - 2 * t, c);
}

static DWORD MulA(DWORD c, float a) { return (c & 0x00FFFFFF) | ((DWORD)Clampi((int)(((c >> 24) & 255) * a), 0, 255) << 24); }
static DWORD Shade(DWORD c, float k)
{
	int r = (int)((c & 255) * k), g = (int)(((c >> 8) & 255) * k), b = (int)(((c >> 16) & 255) * k);
	return RGBA8(r, g, b, (c >> 24) & 255);
}

void Renderer::HudPanel(float x, float y, float w, float h, DWORD fill, DWORD border, float alpha)
{
	// 角を 1px 落とした二重枠＋縦グラデ
	HudRectGrad(x + 2, y + 2, w - 4, h - 4, MulA(Shade(fill, 1.12f), alpha), MulA(Shade(fill, .82f), alpha));
	DWORD b = MulA(border, alpha);
	HudRect(x + 3, y, w - 6, 2, b); HudRect(x + 3, y + h - 2, w - 6, 2, b);
	HudRect(x, y + 3, 2, h - 6, b); HudRect(x + w - 2, y + 3, 2, h - 6, b);
	HudRect(x + 1, y + 1, 2, 2, b); HudRect(x + w - 3, y + 1, 2, 2, b); HudRect(x + 1, y + h - 3, 2, 2, b); HudRect(x + w - 3, y + h - 3, 2, 2, b);
	HudRect(x + 4, y + 4, w - 8, 1, MulA(RGBA8(255, 255, 255, 60), alpha));
}

void Renderer::HudImage(ID3D11ShaderResourceView* srv, float x, float y, float w, float h, float u0, float v0, float u1, float v1, DWORD c)
{
	HudPush(x, y, w, h, u0, v0, u1, v1, c, c, srv);
}

void Renderer::HudQuad(ID3D11ShaderResourceView* srv, const float* xy, const float* uv, DWORD c)
{
	if (!xy || !uv) return;
	if (!srv) srv = m_srv[TEX_WHITE];
	if (srv != m_hudSrv) { FlushHud(); m_hudSrv = srv; }
	if (m_hud.size() + 6 > 24576) FlushHud();
	VtxH p[4];
	for (int i = 0; i < 4; i++) {
		p[i].x = xy[i * 2]; p[i].y = xy[i * 2 + 1];
		p[i].u = uv[i * 2]; p[i].v = uv[i * 2 + 1];
		p[i].c = c;
	}
	m_hud.push_back(p[0]); m_hud.push_back(p[1]); m_hud.push_back(p[2]);
	m_hud.push_back(p[0]); m_hud.push_back(p[2]); m_hud.push_back(p[3]);
}

TextSprite* Renderer::GetText(const wchar_t* s, float px, int bold, int shadow, int box, float boxW, float boxH)
{
	if (!s || !s[0]) return NULL;
	wchar_t key[48];
	_snwprintf_s(key, _TRUNCATE, L"%d|%d|%d|%d|%d|", (int)(px * 4), bold, shadow, box ? (int)boxW : 0, box ? (int)boxH : 0);
	std::wstring k = std::wstring(key) + s;
	auto it = m_text.find(k);
	if (it != m_text.end()) { it->second.lastFrame = m_frame; return &it->second; }
	int len = (int)wcslen(s);
	int w, h;
	if (box) { w = max(16, (int)ceilf(boxW)); h = max(16, (int)ceilf(boxH)); }
	else { w = Clampi((int)(len * px * 1.15f + px * 2 + 8), 16, 4096); h = max(16, (int)(px * 1.6f + 6)); }
	Soft3DTextD2DCanvas* cv = Soft3DTextD2D_Begin(w, h);
	if (!cv) return NULL;
	float sd = max(1.f, px / 14.f);
	if (shadow)
		Soft3DTextD2D_DrawTextShadow(cv, s, 2, 1, (float)w - 4, (float)h - 2, px, bold ? 1 : 0, 0, box ? 0 : 1, 255, 255, 255, 255, sd, sd, 220, 0, 0, 0, box ? 1 : 0);
	else
		Soft3DTextD2D_DrawText(cv, s, 2, 1, (float)w - 4, (float)h - 2, px, bold ? 1 : 0, 0, box ? 0 : 1, 255, 255, 255, 255, box ? 1 : 0);
	const BYTE* bits = NULL; UINT stride = 0;
	TextSprite ts = {};
	if (Soft3DTextD2D_End(cv, &bits, &stride) && bits) {
		int maxX = 0, maxY = 0;
		for (int y = 0; y < h; y++) {
			const BYTE* row = bits + (size_t)y * stride;
			for (int x = w - 1; x > maxX; x--) if (row[x * 4 + 3] > 8) { maxX = x; break; }
			for (int x = 0; x < w; x++) if (row[x * 4 + 3] > 8) { maxY = y; break; }
		}
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
		td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA sdd = { bits, stride, 0 };
		if (SUCCEEDED(m_dev->CreateTexture2D(&td, &sdd, &ts.tex)))
			m_dev->CreateShaderResourceView(ts.tex, NULL, &ts.srv);
		ts.w = w; ts.h = h;
		ts.cw = box ? w : min(w, maxX + 3);
		ts.ch = box ? min(h, maxY + 3) : h;
		ts.lastFrame = m_frame;
	}
	Soft3DTextD2D_Release(cv);
	if (!ts.srv) { TAC_REL(ts.tex); return NULL; }
	auto r = m_text.emplace(k, ts);
	return &r.first->second;
}

float Renderer::TextWidth(const wchar_t* s, float px, int bold)
{
	TextSprite* t = GetText(s, px, bold, 1, 0, 0, 0);
	return t ? (float)t->cw : 0.f;
}

float Renderer::HudText(const wchar_t* s, float x, float y, float px, DWORD c, int align, int bold, int shadow, float alpha)
{
	TextSprite* t = GetText(s, px, bold, shadow, 0, 0, 0);
	if (!t) return 0;
	float w = (float)t->cw;
	float dx = align == 1 ? x - w * .5f : (align == 2 ? x - w : x);
	float dy = y - t->h * .5f;
	HudPush(floorf(dx), floorf(dy), w, (float)t->h, 0, 0, w / t->w, 1, MulA(c, alpha), MulA(c, alpha), t->srv);
	return w;
}

void Renderer::HudTextBox(const wchar_t* s, float x, float y, float w, float h, float px, DWORD c, int bold)
{
	TextSprite* t = GetText(s, px, bold, 1, 1, w, h);
	if (!t) return;
	HudPush(floorf(x), floorf(y), (float)t->w, (float)t->h, 0, 0, 1, 1, c, c, t->srv);
}

void Renderer::EndHud() { FlushHud(); }

void Renderer::Present()
{
	if (!m_swap) return;
	const BOOL vis = m_hwnd && ::IsWindowVisible(m_hwnd);
	/* 表示前の Present(1) はフレームが退役せず、クリックまで描画が止まる */
	const UINT flags = m_presented ? 0u : DXGI_PRESENT_DO_NOT_WAIT;
	const UINT interval = m_presented ? 1u : 0u;
	HRESULT hr = m_swap->Present(interval, flags);
	if (hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
	if (SUCCEEDED(hr) && vis) m_presented = TRUE;
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) { m_lost = TRUE; m_lastHr = hr; }
}

void Renderer::EndFrameTextGc()
{
	if (m_text.size() < 400) return;
	for (auto it = m_text.begin(); it != m_text.end();) {
		if (m_frame - it->second.lastFrame > 300) { TAC_REL(it->second.srv); TAC_REL(it->second.tex); it = m_text.erase(it); }
		else ++it;
	}
}

BOOL Renderer::ProjectToScreen(V3 p, float& sx, float& sy) const
{
	const float* m = m_vp.m;
	float x = p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12];
	float y = p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13];
	float w = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
	if (w < 1e-4f) return FALSE;
	sx = (x / w * .5f + .5f) * m_w;
	sy = (1.f - (y / w * .5f + .5f)) * m_h;
	return TRUE;
}

void Renderer::ScreenRay(float sx, float sy, V3& org, V3& dir) const
{
	M4 inv = MInverse(m_vp);
	float nx = sx / m_w * 2.f - 1.f, ny = 1.f - sy / m_h * 2.f;
	V3 a = MXformPt(inv, v3(nx, ny, 0.f));
	V3 b = MXformPt(inv, v3(nx, ny, 1.f));
	org = a;
	dir = Norm(b - a);
}

} // namespace otac
