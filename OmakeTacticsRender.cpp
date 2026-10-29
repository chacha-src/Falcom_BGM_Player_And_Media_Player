// OmakeTacticsRender.cpp — おまけタクティクス: DirectX11 描画

#include "stdafx.h"
#include "resource.h"
#include "OmakeTacticsRender.h"
#include "Soft3DTexRes.h"
#include "Soft3DTextD2D.h"
#include <d3dcompiler.h>
#include <d3d11_1.h>
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
	m_hwnd = NULL; m_wait = NULL; m_lost = FALSE; m_presented = FALSE; m_waitStuck = 0; m_lastRebind = 0;
	m_w = m_h = 0; m_quality = 1; m_msaa = 1; m_lastHr = S_OK; m_failStage = 0; m_assetsOk = FALSE; m_loadFn = NULL; m_loadUser = NULL; m_pass = PASS_MAIN;
	m_dev = NULL; m_ctx = NULL; m_swap = NULL; m_bbRtv = NULL; m_bb = NULL; m_msTex = NULL; m_msRtv = NULL; m_dsTex = NULL; m_dsv = NULL;
	m_sceneTex = NULL; m_sceneRtv = NULL; m_sceneSrv = NULL;
	m_blA = m_blB = NULL; m_blARtv = m_blBRtv = NULL; m_blASrv = m_blBSrv = NULL; m_blW = m_blH = 0;
	m_shTex = NULL; m_shDsv = NULL; m_shSrv = NULL; m_shSize = 0;
	m_ptTex = NULL; m_ptRtv = NULL; m_srvPortrait = NULL; m_ptDs = NULL; m_ptDsv = NULL;
	m_vsS = m_vsK = m_vsH = m_vsP = m_vsQ = NULL;
	m_psM = m_psO = m_psOV = m_psH = m_psP = m_psSky = NULL;
	m_psBloom = m_psBlur = m_psGrade = m_psUi = NULL;
	m_psProp = m_psWater = m_psLava = m_psOverride = NULL;
	for (int gi = 0; gi < 8; gi++) m_psGround[gi] = NULL;
	m_groundTex = NULL; m_groundSrv = NULL; m_matTex = NULL; m_matSrv = NULL;
	m_csSkin = NULL; m_cbSkinBone = m_cbSkinParm = m_cbHairCloth = m_faceDummy = NULL; m_faceDummySRV = NULL;
	m_csCloth[0] = m_csCloth[1] = m_csCloth[2] = NULL; m_cbCloth = NULL;
	m_hairSB = m_hairVB = NULL; m_hairBind = m_hairSrv = NULL; m_hairUAV = NULL; m_hairTex = NULL; m_hairNv = 0;
	memset(m_hairCap, 0, sizeof(m_hairCap));
	memset(m_hairCapN, 0, sizeof(m_hairCapN));
	m_scalpModel = -1;
	m_capC = RGBA8(70, 70, 70);
	m_ilS = m_ilK = m_ilH = m_ilP = NULL;
	m_cbFrame = m_cbObj = m_cbBones = m_vbHud = m_vbDyn = NULL; m_vbDynBytes = 0;
	m_sLin = m_sCmp = m_sClamp = NULL;
	m_rsBack = m_rsFront = m_rsNone = m_rsShadow = m_rsHud = NULL;
	m_dsWrite = m_dsRead = m_dsOff = NULL;
	m_bsOpaque = m_bsAlpha = m_bsPremul = m_bsAdd = NULL;
	for (int i = 0; i < TEX_MAX; i++) { m_tex[i] = NULL; m_srv[i] = NULL; }
	m_texMini = NULL; m_srvMini = NULL;
	m_waterColor = RGBA8(60, 130, 190);
	m_theme = 0;
	m_monN = 0; m_monBytes = NULL; m_monBytesN = 0;
	m_keepModels = m_keepRigs = m_keepCreatures = 0;
	m_body0M = m_body0R = m_body0C = m_bodyN = 0;
	m_gearShown = m_avaReady = 0;
	memset(m_bodySlot, 0xFF, sizeof(m_bodySlot));
	memset(m_monModel, 0xFF, sizeof(m_monModel));
	memset(m_monBody, 0, sizeof(m_monBody));
	memset(&m_fcb, 0, sizeof(m_fcb));
	m_lightVP = MIdent();
	m_hudSrv = NULL;
	m_inPortrait = FALSE; m_portraitSlot = 0;
	m_avaSegN = 0;
	memset(m_text, 0, sizeof(m_text));
}

Renderer::~Renderer() { Release(); }

void Renderer::ReleaseTargets()
{
	if (m_ctx) m_ctx->OMSetRenderTargets(0, NULL, NULL);
	if (m_msRtv == m_bbRtv) m_msRtv = NULL;
	TAC_REL(m_msRtv); TAC_REL(m_msTex);
	TAC_REL(m_bbRtv); TAC_REL(m_bb);
	TAC_REL(m_sceneSrv); TAC_REL(m_sceneRtv); TAC_REL(m_sceneTex);
	TAC_REL(m_blASrv); TAC_REL(m_blARtv); TAC_REL(m_blA);
	TAC_REL(m_blBSrv); TAC_REL(m_blBRtv); TAC_REL(m_blB);
	TAC_REL(m_dsv); TAC_REL(m_dsTex);
}

static void ClearMonPack();

void Renderer::Release()
{
	m_gearShown = 0;
	m_avaReady = 0;
	ClearMonPack();
	for (int ti = 0; ti < TAC_TEXTN; ti++) if (m_text[ti].live) { TAC_REL(m_text[ti].sp.srv); TAC_REL(m_text[ti].sp.tex); m_text[ti].live = 0; }
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
	for (auto& s : m_skins) ReleaseSkinGpu(s);
	m_skins.clear();
	m_monN = 0; m_monBytes = NULL; m_monBytesN = 0;
	ReleaseAvatarCache();
	for (auto& c : m_gear) c.Release();
	m_gear.clear();
	m_gearGrip.clear();
	m_assetsOk = FALSE;
	ReleaseTargets();
	TAC_REL(m_shSrv); TAC_REL(m_shDsv); TAC_REL(m_shTex);
	TAC_REL(m_srvPortrait); TAC_REL(m_ptRtv); TAC_REL(m_ptTex); TAC_REL(m_ptDsv); TAC_REL(m_ptDs);
	TAC_REL(m_vsS); TAC_REL(m_vsK); TAC_REL(m_vsH); TAC_REL(m_vsP); TAC_REL(m_vsQ);
	TAC_REL(m_psM); TAC_REL(m_psO); TAC_REL(m_psOV); TAC_REL(m_psH); TAC_REL(m_psP); TAC_REL(m_psSky);
	TAC_REL(m_psBloom); TAC_REL(m_psBlur); TAC_REL(m_psGrade); TAC_REL(m_psUi);
	TAC_REL(m_psProp); TAC_REL(m_psWater); TAC_REL(m_psLava);
	for (int gi = 0; gi < 8; gi++) TAC_REL(m_psGround[gi]);
	m_psOverride = NULL;
	TAC_REL(m_groundSrv); TAC_REL(m_groundTex); TAC_REL(m_matSrv); TAC_REL(m_matTex);
	TAC_REL(m_csSkin); TAC_REL(m_cbSkinBone); TAC_REL(m_cbSkinParm); TAC_REL(m_cbHairCloth); TAC_REL(m_faceDummySRV); TAC_REL(m_faceDummy);
	for (int ci = 0; ci < 3; ci++) TAC_REL(m_csCloth[ci]);
	TAC_REL(m_cbCloth);
	ReleaseHair();
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
	m_waitStuck = 0;
	m_lastRebind = 0;
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

static BOOL MakeColorRt(ID3D11Device* dev, int w, int h, ID3D11Texture2D** tex, ID3D11RenderTargetView** rtv, ID3D11ShaderResourceView** srv)
{
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = max(1, w); td.Height = max(1, h); td.MipLevels = 1; td.ArraySize = 1; td.SampleDesc.Count = 1;
	td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(dev->CreateTexture2D(&td, NULL, tex))) return FALSE;
	if (FAILED(dev->CreateRenderTargetView(*tex, NULL, rtv)) || FAILED(dev->CreateShaderResourceView(*tex, NULL, srv))) {
		TAC_REL(*rtv); TAC_REL(*srv); TAC_REL(*tex);
		return FALSE;
	}
	return TRUE;
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
	int div = m_quality <= 0 ? 4 : 2;
	m_blW = max(8, m_w / div); m_blH = max(8, m_h / div);
	if (!MakeColorRt(m_dev, m_w, m_h, &m_sceneTex, &m_sceneRtv, &m_sceneSrv)
		|| !MakeColorRt(m_dev, m_blW, m_blH, &m_blA, &m_blARtv, &m_blASrv)
		|| !MakeColorRt(m_dev, m_blW, m_blH, &m_blB, &m_blBRtv, &m_blBSrv)) {
		TAC_REL(m_sceneSrv); TAC_REL(m_sceneRtv); TAC_REL(m_sceneTex);
		TAC_REL(m_blASrv); TAC_REL(m_blARtv); TAC_REL(m_blA);
		TAC_REL(m_blBSrv); TAC_REL(m_blBRtv); TAC_REL(m_blB);
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
	m_waitStuck = 0;
	if (FAILED(hr)) { m_lastHr = hr; m_lost = TRUE; return FALSE; }
	return CreateTargets();
}

BOOL Renderer::FrameReady()
{
	if (!m_wait || !m_presented) return TRUE;
	if (WaitForSingleObject(m_wait, 0) == WAIT_OBJECT_0) {
		m_waitStuck = 0;
		return TRUE;
	}
	/* 表示直後はフレームが退役せず、ここで描画を止めるとクリックまで空のままになる */
	const DWORD now = GetTickCount();
	if (!m_waitStuck) m_waitStuck = now ? now : 1;
	if (now - m_waitStuck < 120) return FALSE;
	m_presented = FALSE;
	m_waitStuck = 0;
	if (now - m_lastRebind > 400) {
		m_lastRebind = now;
		RefreshSurface();
	}
	return TRUE;
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
		{ "PSBloom", IDR_CSO_TAC_PSBLOOM, &m_psBloom }, { "PSBlur", IDR_CSO_TAC_PSBLUR, &m_psBlur }, { "PSGrade", IDR_CSO_TAC_PSGRADE, &m_psGrade },
		{ "PSUI", IDR_CSO_TAC_PSUI, &m_psUi },
		{ "PSG0", IDR_CSO_TAC_PSG0, &m_psGround[0] }, { "PSG1", IDR_CSO_TAC_PSG1, &m_psGround[1] },
		{ "PSG2", IDR_CSO_TAC_PSG2, &m_psGround[2] }, { "PSG3", IDR_CSO_TAC_PSG3, &m_psGround[3] },
		{ "PSG4", IDR_CSO_TAC_PSG4, &m_psGround[4] }, { "PSG5", IDR_CSO_TAC_PSG5, &m_psGround[5] },
		{ "PSG6", IDR_CSO_TAC_PSG6, &m_psGround[6] }, { "PSG7", IDR_CSO_TAC_PSG7, &m_psGround[7] },
		{ "PSProp", IDR_CSO_TAC_PSPROP, &m_psProp }, { "PSWater", IDR_CSO_TAC_PSWATER, &m_psWater }, { "PSLava", IDR_CSO_TAC_PSLAVA, &m_psLava },
	};
	for (auto& d : ps) {
		ID3DBlob* b = NULL;
		if (FAILED(hr = LoadBlob(d.e, "ps_5_0", d.rid, &b))) { m_lastHr = hr; return FALSE; }
		hr = m_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, d.ps);
		TAC_REL(b);
		if (FAILED(hr)) { m_lastHr = hr; return FALSE; }
	}
	(void)sizeof(VSDef);
	ID3DBlob* bC = NULL;
	if (SUCCEEDED(LoadBlob("CSSkin", "cs_5_0", IDR_CSO_TAC_CSSKIN, &bC)) && bC) {
		if (FAILED(m_dev->CreateComputeShader(bC->GetBufferPointer(), bC->GetBufferSize(), NULL, &m_csSkin))) m_csSkin = NULL;
		TAC_REL(bC);
	}
	if (m_csSkin) {
		D3D11_BUFFER_DESC bd = {};
		bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.ByteWidth = 64 * MAXJ;
		if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbSkinBone))) { TAC_REL(m_csSkin); }
		bd.ByteWidth = 96;
		if (m_csSkin && FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbSkinParm))) { TAC_REL(m_csSkin); TAC_REL(m_cbSkinBone); }
		bd.ByteWidth = (9 + 1 + SKULL_NA * SKULL_NE / 4) * 16;
		if (m_csSkin && FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbHairCloth))) { TAC_REL(m_csSkin); TAC_REL(m_cbSkinBone); TAC_REL(m_cbSkinParm); }
		struct { float d[4], n[4]; } z = {};
		bd.Usage = D3D11_USAGE_IMMUTABLE; bd.BindFlags = D3D11_BIND_SHADER_RESOURCE; bd.CPUAccessFlags = 0;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride = 32; bd.ByteWidth = 32;
		D3D11_SUBRESOURCE_DATA sd = { &z, 0, 0 };
		if (m_csSkin && FAILED(m_dev->CreateBuffer(&bd, &sd, &m_faceDummy))) { TAC_REL(m_csSkin); TAC_REL(m_cbSkinBone); TAC_REL(m_cbSkinParm); TAC_REL(m_cbHairCloth); }
		if (m_faceDummy) {
			D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
			sv.Format = DXGI_FORMAT_UNKNOWN; sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER; sv.Buffer.NumElements = 1;
			if (FAILED(m_dev->CreateShaderResourceView(m_faceDummy, &sv, &m_faceDummySRV))) { TAC_REL(m_csSkin); TAC_REL(m_cbSkinBone); TAC_REL(m_cbSkinParm); TAC_REL(m_cbHairCloth); TAC_REL(m_faceDummy); }
		}
	}
	// 布は無くても描ける。三つ揃わなければ体に沿わせたまま。
	if (m_csSkin) {
		static const struct { const char* e; int rid; } cl[3] = {
			{ "CSClothStep", IDR_CSO_TAC_CSCLTHS }, { "CSClothRelax", IDR_CSO_TAC_CSCLTHR }, { "CSClothWrite", IDR_CSO_TAC_CSCLTHW } };
		BOOL all = TRUE;
		for (int i = 0; i < 3; i++) {
			ID3DBlob* b = NULL;
			if (SUCCEEDED(Soft3DLoadCso(L"tac", cl[i].e, "cs_5_0", cl[i].rid, L"omake_tactics_cloth.hlsl", (void**)&b)) && b) {
				if (FAILED(m_dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_csCloth[i]))) m_csCloth[i] = NULL;
				TAC_REL(b);
			}
			if (!m_csCloth[i]) all = FALSE;
		}
		D3D11_BUFFER_DESC bd = {};
		bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		bd.ByteWidth = 192 + 64 * CLOTH_SEGS + 128 * CLOTH_SEGS + 48;
		if (!all || FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbCloth))) {
			for (int i = 0; i < 3; i++) TAC_REL(m_csCloth[i]);
			m_cbCloth = NULL;
		}
	}
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

// 縦長ストリップ（正方形 × slices）を配列にする。16 スロット表には入れない。
static BOOL LoadStrip(ID3D11Device* dev, int rid, int slices, ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv)
{
	*tex = NULL; *srv = NULL;
	if (slices < 1 || slices > 16) return FALSE;
	HMODULE hm = AfxGetResourceHandle();
	HRSRC rs = FindResource(hm, MAKEINTRESOURCE(rid), RT_RCDATA);
	if (!rs) { hm = GetModuleHandle(NULL); rs = FindResource(hm, MAKEINTRESOURCE(rid), RT_RCDATA); }
	if (!rs) return FALSE;
	HGLOBAL hg = LoadResource(hm, rs);
	DWORD len = SizeofResource(hm, rs);
	const BYTE* data = (const BYTE*)LockResource(hg);
	if (!data || len < 32) return FALSE;
	IWICImagingFactory* f = NULL;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
	if (FAILED(hr) || !f) return FALSE;
	IWICStream* st = NULL; IWICBitmapDecoder* dec = NULL; IWICBitmapFrameDecode* fr = NULL; IWICFormatConverter* cv = NULL;
	BOOL ok = FALSE;
	static Buf<BYTE> px;
	if (SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)data, len))
		&& SUCCEEDED(f->CreateDecoderFromStream(st, NULL, WICDecodeMetadataCacheOnLoad, &dec))
		&& SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&cv))
		&& SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
		UINT w = 0, h = 0;
		cv->GetSize(&w, &h);
		if (w >= 32 && w <= 512 && h == w * (UINT)slices) {
			px.resize((int)(w * h * 4));
			if (px.data() && SUCCEEDED(cv->CopyPixels(NULL, w * 4, w * h * 4, px.data()))) {
				D3D11_TEXTURE2D_DESC td = {};
				td.Width = w; td.Height = w; td.MipLevels = 1; td.ArraySize = (UINT)slices;
				td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
				td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				D3D11_SUBRESOURCE_DATA sd[16];
				for (int i = 0; i < slices; i++) {
					sd[i].pSysMem = px.data() + (size_t)i * w * w * 4;
					sd[i].SysMemPitch = w * 4;
					sd[i].SysMemSlicePitch = 0;
				}
				if (SUCCEEDED(dev->CreateTexture2D(&td, sd, tex)) && *tex) {
					D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
					sv.Format = td.Format;
					sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
					sv.Texture2DArray.MipLevels = 1;
					sv.Texture2DArray.ArraySize = (UINT)slices;
					ok = SUCCEEDED(dev->CreateShaderResourceView(*tex, &sv, srv));
				}
			}
		}
	}
	if (!ok) { TAC_REL(*srv); TAC_REL(*tex); }
	TAC_REL(cv); TAC_REL(fr); TAC_REL(dec); TAC_REL(st); TAC_REL(f);
	return ok;
}
BOOL Renderer::CreateProcTextures()
{
	{
		DWORD w[16];
		for (int i = 0; i < 16; i++) w[i] = 0xFFFFFFFF;
		if (!MakeTex(m_dev, m_ctx, w, 4, 4, FALSE, &m_tex[TEX_WHITE], &m_srv[TEX_WHITE])) return FALSE;
	}
	{
		Buf<DWORD> p(64 * 64);
		for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
			DWORD c = PaletteColor((y / 4) * 16 + (x / 4));
			p[y * 64 + x] = BGRA(c & 255, (c >> 8) & 255, (c >> 16) & 255);
		}
		if (!MakeTex(m_dev, m_ctx, p.data(), 64, 64, FALSE, &m_tex[TEX_PAL], &m_srv[TEX_PAL])) return FALSE;
	}
	{
		const int S = 512, T = 128;
		Buf<DWORD> p(S * S);
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
	if (!LoadStrip(m_dev, IDR_TAC_DETAIL, 16, &m_groundTex, &m_groundSrv)) return FALSE;
	if (!LoadStrip(m_dev, IDR_TAC_MAT, 8, &m_matTex, &m_matSrv)) return FALSE;
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
		if (w > 0 && h > 0 && w <= 8192 && h <= 8192 && (size_t)w * h <= 0x7fffffff) {
			static Buf<DWORD> px;
			px.resize((int)((size_t)w * h));
			if (px.data() && (size_t)px.size() >= (size_t)w * h && SUCCEEDED(cv->CopyPixels(NULL, w * 4, w * h * 4, (BYTE*)px.data())))
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

static CRITICAL_SECTION g_meshCs, g_fillCs;
static volatile LONG g_loadCs = 0;
static void InitLoadCs()
{
	if (g_loadCs == 2) return;
	if (InterlockedCompareExchange(&g_loadCs, 1, 0) == 0) {
		InitializeCriticalSection(&g_meshCs);
		InitializeCriticalSection(&g_fillCs);
		g_loadCs = 2;
	} else while (g_loadCs != 2) Sleep(0);
}

BOOL Renderer::MakeMesh(GpuMesh& out, const void* v, UINT stride, UINT nv, const WORD* idx, UINT ni, BOOL dynamic)
{
	out.Release();
	if (!nv || !ni) return FALSE;
	InitLoadCs();
	EnterCriticalSection(&g_meshCs);
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = stride * nv; bd.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
	D3D11_SUBRESOURCE_DATA sd = { v, 0, 0 };
	if (FAILED(m_dev->CreateBuffer(&bd, &sd, &out.vb))) { LeaveCriticalSection(&g_meshCs); return FALSE; }
	bd.ByteWidth = ((ni * 2) + 3) & ~3u; bd.BindFlags = D3D11_BIND_INDEX_BUFFER; bd.Usage = D3D11_USAGE_IMMUTABLE; bd.CPUAccessFlags = 0;
	static Buf<WORD> tmp;
	tmp.assign(idx, idx + ni);
	tmp.resize(bd.ByteWidth / 2, 0);
	sd.pSysMem = tmp.data();
	if (FAILED(m_dev->CreateBuffer(&bd, &sd, &out.ib))) { out.Release(); LeaveCriticalSection(&g_meshCs); return FALSE; }
	out.nIdx = ni; out.stride = stride; out.i32 = 0;
	LeaveCriticalSection(&g_meshCs);
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
	m.palmOk = 0;
	memset(m.palmR, 0, sizeof(m.palmR));
	memset(m.palmL, 0, sizeof(m.palmL));
	memset(m.axisR, 0, sizeof(m.axisR));
	memset(m.axisL, 0, sizeof(m.axisL));
}

static int g_avaNeedReset = 0;

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
	if (ver != 1 || nTex > 12 || nRig > 8 || nMod > 32 || nAtt > 64 || nSt > 48) return FALSE;
	int pulseN = (int)(nTex + nRig + nMod + nAtt + nSt + 1);
	int pulseI = 0;
	auto pulse = [&]() {
		pulseI++;
		if (pulseN < pulseI) pulseN = pulseI;
		LoadPulse(pulseI, pulseN);
	};
	auto reserve = [&](int units) { if (units > 1) pulseN += units - 1; };
	for (UINT i = 0; i < nTex; i++) {
		UINT len = rd.U32();
		const BYTE* b = rd.Bytes(len);
		rd.Align4();
		if (!b || !LoadPngTex(b, len, (int)i)) return FALSE;
		pulse();
	}
	rigs.resize(nRig);
	for (UINT r = 0; r < nRig; r++) {
		Rig& g = rigs[r];
		g.nJ = (int)rd.U32();
		if (g.nJ <= 0 || g.nJ > MAXJ) return FALSE;
		for (int j = 0; j < g.nJ; j++) { rd.Name(g.names[j], 24); g.parent[j] = rd.I32(); }
		UINT nc = rd.U32();
		g.hips = 0;
		if (nc > 40) return FALSE;
		g.clips.resize(nc);
		reserve(nc > 0 ? (int)nc : 1);
		if (nc == 0) pulse();
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
			pulse();
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
		reserve(np > 0 ? (int)np : 1);
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
			static Buf<VtxK> v;
			static Buf<WORD> partIdx;
			v.resize((int)nv);
			if (!v.data() || (UINT)v.size() < nv) return FALSE;
			memcpy(v.data(), vb, (size_t)nv * 40);
			for (auto& x : v) {
				x.p[2] = -x.p[2]; x.n[2] = -x.n[2];
				if (x.j[0] == m.headJ && x.w[0] > 200) {
					for (int k = 0; k < 3; k++) { m.headMin[k] = min(m.headMin[k], x.p[k]); m.headMax[k] = max(m.headMax[k], x.p[k]); }
				}
			}
			partIdx.assign(ib, ib + ni);
			if ((UINT)partIdx.size() < ni) return FALSE;
			for (UINT k = 0; k + 2 < ni; k += 3) std::swap(partIdx[k + 1], partIdx[k + 2]);
			GpuMesh gm;
			if (!MakeMesh(gm, v.data(), sizeof(VtxK), nv, partIdx.data(), ni)) return FALSE;
			gm.tex = m.tex; gm.glow = (int)glow;
			m.parts.push_back(gm);
			pulse();
		}
		if (np == 0) pulse();
		// 頭部範囲をボーン基準へ
		if (m.headJ >= 0 && m.headMin[0] < 1e8f) {
			M4 bindHead = MInverse(m.invBind[m.headJ]);
			V3 o = v3(bindHead.m[12], bindHead.m[13], bindHead.m[14]);
			for (int k = 0; k < 3; k++) { m.headMin[k] -= (&o.x)[k]; m.headMax[k] -= (&o.x)[k]; }
		} else {
			m.headMin[0] = -.5f; m.headMin[1] = 0; m.headMin[2] = -.5f; m.headMax[0] = .5f; m.headMax[1] = 1.f; m.headMax[2] = .5f;
		}
		m.live = 1;
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
		static Buf<VtxS> v;
		static Buf<WORD> attIdx;
		v.resize((int)nv);
		if (!v.data() || (UINT)v.size() < nv) return FALSE;
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
		attIdx.assign(ib, ib + ni);
		if ((UINT)attIdx.size() < ni) return FALSE;
		for (UINT k = 0; k + 2 < ni; k += 3) std::swap(attIdx[k + 1], attIdx[k + 2]);
		if (!MakeMesh(a.mesh, v.data(), sizeof(VtxS), nv, attIdx.data(), ni)) return FALSE;
		a.mesh.tex = a.tex;
		pulse();
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
		pulse();
	}
	if (rd.bad) return FALSE;
	// 人型はパーティと、作成・戦闘で使う体だけ後から読む。ここではカタログの位置だけ。
	LoadCreaturePack();
	pulse();
	m_assetsOk = TRUE;
	return TRUE;
}

// packed=0 は頂点9 float。packed=1 は位置・法線 int16 と色バイト（TACP / TACQ）。
static BOOL ReadColorMesh(Reader& rd, int packed, Buf<VtxS>& v, Buf<WORD>& idx)
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

static void DropSkinGpu(Renderer::CreatureSkin& src)
{
	TAC_REL(src.bindSRV); TAC_REL(src.bindSB);
	TAC_REL(src.outUAV); TAC_REL(src.outVB);
	TAC_REL(src.faceSRV); TAC_REL(src.faceSB);
	// baldIB はここから外す。EnsureSkinGpu は初回に ReleaseSkinGpu を呼ぶ。
	// 一緒に放すと、髪を消した索引が初回の影描画で消えて、次からポニーが戻る。
	src.faceGot = -1;
	src.gpuNv = 0;
}

static BOOL IsBodyName(const char* n)
{
	if (!n || strncmp(n, "Hum", 3) != 0) return FALSE;
	char c = n[3];
	if (c == 0) return TRUE;
	if (c >= 'A' && c <= 'Z') return TRUE;
	if (c >= '0' && c <= '9') return TRUE;
	return FALSE;
}

static BOOL SkipTackRec(Reader& rd, char* name, float* foot);

static float g_tacFoot[1024];
static unsigned char g_footGot[1024];
static const BYTE* g_packBytes = NULL;
static DWORD g_packBytesN = 0;
static int g_packOff[1024];
static int g_packN = 0;
static char g_faceNm[64][32];
static int g_faceSetI[64], g_face0[64], g_faceNvI[64];
static float g_faceLinI[64][9];
static int g_faceLinkN = 0;
static int g_faceReady = 0;

static void ClearMonPack()
{
	g_packBytes = NULL;
	g_packBytesN = 0;
	g_packN = 0;
	g_faceLinkN = 0;
	g_faceReady = 0;
	memset(g_footGot, 0, sizeof(g_footGot));
	memset(g_tacFoot, 0, sizeof(g_tacFoot));
}

float TacMonsterFoot(int id)
{
	if (id < 0 || id >= 1024) return 0.f;
	if (!g_footGot[id] && g_packBytes && id < g_packN && g_packOff[id] > 0 && g_packOff[id] < (int)g_packBytesN) {
		Reader one(g_packBytes + g_packOff[id], g_packBytesN - (DWORD)g_packOff[id]);
		char name[32];
		float foot = 0.f;
		if (SkipTackRec(one, name, &foot)) g_tacFoot[id] = foot;
		g_footGot[id] = 1;
	}
	return g_tacFoot[id] > 0.f ? g_tacFoot[id] : 0.f;
}

// バインド座標は数センチ。描画は休息姿勢の骨スケールで身長約 1.7 まで伸びる。
// その姿勢で、高さの下 45%（腹や足。翼や腕は入らない）の半幅を返す。
static float PosedFootHalf(UINT nJ, const int* parent, const V3* restT, const Q4* restR, const V3* restS,
	const M4& top, const M4* inv, UINT nv, const short* ps, const BYTE* jj, const BYTE* ww,
	float mid0, float mid1, float mid2, float hx, float hy, float hz)
{
	static M4 glob[MAXJ], pal[MAXJ];
	for (UINT j = 0; j < nJ; j++) {
		M4 loc = MTRS(restT[j], restR[j], restS[j]);
		int pa = parent[j];
		const M4& up = (pa >= 0 && pa < (int)j) ? glob[pa] : top;
		glob[j] = MMul(loc, up);
		pal[j] = MMul(inv[j], glob[j]);
	}
	auto Skin = [&](UINT k, float& ox, float& oy, float& oz) {
		float x = mid0 + (ps[k * 3] / 32767.f) * hx;
		float y = mid1 + (ps[k * 3 + 1] / 32767.f) * hy;
		float z = -(mid2 + (ps[k * 3 + 2] / 32767.f) * hz);
		ox = oy = oz = 0.f;
		float tw = 0.f;
		for (int c = 0; c < 4; c++) {
			float w = ww[k * 4 + c] / 255.f;
			unsigned j = jj[k * 4 + c];
			if (w <= 0.f || j >= nJ) continue;
			V3 p = MXformPt(pal[j], v3(x, y, z));
			ox += p.x * w; oy += p.y * w; oz += p.z * w;
			tw += w;
		}
		if (tw <= 0.f) { ox = x; oy = y; oz = z; }
	};
	float y0 = 1e9f, y1 = -1e9f;
	for (UINT k = 0; k < nv; k++) {
		float x, y, z;
		Skin(k, x, y, z);
		if (y < y0) y0 = y;
		if (y > y1) y1 = y;
	}
	float cut = y0 + (y1 - y0) * 0.45f;
	float mx = 0.f;
	for (UINT k = 0; k < nv; k++) {
		float x, y, z;
		Skin(k, x, y, z);
		if (y > cut) continue;
		float h = fabsf(x) > fabsf(z) ? fabsf(x) : fabsf(z);
		if (h > mx) mx = h;
	}
	return mx;
}

// 目録用。描画と同じ休息姿勢で、地面側の胴体がタイルからどれだけ出るかを測る。
static BOOL SkipTackRec(Reader& rd, char* name, float* foot)
{
	if (foot) *foot = 0.f;
	rd.Name(name, 32);
	if (rd.bad) return FALSE;
	UINT nJ = rd.U32();
	if (nJ < 1 || nJ > MAXJ) return FALSE;
	static int parent[MAXJ];
	static V3 restT[MAXJ];
	static Q4 restR[MAXJ];
	static V3 restS[MAXJ];
	static M4 inv[MAXJ];
	for (UINT j = 0; j < nJ; j++) { char tmp[24]; rd.Name(tmp, 24); parent[j] = rd.I32(); }
	UINT nc = rd.U32();
	// 人体はモーションキャプチャのスキル別クリップまで持つ。上限は Rig::clips と同じ。
	if (nc > 40) return FALSE;
	for (UINT c = 0; c < nc; c++) {
		char cn[32];
		rd.Name(cn, 32);
		UINT nf = rd.U32(); rd.F32(); rd.U32();
		if (nf == 0 || nf > 64) return FALSE;
		if (!rd.Bytes((size_t)nf * nJ * 8)) return FALSE;
		if (!rd.Bytes((size_t)nf * 12)) return FALSE;
	}
	for (UINT j = 0; j < nJ; j++) {
		float t[10];
		for (int k = 0; k < 10; k++) t[k] = rd.F32();
		restT[j] = v3(t[0], t[1], -t[2]);
		Q4 qq = { -t[3], -t[4], t[5], t[6] };
		float l = sqrtf(qq.x * qq.x + qq.y * qq.y + qq.z * qq.z + qq.w * qq.w);
		if (l > 1e-6f) { qq.x /= l; qq.y /= l; qq.z /= l; qq.w /= l; }
		restR[j] = qq;
		restS[j] = v3(t[7], t[8], t[9]);
	}
	M4 top;
	for (int k = 0; k < 16; k++) top.m[k] = rd.F32();
	FlipM(top);
	for (UINT j = 0; j < nJ; j++) { for (int k = 0; k < 16; k++) inv[j].m[k] = rd.F32(); FlipM(inv[j]); }
	UINT nv = rd.U32(), ni = rd.U32();
	if (!nv || !ni || nv > 70000 || ni > 200000) return FALSE;
	float emin[3], emax[3], mid[3], halfv[3];
	for (int a = 0; a < 3; a++) emin[a] = rd.F32();
	for (int a = 0; a < 3; a++) emax[a] = rd.F32();
	for (int a = 0; a < 3; a++) { mid[a] = (emin[a] + emax[a]) * 0.5f; halfv[a] = (emax[a] - emin[a]) * 0.5f; }
	const short* ps = (const short*)rd.Bytes((size_t)nv * 6);
	if (!rd.Bytes((size_t)nv * 6)) return FALSE;
	if (!rd.Bytes((size_t)nv * 3)) return FALSE;
	const BYTE* jj = rd.Bytes((size_t)nv * 4);
	const BYTE* ww = rd.Bytes((size_t)nv * 4);
	if (ps && jj && ww && foot)
		*foot = PosedFootHalf(nJ, parent, restT, restR, restS, top, inv, nv, ps, jj, ww, mid[0], mid[1], mid[2], halfv[0], halfv[1], halfv[2]);
	rd.Align4();
	if (!rd.Bytes((size_t)ni * 2)) return FALSE;
	rd.Align4();
	return !rd.bad;
}

static int CmpFloat(const void* a, const void* b)
{
	float d = *(const float*)a - *(const float*)b;
	return (d > 0.f) - (d < 0.f);
}
static float Quantile(Buf<float>& a, float q)
{
	if (a.empty()) return 0.f;
	qsort(a.data(), a.size(), sizeof(float), CmpFloat);
	float f = q * (float)(a.size() - 1);
	int i = (int)f;
	if (i < 0) i = 0;
	if (i >= (int)a.size() - 1) return a[a.size() - 1];
	float t = f - (float)i;
	return a[i] * (1.f - t) + a[i + 1] * t;
}

// 顔は -Z。焼き込みポニーは頭蓋と同じ色なので、耳より後ろで下へ垂れた塊として外す。
static void MarkMeshHair(Model& m, Renderer::CreatureSkin& skin, int headJ, float ox, float oy, float oz)
{
	const int nv = (int)skin.bind.size();
	skin.meshHair.fill(nv, 0);
	if ((int)skin.meshHair.size() != nv || headJ < 0) return;
	static Buf<int> hv, band, front;
	hv.clear();
	float yTop = -1e9f;
	for (int k = 0; k < nv; k++) {
		const Renderer::CSkinV& b = skin.bind[k];
		int best = 0, bw = -1;
		for (int c = 0; c < 4; c++) if ((int)b.w[c] > bw) { bw = (int)b.w[c]; best = b.j[c]; }
		if (best != headJ || bw < 200) continue;
		hv.push_back(k);
		if (b.p[1] > yTop) yTop = b.p[1];
	}
	if (hv.size() < 80) return;
	band.clear();
	for (int k : hv) if (skin.bind[k].p[1] > yTop - 0.28f) band.push_back(k);
	if (band.size() < 40) return;
	static Buf<float> tmp;
	tmp.clear();
	for (int k : band) tmp.push_back(skin.bind[k].p[0]);
	float cx = Quantile(tmp, 0.5f);
	tmp.clear();
	for (int k : band) tmp.push_back(fabsf(skin.bind[k].p[0] - cx));
	float rx = Quantile(tmp, 0.92f);
	if (rx < 0.045f) rx = 0.045f;
	if (rx > 0.16f) return;
	front.clear();
	for (int k : band) if (fabsf(skin.bind[k].p[0] - cx) < rx * 0.85f) front.push_back(k);
	if (front.size() < 20) return;
	tmp.clear();
	for (int k : front) tmp.push_back(skin.bind[k].p[2]);
	float faceZ = Quantile(tmp, 0.12f);
	tmp.clear();
	for (int k : front) tmp.push_back(skin.bind[k].p[1]);
	float cy = Quantile(tmp, 0.5f);
	tmp.clear();
	for (int k : front) tmp.push_back(fabsf(skin.bind[k].p[1] - cy));
	float ry = Quantile(tmp, 0.88f);
	if (ry < rx * 1.05f) ry = rx * 1.05f;
	if (ry > 0.22f) return;
	float cz = faceZ + rx * 0.85f;
	float rz = rx * 0.72f;
	if (rz < 0.05f) rz = 0.05f;
	m.headMin[0] = cx - rx - ox; m.headMin[1] = cy - ry - oy; m.headMin[2] = cz - rz - oz;
	m.headMax[0] = cx + rx - ox; m.headMax[1] = cy + ry - oy; m.headMax[2] = cz + rz - oz;
	// HumF0 は耳の幅が最大になる Z=0 までが頭蓋で、下端は 1.50 付近。
	// Z=0.08 から下端が 1.41 へ落ち、頂点 603 個が後ろへ 1.34 まで垂れる。これがポニー本体。
	// 顔の三角形は 0 個、後頭の三角形は 3 頂点ともこちらのときだけ消える。
	const float bin = 0.02f;
	float earZ = 0.f, earSpan = -1.f;
	for (float z0 = -0.16f; z0 < 0.20f; z0 += bin) {
		float x0 = 1e9f, x1 = -1e9f;
		int n = 0;
		for (int k : hv) {
			float z = skin.bind[k].p[2];
			if (z < z0 || z >= z0 + bin) continue;
			float x = skin.bind[k].p[0];
			if (x < x0) x0 = x;
			if (x > x1) x1 = x;
			n++;
		}
		if (n >= 12 && x1 - x0 > earSpan) { earSpan = x1 - x0; earZ = z0; }
	}
	float zCut = 1e9f, skullY = 0.f;
	bool haveY = false;
	for (float z0 = earZ; z0 < 0.22f; z0 += bin) {
		float y0 = 1e9f;
		int n = 0;
		for (int k : hv) {
			float z = skin.bind[k].p[2];
			if (z < z0 || z >= z0 + bin) continue;
			if (skin.bind[k].p[1] < y0) y0 = skin.bind[k].p[1];
			n++;
		}
		if (n < 12) continue;
		if (!haveY) { skullY = y0; haveY = true; continue; }
		if (y0 < skullY - 0.04f) { zCut = z0; break; }
		if (y0 < skullY) skullY = y0;
	}
	if (zCut < 10.f) {
		int nMark = 0, nCrown = 0;
		for (int k : hv) if (skin.bind[k].p[2] > zCut) {
			nMark++;
			if (skin.bind[k].p[1] > yTop - 0.06f) nCrown++;
		}
		// 帽子や広い頭は耳の幅の切れ目が頭蓋の途中に落ち、頭のほぼ全部がポニーになる。
		// 消したあとに被せる髪は笠なので、顔と頭頂が抜けると仰け反りで禿げて見える。
		// 垂れた房だけ外す。頭頂が混ざる、または頭の四分の一を超えるときは何も消さない。
		if (nCrown > 0 || nMark * 4 > (int)hv.size()) return;
		for (int k : hv) if (skin.bind[k].p[2] > zCut) skin.meshHair[k] = 1;
	}
}

static void BuildBaldIB(ID3D11Device* dev, Renderer::CreatureSkin& skin, const WORD* idx, UINT ni)
{
	TAC_REL(skin.baldIB);
	skin.baldN = 0;
	if (!dev || !idx || ni < 3 || skin.meshHair.empty()) return;
	const int nh = (int)skin.meshHair.size();
	int nHair = 0;
	for (int i = 0; i < nh; i++) if (skin.meshHair[i]) nHair++;
	if (nHair < 30) return;
	static Buf<WORD> bald;
	bald.clear();
	const int nv = (int)skin.bind.size();
	for (UINT t = 0; t + 2 < ni; t += 3) {
		int a = idx[t], b = idx[t + 1], c = idx[t + 2];
		if ((unsigned)a >= (unsigned)nv || (unsigned)b >= (unsigned)nv || (unsigned)c >= (unsigned)nv) continue;
		// 1 頂点だけの共有では後頭を巻き込まない。ポニー側が 2 頂点以上の面だけ落とす。
		int nhair = (a < nh && skin.meshHair[a]) + (b < nh && skin.meshHair[b]) + (c < nh && skin.meshHair[c]);
		if (nhair >= 2) continue;
		bald.push_back((WORD)a); bald.push_back((WORD)b); bald.push_back((WORD)c);
	}
	if (bald.size() < 3) return;
	UINT n = (UINT)bald.size();
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = ((n * 2u) + 3u) & ~3u;
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	static Buf<WORD> pad;
	pad.assign(bald.data(), bald.data() + n);
	pad.resize((int)(bd.ByteWidth / 2), 0);
	D3D11_SUBRESOURCE_DATA sd = { pad.data(), 0, 0 };
	if (FAILED(dev->CreateBuffer(&bd, &sd, &skin.baldIB))) { skin.baldIB = NULL; return; }
	skin.baldN = n;
}

// slot < -1 は予約済みの人型枠（-2 が 0 番）。-1 は末尾へ足す。0 以上は戦闘枠。
static BOOL FillCreature(Renderer& R, Reader& rd, int packIndex, int slot)
{
	const int reserved = slot <= -2 ? (-slot - 2) : -1;
	const int appending = slot < 0 && reserved < 0;
	int cSlot, mSlot, rSlot;
	if (reserved >= 0) {
		if (reserved >= R.m_bodyN) return FALSE;
		cSlot = R.m_body0C + reserved;
		mSlot = R.m_body0M + reserved;
		rSlot = R.m_body0R + reserved;
		if (cSlot < 0 || mSlot < 0 || rSlot < 0) return FALSE;
		if (cSlot >= (int)R.m_creatures.size() || mSlot >= (int)R.models.size() || rSlot >= (int)R.rigs.size()) return FALSE;
		if (cSlot >= (int)R.m_skins.size()) return FALSE;
	} else if (appending) {
		cSlot = (int)R.m_creatures.size();
		mSlot = (int)R.models.size();
		rSlot = (int)R.rigs.size();
		if (cSlot >= 160 || mSlot >= 192 || rSlot >= 192) return FALSE;
		R.models.resize(mSlot + 1);
		R.rigs.resize(rSlot + 1);
		R.m_creatures.resize(cSlot + 1);
		R.m_skins.resize(cSlot + 1);
	} else {
		cSlot = R.m_keepCreatures + slot;
		mSlot = R.m_keepModels + slot;
		rSlot = R.m_keepRigs + slot;
		if (cSlot < 0 || cSlot >= 160 || mSlot < 0 || mSlot >= 192 || rSlot < 0 || rSlot >= 192) return FALSE;
		if ((int)R.models.size() < mSlot + 1) R.models.resize(mSlot + 1);
		if ((int)R.rigs.size() < rSlot + 1) R.rigs.resize(rSlot + 1);
		if ((int)R.m_creatures.size() < cSlot + 1) R.m_creatures.resize(cSlot + 1);
		if ((int)R.m_skins.size() < cSlot + 1) R.m_skins.resize(cSlot + 1);
	}
	auto Bail = [&]() -> BOOL {
		R.m_creatures[cSlot].Release();
		DropSkinGpu(R.m_skins[cSlot]);
		if (appending) {
			R.models.resize(mSlot);
			R.rigs.resize(rSlot);
			R.m_creatures.resize(cSlot);
			R.m_skins.resize(cSlot);
		}
		return FALSE;
	};
	Rig& g = R.rigs[rSlot];
	Model& m = R.models[mSlot];
	m.live = 0;
	Renderer::CreatureSkin& skin = R.m_skins[cSlot];
	GpuMesh& gm = R.m_creatures[cSlot];
	gm.Release();
	DropSkinGpu(skin);
	if (slot >= 0) {
		skin.bind.arena = 2; skin.cloth.arena = 2; skin.headTri.arena = 2;
		skin.meshHair.arena = 2;
		for (int ci = 0; ci < g.clips.size(); ci++) {
			g.clips[ci].q.arena = 2;
			g.clips[ci].hips.arena = 2;
		}
	}
	skin.cloth.clear();
	skin.faceSet = -1;
	skin.faceN = 0;
	skin.faceStart = 0;
	skin.headTri.clear();
	skin.meshHair.clear();
	skin.extR = 0;
	skin.crownOk = 0;
	memset(skin.skullR, 0, sizeof(skin.skullR));
			char name[32];
			rd.Name(name, 32);
			UINT nJ = rd.U32();
			if (nJ < 1 || nJ > MAXJ) return Bail();
			g.nJ = (int)nJ;
			g.hips = 0;
			for (UINT j = 0; j < nJ; j++) { rd.Name(g.names[j], 24); g.parent[j] = rd.I32(); }
			UINT nc = rd.U32();
			if (nc > 40) return Bail();
			g.clips.resize(nc);
			for (UINT c = 0; c < nc; c++) {
				Clip& cl = g.clips[c];
				if (slot >= 0) { cl.q.arena = 2; cl.hips.arena = 2; }
				rd.Name(cl.name, 32);
				cl.nFrames = (int)rd.U32(); cl.fps = rd.F32(); g.hips = (int)rd.U32();
				if (cl.nFrames <= 0 || cl.nFrames > 64) return Bail();
				const short* q = (const short*)rd.Bytes((size_t)cl.nFrames * nJ * 8);
				const float* h = (const float*)rd.Bytes((size_t)cl.nFrames * 12);
				if (!q || !h) return Bail();
				int qn = cl.nFrames * (int)nJ;
				cl.q.resize(qn);
				if (cl.q.size() != qn || !cl.q.data()) return Bail();
				for (size_t k = 0; k < cl.q.size(); k++) {
					Q4 v = { -q[k * 4] / 32767.f, -q[k * 4 + 1] / 32767.f, q[k * 4 + 2] / 32767.f, q[k * 4 + 3] / 32767.f };
					float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z + v.w * v.w);
					if (l > 1e-6f) { v.x /= l; v.y /= l; v.z /= l; v.w /= l; }
					cl.q[k] = v;
				}
				cl.hips.resize(cl.nFrames);
				if (cl.hips.size() != cl.nFrames || !cl.hips.data()) return Bail();
				for (int f = 0; f < cl.nFrames; f++) cl.hips[f] = v3(h[f * 3], h[f * 3 + 1], -h[f * 3 + 2]);
			}
			strncpy_s(m.name, name, 31);
			m.rig = rSlot;
			m.tex = R.TexWhite();
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
			if (!nv || !ni || nv > 70000 || ni > 200000) return Bail();
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
			if (!ps || !ns || !rgb || !jj || !ww || !ib) return Bail();
			skin.model = mSlot;
			if (slot >= 0) skin.bind.arena = 2;
			skin.bind.resize(nv);
			if ((int)skin.bind.size() != (int)nv || !skin.bind.data()) return Bail();
			static Buf<VtxS> poseV;
			static Buf<WORD> idx;
			poseV.resize((int)nv);
			if ((int)poseV.size() != (int)nv || !poseV.data()) return Bail();
			idx.assign(ib, ib + ni);
			if ((int)idx.size() != (int)ni) return Bail();
			for (UINT k = 0; k + 2 < ni; k += 3) std::swap(idx[k + 1], idx[k + 2]);
			for (UINT k = 0; k < nv; k++) {
				Renderer::CSkinV& d = skin.bind[k];
				d.p[0] = mid[0] + (ps[k * 3] / 32767.f) * half[0];
				d.p[1] = mid[1] + (ps[k * 3 + 1] / 32767.f) * half[1];
				d.p[2] = -(mid[2] + (ps[k * 3 + 2] / 32767.f) * half[2]);
				d.n[0] = ns[k * 3] / 32767.f;
				d.n[1] = ns[k * 3 + 1] / 32767.f;
				d.n[2] = -(ns[k * 3 + 2] / 32767.f);
				d.c = RGBA8(rgb[k * 3], rgb[k * 3 + 1], rgb[k * 3 + 2]);
				for (int c = 0; c < 4; c++) { d.j[c] = jj[k * 4 + c]; d.w[c] = ww[k * 4 + c]; }
				if ((k & 8191u) == 0) R.LoadPulse((int)k, (int)nv > 0 ? (int)nv * 2 : 1);
				VtxS& s = poseV[k];
				s.p[0] = d.p[0]; s.p[1] = d.p[1]; s.p[2] = d.p[2];
				s.n[0] = d.n[0]; s.n[1] = d.n[1]; s.n[2] = d.n[2];
				s.uv[0] = s.uv[1] = 0;
				s.c = d.c;
			}
			if (nv > 0) {
				double sum = 0;
				for (UINT k = 0; k < nv; k++) {
					DWORD c = skin.bind[k].c;
					sum += (c & 255) * 0.30 + ((c >> 8) & 255) * 0.59 + ((c >> 16) & 255) * 0.11;
				}
				if (sum / (double)nv < 18.0) {
					unsigned hsh = 2166136261u;
					for (const char* s = name; s && *s; ++s) hsh = hsh * 16777619u ^ (unsigned char)*s;
					float hue = (float)(hsh % 360) * 0.0174533f;
					float br = 0.55f + 0.25f * sinf(hue);
					float bg = 0.42f + 0.22f * sinf(hue + 2.1f);
					float bb = 0.32f + 0.18f * sinf(hue + 4.2f);
					float y0 = 1e9f, y1 = -1e9f;
					for (UINT k = 0; k < nv; k++) {
						float y = skin.bind[k].p[1];
						if (y < y0) y0 = y;
						if (y > y1) y1 = y;
					}
					float span = y1 - y0;
					if (span < 0.02f) span = 0.02f;
					for (UINT k = 0; k < nv; k++) {
						float t = (skin.bind[k].p[1] - y0) / span;
						if (t < 0.f) t = 0.f;
						if (t > 1.f) t = 1.f;
						float shade = 0.45f + 0.7f * t;
						auto q = [](float x) { int v = (int)(x * 255.f + .5f); return v < 0 ? 0 : (v > 255 ? 255 : v); };
						DWORD c = (DWORD)q(br * shade) | ((DWORD)q(bg * shade) << 8) | ((DWORD)q(bb * shade) << 16) | 0xFF000000u;
						skin.bind[k].c = c;
						poseV[k].c = c;
					}
				}
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
				static Buf<UINT> hv;
				hv.clear();
				struct ColC { DWORD c; int n; } colN[64];
				int nCol = 0;
				for (UINT k = 0; k < nv; k++) {
					int best = 0, bw = -1;
					for (int c = 0; c < 4; c++) if ((int)skin.bind[k].w[c] > bw) { bw = skin.bind[k].w[c]; best = skin.bind[k].j[c]; }
					if (best != headJ || bw < 255) continue;
					hv.push_back(k);
					DWORD cc = skin.bind[k].c & 0x00FFFFFFu;
					int found = -1;
					for (int ci = 0; ci < nCol; ci++) if (colN[ci].c == cc) { found = ci; break; }
					if (found >= 0) colN[found].n++;
					else if (nCol < 64) { colN[nCol].c = cc; colN[nCol].n = 1; nCol++; }
				}
				DWORD skinC = 0; int skinN = 0;
				for (int ci = 0; ci < nCol; ci++) if (colN[ci].n > skinN) { skinN = colN[ci].n; skinC = colN[ci].c; }
				static Buf<V3> sk;
				sk.clear();
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
				int neckJ = -1;
				for (UINT j = 0; j < nJ; j++) if (strcmp(g.names[j], "Neck") == 0) { neckJ = (int)j; break; }
				float neckY = 0.f;
				if (neckJ >= 0) {
					M4 bn = MInverse(m.invBind[neckJ]);
					neckY = bn.m[13];
				} else neckY = oy - 0.08f;
				auto headVert = [&](int v) -> bool {
					if (v < 0 || v >= (int)nv) return false;
					int best = 0, bw = -1;
					for (int c = 0; c < 4; c++) if ((int)skin.bind[v].w[c] > bw) { bw = skin.bind[v].w[c]; best = skin.bind[v].j[c]; }
					return best == headJ && skin.bind[v].p[1] > neckY + 0.012f;
				};
				for (UINT t = 0; t + 2 < (UINT)idx.size(); t += 3) {
					int a = idx[t], b = idx[t + 1], c = idx[t + 2];
					if (headVert(a) && headVert(b) && headVert(c)) {
						skin.headTri.push_back((WORD)a);
						skin.headTri.push_back((WORD)b);
						skin.headTri.push_back((WORD)c);
					}
				}
				MarkMeshHair(m, skin, headJ, ox, oy, oz);
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
				static Buf<BYTE> dyeable;
				dyeable.fill((int)nv, 0);
				float hist[12] = {};
				for (UINT k = 0; k < nv; k++) {
					const Renderer::CSkinV& b = skin.bind[k];
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
				skin.cloth.fill((int)nv, 0);
				for (UINT k = 0; k < nv; k++) if (dyeable[k]) {
					float h, s, v;
					hsv(skin.bind[k].c, h, s, v);
					float dh = fabsf(h - domH);
					if (dh > 180.f) dh = 360.f - dh;
					if (dh < 40.f) skin.cloth[k] = 1;
				}
			}
			if (!skin.bind.empty()) {
				float mn[3] = { skin.bind[0].p[0], skin.bind[0].p[1], skin.bind[0].p[2] };
				float mx[3] = { mn[0], mn[1], mn[2] };
				for (const Renderer::CSkinV& b : skin.bind) {
					for (int k = 0; k < 3; k++) {
						if (b.p[k] < mn[k]) mn[k] = b.p[k];
						if (b.p[k] > mx[k]) mx[k] = b.p[k];
					}
				}
				float hx = (mx[0] - mn[0]) * .5f, hy = (mx[1] - mn[1]) * .5f, hz = (mx[2] - mn[2]) * .5f;
				skin.extX = (mn[0] + mx[0]) * .5f;
				skin.extY = (mn[1] + mx[1]) * .5f;
				skin.extZ = (mn[2] + mx[2]) * .5f;
				skin.extR = max(hx, max(hy, hz));
			}
			BuildBaldIB(R.Dev(), skin, idx.data(), ni);
			R.LoadPulse(1, 2);
			if (!R.MakeMesh(gm, poseV.data(), sizeof(VtxS), nv, idx.data(), ni)) return Bail();
			R.LoadPulse(2, 2);
			gm.tex = R.TexWhite();
			ModelSetupJoints(m, &g);
			if (strncmp(name, "Hum", 3) == 0 && m.nJ > 1 && nv > 80) {
				// 関節位置と invBind はパックのまま。ここを書き換えると、その骨に乗った頂点だけスキンが外れて体が消える。
				M4 glob[MAXJ];
				for (int j = 0; j < m.nJ; j++) glob[j] = MInverse(m.invBind[j]);
				for (int side = 0; side < 2; side++) {
					const char* hn = side == 0 ? "Hand.R" : "Hand.L";
					int hand = -1;
					for (int j = 0; j < g.nJ; j++) if (!strcmp(g.names[j], hn)) { hand = j; break; }
					if (hand < 0 || hand >= m.nJ) continue;
					int elbow = m.parent[hand];
					int finger[24];
					int nf = 0;
					for (int j = 0; j < g.nJ && nf < 24; j++) {
						const char* nm = g.names[j];
						int on = 0;
						if (j == hand) on = 1;
						else if (strstr(nm, side == 0 ? ".R" : ".L") && (strstr(nm, "Index") || strstr(nm, "Middle") || strstr(nm, "Ring") || strstr(nm, "Pinky") || strstr(nm, "Thumb"))) on = 1;
						if (on) finger[nf++] = j;
					}
					V3 hpos = v3(glob[hand].m[12], glob[hand].m[13], glob[hand].m[14]);
					double sx = 0, sy = 0, sz = 0;
					int n = 0;
					for (UINT k = 0; k < nv; k++) {
						int best = 0, bw = -1;
						for (int c = 0; c < 4; c++) if ((int)skin.bind[k].w[c] > bw) { bw = skin.bind[k].w[c]; best = skin.bind[k].j[c]; }
						int hit = 0;
						for (int f = 0; f < nf; f++) if (finger[f] == best) { hit = 1; break; }
						if (!hit) continue;
						sx += skin.bind[k].p[0]; sy += skin.bind[k].p[1]; sz += skin.bind[k].p[2]; n++;
					}
					if (n < 40) {
						sx = sy = sz = 0; n = 0;
						for (UINT k = 0; k < nv; k++) {
							float dx = skin.bind[k].p[0] - hpos.x, dy = skin.bind[k].p[1] - hpos.y, dz = skin.bind[k].p[2] - hpos.z;
							if (dx * dx + dy * dy + dz * dz > 0.12f * 0.12f) continue;
							sx += skin.bind[k].p[0]; sy += skin.bind[k].p[1]; sz += skin.bind[k].p[2]; n++;
						}
					}
					if (n < 40) {
						// 関節が胴の中にあり、近くに拳が無い。腕の高さでいちばん外側の塊を拳にする。
						float bestX = side == 0 ? -1e9f : 1e9f;
						for (UINT k = 0; k < nv; k++) {
							float y = skin.bind[k].p[1];
							if (y < 0.55f || y > 1.25f) continue;
							float x = skin.bind[k].p[0];
							if (side == 0 ? x > bestX : x < bestX) bestX = x;
						}
						sx = sy = sz = 0; n = 0;
						for (UINT k = 0; k < nv; k++) {
							float y = skin.bind[k].p[1], x = skin.bind[k].p[0];
							if (y < 0.55f || y > 1.25f) continue;
							if (fabsf(x - bestX) > 0.08f) continue;
							sx += skin.bind[k].p[0]; sy += skin.bind[k].p[1]; sz += skin.bind[k].p[2]; n++;
						}
					}
					if (n < 20) continue;
					V3 fist = v3((float)(sx / n), (float)(sy / n), (float)(sz / n));
					V3 local = MXformPt(m.invBind[hand], fist);
					V3 axis = v3(0, 1, 0);
					if (elbow >= 0 && elbow < m.nJ) {
						V3 ep = v3(glob[elbow].m[12], glob[elbow].m[13], glob[elbow].m[14]);
						V3 dir = fist - ep;
						if (Len(dir) > 1e-4f) axis = MXformDir(m.invBind[hand], dir);
					}
					if (Len(axis) < 1e-4f) axis = v3(0, 1, 0);
					axis = Norm(axis);
					float* pp = side == 0 ? m.palmR : m.palmL;
					float* aa = side == 0 ? m.axisR : m.axisL;
					pp[0] = local.x; pp[1] = local.y; pp[2] = local.z;
					aa[0] = axis.x; aa[1] = axis.y; aa[2] = axis.z;
					m.palmOk |= side == 0 ? 1 : 2;
				}
			}
			if (headJ >= 0 && !skin.bind.empty()) {
				float yTop = -1e9f;
				const int nvb = (int)skin.bind.size();
				for (int i = 0; i < nvb; i++) {
					const Renderer::CSkinV& v = skin.bind[i];
					int best = 0, bw = -1;
					for (int c = 0; c < 4; c++) if ((int)v.w[c] > bw) { bw = (int)v.w[c]; best = (int)v.j[c]; }
					if (best == headJ && bw >= 200 && v.p[1] > yTop) yTop = v.p[1];
				}
				if (yTop > 0.4f) {
					double sx = 0, sz = 0;
					int n = 0;
					for (int i = 0; i < nvb; i++) {
						const Renderer::CSkinV& v = skin.bind[i];
						int best = 0, bw = -1;
						for (int c = 0; c < 4; c++) if ((int)v.w[c] > bw) { bw = (int)v.w[c]; best = (int)v.j[c]; }
						if (best != headJ || bw < 200 || v.p[1] < yTop - 0.035f) continue;
						sx += v.p[0]; sz += v.p[2]; n++;
					}
					if (n > 0) {
						skin.crown[0] = (float)(sx / n);
						skin.crown[1] = yTop;
						skin.crown[2] = (float)(sz / n);
						skin.crownOk = 1;
					}
					// 髪の型は頭の形と合わず、頭頂や分け目で頭蓋がカードの間から出る。
					// 向きごとの頭蓋の半径を測っておき、その内側に入った髪だけ外へ出す。
					// 首と焼き込みの房は数えない。顔の下半分はシェーダ側で外す。
					float neckY = yTop - 0.30f;
					for (UINT j = 0; j < nJ; j++) if (strcmp(g.names[j], "Neck") == 0) { neckY = MInverse(m.invBind[j]).m[13]; break; }
					const int nmh = (int)skin.meshHair.size();
					float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
					for (int i = 0; i < nvb; i++) {
						const Renderer::CSkinV& v = skin.bind[i];
						if (v.p[1] < yTop - 0.10f || (i < nmh && skin.meshHair[i])) continue;
						int best = 0, bw = -1;
						for (int c = 0; c < 4; c++) if ((int)v.w[c] > bw) { bw = (int)v.w[c]; best = (int)v.j[c]; }
						if (best != headJ || bw < 200) continue;
						if (v.p[0] < x0) x0 = v.p[0];
						if (v.p[0] > x1) x1 = v.p[0];
						if (v.p[2] < z0) z0 = v.p[2];
						if (v.p[2] > z1) z1 = v.p[2];
					}
					if (x0 < x1 && z0 < z1) {
						skin.skullC[0] = (x0 + x1) * 0.5f;
						skin.skullC[1] = yTop - 0.095f;
						skin.skullC[2] = (z0 + z1) * 0.5f;
						for (int i = 0; i < nvb; i++) {
							const Renderer::CSkinV& v = skin.bind[i];
							if (v.p[1] < neckY + 0.012f || (i < nmh && skin.meshHair[i])) continue;
							int best = 0, bw = -1;
							for (int c = 0; c < 4; c++) if ((int)v.w[c] > bw) { bw = (int)v.w[c]; best = (int)v.j[c]; }
							if (best != headJ || bw < 200) continue;
							float dx = v.p[0] - skin.skullC[0], dy = v.p[1] - skin.skullC[1], dz = v.p[2] - skin.skullC[2];
							float r = sqrtf(dx * dx + dy * dy + dz * dz);
							if (r < 1e-5f) continue;
							float s = dy / r;
							float el = asinf(s < -1.f ? -1.f : (s > 1.f ? 1.f : s));
							int ia = (int)floorf((atan2f(dx, dz) + 3.14159265f) * (SKULL_NA / 6.2831853f));
							int ie = (int)floorf((el + 0.6f) * (SKULL_NE / 2.1707963f));
							ia = ((ia % SKULL_NA) + SKULL_NA) % SKULL_NA;
							if (ie < 0 || ie >= SKULL_NE) continue;
							float& R = skin.skullR[ie * SKULL_NA + ia];
							if (r > R) R = r;
						}
					}
				}
			}
			if (packIndex >= 0 && packIndex < Renderer::MON_CATALOG) R.m_monModel[packIndex] = mSlot;
			InterlockedExchange((volatile long*)&m.live, 1);
			return TRUE;
}

void Renderer::LoadCreaturePack()
{
	TacMonRestart();
	for (auto& c : m_creatures) c.Release();
	m_creatures.clear();
	for (auto& s : m_skins) ReleaseSkinGpu(s);
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
		m_monBytes = data;
		m_monBytesN = size;
		if (n > (UINT)MON_CATALOG) n = (UINT)MON_CATALOG;
		m_monN = 0;
		memset(g_footGot, 0, sizeof(g_footGot));
		memset(g_tacFoot, 0, sizeof(g_tacFoot));
		g_packBytes = data;
		g_packBytesN = size;
		char bodyName[64][32];
		int bodyPack[64];
		int nBody = 0;
		for (UINT i = 0; i < n; i++) {
			if (rd.o > 0x7fffffff) break;
			m_monOff[i] = (int)(4 + rd.o);
			if (i < 1024) g_packOff[i] = m_monOff[i];
			char name[32];
			// 足幅はここでは測らない。全種の頂点を撫でると起動が止まる。出す種だけ TacMonsterFoot が測る。
			if (!SkipTackRec(rd, name, NULL)) break;
			m_monBody[i] = 0;
			m_bodySlot[i] = -1;
			m_monModel[i] = -1;
			if (IsBodyName(name) && nBody < 64) {
				memcpy(bodyName[nBody], name, 32);
				bodyName[nBody][31] = 0;
				bodyPack[nBody] = (int)i;
				nBody++;
			}
			m_monN++;
		}
		g_packN = m_monN;
		int roomM = 192 - (int)models.size() - 8;
		int roomR = 192 - (int)rigs.size();
		int roomC = 160 - (int)m_creatures.size();
		int room = roomM;
		if (roomR < room) room = roomR;
		if (roomC < room) room = roomC;
		if (room < 0) room = 0;
		if (nBody > room) nBody = room;
		m_body0M = (int)models.size();
		m_body0R = (int)rigs.size();
		m_body0C = (int)m_creatures.size();
		m_bodyN = nBody;
		if (nBody > 0) {
			models.resize(m_body0M + nBody);
			rigs.resize(m_body0R + nBody);
			m_creatures.resize(m_body0C + nBody);
			m_skins.resize(m_body0C + nBody);
		}
		for (int k = 0; k < nBody; k++) {
			Model& slot = models[m_body0M + k];
			slot.name[0] = 0;
			memcpy(slot.name, bodyName[k], 32);
			slot.live = 0;
			slot.nJ = 0;
			slot.rig = -1;
			slot.headJ = slot.chestJ = slot.handR = slot.handL = slot.hipsJ = -1;
			slot.palmOk = 0;
			slot.parts.reset();
			m_monBody[bodyPack[k]] = 1;
			m_bodySlot[bodyPack[k]] = k;
			m_skins[m_body0C + k].model = -1;
			m_skins[m_body0C + k].faceSet = -1;
			m_skins[m_body0C + k].faceN = 0;
			m_skins[m_body0C + k].bind.clear();
		}
		m_keepRigs = (int)rigs.size();
		m_keepCreatures = (int)m_creatures.size();
		TacMonKeep();
		return;
	}
	const int packed = memcmp(data, "TACP", 4) == 0;
	if (!packed && memcmp(data, "TACM", 4) != 0) return;
	Reader rd(data + 4, size - 4);
	UINT n = rd.U32();
	if (n > 256) return;
	m_creatures.reserve(n);
	static Buf<VtxS> v;
	static Buf<WORD> idx;
	for (UINT i = 0; i < n; i++) {
		char name[32];
		rd.Name(name, 32);
		if (!ReadColorMesh(rd, packed, v, idx)) break;
		GpuMesh gm;
		if (!MakeMesh(gm, v.data(), sizeof(VtxS), (UINT)v.size(), idx.data(), (UINT)idx.size())) continue;
		gm.tex = TEX_WHITE;
		if ((int)m_creatures.size() >= 160) { gm.Release(); return; }
		m_creatures.push_back(gm);
	}
}

void Renderer::EnsureBody(const char* name)
{
	if (!name || !name[0] || FindModel(name) >= 0) return;
	int pack = -1, bi = -1;
	for (int i = 0; i < m_monN && i < MON_CATALOG; i++) {
		int s = m_bodySlot[i];
		if (s < 0 || s >= m_bodyN) continue;
		if (strcmp(models[m_body0M + s].name, name)) continue;
		pack = i;
		bi = s;
		break;
	}
	if (bi < 0 || !m_monBytes || m_monOff[pack] <= 0 || (DWORD)m_monOff[pack] >= m_monBytesN) return;
	InitLoadCs();
	EnterCriticalSection(&g_fillCs);
	Reader one(m_monBytes + m_monOff[pack], m_monBytesN - (DWORD)m_monOff[pack]);
	BOOL ok = FillCreature(*this, one, pack, -2 - bi);
	LeaveCriticalSection(&g_fillCs);
	if (!ok || !g_faceReady) return;
	int slot = -1;
	for (int k = 0; k < (int)m_skins.size(); k++) if (m_skins[k].model >= 0 && m_skins[k].model < (int)models.size() && !strcmp(models[m_skins[k].model].name, name)) { slot = k; break; }
	if (slot < 0) return;
	for (int i = 0; i < g_faceLinkN; i++) if (!strcmp(g_faceNm[i], name)) {
		CreatureSkin& s = m_skins[slot];
		if ((size_t)g_face0[i] + (size_t)g_faceNvI[i] != s.bind.size()) continue;
		memcpy(s.faceLin, g_faceLinI[i], sizeof(s.faceLin));
		s.faceSet = g_faceSetI[i];
		s.faceStart = g_face0[i];
		s.faceN = g_faceNvI[i];
		return;
	}
	if (m_skins[slot].faceSet < 0) BindFaceSynth(m_skins[slot], 0, 0);
}

void Renderer::LoadBattleCreatures(const int* pack, int n)
{
	if (!m_monBytes || m_monN <= 0) return;
	if (m_ctx) {
		ID3D11Buffer* none = NULL;
		UINT z = 0;
		m_ctx->IASetVertexBuffers(0, 1, &none, &z, &z);
		m_ctx->IASetIndexBuffer(NULL, DXGI_FORMAT_R16_UINT, 0);
		ID3D11ShaderResourceView* nos[4] = {};
		m_ctx->PSSetShaderResources(0, 4, nos);
		m_ctx->VSSetShaderResources(0, 4, nos);
		m_ctx->CSSetShaderResources(0, 3, nos);
		ID3D11UnorderedAccessView* nou = NULL;
		m_ctx->CSSetUnorderedAccessViews(0, 1, &nou, NULL);
	}
	for (int i = 0; i < m_monN; i++) if (!m_monBody[i]) m_monModel[i] = -1;
	int nRel = (int)m_creatures.size();
	if ((int)m_skins.size() > nRel) nRel = (int)m_skins.size();
	for (int c = m_keepCreatures; c < nRel; c++) {
		if (c < (int)m_creatures.size()) m_creatures[c].Release();
		if (c >= (int)m_skins.size()) continue;
		ReleaseSkinGpu(m_skins[c]);
		TAC_REL(m_skins[c].baldIB);
		m_skins[c].baldN = 0;
		m_skins[c].bind.arena = 2; m_skins[c].bind.reset();
		m_skins[c].cloth.arena = 2; m_skins[c].cloth.reset();
		m_skins[c].headTri.arena = 2; m_skins[c].headTri.reset();
		m_skins[c].meshHair.arena = 2; m_skins[c].meshHair.reset();
	}
	for (int i = m_keepRigs; i < (int)rigs.size(); i++) {
		for (int c = 0; c < rigs[i].clips.size(); c++) {
			rigs[i].clips[c].q.arena = 2; rigs[i].clips[c].q.reset();
			rigs[i].clips[c].hips.arena = 2; rigs[i].clips[c].hips.reset();
		}
	}
	TacMonReset();
	int slot = 0;
	int room = 160 - m_keepCreatures;
	int roomM = 192 - m_keepModels;
	int roomR = 192 - m_keepRigs;
	if (roomM < room) room = roomM;
	if (roomR < room) room = roomR;
	if (room < 0) room = 0;
	if (!pack) n = 0;
	for (int i = 0; i < n && slot < room; i++) {
		int id = pack[i];
		if (id < 0 || id >= m_monN || m_monBody[id]) continue;
		if (m_monModel[id] >= 0) continue;
		Reader one(m_monBytes + m_monOff[id], m_monBytesN - (DWORD)m_monOff[id]);
		InitLoadCs();
		EnterCriticalSection(&g_fillCs);
		BOOL ok = FillCreature(*this, one, id, slot);
		LeaveCriticalSection(&g_fillCs);
		if (!ok) continue;
		slot++;
	}
	if ((int)models.size() > m_keepModels + slot) models.resize(m_keepModels + slot);
	if ((int)rigs.size() > m_keepRigs + slot) rigs.resize(m_keepRigs + slot);
	if ((int)m_creatures.size() > m_keepCreatures + slot) m_creatures.resize(m_keepCreatures + slot);
	if ((int)m_skins.size() > m_keepCreatures + slot) m_skins.resize(m_keepCreatures + slot);
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
	SkinAndDraw(m_skins[slot], m_creatures[slot], NULL, 0.f, pose, world, dp);
}

void Renderer::ReleaseSkinGpu(CreatureSkin& src)
{
	DropSkinGpu(src);
}

BOOL Renderer::EnsureSkinGpu(CreatureSkin& src, const float* jig)
{
	if (src.bindSRV && src.outUAV) return TRUE;
	if (!m_csSkin || !m_dev || src.bind.empty() || src.bind.size() > 64000) return FALSE;
	const int nv = (int)src.bind.size();
	struct GpuSkinIn { float p[4], n[4], bx[4], bn[4]; unsigned pack[4]; };
	static_assert(sizeof(GpuSkinIn) == 80, "GpuSkinIn");
	static Buf<GpuSkinIn> rec;
	rec.resize(nv);
	if (!rec.data() || rec.size() < nv) return FALSE;
	for (int i = 0; i < nv; i++) {
		const CSkinV& s = src.bind[(size_t)i];
		GpuSkinIn& g = rec[(size_t)i];
		g.p[0] = s.p[0]; g.p[1] = s.p[1]; g.p[2] = s.p[2];
		g.p[3] = (i < (int)src.cloth.size() && src.cloth[(size_t)i]) ? 1.f : 0.f;
		g.n[0] = s.n[0]; g.n[1] = s.n[1]; g.n[2] = s.n[2]; g.n[3] = 0.f;
		if (jig) { g.bx[0] = jig[(size_t)i * 3]; g.bx[1] = jig[(size_t)i * 3 + 1]; g.bx[2] = jig[(size_t)i * 3 + 2]; }
		else g.bx[0] = g.bx[1] = g.bx[2] = 0.f;
		g.bx[3] = 0.f;
		g.bn[0] = g.bn[1] = g.bn[2] = g.bn[3] = 0.f;
		g.pack[0] = s.c;
		g.pack[1] = (unsigned)s.j[0] | ((unsigned)s.j[1] << 8) | ((unsigned)s.j[2] << 16) | ((unsigned)s.j[3] << 24);
		g.pack[2] = (unsigned)s.w[0] | ((unsigned)s.w[1] << 8) | ((unsigned)s.w[2] << 16) | ((unsigned)s.w[3] << 24);
		g.pack[3] = 0;
	}
	ReleaseSkinGpu(src);
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = nv * (UINT)sizeof(GpuSkinIn);
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = sizeof(GpuSkinIn);
	D3D11_SUBRESOURCE_DATA sd = { rec.data(), 0, 0 };
	if (FAILED(m_dev->CreateBuffer(&bd, &sd, &src.bindSB))) { ReleaseSkinGpu(src); return FALSE; }
	D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.Format = DXGI_FORMAT_UNKNOWN;
	sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	sv.Buffer.NumElements = (UINT)nv;
	if (FAILED(m_dev->CreateShaderResourceView(src.bindSB, &sv, &src.bindSRV))) { ReleaseSkinGpu(src); return FALSE; }
	bd = {};
	bd.ByteWidth = (UINT)nv * 36u;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &src.outVB))) { ReleaseSkinGpu(src); return FALSE; }
	D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
	ud.Format = DXGI_FORMAT_R32_TYPELESS;
	ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	ud.Buffer.FirstElement = 0;
	ud.Buffer.NumElements = (UINT)nv * 36u / 4u;
	ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	if (FAILED(m_dev->CreateUnorderedAccessView(src.outVB, &ud, &src.outUAV))) { ReleaseSkinGpu(src); return FALSE; }
	src.gpuNv = nv;
	return TRUE;
}

void Renderer::BindFaceSynth(CreatureSkin& s, int first, int count)
{
	if (s.model < 0 || s.model >= (int)models.size()) return;
	const char* nm = models[s.model].name;
	const BOOL fem = strncmp(nm, "HumF", 4) == 0;
	const BOOL mal = strncmp(nm, "HumM", 4) == 0;
	if ((!fem && !mal) || nm[4] < '0' || nm[4] > '9') return;
	s.faceSet = -1;
	s.faceN = 0;
	s.faceSynth = 0;
	const int headJ = models[s.model].headJ;
	if (headJ < 0) return;
	const int nv = (int)s.bind.size();
	int lo = count > 0 ? first : 0;
	int hi = count > 0 ? first + count : nv;
	if (lo < 0) lo = 0;
	if (hi > nv) hi = nv;
	if (hi - lo < 40) return;
	static Buf<int> hv;
	hv.clear();
	float yTop = -1e9f, sx = 0;
	for (int k = lo; k < hi; k++) {
		const CSkinV& b = s.bind[k];
		int best = 0, bw = -1;
		for (int c = 0; c < 4; c++) if ((int)b.w[c] > bw) { bw = (int)b.w[c]; best = b.j[c]; }
		if (best != headJ || bw < 200) continue;
		hv.push_back(k);
		if (b.p[1] > yTop) yTop = b.p[1];
		sx += b.p[0];
	}
	if (hv.size() < 80) return;
	const float cxAll = sx / (float)hv.size();
	static Buf<int> cran;
	cran.clear();
	for (int k : hv) {
		const float* p = s.bind[k].p;
		if (p[1] < yTop - 0.26f) continue;
		if (fabsf(p[0] - cxAll) > 0.16f) continue;
		cran.push_back(k);
	}
	Buf<int>& use = cran.size() >= 40 ? cran : hv;
	float sx2 = 0;
	for (int k : use) sx2 += s.bind[k].p[0];
	const float cx = sx2 / (float)use.size();
	float half = 0;
	for (int k : use) {
		float ax = fabsf(s.bind[k].p[0] - cx);
		if (ax > half) half = ax;
	}
	if (half > 0.12f) half = 0.12f;
	if (half < 0.05f) half = 0.05f;
	const float eyeY = yTop - 0.125f;
	float eyeZ = 1e9f;
	for (int k : hv) {
		const float* p = s.bind[k].p;
		if (fabsf(p[0] - cx) < half * 0.45f && fabsf(p[1] - eyeY) < 0.035f && p[2] < eyeZ) eyeZ = p[2];
	}
	if (eyeZ > 10.f) return;
	auto snapEye = [&](float side, float* out) {
		double ax = 0, ay = 0, az = 0; int n = 0;
		for (int k : hv) {
			const CSkinV& b = s.bind[k];
			float dx = b.p[0] - (cx + side), dy = b.p[1] - eyeY, dz = b.p[2] - (eyeZ + 0.012f);
			if (dx * dx + dy * dy + dz * dz > 0.020f * 0.020f) continue;
			float lum = ((b.c & 255) + ((b.c >> 8) & 255) + ((b.c >> 16) & 255)) / (3.f * 255.f);
			if (lum > 0.22f) continue;
			ax += b.p[0]; ay += b.p[1]; az += b.p[2]; n++;
		}
		if (n >= 6) { out[0] = (float)(ax / n); out[1] = (float)(ay / n); out[2] = (float)(az / n); }
		else { out[0] = cx + side; out[1] = eyeY; out[2] = eyeZ + 0.012f; }
	};
	snapEye(-half * 0.34f, s.faceEye[0]);
	snapEye(half * 0.34f, s.faceEye[1]);
	int nose = -1, chin = -1;
	float noseZ = 1e9f, chinY = 1e9f;
	for (int k : hv) {
		const float* p = s.bind[k].p;
		if (fabsf(p[0] - cx) < half * 0.16f && p[1] < eyeY - 0.012f && p[1] > eyeY - 0.07f && p[2] < noseZ) { noseZ = p[2]; nose = k; }
		if (fabsf(p[0] - cx) < half * 0.22f && p[2] < eyeZ + 0.035f && p[1] < eyeY - 0.04f && p[1] < chinY) { chinY = p[1]; chin = k; }
	}
	if (nose >= 0) memcpy(s.faceNose, s.bind[nose].p, sizeof(s.faceNose));
	else { s.faceNose[0] = cx; s.faceNose[1] = eyeY - 0.03f; s.faceNose[2] = eyeZ; }
	if (chin >= 0) memcpy(s.faceChin, s.bind[chin].p, sizeof(s.faceChin));
	else { s.faceChin[0] = cx; s.faceChin[1] = eyeY - 0.09f; s.faceChin[2] = eyeZ + 0.02f; }
	s.faceHalf = half;
	s.faceCx = cx;
	int i0 = hv[0], i1 = hv[0];
	for (int k : hv) { if (k < i0) i0 = k; if (k > i1) i1 = k; }
	s.faceStart = i0;
	s.faceN = i1 - i0 + 1;
	if (s.faceN < 30 || s.faceN > 20000) { s.faceN = 0; return; }
	s.faceSet = fem ? 1 : 0;
	s.faceSynth = 1;
	const float idn[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	memcpy(s.faceLin, idn, sizeof(idn));
	s.faceGot = -1;
}

void Renderer::UpdateFaceSynth(CreatureSkin& src, int face)
{
	struct GpuFaceIn { float d[4], n[4]; };
	static_assert(sizeof(GpuFaceIn) == 32, "GpuFaceIn");
	const int n = src.faceN;
	if (face < 1 || face > 5 || n <= 0) return;
	static Buf<GpuFaceIn> rec;
	rec.resize(n);
	if (!rec.data() || rec.size() < n) return;
	memset(rec.data(), 0, sizeof(GpuFaceIn) * (size_t)n);
	const BOOL fem = src.faceSet == 1;
	DWORD iris;
	if (fem) {
		static const DWORD c[5] = { RGBA8(51, 31, 20), RGBA8(92, 112, 140), RGBA8(140, 96, 48), RGBA8(56, 140, 78), RGBA8(176, 100, 28) };
		iris = c[face - 1];
	} else {
		static const DWORD c[5] = { RGBA8(40, 24, 16), RGBA8(86, 107, 133), RGBA8(56, 128, 72), RGBA8(92, 56, 26), RGBA8(158, 102, 30) };
		iris = c[face - 1];
	}
	// 0 丸い大眼 1 細い眼 2 やわらか 3 尖った耳 4 大きい眼と広い口
	int kind = face - 1;
	if (!fem) { static const int mk[5] = { 1, 2, 3, 0, 4 }; kind = mk[face - 1]; }
	const int headJ = (src.model >= 0 && src.model < (int)models.size()) ? models[src.model].headJ : -1;
	const float* EL = src.faceEye[0];
	const float* ER = src.faceEye[1];
	const float* nose = src.faceNose;
	const float* chin = src.faceChin;
	const float half = src.faceHalf > 0.04f ? src.faceHalf : 0.08f;
	const float cx = src.faceCx;
	const float* eyes[2] = { EL, ER };
	for (int i = 0; i < n; i++) {
		const int v = src.faceStart + i;
		if (v < 0 || v >= (int)src.bind.size()) continue;
		const CSkinV& b = src.bind[v];
		if (headJ >= 0) {
			int best = 0, bw = -1;
			for (int c = 0; c < 4; c++) if ((int)b.w[c] > bw) { bw = (int)b.w[c]; best = b.j[c]; }
			if (best != headJ || bw < 200) continue;
		}
		if (v < (int)src.meshHair.size() && src.meshHair[v]) continue;
		const float x = b.p[0], y = b.p[1], z = b.p[2];
		float ox = 0, oy = 0, oz = 0;
		int paint = 0;
		const float lum = ((b.c & 255) + ((b.c >> 8) & 255) + ((b.c >> 16) & 255)) / (3.f * 255.f);
		DWORD eyeCol = 0;
		for (int e = 0; e < 2; e++) {
			const float dx = x - eyes[e][0], dy = y - eyes[e][1], dz = z - eyes[e][2];
			const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
			// 瞳そのものは動かさない。暗い虹彩だけ、元の明度を保って色相を替える。
			if (dist < 0.008f && lum > 0.035f && lum < 0.16f && !paint) {
				float ir = (iris & 255) / 255.f, ig = ((iris >> 8) & 255) / 255.f, ib = ((iris >> 16) & 255) / 255.f;
				float iLum = 0.30f * ir + 0.59f * ig + 0.11f * ib;
				if (iLum < 0.05f) iLum = 0.05f;
				float s = (0.22f + lum) / iLum;
				if (s > 0.85f) s = 0.85f;
				auto q8 = [](float v) { int i = (int)(v * 255.f + .5f); return i < 0 ? 0 : (i > 255 ? 255 : i); };
				eyeCol = RGBA8(q8(ir * s), q8(ig * s), q8(ib * s));
				paint = 1;
			}
			// まぶたは面に沿って数ミリだけ。奥へ押すと眼窩が潰れる。
			if (dist < 0.011f || dist >= 0.020f || lum < 0.28f) continue;
			const float t = 1.f - (dist - 0.011f) / 0.009f;
			float grow = 0, flatten = 0, lift = 0;
			if (kind == 0) grow = 0.18f;
			else if (kind == 1) flatten = 0.22f;
			else if (kind == 2) { grow = 0.12f; lift = -0.002f; }
			else if (kind == 3) grow = 0.08f;
			else { grow = 0.14f; lift = 0.002f; }
			ox += dx * grow * t;
			oy += dy * grow * t - dy * flatten * t + lift * t;
		}
		const float ax = fabsf(x - cx);
		if (kind == 3 && ax > half * 0.70f && y > EL[1] - 0.02f && y < EL[1] + 0.08f && z > EL[2] + 0.02f && z < EL[2] + 0.16f) {
			float t = (ax - half * 0.70f) / (half * 0.32f);
			if (t > 1.f) t = 1.f;
			ox += (x >= cx ? 0.014f : -0.014f) * t;
			oy += 0.024f * t;
		}
		if ((kind == 0 || kind == 2) && ax > half * 0.40f && ax < half * 0.88f && y < EL[1] && y > chin[1] && z < EL[2] + 0.04f && z > nose[2] - 0.005f) {
			float t = 1.f - fabsf(y - (EL[1] + chin[1]) * 0.5f) / (EL[1] - chin[1] + 0.001f);
			if (t < 0) t = 0;
			ox += (x >= cx ? 1.f : -1.f) * (kind == 0 ? 0.012f : 0.007f) * t;
		}
		if (kind == 0) {
			float dx = x - nose[0], dy = y - nose[1], dz = z - nose[2];
			float d2 = dx * dx + dy * dy + dz * dz;
			if (d2 < 0.014f * 0.014f) oz += 0.008f * (1.f - sqrtf(d2) / 0.014f);
		}
		if (kind == 1) {
			float dx = x - chin[0], dy = y - chin[1], dz = z - chin[2];
			float d2 = dx * dx + dy * dy + dz * dz;
			if (d2 < 0.020f * 0.020f) {
				float t = 1.f - sqrtf(d2) / 0.020f;
				oz -= 0.009f * t;
				oy -= 0.005f * t;
			}
		}
		if (kind == 3 && y < nose[1] && y > chin[1] - 0.01f && ax < half * 0.55f && z < EL[2] + 0.03f) {
			float t = (nose[1] - y) / (nose[1] - chin[1] + 0.001f);
			if (t > 1.f) t = 1.f;
			if (t < 0) t = 0;
			ox -= (x - cx) * 0.30f * t;
		}
		if (kind == 4 && y < nose[1] && y > chin[1] && ax < half * 0.42f && z < nose[2] + 0.025f) {
			float mid = (nose[1] + chin[1]) * 0.5f;
			float t = 1.f - fabsf(y - mid) / ((nose[1] - chin[1]) * 0.5f + 0.001f);
			if (t > 0 && ax > 0.004f) ox += (x >= cx ? 0.008f : -0.008f) * t;
		}
		float L2 = ox * ox + oy * oy + oz * oz;
		if (L2 > 0.030f * 0.030f) {
			float s = 0.030f / sqrtf(L2);
			ox *= s; oy *= s; oz *= s;
		}
		if (ox == 0 && oy == 0 && oz == 0 && !paint) continue;
		GpuFaceIn& g = rec[(size_t)i];
		g.d[0] = ox; g.d[1] = oy; g.d[2] = oz;
		g.d[3] = paint ? 2.f : 1.f;
		if (paint) memcpy(&g.n[3], &eyeCol, 4);
	}
	if (!src.faceSB || src.faceGot < 0) {
		TAC_REL(src.faceSRV); TAC_REL(src.faceSB);
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (UINT)n * 32u;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = 32;
		D3D11_SUBRESOURCE_DATA sd = { rec.data(), 0, 0 };
		if (FAILED(m_dev->CreateBuffer(&bd, &sd, &src.faceSB))) return;
		D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
		sv.Format = DXGI_FORMAT_UNKNOWN;
		sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		sv.Buffer.NumElements = (UINT)n;
		if (FAILED(m_dev->CreateShaderResourceView(src.faceSB, &sv, &src.faceSRV))) { TAC_REL(src.faceSB); return; }
	} else {
		m_ctx->UpdateSubresource(src.faceSB, 0, NULL, rec.data(), 0, 0);
	}
	src.faceGot = face;
}

void Renderer::UpdateFaceGpu(CreatureSkin& src, int face)
{
	if (src.faceGot == face && src.faceSRV) return;
	if (src.faceN <= 0 || src.faceSet < 0 || src.faceSet > 1 || face <= 0) return;
	if (src.faceSynth) { UpdateFaceSynth(src, face); return; }
	const FaceSet& fs = m_faces[src.faceSet];
	if (face >= fs.nFace || fs.nv <= 0) return;
	const int n = src.faceN < fs.nv ? src.faceN : fs.nv;
	struct GpuFaceIn { float d[4], n[4]; };
	static_assert(sizeof(GpuFaceIn) == 32, "GpuFaceIn");
	static Buf<GpuFaceIn> rec;
	rec.resize(n);
	if (!rec.data() || rec.size() < n) return;
	const float* L = src.faceLin;
	for (int i = 0; i < n; i++) {
		size_t q = (size_t)face * (size_t)fs.nv + (size_t)i;
		if (q * 3 + 2 >= fs.d.size() || q * 4 + 3 >= fs.n.size() || q >= fs.c.size()) break;
		const short* d = &fs.d[q * 3];
		float dx = d[0] * (0.25f / 32767.f), dy = d[1] * (0.25f / 32767.f), dz = d[2] * (0.25f / 32767.f);
		rec[(size_t)i].d[0] = L[0] * dx + L[1] * dy + L[2] * dz;
		rec[(size_t)i].d[1] = L[3] * dx + L[4] * dy + L[5] * dz;
		rec[(size_t)i].d[2] = L[6] * dx + L[7] * dy + L[8] * dz;
		const signed char* nn = &fs.n[q * 4];
		float nx = nn[0] / 127.f, ny = nn[1] / 127.f, nz = nn[2] / 127.f;
		rec[(size_t)i].n[0] = L[0] * nx + L[1] * ny + L[2] * nz;
		rec[(size_t)i].n[1] = L[3] * nx + L[4] * ny + L[5] * nz;
		rec[(size_t)i].n[2] = L[6] * nx + L[7] * ny + L[8] * nz;
		unsigned c = fs.c[q];
		memcpy(&rec[(size_t)i].n[3], &c, 4);
		rec[(size_t)i].d[3] = 3.f;
	}
	if (!src.faceSB || src.faceGot < 0) {
		TAC_REL(src.faceSRV); TAC_REL(src.faceSB);
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (UINT)n * 32u;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = 32;
		D3D11_SUBRESOURCE_DATA sd = { rec.data(), 0, 0 };
		if (FAILED(m_dev->CreateBuffer(&bd, &sd, &src.faceSB))) return;
		D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
		sv.Format = DXGI_FORMAT_UNKNOWN;
		sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		sv.Buffer.NumElements = (UINT)n;
		if (FAILED(m_dev->CreateShaderResourceView(src.faceSB, &sv, &src.faceSRV))) { TAC_REL(src.faceSB); return; }
	} else {
		m_ctx->UpdateSubresource(src.faceSB, 0, NULL, rec.data(), 0, 0);
	}
	src.faceGot = face;
}

BOOL Renderer::GpuSkinAndDraw(CreatureSkin& src, const float* jig, float amt, const Pose& pose, const M4& world, const DrawParams& dp, GpuMesh& base,
	AvaCache* cloth, const void* owner)
{
	if (!m_csSkin || !m_cbSkinBone || !m_cbSkinParm || !m_faceDummySRV || !base.ib) return FALSE;
	if (!EnsureSkinGpu(src, jig)) return FALSE;
	const BOOL faceOn = dp.face > 0 && src.faceSet >= 0 && src.faceSet <= 1 && dp.face < m_faces[src.faceSet].nFace && src.faceN > 0;
	if (faceOn) UpdateFaceGpu(src, dp.face);
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(m_ctx->Map(m_cbSkinBone, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return FALSE;
	memset(ms.pData, 0, 64u * (unsigned)MAXJ);
	int nj = pose.nJ;
	if (nj < 0) nj = 0;
	if (nj > MAXJ) nj = MAXJ;
	if (nj) memcpy(ms.pData, pose.palette, sizeof(M4) * (size_t)nj);
	m_ctx->Unmap(m_cbSkinBone, 0);
	struct Parm { float nip0[4], nip1[4], bust[4], info[4]; unsigned extra[4]; float jig[4]; };
	static_assert(sizeof(Parm) == 96, "SkinParm");
	Parm prm;
	memset(&prm, 0, sizeof(prm));
	if (jig && src.model >= 0 && src.model < (int)models.size()) {
		int cj = models[src.model].chestJ;
		if (cj >= 0 && cj < pose.nJ && src.bustFrame != m_frame) {
			const float* gm = pose.global[cj].m;
			float x = gm[12], y = gm[13], z = gm[14];
			if (src.bustPrevOk) {
				float dx = x - src.bustPrev[0], dy = y - src.bustPrev[1], dz = z - src.bustPrev[2];
				if (dx * dx + dy * dy + dz * dz < 0.08f * 0.08f) {
					float tgt[3] = { -dx * 0.90f, -dy * 1.25f, -dz * 0.90f };
					for (int a = 0; a < 3; a++) {
						src.bustVel[a] += tgt[a] * 0.65f + (0.f - src.bustLag[a]) * 0.22f;
						src.bustVel[a] *= 0.84f;
						src.bustLag[a] += src.bustVel[a];
					}
					float L2 = src.bustLag[0] * src.bustLag[0] + src.bustLag[1] * src.bustLag[1] + src.bustLag[2] * src.bustLag[2];
					if (L2 > 0.020f * 0.020f) {
						float s = 0.020f / sqrtf(L2);
						for (int a = 0; a < 3; a++) { src.bustLag[a] *= s; src.bustVel[a] *= s; }
					}
				} else {
					src.bustLag[0] = src.bustLag[1] = src.bustLag[2] = 0.f;
					src.bustVel[0] = src.bustVel[1] = src.bustVel[2] = 0.f;
				}
			}
			src.bustPrev[0] = x; src.bustPrev[1] = y; src.bustPrev[2] = z;
			src.bustPrevOk = 1;
			src.bustFrame = m_frame;
		}
		prm.jig[0] = src.bustLag[0]; prm.jig[1] = src.bustLag[1]; prm.jig[2] = src.bustLag[2];
	}
	prm.jig[3] = (float)dp.skinTone;
	prm.bust[0] = amt;
	prm.bust[2] = dp.clothHue;
	prm.bust[3] = (float)dp.hairJoint;
	prm.info[1] = (float)nj;
	if (faceOn && src.faceSRV && src.faceGot == dp.face) {
		const FaceSet& fs = m_faces[src.faceSet];
		int n = src.faceSynth ? src.faceN : (src.faceN < fs.nv ? src.faceN : fs.nv);
		prm.info[0] = (float)n;
		prm.info[2] = (float)src.faceStart;
	}
	prm.extra[0] = dp.hairTint;
	prm.extra[1] = (unsigned)src.gpuNv;
	prm.extra[2] = dp.clothTint & 0x00FFFFFFu;
	prm.extra[3] = dp.clothTint ? 1u : 0u;
	prm.info[3] = dp.bodyScale > 0.05f ? dp.bodyScale : 1.f;
	if (FAILED(m_ctx->Map(m_cbSkinParm, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return FALSE;
	memcpy(ms.pData, &prm, sizeof(prm));
	m_ctx->Unmap(m_cbSkinParm, 0);
	ID3D11Buffer* noneVB = NULL;
	UINT zero = 0;
	m_ctx->IASetVertexBuffers(0, 1, &noneVB, &zero, &zero);
	ID3D11Buffer* cbs[3] = { m_cbSkinBone, m_cbSkinParm, m_cbHairCloth };
	m_ctx->CSSetConstantBuffers(0, 3, cbs);
	ID3D11ShaderResourceView* srvs[2] = { src.bindSRV, (faceOn && src.faceSRV) ? src.faceSRV : m_faceDummySRV };
	m_ctx->CSSetShaderResources(0, 2, srvs);
	UINT cnt = 0;
	m_ctx->CSSetUnorderedAccessViews(0, 1, &src.outUAV, &cnt);
	m_ctx->CSSetShader(m_csSkin, NULL, 0);
	m_ctx->Dispatch(((UINT)src.gpuNv + 63u) / 64u, 1, 1);
	ID3D11UnorderedAccessView* nou = NULL;
	ID3D11ShaderResourceView* nos[3] = { NULL, NULL, NULL };
	ID3D11Buffer* nob[3] = { NULL, NULL, NULL };
	m_ctx->CSSetUnorderedAccessViews(0, 1, &nou, NULL);
	m_ctx->CSSetShaderResources(0, 3, nos);
	m_ctx->CSSetConstantBuffers(0, 3, nob);
	m_ctx->CSSetShader(NULL, NULL, 0);
	if (cloth && owner) RunCloth(*cloth, owner, src, pose, world, dp);
	ID3D11Buffer* saved = base.vb;
	UINT savedStride = base.stride;
	base.vb = src.outVB;
	base.stride = sizeof(VtxS);
	DrawStatic(base, world, dp);
	base.vb = saved;
	base.stride = savedStride;
	return TRUE;
}

void Renderer::SkinAndDraw(CreatureSkin& src, GpuMesh& base, const float* jig, float amt, const Pose& pose, const M4& world, const DrawParams& dp,
	AvaCache* cloth, const void* owner)
{
	struct BaldHold {
		GpuMesh& mesh; ID3D11Buffer* ib; UINT n; BOOL on;
		BaldHold(GpuMesh& mesh, CreatureSkin& src, const DrawParams& dp) : mesh(mesh), ib(NULL), n(0), on(FALSE) {
			if (dp.hideMeshHair && src.baldIB && src.baldN >= 3) {
				ib = mesh.ib; n = mesh.nIdx; mesh.ib = src.baldIB; mesh.nIdx = src.baldN; on = TRUE;
			}
		}
		~BaldHold() { if (on) { mesh.ib = ib; mesh.nIdx = n; } }
	} hold(base, src, dp);
	// 頂点が数センチで骨格側に倍率があるメッシュは、計算シェーダだとタイル上に残らない。
	// 肖像もマップも、ここのスキン結果（身長およそ 1.7）をそのまま描く。
	const BOOL boneScale = !jig && src.extR > 0.f && src.extR < 0.2f;
	if (!boneScale && GpuSkinAndDraw(src, jig, jig ? amt : 0.f, pose, world, dp, base, cloth, owner)) return;
	static Buf<VtxS> v;
	v.resize((int)src.bind.size());
	if (!v.data() || v.size() < (int)src.bind.size()) return;
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
	auto dyeCloth = [&](DWORD c) -> DWORD {
		float r = (c & 255) / 255.f, g = ((c >> 8) & 255) / 255.f, b = ((c >> 16) & 255) / 255.f;
		float tr = (dp.clothTint & 255) / 255.f, tg = ((dp.clothTint >> 8) & 255) / 255.f, tb = ((dp.clothTint >> 16) & 255) / 255.f;
		float y = 0.30f * r + 0.59f * g + 0.11f * b;
		float y2 = (y - 0.45f) * 2.2f + 0.50f;
		if (y2 < 0.f) y2 = 0.f;
		if (y2 > 1.f) y2 = 1.f;
		float lr = tr * 0.16f, lg = tg * 0.16f, lb = tb * 0.16f;
		float hr = tr + (1.f - tr) * 0.85f, hg = tg + (1.f - tg) * 0.85f, hb = tb + (1.f - tb) * 0.85f;
		float orr, og, ob;
		if (y2 < 0.5f) {
			float t = y2 * 2.f;
			orr = lr + (tr - lr) * t; og = lg + (tg - lg) * t; ob = lb + (tb - lb) * t;
		} else {
			float t = (y2 - 0.5f) * 2.f;
			orr = tr + (hr - tr) * t; og = tg + (hg - tg) * t; ob = tb + (hb - tb) * t;
		}
		float cr = r - y, cg = g - y, cb = b - y;
		float amp = sqrtf(cr * cr + cg * cg + cb * cb);
		float wgt = amp * 3.5f;
		if (wgt > 1.f) wgt = 1.f;
		float shift = atan2f(cg - cb, cr) * wgt * 0.30f;
		float ca = cosf(shift), sn = sinf(shift);
		const float kk = 0.57735f;
		float kd = kk * (orr + og + ob) * kk * (1.f - ca);
		float cx = kk * (ob - og), cy = kk * (orr - ob), cz = kk * (og - orr);
		float nr = orr * ca + cx * sn + kd, ng = og * ca + cy * sn + kd, nb = ob * ca + cz * sn + kd;
		auto q = [](float x) { int v = (int)(x * 255.f + .5f); return v < 0 ? 0 : (v > 255 ? 255 : v); };
		return (c & 0xFF000000u) | (DWORD)q(nr) | ((DWORD)q(ng) << 8) | ((DWORD)q(nb) << 16);
	};
	const FaceSet* fs = (dp.face > 0 && src.faceSet >= 0 && dp.face < m_faces[src.faceSet].nFace) ? &m_faces[src.faceSet] : NULL;
	for (size_t k = 0; k < src.bind.size(); k++) {
		CSkinV b = src.bind[k];
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
		float hs = dp.bodyScale > 0.05f ? dp.bodyScale : 1.f;
		d.p[0] = p.x * hs; d.p[1] = p.y * hs; d.p[2] = p.z * hs;
		d.n[0] = n.x; d.n[1] = n.y; d.n[2] = n.z;
		d.uv[0] = d.uv[1] = 0;
		if (dp.clothTint && k < src.cloth.size() && src.cloth[k]) d.c = dyeCloth(b.c);
		else d.c = (dye && src.cloth[k]) ? rotHue(b.c) : b.c;
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
	m_gearGrip.clear();
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
	static Buf<VtxS> v;
	static Buf<WORD> idx;
	for (UINT i = 0; i < n; i++) {
		char name[32];
		rd.Name(name, 32);
		if (!ReadColorMesh(rd, packed, v, idx)) break;
		GpuMesh gm;
		if (!MakeMesh(gm, v.data(), sizeof(VtxS), (UINT)v.size(), idx.data(), (UINT)idx.size())) { LoadPulse((int)i + 1, (int)n); continue; }
		gm.tex = TEX_WHITE;
		if ((int)m_gear.size() >= 256) { gm.Release(); break; }
		m_gear.push_back(gm);
		// 持つ手の指の型。弩は引き金、弓は握り、矢と投げ矢は摘まむ。冠や鎧は手に持たない。
		BYTE grip = GRIP_NONE;
		if (strstr(name, "Crossbow")) grip = GRIP_TRIGGER;
		else if (strstr(name, "Bow")) grip = GRIP_BOW;
		else if (strstr(name, "Book")) grip = GRIP_BOOK;
		else if (strstr(name, "Shield")) grip = GRIP_SHIELD;
		else if (strstr(name, "Arrow") || strstr(name, "Dart")) grip = GRIP_PINCH;
		else if (strstr(name, "Wand")) grip = GRIP_WAND;
		else if (strstr(name, "Staff") || strstr(name, "Spear") || strstr(name, "Scythe")) grip = GRIP_STAFF;
		else if (strstr(name, "Sword") || strstr(name, "Claymore") || strstr(name, "Axe") || strstr(name, "Hammer") || strstr(name, "Dagger") || strstr(name, "Knife") || strstr(name, "Blade")) grip = GRIP_FIST;
		m_gearGrip.push_back(grip);
		LoadPulse((int)i + 1, (int)n);
	}
	InterlockedExchange(&m_gearShown, (long)m_gear.size());
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
	int faceI = 0, faceN = (int)ng;
	for (UINT g = 0; g < ng; g++) {
		UINT nf = rd.U32(), nv = rd.U32();
		if (nf == 0 || nf > 32 || nv == 0 || nv > 20000) return;
		if (faceN < faceI + (int)nf) faceN = faceI + (int)nf;
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
			faceI++;
			LoadPulse(faceI, faceN);
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
		if (g_faceLinkN < 64) {
			memcpy(g_faceNm[g_faceLinkN], name, 32);
			g_faceNm[g_faceLinkN][31] = 0;
			g_faceSetI[g_faceLinkN] = (int)g;
			g_face0[g_faceLinkN] = (int)start;
			g_faceNvI[g_faceLinkN] = (int)nv;
			memcpy(g_faceLinI[g_faceLinkN], lin, 9 * sizeof(float));
			g_faceLinkN++;
		}
		for (CreatureSkin& s : m_skins) {
			if (s.model < 0 || s.model >= (int)models.size() || strcmp(models[s.model].name, name) != 0) continue;
			if ((size_t)start + nv != s.bind.size()) break;
			memcpy(s.faceLin, lin, sizeof(lin));
			s.faceSet = (int)g;
			s.faceStart = (int)start;
			s.faceN = (int)nv;
			break;
		}
	}
	for (CreatureSkin& s : m_skins) if (s.model >= 0 && s.faceSet < 0 && !s.bind.empty()) BindFaceSynth(s, 0, 0);
	g_faceReady = 1;
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

// 頂点の展開は描く衣装だけ。起動時はバイト列の位置だけ覚える。
static BOOL PeekAvaMesh(Reader& rd, const BYTE** begin, UINT* bytes, UINT* nvOut)
{
	size_t s = rd.o;
	UINT nv = rd.U32();
	if (nv == 0 || nv > 65535 || rd.bad) return FALSE;
	if (!rd.Bytes(24)) return FALSE;
	if (!rd.Bytes((size_t)nv * 22)) return FALSE;
	UINT has = rd.U32();
	if (rd.bad) return FALSE;
	if (has && !rd.Bytes((size_t)nv * 12)) return FALSE;
	if (begin) *begin = rd.p + s;
	if (bytes) *bytes = (UINT)(rd.o - s);
	if (nvOut) *nvOut = nv;
	return !rd.bad;
}

static BOOL EnsureAvaCos(Renderer::AvaCos& c)
{
	if (!c.m.v.empty()) return TRUE;
	if (!c.pending || c.pendingN < 16) return FALSE;
	Reader rd(c.pending, c.pendingN);
	if (!ReadAvaMesh(rd, c.m) || c.m.v.empty()) {
		c.m = Renderer::AvaMesh();
		c.pending = NULL;
		return FALSE;
	}
	c.pending = NULL;
	return TRUE;
}

static bool ReadWords(Reader& rd, Buf<WORD>& out, size_t n)
{
	const BYTE* p = rd.Bytes(n * 2);
	if (!p) return false;
	out.resize(n);
	if (n) memcpy(out.data(), p, n * 2);
	return true;
}

void Renderer::ReleaseClothInst()
{
	for (ClothInst& c : m_clothInst) { TAC_REL(c.uav); TAC_REL(c.sb); }
	m_clothInst.clear();
}

void Renderer::ReleaseAvatarCache()
{
	for (AvaCache& c : m_avaCache) {
		TAC_REL(c.skin.baldIB);
		c.skin.baldN = 0;
		ReleaseSkinGpu(c.skin);
		c.gm.Release();
		for (int b = 0; b < 3; b++) { TAC_REL(c.clothSRV[b]); TAC_REL(c.clothSB[b]); }
		c.clothN = c.clothVN = 0;
	}
	m_avaCache.clear();
	ReleaseClothInst();
}

void Renderer::LoadAvatarPack()
{
	ReleaseAvatarCache();
	m_avaSegN = 0;
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
	if (ns == 0 || ns > 64) return;
	for (UINT s = 0; s < ns; s++) {
		char nm[24];
		rd.Name(nm, 24);
		if (m_avaSegN < 64) { memcpy(m_avaSeg[m_avaSegN], nm, 24); m_avaSeg[m_avaSegN][23] = 0; m_avaSegN++; }
	}
	UINT nm = rd.U32();
	if (nm > 64) { m_avaSegN = 0; return; }
	int avSeen = 0, avDen = (int)nm + 16;
	for (UINT i = 0; i < nm; i++) {
		AvaModel am;
		rd.Name(am.name, 32);
		am.gender = (int)rd.U32();
		const BYTE* p = rd.Bytes((size_t)ns * 12 * 4);
		if (!p || am.gender < 0 || am.gender > 1) { m_avaSegN = 0; m_avaModels.clear(); return; }
		am.M.resize((size_t)ns * 12);
		memcpy(am.M.data(), p, (size_t)ns * 12 * 4);
		for (UINT s = 0; s < ns; s++) {
			char jn[24];
			rd.Name(jn, 24);
			if (am.nJoint < 64) { memcpy(am.joint[am.nJoint], jn, 24); am.joint[am.nJoint][23] = 0; am.nJoint++; }
		}
		m_avaModels.push_back(am);
		avSeen++;
		LoadPulse(avSeen, avDen);
	}
	for (int g = 0; g < 2; g++) {
		AvaBody& b = m_avaBody[g];
		b.nFace = (int)rd.U32();
		bool ok = ReadAvaMesh(rd, b.m);
		UINT nb = ok ? rd.U32() : 0, ne = ok ? rd.U32() : 0;
		ok = ok && ReadWords(rd, b.bodyTri, (size_t)nb * 3) && ReadWords(rd, b.extraTri, (size_t)ne * 3);
		if (!ok || b.nFace < 0 || b.nFace > (int)b.m.v.size()) { m_avaSegN = 0; m_avaModels.clear(); m_avaBody[0] = m_avaBody[1] = AvaBody(); return; }
		avSeen++;
		LoadPulse(avSeen, avDen);
	}
	UINT na = rd.U32();
	if (na > 256) return;
	if (avDen < avSeen + (int)na) avDen = avSeen + (int)na;
	for (UINT i = 0; i < na; i++) {
		AvaCos c;
		rd.Name(c.name, 32);
		c.gender = (int)rd.U32();
		c.pending = NULL;
		c.pendingN = 0;
		UINT nv = 0;
		const BYTE* mesh = NULL;
		UINT meshN = 0;
		if (c.gender < 0 || c.gender > 1 || !PeekAvaMesh(rd, &mesh, &meshN, &nv)) break;
		c.pending = mesh;
		c.pendingN = meshN;
		UINT nt = rd.U32();
		if (!ReadWords(rd, c.tri, (size_t)nt * 3)) break;
		size_t nk = (m_avaBody[c.gender].bodyTri.size() / 3 + 7) / 8;
		const BYTE* kp = rd.Bytes(nk);
		if (!kp) break;
		c.keep.assign(kp, kp + nk);
		if (m_avaBody[c.gender].m.v.size() + nv > 65535) { avSeen++; LoadPulse(avSeen, avDen); continue; }
		m_avaCos.push_back(std::move(c));
		avSeen++;
		LoadPulse(avSeen, avDen);
	}
	InterlockedExchange(&m_avaReady, 1);
}

int Renderer::AvatarCount(int gender) const
{
	if (!m_avaReady) return 0;
	int n = 0;
	for (const AvaCos& c : m_avaCos) if (c.gender == gender) n++;
	return n;
}

static int CmpU64(const void* a, const void* b)
{
	unsigned long long x = *(const unsigned long long*)a, y = *(const unsigned long long*)b;
	return (x > y) - (x < y);
}

// 胸の形を作る基準。左右の乳首（体の頂点。0=-X 側）と、胴の縦軸の z（乳首の高さの胴の前後の中点）。
struct BustRef { int kn[2]; float z0; };

// 胸 A〜H。土台の幅（胴の面に沿った弧長、m）、張り出し ÷ 幅、垂れ（張り出しに対する下への量）。
// HumF0（アンダー 66.4cm）の巻き尺のトップ − アンダーが JIS の各カップの中央（A=10 … H=27.5cm）に来る。
static const float kBustW[BUST_N] = { .090f, .110f, .130f, .145f, .160f, .175f, .190f, .205f };
static const float kBustP[BUST_N] = { .10f, .22f, .33f, .41f, .49f, .56f, .58f, .60f };
static const float kBustSag[BUST_N] = { 0.f, .10f, .16f, .22f, .28f, .33f, .38f, .42f };

static unsigned long long EdgeKey(int a, int b)
{
	return a < b ? ((unsigned long long)(unsigned)a << 32) | (unsigned)b : ((unsigned long long)(unsigned)b << 32) | (unsigned)a;
}

static int FindU64(const Buf<unsigned long long>& A, unsigned long long k)
{
	int lo = 0, hi = A.size() - 1;
	while (lo <= hi) {
		const int m = (lo + hi) >> 1;
		if (A[m] == k) return m;
		if (A[m] < k) lo = m + 1; else hi = m - 1;
	}
	return -1;
}

// 胸の周り（H の土台の 1.4 倍）の三角形を、辺が 7mm 以下になるまで 2 段まで割る。体は見えている三角形だけ、服は全部を割る。
// 辺の印は継ぎ目を溶接した頂点で付け、2 辺に印がある三角形は 3 辺とも割る（閉じるまで繰り返す）。印が 1 辺だけの三角形は
// 2 つに割り、T 字の隙間を作らない。新しい頂点は辺の両端の平均で、骨の重みは両端を足して大きい 4 つを残す。
// 頂点は [体][体の新][服][服の新] に並べ直す（元の体の頂点の番号は変わらない）。体の元の三角形 t の子は bTri の
// bOff[t]〜bOff[t+1]、服の三角形は cTri（服の中の番号）。増やせる頂点は maxAdd まで（超える段は割る辺の下限を上げる）。
// 胸の差分が無い体は割らずに写すだけ。FALSE は確保の失敗。
static BOOL RefineBust(Renderer::CreatureSkin& sk, Buf<float>& bx, Buf<float>& bn, int nb, int nv, const Buf<WORD>& bodyTri,
	const Buf<BYTE>& keep, const Buf<WORD>& cosTri, const BYTE* armJ, int maxAdd,
	Buf<int>& bOff, Buf<WORD>& bTri, Buf<WORD>& cTri, int& nbA, int& nvA, BustRef& br)
{
	typedef Renderer::CSkinV SV;
	const int nBT = bodyTri.size() / 3, nCT = cosTri.size() / 3;
	nbA = nb; nvA = nv;
	br.kn[0] = br.kn[1] = -1; br.z0 = 0.f;
	if (nb <= 0 || nv < nb || sk.bind.size() < nv) return FALSE;
	// 作業領域は使い回す（アバターの arena へ切ると、カップや衣装を替えるたびに埋まる）。
	static Buf<int> T, Tt, T2, Tt2, root, remap, cnt;
	static Buf<SV> V;
	static Buf<BYTE> C, part, inR;
	static Buf<float> DX, DN;
	static Buf<unsigned long long> M, R, add;
	T.clear(); Tt.clear();
	for (int t = 0; t < nBT; t++) { for (int e = 0; e < 3; e++) T.push_back(bodyTri[t * 3 + e]); Tt.push_back(t); }
	for (int t = 0; t < nCT; t++) { for (int e = 0; e < 3; e++) T.push_back(cosTri[t * 3 + e] + nb); Tt.push_back(-1); }
	if (T.size() < (nBT + nCT) * 3 || Tt.size() < nBT + nCT) return FALSE;
	const BOOL hasD = bx.size() >= nv * 3 && bn.size() >= nv * 3;
	V.resize(nv); C.resize(nv); part.resize(nv); DX.resize(nv * 3); DN.resize(nv * 3);
	if (V.size() < nv || C.size() < nv || part.size() < nv || DX.size() < nv * 3 || DN.size() < nv * 3) return FALSE;
	for (int k = 0; k < nv; k++) {
		V[k] = sk.bind[k];
		C[k] = k < sk.cloth.size() ? sk.cloth[k] : 0;
		part[k] = k < nb ? 0 : 1;
		for (int a = 0; a < 3; a++) { DX[k * 3 + a] = hasD ? bx[k * 3 + a] : 0.f; DN[k * 3 + a] = hasD ? bn[k * 3 + a] : 0.f; }
	}
	auto armOf = [&](const SV& q) { int m = 0; for (int c = 1; c < 4; c++) if (q.w[c] > q.w[m]) m = c; return armJ[q.j[m]] != 0; };
	BOOL ok = hasD;
	// 左右の乳首: 最大の差分を足した形でいちばん前の点。
	for (int s = 0; ok && s < 2; s++) {
		float best = 1e9f;
		for (int k = 0; k < nb; k++) {
			const float* p = V[k].p; const float* d = &DX[k * 3];
			if ((s ? p[0] : -p[0]) <= 0.02f || p[2] >= 0.f || d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= 1e-4f) continue;
			if (p[2] + d[2] < best) { best = p[2] + d[2]; br.kn[s] = k; }
		}
		if (br.kn[s] < 0) ok = FALSE;
	}
	if (ok) {
		float lo = 1e9f, hi = -1e9f;
		const float yn = V[br.kn[1]].p[1];
		for (int k = 0; k < nb; k++) {
			const float* p = V[k].p;
			if (fabsf(p[1] - yn) >= 0.01f || fabsf(p[0]) >= 0.03f || armOf(V[k])) continue;
			lo = fminf(lo, p[2]); hi = fmaxf(hi, p[2]);
		}
		if (lo < hi) br.z0 = (lo + hi) * 0.5f; else { ok = FALSE; br.kn[0] = br.kn[1] = -1; }
	}
	float thN[2] = {}, rhN[2] = {}, yN[2] = {};
	for (int s = 0; ok && s < 2; s++) {
		const float* p = V[br.kn[s]].p;
		thN[s] = atan2f(p[0], br.z0 - p[2]); rhN[s] = sqrtf(p[0] * p[0] + (p[2] - br.z0) * (p[2] - br.z0)); yN[s] = p[1];
	}
	const float W7 = kBustW[BUST_N - 1];
	const float rao = .48f * W7 * 1.4f, rai = .42f * W7 * 1.4f, rbu = fminf(.5f * W7, .08f) * 1.4f, rbd = .4f * W7 * 1.4f;
	int added = 0;
	float L = 0.007f;
	for (int lev = 0; ok && lev < 2; lev++) {
		const int n = V.size();
		// 体と服は別々に、同じ位置の頂点を 0.1mm で一つにまとめる。
		root.resize(n); inR.resize(n);
		if (root.size() < n || inR.size() < n) return FALSE;
		{
			int cap = 1024;
			while (cap < n * 2) cap *= 2;
			static Buf<unsigned long long> hk;
			static Buf<int> hv;
			hk.fill(cap, ~0ull);
			hv.resize(cap);
			if (hk.size() < cap || hv.size() < cap) return FALSE;
			for (int k = 0; k < n; k++) {
				unsigned long long key = part[k];
				for (int a = 0; a < 3; a++) {
					float f = V[k].p[a] * 1e4f;
					long long q = (long long)(f < 0.f ? f - .5f : f + .5f) + (1 << 20);
					key = (key << 21) | ((unsigned long long)q & 0x1FFFFF);
				}
				unsigned h = (unsigned)((key * 11400714819323198485ull) >> 32);
				for (int s = 0; s < cap; s++) {
					int slot = (int)((h + (unsigned)s) & (unsigned)(cap - 1));
					if (hk[slot] == ~0ull) { hk[slot] = key; hv[slot] = k; root[k] = k; break; }
					if (hk[slot] == key) { root[k] = hv[slot]; break; }
				}
			}
		}
		for (int k = 0; k < n; k++) {
			const float* p = V[k].p;
			const float th = atan2f(p[0], br.z0 - p[2]);
			inR[k] = 0;
			if (fabsf(th) >= 1.7f) continue;
			for (int s = 0; s < 2; s++) {
				const float u = (th - thN[s]) * (s ? 1.f : -1.f) * rhN[s], v = p[1] - yN[s];
				const float a = u > 0.f ? rao : rai, b = v > 0.f ? rbu : rbd;
				if ((u / a) * (u / a) + (v / b) * (v / b) < 1.f) { inR[k] = 1; break; }
			}
		}
		const int nT = T.size() / 3;
		for (;;) {
			M.clear();
			for (int i = 0; i < nT; i++) {
				const int t = Tt[i];
				if (t >= 0 && !(t / 8 < keep.size() && ((keep[t / 8] >> (t & 7)) & 1))) continue;
				const int* q = &T[i * 3];
				if (!inR[q[0]] && !inR[q[1]] && !inR[q[2]]) continue;
				for (int e = 0; e < 3; e++) {
					const float* A = V[q[e]].p; const float* B = V[q[(e + 1) % 3]].p;
					const float dx = A[0] - B[0], dy = A[1] - B[1], dz = A[2] - B[2];
					if (dx * dx + dy * dy + dz * dz > L * L) M.push_back(EdgeKey(root[q[e]], root[q[(e + 1) % 3]]));
				}
			}
			int nM = 0;
			if (M.size() > 1) qsort(M.data(), M.size(), sizeof(unsigned long long), CmpU64);
			for (int i = 0; i < M.size(); i++) if (nM == 0 || M[i] != M[nM - 1]) M[nM++] = M[i];
			M.resize(nM);
			// 2 辺に印がある三角形は 3 辺目にも印を付ける。増えなくなるまで。
			for (;;) {
				add.clear();
				for (int i = 0; i < nT; i++) {
					const int* q = &T[i * 3];
					unsigned long long k3[3];
					int c = 0, miss = -1;
					for (int e = 0; e < 3; e++) {
						k3[e] = EdgeKey(root[q[e]], root[q[(e + 1) % 3]]);
						if (FindU64(M, k3[e]) >= 0) c++; else miss = e;
					}
					if (c == 2) add.push_back(k3[miss]);
				}
				if (add.empty()) break;
				for (int i = 0; i < add.size(); i++) M.push_back(add[i]);
				nM = 0;
				qsort(M.data(), M.size(), sizeof(unsigned long long), CmpU64);
				for (int i = 0; i < M.size(); i++) if (nM == 0 || M[i] != M[nM - 1]) M[nM++] = M[i];
				M.resize(nM);
			}
			// 割る辺の生の頂点の組（継ぎ目の両側は別の頂点になる）。
			R.clear();
			for (int i = 0; i < nT; i++) {
				const int* q = &T[i * 3];
				for (int e = 0; e < 3; e++)
					if (FindU64(M, EdgeKey(root[q[e]], root[q[(e + 1) % 3]])) >= 0) R.push_back(EdgeKey(q[e], q[(e + 1) % 3]));
			}
			int nR = 0;
			if (R.size() > 1) qsort(R.data(), R.size(), sizeof(unsigned long long), CmpU64);
			for (int i = 0; i < R.size(); i++) if (nR == 0 || R[i] != R[nR - 1]) R[nR++] = R[i];
			R.resize(nR);
			if (!nR || added + nR <= maxAdd) break;
			L *= 1.25f;
		}
		if (R.empty()) break;
		const int n0 = V.size(), nR = R.size();
		V.resize(n0 + nR); C.resize(n0 + nR); part.resize(n0 + nR); DX.resize((n0 + nR) * 3); DN.resize((n0 + nR) * 3);
		if (V.size() < n0 + nR || C.size() < n0 + nR || part.size() < n0 + nR || DX.size() < (n0 + nR) * 3 || DN.size() < (n0 + nR) * 3) return FALSE;
		for (int i = 0; i < nR; i++) {
			const int a = (int)(R[i] >> 32), b = (int)(R[i] & 0xFFFFFFFFu);
			const SV& A = V[a]; const SV& B = V[b];
			SV& o = V[n0 + i];
			for (int c = 0; c < 3; c++) { o.p[c] = (A.p[c] + B.p[c]) * .5f; o.n[c] = A.n[c] + B.n[c]; }
			const float l = sqrtf(o.n[0] * o.n[0] + o.n[1] * o.n[1] + o.n[2] * o.n[2]);
			for (int c = 0; c < 3; c++) o.n[c] = l > 1e-6f ? o.n[c] / l : A.n[c];
			o.c = 0;
			for (int s = 0; s < 32; s += 8) o.c |= (DWORD)(((((A.c >> s) & 255) + ((B.c >> s) & 255) + 1) >> 1) << s);
			int jj[8], ww[8], m = 0;
			for (int pass = 0; pass < 2; pass++) {
				const SV& S = pass ? B : A;
				for (int c = 0; c < 4; c++) {
					if (!S.w[c]) continue;
					int f = -1;
					for (int q = 0; q < m; q++) if (jj[q] == S.j[c]) f = q;
					if (f < 0) { jj[m] = S.j[c]; ww[m] = 0; f = m++; }
					ww[f] += S.w[c];
				}
			}
			for (int x = 0; x < m; x++) for (int y = x + 1; y < m; y++) if (ww[y] > ww[x]) { int t = ww[x]; ww[x] = ww[y]; ww[y] = t; t = jj[x]; jj[x] = jj[y]; jj[y] = t; }
			const int kn = m < 4 ? m : 4;
			int sum = 0, tot = 0;
			for (int c = 0; c < kn; c++) sum += ww[c];
			for (int c = 0; c < 4; c++) {
				if (c < kn && sum > 0) { o.j[c] = (unsigned char)jj[c]; o.w[c] = (unsigned char)((ww[c] * 255 + sum / 2) / sum); tot += o.w[c]; }
				else { o.j[c] = kn ? (unsigned char)jj[0] : A.j[c]; o.w[c] = kn ? 0 : A.w[c]; }
			}
			if (kn && sum > 0) o.w[0] = (unsigned char)(o.w[0] + 255 - tot);
			C[n0 + i] = C[a];
			part[n0 + i] = part[a];
			for (int c = 0; c < 3; c++) {
				DX[(n0 + i) * 3 + c] = (DX[a * 3 + c] + DX[b * 3 + c]) * .5f;
				DN[(n0 + i) * 3 + c] = (DN[a * 3 + c] + DN[b * 3 + c]) * .5f;
			}
		}
		added += nR;
		T2.clear(); Tt2.clear();
		auto mid = [&](int a, int b) { return n0 + FindU64(R, EdgeKey(a, b)); };
		for (int i = 0; i < nT; i++) {
			const int q[3] = { T[i * 3], T[i * 3 + 1], T[i * 3 + 2] };
			int mask = 0;
			for (int e = 0; e < 3; e++) if (FindU64(M, EdgeKey(root[q[e]], root[q[(e + 1) % 3]])) >= 0) mask |= 1 << e;
			if (!mask) {
				for (int e = 0; e < 3; e++) T2.push_back(q[e]);
				Tt2.push_back(Tt[i]);
			} else if (mask == 7) {
				const int ab = mid(q[0], q[1]), bc = mid(q[1], q[2]), ca = mid(q[2], q[0]);
				const int ch[12] = { q[0], ab, ca, q[1], bc, ab, q[2], ca, bc, ab, bc, ca };
				for (int e = 0; e < 12; e++) T2.push_back(ch[e]);
				for (int e = 0; e < 4; e++) Tt2.push_back(Tt[i]);
			} else {
				const int e = mask == 1 ? 0 : (mask == 2 ? 1 : 2);
				const int p = q[e], r = q[(e + 1) % 3], o = q[(e + 2) % 3], mm = mid(p, r);
				const int ch[6] = { p, mm, o, mm, r, o };
				for (int c = 0; c < 6; c++) T2.push_back(ch[c]);
				Tt2.push_back(Tt[i]); Tt2.push_back(Tt[i]);
			}
		}
		if (T2.size() != Tt2.size() * 3) return FALSE;
		T.swap(T2); Tt.swap(Tt2);
	}
	// [体][体の新][服][服の新] へ並べ直す。
	const int n = V.size();
	int addB = 0;
	for (int k = nv; k < n; k++) if (!part[k]) addB++;
	nbA = nb + addB; nvA = n;
	remap.resize(n);
	if (remap.size() < n) return FALSE;
	{
		int ib = nb, ic = nbA + (nv - nb);
		for (int k = 0; k < n; k++) remap[k] = k < nb ? k : (k < nv ? k + addB : (part[k] ? ic++ : ib++));
	}
	sk.bind.resize(n); sk.cloth.resize(n);
	if (sk.bind.size() < n || sk.cloth.size() < n) return FALSE;
	if (hasD) {
		bx.resize(n * 3); bn.resize(n * 3);
		if (bx.size() < n * 3 || bn.size() < n * 3) return FALSE;
	}
	for (int k = 0; k < n; k++) {
		const int d = remap[k];
		sk.bind[d] = V[k];
		sk.cloth[d] = C[k];
		if (hasD) for (int a = 0; a < 3; a++) { bx[d * 3 + a] = DX[k * 3 + a]; bn[d * 3 + a] = DN[k * 3 + a]; }
	}
	const int nT = T.size() / 3;
	cnt.fill(nBT + 1, 0);
	bOff.resize(nBT + 1);
	if (cnt.size() < nBT + 1 || bOff.size() < nBT + 1) return FALSE;
	for (int i = 0; i < nT; i++) if (Tt[i] >= 0) cnt[Tt[i]]++;
	bOff[0] = 0;
	for (int t = 0; t < nBT; t++) bOff[t + 1] = bOff[t] + cnt[t];
	for (int t = 0; t < nBT; t++) cnt[t] = bOff[t];
	bTri.resize(bOff[nBT] * 3);
	cTri.clear();
	if (bTri.size() < bOff[nBT] * 3) return FALSE;
	for (int i = 0; i < nT; i++) {
		const int t = Tt[i];
		if (t >= 0) { const int c = cnt[t]++; for (int e = 0; e < 3; e++) bTri[c * 3 + e] = (WORD)remap[T[i * 3 + e]]; }
		else for (int e = 0; e < 3; e++) cTri.push_back((WORD)(remap[T[i * 3 + e]] - nbA));
	}
	return cTri.size() == (nT - bOff[nBT]) * 3;
}

// 胸 cup（0=A〜7=H）の形をバインドへ焼く。差分を押し出すのをやめ、形から作る。
// ・胴の縦軸まわりの円柱座標（角度・高さ）で胸の範囲を測る。幅は胴の面に沿った弧長なので、胸は胴の横へ回り込む。
// ・バインドの胸の下側は巻き込んでいて、そのまま平らに並べると裏返る三角形が出る。裏返った所（と 2 輪の隣）だけを
//   網でならして、折れない並び（X）を作る。
// ・胸壁: 胸の外の輪（H の土台の 1.15〜1.7 倍）の A の形（最小の差分）の半径へ 3 次の曲面を当て、内側をそれで置き換える。
// ・山: 上は丸い山を縁で滑らかに胸へ溶かし、下は乳房下の折れ目（大きいほど締まり、真下だけ）。乳輪と乳首を載せ、
//   垂れの分だけ下げる。押し出す向きは胴の外向きを正面へ寄せたもの。腕・肩・首から 3cm 以内は弱める。
// ・服 [nb, ne) は、近い体の頂点の胸壁の動きと並び X・腕からの重みを、半径 1.5cm のガウスで受け、その位置で山を作る。
// 法線は元の法線へ、形の面法線とバインドの面法線の差を足す。jig は山の分の差分（揺れの重み）。
static void ShapeCup(Renderer::CSkinV* v, int nv, int nb, int ne, const float* bn, int cup, const BustRef& br,
	const WORD* bodyTri, int nBodyTri, const BYTE* armJ, const WORD* idx, int nIdx, float* jig)
{
	if (nb <= 0 || nb > ne || ne > nv || !bn || br.kn[0] < 0 || br.kn[1] < 0 || br.kn[0] >= nb || br.kn[1] >= nb) return;
	cup = Clampi(cup, 0, BUST_N - 1);
	static Buf<float> P, S, Wl, X, X2, tw, D, U, L;
	static Buf<int> root;
	static Buf<BYTE> arm, in, mv, mv2;
	static Buf<unsigned long long> E;
	static Buf<int> Tu, cand;
	P.resize(nv * 3); S.resize(nb * 3); Wl.resize(nb * 3); X.resize(nb * 2); X2.resize(nb * 2); tw.resize(nb); D.resize(nv * 3);
	root.resize(nb); arm.resize(nb); in.resize(nb); mv.resize(nb); mv2.resize(nb);
	if (P.size() < nv * 3 || S.size() < nb * 3 || Wl.size() < nb * 3 || X.size() < nb * 2 || X2.size() < nb * 2 || tw.size() < nb || D.size() < nv * 3
		|| root.size() < nb || arm.size() < nb || in.size() < nb || mv.size() < nb || mv2.size() < nb) return;
	for (int k = 0; k < nv; k++) for (int a = 0; a < 3; a++) { P[k * 3 + a] = v[k].p[a] + bn[k * 3 + a]; D[k * 3 + a] = 0.f; }
	for (int k = 0; k < nb * 3; k++) S[k] = Wl[k] = P[k];
	// 継ぎ目で同じ位置の頂点を 0.1mm で一つにまとめる。
	{
		int cap = 1024;
		while (cap < nb * 2) cap *= 2;
		static Buf<unsigned long long> hk;
		static Buf<int> hv;
		hk.fill(cap, ~0ull);
		hv.resize(cap);
		if (hk.size() < cap || hv.size() < cap) return;
		for (int k = 0; k < nb; k++) {
			unsigned long long key = 0;
			for (int a = 0; a < 3; a++) {
				float f = v[k].p[a] * 1e4f;
				long long q = (long long)(f < 0.f ? f - .5f : f + .5f) + (1 << 20);
				key = (key << 21) | ((unsigned long long)q & 0x1FFFFF);
			}
			unsigned h = (unsigned)((key * 11400714819323198485ull) >> 32);
			for (int s = 0; s < cap; s++) {
				int slot = (int)((h + (unsigned)s) & (unsigned)(cap - 1));
				if (hk[slot] == ~0ull) { hk[slot] = key; hv[slot] = k; root[k] = k; break; }
				if (hk[slot] == key) { root[k] = hv[slot]; break; }
			}
		}
	}
	const float z0 = br.z0;
	const float rho0 = sqrtf(v[br.kn[1]].p[0] * v[br.kn[1]].p[0] + (v[br.kn[1]].p[2] - z0) * (v[br.kn[1]].p[2] - z0));
	if (rho0 < 0.02f) return;
	for (int k = 0; k < nb; k++) {
		const Renderer::CSkinV& q = v[k];
		int m = 0;
		for (int c = 1; c < 4; c++) if (q.w[c] > q.w[m]) m = c;
		arm[k] = armJ[q.j[m]] ? 1 : 0;
		X[k * 2] = atan2f(q.p[0], z0 - q.p[2]) * rho0;
		X[k * 2 + 1] = q.p[1];
	}
	// 乳首の角度・高さ・半径。r は H の土台を k 倍した楕円での距離（1 で縁）。
	float thN[2], yN[2], rhN[2];
	for (int s = 0; s < 2; s++) {
		const float* p = v[br.kn[s]].p;
		thN[s] = X[br.kn[s] * 2] / rho0; yN[s] = p[1];
		rhN[s] = sqrtf(p[0] * p[0] + (p[2] - z0) * (p[2] - z0));
	}
	const float W7 = kBustW[BUST_N - 1];
	auto rad = [&](float th, float y, int s, float W, float kk, float* uo, float* vo) {
		const float u = (th - thN[s]) * (s ? 1.f : -1.f) * rhN[s], vv = y - yN[s];
		const float a = (u > 0.f ? .48f * W : .42f * W) * kk, b = (vv > 0.f ? fminf(.5f * W, .08f) : .4f * W) * kk;
		if (uo) *uo = u;
		if (vo) *vo = vv;
		return sqrtf((u / a) * (u / a) + (vv / b) * (vv / b));
	};
	// 裏返らない並び。胸の範囲（H の 1.3 倍）の溶接した三角形の向きを数え、多い向きと逆の三角形の頂点と 2 輪をならす。
	for (int k = 0; k < nb; k++) {
		const float th = X[k * 2] / rho0;
		in[k] = root[k] == k && fabsf(th) < 1.7f && (rad(th, X[k * 2 + 1], 0, W7, 1.3f, 0, 0) < 1.f || rad(th, X[k * 2 + 1], 1, W7, 1.3f, 0, 0) < 1.f);
	}
	Tu.clear();
	for (int t = 0; t < nBodyTri; t++) {
		int q[3];
		for (int e = 0; e < 3; e++) q[e] = bodyTri[t * 3 + e] < nb ? root[bodyTri[t * 3 + e]] : -1;
		if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[0] == q[1] || q[1] == q[2] || q[2] == q[0] || !in[q[0]] || !in[q[1]] || !in[q[2]]) continue;
		for (int e = 0; e < 3; e++) Tu.push_back(q[e]);
	}
	E.clear();
	for (int i = 0; i + 2 < Tu.size(); i += 3)
		for (int e = 0; e < 3; e++) for (int f = 0; f < 3; f++) if (e != f) E.push_back(((unsigned long long)(unsigned)Tu[i + e] << 32) | (unsigned)Tu[i + f]);
	int nE = 0;
	if (E.size() > 1) qsort(E.data(), E.size(), sizeof(unsigned long long), CmpU64);
	for (int i = 0; i < E.size(); i++) if (nE == 0 || E[i] != E[nE - 1]) E[nE++] = E[i];
	E.resize(nE);
	auto area = [&](int i) {
		const float* a = &X[Tu[i] * 2]; const float* b = &X[Tu[i + 1] * 2]; const float* c = &X[Tu[i + 2] * 2];
		return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
	};
	int pos = 0, neg = 0;
	for (int i = 0; i + 2 < Tu.size(); i += 3) { const float ar = area(i); if (ar > 0.f) pos++; else if (ar < 0.f) neg++; }
	const float sgn = pos >= neg ? 1.f : -1.f;
	for (int it = 0; it < 60; it++) {
		for (int k = 0; k < nb; k++) mv[k] = 0;
		BOOL bad = FALSE;
		for (int i = 0; i + 2 < Tu.size(); i += 3) if (area(i) * sgn <= 0.f) { mv[Tu[i]] = mv[Tu[i + 1]] = mv[Tu[i + 2]] = 1; bad = TRUE; }
		if (!bad) break;
		for (int ring = 0; ring < 2; ring++) {
			for (int k = 0; k < nb; k++) mv2[k] = mv[k];
			for (int i = 0; i < nE; i++) if (mv2[(int)(E[i] >> 32)]) mv[(int)(E[i] & 0xFFFFFFFFu)] = 1;
		}
		for (int k = 0; k < nb; k++) if (!in[k]) mv[k] = 0;
		for (int j = 0; j < 10; j++) {
			for (int k = 0; k < nb; k++) if (mv[k]) { X2[k * 2] = X[k * 2]; X2[k * 2 + 1] = X[k * 2 + 1]; }
			for (int i = 0; i < nE;) {
				const int s = (int)(E[i] >> 32);
				float ax = 0.f, ay = 0.f; int c = 0;
				for (; i < nE && (int)(E[i] >> 32) == s; i++) { const int d = (int)(E[i] & 0xFFFFFFFFu); ax += X[d * 2]; ay += X[d * 2 + 1]; c++; }
				if (mv[s] && c) { X2[s * 2] = ax / c; X2[s * 2 + 1] = ay / c; }
			}
			for (int k = 0; k < nb; k++) if (mv[k]) { X[k * 2] = X2[k * 2]; X[k * 2 + 1] = X2[k * 2 + 1]; }
		}
	}
	for (int k = 0; k < nb; k++) { X[k * 2] = X[root[k] * 2]; X[k * 2 + 1] = X[root[k] * 2 + 1]; }
	for (int s = 0; s < 2; s++) { thN[s] = X[br.kn[s] * 2] / rho0; yN[s] = X[br.kn[s] * 2 + 1]; }
	// 腕らしさ: 腕・肩・首の頂点からの距離で 0→1（3cm）。胸の近く（H の 1.45 倍）だけ測る。
	{
		float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
		for (int k = 0; k < nb; k++) {
			const float th = X[k * 2] / rho0;
			in[k] = root[k] == k && fabsf(th) < 1.8f && (rad(th, X[k * 2 + 1], 0, W7, 1.45f, 0, 0) < 1.f || rad(th, X[k * 2 + 1], 1, W7, 1.45f, 0, 0) < 1.f);
			tw[k] = 0.f;
			if (in[k]) for (int a = 0; a < 3; a++) { lo[a] = fminf(lo[a], v[k].p[a]); hi[a] = fmaxf(hi[a], v[k].p[a]); }
		}
		cand.clear();
		for (int k = 0; k < nb; k++) {
			const float* p = v[k].p;
			if (arm[k] && root[k] == k && p[0] > lo[0] - .03f && p[0] < hi[0] + .03f && p[1] > lo[1] - .03f && p[1] < hi[1] + .03f && p[2] > lo[2] - .03f && p[2] < hi[2] + .03f) cand.push_back(k);
		}
		for (int k = 0; k < nb; k++) {
			if (!in[k]) continue;
			if (arm[k]) continue;
			float d2 = .03f * .03f;
			for (int i = 0; i < cand.size(); i++) {
				const float* q = v[cand[i]].p;
				const float dx = q[0] - v[k].p[0], dy = q[1] - v[k].p[1], dz = q[2] - v[k].p[2];
				d2 = fminf(d2, dx * dx + dy * dy + dz * dz);
			}
			tw[k] = Smooth01(sqrtf(d2) / .03f);
		}
		for (int k = 0; k < nb; k++) tw[k] = tw[root[k]];
	}
	// 胸壁。左右ごとに 3 次の曲面 rW(u, v) を当て、内側（H の 1.35 倍まで）をそれへ寄せる。
	for (int s = 0; s < 2; s++) {
		double A[10][11] = {};
		int nr = 0;
		for (int k = 0; k < nb; k++) {
			const float th = X[k * 2] / rho0;
			if (fabsf(th) >= 1.6f || arm[k] || th * (s ? 1.f : -1.f) <= 0.f) continue;
			float u, vv;
			const float r = rad(th, X[k * 2 + 1], s, W7, 1.f, &u, &vv);
			if (r <= 1.15f || r >= 1.7f) continue;
			const double f[10] = { 1., u, vv, (double)u * u, (double)u * vv, (double)vv * vv, (double)u * u * u, (double)u * u * vv, (double)u * vv * vv, (double)vv * vv * vv };
			const float* p = &S[k * 3];
			const double rA = sqrt((double)p[0] * p[0] + ((double)p[2] - z0) * ((double)p[2] - z0));
			for (int i = 0; i < 10; i++) { for (int j = 0; j < 10; j++) A[i][j] += f[i] * f[j]; A[i][10] += f[i] * rA; }
			nr++;
		}
		if (nr < 20) continue;
		for (int i = 0; i < 10; i++) {
			int pv = i;
			for (int r = i + 1; r < 10; r++) if (fabs(A[r][i]) > fabs(A[pv][i])) pv = r;
			if (fabs(A[pv][i]) < 1e-30) { nr = 0; break; }
			if (pv != i) for (int c = 0; c < 11; c++) { const double t = A[i][c]; A[i][c] = A[pv][c]; A[pv][c] = t; }
			for (int r = 0; r < 10; r++) if (r != i) {
				const double f = A[r][i] / A[i][i];
				for (int c = i; c < 11; c++) A[r][c] -= f * A[i][c];
			}
		}
		if (!nr) continue;
		double cf[10];
		for (int i = 0; i < 10; i++) cf[i] = A[i][10] / A[i][i];
		for (int k = 0; k < nb; k++) {
			const float th = X[k * 2] / rho0;
			float u, vv;
			const float r = rad(th, X[k * 2 + 1], s, W7, 1.f, &u, &vv);
			const float wf = Smooth01((1.35f - r) / .35f) * Smooth01(th * (s ? 1.f : -1.f) * rhN[s] / .012f) * tw[k];
			if (wf <= 0.f) continue;
			const float rW = (float)(cf[0] + cf[1] * u + cf[2] * vv + cf[3] * u * u + cf[4] * u * vv + cf[5] * vv * vv
				+ cf[6] * u * u * u + cf[7] * u * u * vv + cf[8] * u * vv * vv + cf[9] * vv * vv * vv);
			const float tg[3] = { rW * sinf(th), X[k * 2 + 1], z0 - rW * cosf(th) };
			for (int a = 0; a < 3; a++) Wl[k * 3 + a] += wf * (tg[a] - Wl[k * 3 + a]);
		}
	}
	for (int k = 0; k < nb; k++) for (int a = 0; a < 3; a++) Wl[k * 3 + a] = Wl[root[k] * 3 + a];
	// 山。上は丸い山を縁で滑らかに溶かし、下の折れ目は真下だけ締める。
	const float W = kBustW[cup], PP = kBustP[cup] * W, sag = kBustSag[cup];
	const float edc = .08f + .37f * (1.f - (float)cup / 7.f), ar = .017f + .005f * (float)cup / 7.f;
	auto mound = [&](float x0, float y, float t, float* d) {
		d[0] = d[1] = d[2] = 0.f;
		if (t <= 0.f) return;
		const float th = x0 / rho0;
		float best = 0.f;
		for (int s = 0; s < 2; s++) {
			float u, vv;
			const float r = rad(th, y, s, W, 1.f, &u, &vv);
			if (r >= 1.f) continue;
			const float b = vv > 0.f ? fminf(.5f * W, .08f) : .4f * W;
			const float tt = Smooth01(vv / b * 1.5f + .5f);
			float down = Clampf(-vv / b, 0.f, 1.f);
			down = fminf(down * down / fmaxf(r * r, 1e-9f), 1.f);
			const float ed = .45f + (edc - .45f) * down;
			const float fu = powf(1.f - r * r, .75f) * Smooth01((1.f - r) / .45f);
			const float fd = (1.f - powf(r, 2.4f)) * Smooth01((1.f - r) / ed);
			float h = PP * (fd + (fu - fd) * tt);
			if (h > 0.f) {
				// 乳輪（2mm）と乳首（5mm）。縁はなだらかに
				const float rr = sqrtf(u * u + vv * vv);
				const float qa = fmaxf(1.f - (rr / ar) * (rr / ar), 0.f), qn = fmaxf(1.f - (rr / .006f) * (rr / .006f), 0.f);
				h += .002f * qa * qa + .005f * qn * sqrtf(qn);
			}
			h *= t;
			if (h <= best) continue;
			best = h;
			const float td = th * .3f;
			d[0] = sinf(td) * h; d[1] = -sag * h; d[2] = -cosf(td) * h;
		}
	};
	for (int k = 0; k < nb; k++) {
		mound(X[k * 2], X[k * 2 + 1], tw[k], &D[k * 3]);
		for (int a = 0; a < 3; a++) P[k * 3 + a] = Wl[k * 3 + a] + D[k * 3 + a];
	}
	if (ne > nb) {
		// 服は、近い体の頂点（胸の近く）の胸壁の動き・並び・腕からの重みを半径 1.5cm のガウスで平均し、その位置で山を作る。
		const float TR = 0.015f, inv2 = 1.f / (2.f * TR * TR), cut = 25.f * TR * TR;
		float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
		cand.clear();
		for (int k = 0; k < nb; k++) if (in[k]) {
			cand.push_back(k);
			for (int a = 0; a < 3; a++) { lo[a] = fminf(lo[a], S[k * 3 + a]); hi[a] = fmaxf(hi[a], S[k * 3 + a]); }
		}
		for (int a = 0; a < 3; a++) { lo[a] -= 3.f * TR; hi[a] += 3.f * TR; }
		for (int k = nb; k < ne; k++) {
			const float* c = &P[k * 3];
			if (!(c[0] > lo[0] && c[0] < hi[0] && c[1] > lo[1] && c[1] < hi[1] && c[2] > lo[2] && c[2] < hi[2])) continue;
			float sw = 0.f, w3[3] = {}, x0 = 0.f, y0 = 0.f, t0 = 0.f;
			for (int i = 0; i < cand.size(); i++) {
				const int q = cand[i];
				const float dx = S[q * 3] - c[0], dy = S[q * 3 + 1] - c[1], dz = S[q * 3 + 2] - c[2];
				const float d2 = dx * dx + dy * dy + dz * dz;
				if (d2 > cut) continue;
				const float g = expf(-d2 * inv2);
				sw += g;
				for (int a = 0; a < 3; a++) w3[a] += g * (Wl[q * 3 + a] - S[q * 3 + a]);
				x0 += g * X[q * 2]; y0 += g * X[q * 2 + 1]; t0 += g * tw[q];
			}
			if (sw < 1e-6f) continue;
			mound(x0 / sw, y0 / sw, t0 / sw, &D[k * 3]);
			for (int a = 0; a < 3; a++) P[k * 3 + a] = c[a] + w3[a] / sw + D[k * 3 + a];
		}
	}
	// 面法線をバインドと形で積み、差を元の法線へ足す。
	U.resize(nv * 3); L.resize(nv * 3);
	if (U.size() < nv * 3 || L.size() < nv * 3) return;
	for (int k = 0; k < nv * 3; k++) U[k] = L[k] = 0.f;
	for (int t = 0; t + 2 < nIdx; t += 3) {
		const int id[3] = { idx[t], idx[t + 1], idx[t + 2] };
		if (id[0] >= nv || id[1] >= nv || id[2] >= nv) continue;
		for (int pass = 0; pass < 2; pass++) {
			const float* A = pass ? &P[id[0] * 3] : v[id[0]].p;
			const float* B = pass ? &P[id[1] * 3] : v[id[1]].p;
			const float* C = pass ? &P[id[2] * 3] : v[id[2]].p;
			float e1x = B[0] - A[0], e1y = B[1] - A[1], e1z = B[2] - A[2];
			float e2x = C[0] - A[0], e2y = C[1] - A[1], e2z = C[2] - A[2];
			float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
			Buf<float>& acc = pass ? L : U;
			for (int e = 0; e < 3; e++) { acc[id[e] * 3] += nx; acc[id[e] * 3 + 1] += ny; acc[id[e] * 3 + 2] += nz; }
		}
	}
	// 体は継ぎ目を溶接して形の面法線を積む（継ぎ目で陰が割れない）。
	for (int k = 0; k < nb; k++) if (root[k] != k) for (int a = 0; a < 3; a++) L[root[k] * 3 + a] += L[k * 3 + a];
	for (int k = 0; k < nb; k++) if (root[k] != k) for (int a = 0; a < 3; a++) L[k * 3 + a] = L[root[k] * 3 + a];
	for (int k = 0; k < nv; k++) {
		float lb = sqrtf(U[k * 3] * U[k * 3] + U[k * 3 + 1] * U[k * 3 + 1] + U[k * 3 + 2] * U[k * 3 + 2]);
		float lf = sqrtf(L[k * 3] * L[k * 3] + L[k * 3 + 1] * L[k * 3 + 1] + L[k * 3 + 2] * L[k * 3 + 2]);
		const float dx = P[k * 3] - v[k].p[0], dy = P[k * 3 + 1] - v[k].p[1], dz = P[k * 3 + 2] - v[k].p[2];
		// 1mm 以上動いた所は形そのものの法線。元の法線へ差を足すと、大きく動いた胸で筋が出る。
		if (lf > 1e-12f && dx * dx + dy * dy + dz * dz > 1e-6f) {
			for (int a = 0; a < 3; a++) v[k].n[a] = L[k * 3 + a] / lf;
		} else if (lb > 1e-12f && lf > 1e-12f) {
			float n[3];
			for (int a = 0; a < 3; a++) n[a] = v[k].n[a] + L[k * 3 + a] / lf - U[k * 3 + a] / lb;
			float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
			if (l > 1e-6f) for (int a = 0; a < 3; a++) v[k].n[a] = n[a] / l;
		}
		for (int a = 0; a < 3; a++) {
			jig[k * 3 + a] = D[k * 3 + a];
			v[k].p[a] = P[k * 3 + a];
		}
	}
}

// 衣装の背中側（+Z、胴の中央）の外面を高さで残す。袖と腹側は入れない。
static void FillClothBack(Renderer::AvaCache& nc, const Renderer::CreatureSkin& sk, size_t i0, size_t i1)
{
	nc.backN = 0;
	if (i1 <= i0) return;
	float y0 = 1e9f, y1 = -1e9f;
	for (size_t k = i0; k < i1 && k < sk.bind.size(); k++) {
		if (k >= sk.cloth.size() || !sk.cloth[k]) continue;
		float y = sk.bind[k].p[1];
		if (y < y0) y0 = y;
		if (y > y1) y1 = y;
	}
	if (!(y1 > y0 + 0.05f)) return;
	const int N = 32;
	float z[N];
	int cnt[N];
	for (int i = 0; i < N; i++) { z[i] = -1e9f; cnt[i] = 0; }
	float dy = (y1 - y0) / (float)(N - 1);
	for (size_t k = i0; k < i1 && k < sk.bind.size(); k++) {
		if (k >= sk.cloth.size() || !sk.cloth[k]) continue;
		float x = sk.bind[k].p[0], y = sk.bind[k].p[1], zz = sk.bind[k].p[2];
		if (fabsf(x) > 0.18f || zz < -0.02f) continue;
		int b = (int)((y - y0) / dy + 0.5f);
		if (b < 0) b = 0;
		if (b >= N) b = N - 1;
		if (zz > z[b]) z[b] = zz;
		cnt[b]++;
	}
	for (int i = 0; i < N; i++) if (!cnt[i]) {
		int L = i - 1, R = i + 1;
		while (L >= 0 && !cnt[L]) L--;
		while (R < N && !cnt[R]) R++;
		if (L >= 0 && R < N) z[i] = z[L] + (z[R] - z[L]) * ((i - L) / (float)(R - L));
		else if (L >= 0) z[i] = z[L];
		else if (R < N) z[i] = z[R];
	}
	int any = 0;
	for (int i = 0; i < N; i++) if (cnt[i]) any = 1;
	if (!any) return;
	for (int i = 1; i < N - 1; i++) {
		float env = z[i - 1] > z[i + 1] ? z[i - 1] : z[i + 1];
		if (z[i] < env - 0.03f) z[i] = env;
	}
	nc.backY0 = y0;
	nc.backDy = dy;
	nc.backN = N;
	for (int i = 0; i < N; i++) nc.backZ[i] = z[i];
}

// ---------------------------------------------------------------- 衣装の布
// omake_tactics_cloth.hlsl の ClothN・ClothV と同じ並び。
struct ClothGpuN { float x[4]; float p[4]; unsigned nb[4]; unsigned j[4]; float w[4]; float q[4]; };
struct ClothGpuV { unsigned a[4]; float w[4]; float p[4]; float n[4]; };
static_assert(sizeof(ClothGpuN) == 96 && sizeof(ClothGpuV) == 64, "ClothGpu");

// 脚の形の半径（PT 段 × PF 分割）を t（骨に沿った割合 -0.15〜1.15）と φ（前から横への角度）で引く。シェーダの Prof と同じ。
static float ClothProf(const float* P, float t, float phi)
{
	enum { PT = Renderer::CLOTH_PT, PF = Renderer::CLOTH_PF };
	const float ft = Clampf((t + 0.15f) / 1.3f, 0.f, 1.f) * (float)(PT - 1);
	int t0 = (int)ft;
	if (t0 > PT - 2) t0 = PT - 2;
	const float a = ft - (float)t0;
	const float fp = (phi / 6.2831853f + 0.5f) * (float)PF;
	const float fl = floorf(fp);
	const float b = fp - fl;
	const int p0 = (((int)fl % PF) + PF) % PF, p1 = (p0 + 1) % PF;
	const float r0 = P[t0 * PF + p0] + (P[t0 * PF + p1] - P[t0 * PF + p0]) * b;
	const float r1 = P[(t0 + 1) * PF + p0] + (P[(t0 + 1) * PF + p1] - P[(t0 + 1) * PF + p0]) * b;
	return r0 + (r1 - r0) * a;
}

static float SegDist(const float* p, const float* a, const float* b, float* tOut = NULL)
{
	float ab[3], ap[3];
	for (int i = 0; i < 3; i++) { ab[i] = b[i] - a[i]; ap[i] = p[i] - a[i]; }
	float L2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
	float t = L2 > 1e-10f ? Clampf((ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / L2, 0.f, 1.f) : 0.f;
	if (tOut) *tOut = t;
	float d2 = 0.f;
	for (int i = 0; i < 3; i++) { float d = ap[i] - ab[i] * t; d2 += d * d; }
	return sqrtf(d2);
}

static int DomJoint(const Renderer::CSkinV& s)
{
	int best = 0, bw = -1;
	for (int c = 0; c < 4; c++) if ((int)s.w[c] > bw) { bw = (int)s.w[c]; best = s.j[c]; }
	return best;
}

static int UfFind(Buf<int>& p, int x)
{
	while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
	return x;
}

// 衣装 [nb, nv) の布の決まりを作る。バインドを書き換えるので、GPU へ載せる前に呼ぶ。
// ・胸の頂から腰へ垂れる線より奥の布は前へ出し、胸の下に隙間を作る（jig があるとき）。
// ・股の中央を通って裾まで続く部品（ドレス・スカート・着物）は、腰を囲む細かい格子で動かす。頂点は近い 4 点の
//   ずれを混ぜて受けるので、布はいつもなめらか。脚に近い点は脚に付いていき、離れた点（広がった裾）は腰に付いて揺れる。
//   最後に頂点を脚の形（prof）の外へ出すので、布は体の線より内へ入らない。
// ・スカートの下で消してあった脚（素体）を戻す印を expose に付ける。1=脚だが他の服がぴったり覆う 2=見えてよい。
static void PrepCloth(Renderer::AvaCache& nc, int nb, int nv, const Buf<WORD>& cosTri, const int* segJ, const char (*segName)[24], int ns,
	const Model& md, float* jig, Buf<ClothGpuN>& nodes, Buf<ClothGpuV>& verts, Buf<float>& prof, Buf<BYTE>& expose)
{
	nodes.clear();
	verts.clear();
	prof.clear();
	expose.clear();
	nc.clothN = nc.clothVN = 0;
	nc.clothHip = -1;
	nc.segN = 0;
	Renderer::CreatureSkin& sk = nc.skin;
	Renderer::CSkinV* V = sk.bind.data();
	const int n = nv - nb;
	if (n < 16 || nb <= 0 || nv > (int)sk.bind.size() || !V) return;
	auto jointOf = [&](const char* name) -> int {
		for (int s = 0; s < ns; s++) if (!strcmp(segName[s], name)) return segJ[s];
		return -1;
	};
	auto bindPos = [&](int j, float* o) -> BOOL {
		if (j < 0 || j >= md.nJ) return FALSE;
		M4 b = MInverse(md.invBind[j]);
		o[0] = b.m[12]; o[1] = b.m[13]; o[2] = b.m[14];
		return TRUE;
	};
	const int jHips = jointOf("Hips");
	const int jUL[2] = { jointOf("UpperLeg.L"), jointOf("UpperLeg.R") };
	const int jLL[2] = { jointOf("LowerLeg.L"), jointOf("LowerLeg.R") };
	const int jFt[2] = { jointOf("Foot.L"), jointOf("Foot.R") };
	float pUL[2][3], pLL[2][3], pFt[2][3];
	if (jHips < 0) return;
	for (int s = 0; s < 2; s++) {
		if (jUL[s] == jHips || jLL[s] == jHips || jFt[s] == jHips) return;
		if (!bindPos(jUL[s], pUL[s]) || !bindPos(jLL[s], pLL[s]) || !bindPos(jFt[s], pFt[s])) return;
	}
	const float hipY = (pUL[0][1] + pUL[1][1]) * 0.5f, cx = (pUL[0][0] + pUL[1][0]) * 0.5f;
	if (!(hipY > 0.3f && hipY < 2.5f)) return;

	Buf<int> root, par;
	Buf<float> moved, oldZ;
	root.arena = par.arena = moved.arena = oldZ.arena = 1;
	root.resize(n); moved.resize(n); oldZ.resize(n);
	if (root.size() < n || moved.size() < n || oldZ.size() < n) return;
	// 継ぎ目で同じ位置の頂点は 0.1mm で一つにまとめ、代表だけを動かす。まとめないと継ぎ目が開く。
	{
		int cap = 1024;
		while (cap < n * 2) cap *= 2;
		Buf<unsigned long long> hk;
		Buf<int> hv;
		hk.arena = hv.arena = 1;
		hk.fill(cap, ~0ull);
		hv.resize(cap);
		if (hk.size() < cap || hv.size() < cap) return;
		for (int i = 0; i < n; i++) {
			const float* p = V[nb + i].p;
			unsigned long long key = 0;
			for (int a = 0; a < 3; a++) {
				float f = p[a] * 1e4f;
				long long q = (long long)(f < 0.f ? f - .5f : f + .5f) + (1 << 20);
				key = (key << 21) | ((unsigned long long)q & 0x1FFFFF);
			}
			unsigned h = (unsigned)((key * 11400714819323198485ull) >> 32);
			root[i] = i;
			for (int s = 0; s < cap; s++) {
				int slot = (int)((h + (unsigned)s) & (unsigned)(cap - 1));
				if (hk[slot] == ~0ull) { hk[slot] = key; hv[slot] = i; break; }
				if (hk[slot] == key) { root[i] = hv[slot]; break; }
			}
		}
	}
	for (int i = 0; i < n; i++) { moved[i] = 0.f; oldZ[i] = V[nb + i].p[2]; }

	// 胸の下。頂（素体の胸でいちばん前の点）から垂らした布は、下の体（お腹・腰）に触れる所までまっすぐ張る。
	// 縦の列ごとに、頂から下の布の前面へ引いた接線（前面をどこも横切らない一番前の線）より奥の布を前へ出す。
	// 谷間は左右の頂を結ぶ線で渡す。横と背中は触らない（法線が前か下を向く布だけ）。
	if (jig) {
		float apex[2][3] = {}, apexJ[2][3] = {};
		int ok[2] = { 0, 0 };
		for (int k = 0; k < nb; k++) {
			const float* J = &jig[k * 3];
			if (J[0] * J[0] + J[1] * J[1] + J[2] * J[2] < 0.004f * 0.004f) continue;
			const float* p = V[k].p;
			if (p[1] < hipY + 0.2f) continue;
			int s = p[0] < cx ? 0 : 1;
			if (!ok[s] || p[2] < apex[s][2]) {
				for (int a = 0; a < 3; a++) { apex[s][a] = p[a]; apexJ[s][a] = J[a]; }
				ok[s] = 1;
			}
		}
		if (ok[0] && ok[1] && apex[1][0] - apex[0][0] > 0.04f) {
			const float ya = (apex[0][1] + apex[1][1]) * 0.5f, yLow = hipY - 0.12f;
			const float zFront = apex[0][2] < apex[1][2] ? apex[0][2] : apex[1][2];
			auto apexZ = [&](float x, float* u) {
				if (x > apex[0][0] && x < apex[1][0]) {
					*u = (x - apex[0][0]) / (apex[1][0] - apex[0][0]);
					return apex[0][2] + (apex[1][2] - apex[0][2]) * *u;
				}
				*u = 0.f;
				return apex[x < cx ? 0 : 1][2];
			};
			// 列（横 1cm）ごとの前面の奥行き（縦 1cm ごと）。前か下を向く布だけ。
			enum { NBIN = 48 };
			const int NY = Clampi((int)((ya - yLow) / 0.01f) + 1, 2, 160);
			auto binOf = [&](float x) { return (int)floorf((x - cx) / 0.01f) + NBIN / 2; };
			Buf<float> fz;
			fz.arena = 1;
			fz.fill(NBIN * NY, 1e9f);
			float slope[NBIN], yTan[NBIN];
			for (int b = 0; b < NBIN; b++) { slope[b] = 0.f; yTan[b] = ya; }
			if (fz.size() == NBIN * NY && ya - yLow > 0.08f) {
				for (int i = 0; i < n; i++) {
					if (root[i] != i) continue;
					const float* p = V[nb + i].p;
					const float* nr = V[nb + i].n;
					if (p[1] > ya || p[1] < yLow || p[2] > zFront + 0.40f || -nr[2] - 0.5f * nr[1] < 0.2f) continue;
					const int b = binOf(p[0]), yb = (int)((ya - p[1]) / 0.01f);
					if (b < 0 || b >= NBIN || yb < 0 || yb >= NY) continue;
					float& z = fz[b * NY + yb];
					if (p[2] < z) z = p[2];
				}
				// 頂からの接線。頂より 3〜30cm 下で、傾き（下がるほど奥へ）がいちばん小さい所に触れる。
				// 30cm より下まで張るとお腹全体が覆われて妊婦のように見えるので、そこで打ち切る。
				float sl[NBIN];
				for (int b = 0; b < NBIN; b++) {
					float u;
					const float zx = apexZ(cx + ((float)(b - NBIN / 2) + 0.5f) * 0.01f, &u);
					float best = 1e9f, yb = ya;
					for (int k = 3; k < NY && k <= 30; k++) {
						const float z = fz[b * NY + k];
						if (z > 1e8f) continue;
						const float dy = (float)k * 0.01f;
						const float s = (z - zx) / dy;
						if (s < best) { best = s; yb = ya - dy; }
					}
					sl[b] = best < 1e8f ? best : 0.f;
					yTan[b] = best < 1e8f ? yb : ya;
				}
				// 列ごとの傾きの差で布に段が付かないよう、横にならす。
				for (int b = 0; b < NBIN; b++) {
					float a = 0.f, w = 0.f;
					for (int d = -2; d <= 2; d++) {
						const int c = b + d;
						if (c < 0 || c >= NBIN || yTan[c] >= ya) continue;
						a += sl[c]; w += 1.f;
					}
					slope[b] = w > 0.f ? a / w : 0.f;
				}
			}
			for (int i = 0; i < n; i++) {
				if (root[i] != i) continue;
				const float* p = V[nb + i].p;
				const float* nr = V[nb + i].n;
				// 大きい胸は頂が胸板より 15cm 以上前に出るので、奥行きは広く見る。背中の布は法線で外す。
				if (p[1] < yLow || p[1] > ya + 0.05f || p[2] > zFront + 0.40f) continue;
				const float x = p[0];
				const BOOL between = x > apex[0][0] && x < apex[1][0];
				const int s = x < cx ? 0 : 1;
				float u = 0.f;
				const float zx = apexZ(x, &u);
				float wl = between ? 1.f : 1.f - Smooth01((fabsf(x - apex[s][0]) - 0.03f) / 0.07f);
				wl *= Smooth01((-nr[2] - 0.5f * nr[1] - 0.15f) / 0.35f);
				if (wl <= 0.f) continue;
				float zl, wv = 1.f, t = 0.f;
				if (p[1] <= ya) {
					const int b = binOf(x);
					if (b < 0 || b >= NBIN || yTan[b] >= ya || p[1] < yTan[b]) continue;
					t = (ya - p[1]) / (ya - yTan[b]);
					zl = zx + slope[b] * (ya - p[1]);
				} else {
					if (!between) continue;
					zl = zx + (p[1] - ya) * 0.8f;
					wv = 1.f - Smooth01((p[1] - ya) / 0.05f);
				}
				const float mv = (p[2] - zl) * wl * wv;
				if (mv <= 0.0005f) continue;
				moved[i] = mv;
				// 垂れた布は頂と一緒に揺れる。頂に近いほど頂の揺れの重みへ寄せる。
				float* J = &jig[(size_t)(nb + i) * 3];
				const float jw = wl * wv * (1.f - t);
				for (int a = 0; a < 3; a++) {
					float aj = between ? apexJ[0][a] + (apexJ[1][a] - apexJ[0][a]) * u : apexJ[s][a];
					J[a] += (aj - J[a]) * jw;
				}
			}
			BOOL any = FALSE;
			for (int i = 0; i < n; i++) {
				float mv = moved[root[i]];
				if (mv <= 0.f) continue;
				V[nb + i].p[2] -= mv;
				if (root[i] != i && jig) memcpy(&jig[(size_t)(nb + i) * 3], &jig[(size_t)(nb + root[i]) * 3], 12);
				any = TRUE;
			}
			// 面法線をずらす前と後で積み、差を元の法線へ足す。
			if (any) {
				Buf<float> F0, F1;
				F0.arena = F1.arena = 1;
				F0.fill(n * 3, 0.f); F1.fill(n * 3, 0.f);
				if (F0.size() == n * 3 && F1.size() == n * 3) {
					for (int t = 0; t + 2 < cosTri.size(); t += 3) {
						int id[3] = { cosTri[t], cosTri[t + 1], cosTri[t + 2] };
						if (id[0] >= n || id[1] >= n || id[2] >= n) continue;
						if (moved[root[id[0]]] <= 0.f && moved[root[id[1]]] <= 0.f && moved[root[id[2]]] <= 0.f) continue;
						for (int pass = 0; pass < 2; pass++) {
							float P[3][3];
							for (int e = 0; e < 3; e++) {
								const float* q = V[nb + id[e]].p;
								P[e][0] = q[0]; P[e][1] = q[1]; P[e][2] = pass ? q[2] : oldZ[id[e]];
							}
							float e1[3] = { P[1][0] - P[0][0], P[1][1] - P[0][1], P[1][2] - P[0][2] };
							float e2[3] = { P[2][0] - P[0][0], P[2][1] - P[0][1], P[2][2] - P[0][2] };
							float fn[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
							Buf<float>& acc = pass ? F1 : F0;
							for (int e = 0; e < 3; e++) for (int a = 0; a < 3; a++) acc[id[e] * 3 + a] += fn[a];
						}
					}
					for (int i = 0; i < n; i++) {
						if (moved[root[i]] <= 0.f) continue;
						float l0 = sqrtf(F0[i * 3] * F0[i * 3] + F0[i * 3 + 1] * F0[i * 3 + 1] + F0[i * 3 + 2] * F0[i * 3 + 2]);
						float l1 = sqrtf(F1[i * 3] * F1[i * 3] + F1[i * 3 + 1] * F1[i * 3 + 1] + F1[i * 3 + 2] * F1[i * 3 + 2]);
						if (l0 < 1e-12f || l1 < 1e-12f) continue;
						float* nr = V[nb + i].n;
						float q[3];
						for (int a = 0; a < 3; a++) q[a] = nr[a] + F1[i * 3 + a] / l1 - F0[i * 3 + a] / l0;
						float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
						if (l > 1e-6f) for (int a = 0; a < 3; a++) nr[a] = q[a] / l;
					}
				}
			}
		}
	}

	// つながった部品。股の中央（左右の脚の間）を通る頂点が部品の裾まで降りていれば、スカート状。
	// ズボンは中央が股で止まり、靴は中央を通らない。
	par.resize(n);
	if (par.size() < n) return;
	for (int i = 0; i < n; i++) par[i] = i;
	for (int t = 0; t + 2 < cosTri.size(); t += 3) {
		int a = cosTri[t], b = cosTri[t + 1], c = cosTri[t + 2];
		if (a >= n || b >= n || c >= n) continue;
		int ra = UfFind(par, root[a]), rb = UfFind(par, root[b]), rc = UfFind(par, root[c]);
		if (ra != rb) par[ra] = rb;
		rc = UfFind(par, rc); rb = UfFind(par, rb);
		if (rc != rb) par[rc] = rb;
	}
	Buf<float> cMin, cCen;
	cMin.arena = cCen.arena = 1;
	cMin.fill(n, 1e9f); cCen.fill(n, 1e9f);
	if (cMin.size() < n || cCen.size() < n) return;
	for (int i = 0; i < n; i++) {
		if (root[i] != i) continue;
		const float* p = V[nb + i].p;
		int c = UfFind(par, i);
		if (p[1] < cMin[c]) cMin[c] = p[1];
		if (fabsf(p[0] - cx) < 0.035f && p[1] < cCen[c]) cCen[c] = p[1];
	}
	auto skirtComp = [&](int c) { return cMin[c] < hipY - 0.12f && cCen[c] < 1e8f && cCen[c] - cMin[c] < 0.10f; };

	// 脚の形。太もも・すねごとに骨に沿って PT 段・まわり PF 分割し、体の頂点のいちばん外の半径に布の厚み 6mm を足す。
	// 前（-Z）を骨に直交させた向きを φ=0 にする。動いた布は骨の逆行列でこの空間へ戻して比べる。
	enum { PT = Renderer::CLOTH_PT, PF = Renderer::CLOTH_PF, PN = PT * PF };
	prof.fill(Renderer::CLOTH_SEGS * PN, 0.f);
	if (prof.size() < Renderer::CLOTH_SEGS * PN) return;
	auto addSeg = [&](int ja, int jb) {
		if (nc.segN >= Renderer::CLOTH_SEGS || ja < 0 || jb < 0 || ja == jb) return;
		float A[3], B[3];
		if (!bindPos(ja, A) || !bindPos(jb, B)) return;
		V3 ax = v3(B[0] - A[0], B[1] - A[1], B[2] - A[2]);
		const float L = Len(ax);
		if (L < 0.05f) return;
		ax = ax * (1.f / L);
		V3 u = v3(0, 0, -1) - ax * (-ax.z);
		if (Len(u) < 0.2f) u = v3(1, 0, 0) - ax * ax.x;
		u = Norm(u);
		const V3 w = Cross(ax, u);
		const int s = nc.segN;
		float* P = &prof[s * PN];
		for (int k = 0; k < nb; k++) {
			const int d = DomJoint(V[k]);
			if (d != ja && d != jb) continue;
			// 腰と混ぜてあるお尻や股の頂点は脚と一緒には回らない。入れると脚を振ったときに無い肉の塊が腰を通り、布を押し潰す。
			int wl = 0;
			for (int c = 0; c < 4; c++) if ((int)V[k].j[c] == ja || (int)V[k].j[c] == jb) wl += V[k].w[c];
			if (wl < 204) continue;
			const V3 q = v3(V[k].p[0] - A[0], V[k].p[1] - A[1], V[k].p[2] - A[2]);
			const float t = Dot(q, ax) / L;
			if (t < -0.15f || t > 1.15f || (d == jb && t > 1.05f)) continue;
			const V3 rv = q - ax * (t * L);
			const float dist = Len(rv);
			if (dist > 0.25f) continue;
			const float phi = atan2f(Dot(rv, w), Dot(rv, u));
			const int ti = Clampi((int)floorf((t + 0.15f) / 1.3f * (float)(PT - 1) + 0.5f), 0, PT - 1);
			const int pi = (int)floorf((phi / 6.2831853f + 0.5f) * (float)PF + 0.5f) % PF;
			float& r = P[ti * PF + pi];
			if (dist > r) r = dist;
		}
		int got = 0;
		for (int i = 0; i < PN; i++) if (P[i] > 0.f) got++;
		if (got < PN / 4) { memset(P, 0, sizeof(float) * PN); return; }
		// 頂点の来なかった区画は、同じ段のまわり、次に上下の段から写す。
		for (int pass = 0; pass < 4; pass++) for (int ti = 0; ti < PT; ti++) for (int pi = 0; pi < PF; pi++) {
			float& r = P[ti * PF + pi];
			if (r > 0.f) continue;
			for (int d = 1; d <= PF / 2 && r <= 0.f; d++) r = fmaxf(P[ti * PF + (pi + d) % PF], P[ti * PF + (pi - d + PF) % PF]);
			if (r <= 0.f && pass > 0) r = fmaxf(ti > 0 ? P[(ti - 1) * PF + pi] : 0.f, ti < PT - 1 ? P[(ti + 1) * PF + pi] : 0.f);
		}
		for (int i = 0; i < PN; i++) if (P[i] > 0.f) P[i] += 0.006f;
		nc.segJ[s][0] = ja; nc.segJ[s][1] = jb;
		float* G = &nc.segG[s][0][0];
		G[0] = A[0]; G[1] = A[1]; G[2] = A[2]; G[3] = L;
		// G[7] は当たりを始める t。太ももは股より下（最上段の 4cm 下）から。付け根から上の当たりは
		// 脚を振ると付け根を軸に逆向きに回り、腰の布を左右で前と後ろへ押してねじる。
		// そこの体は布の下で消してあるので当たらなくてよい。
		// G[11] は当たりを始めてから太さが全部になるまでの t。いきなり全部の太さで始めると、そこから下の布だけが
		// 押し出されて段が付き、段の隙間から中が見える。12cm かけて 0 から太くする。
		const BOOL thigh = ja == jUL[0] || ja == jUL[1];
		const float yTop = hipY - 0.07f, dy = A[1] - B[1];
		const BOOL taper = thigh && dy > 0.05f;
		G[4] = ax.x; G[5] = ax.y; G[6] = ax.z;
		G[7] = taper ? Clampf((A[1] - yTop) / dy, 0.f, 0.5f) : -0.15f;
		G[8] = u.x; G[9] = u.y; G[10] = u.z; G[11] = taper ? 0.12f / dy : 0.f;
		G[12] = w.x; G[13] = w.y; G[14] = w.z; G[15] = 0.f;
		nc.segN++;
	};
	for (int s = 0; s < 2; s++) addSeg(jUL[s], jLL[s]);
	if (nc.segN < 2) return;
	for (int s = 0; s < 2; s++) addSeg(jLL[s], jFt[s]);
	// 脚の形の面からの距離（負なら中）。バインドでの値。
	auto clearance = [&](const float* p) {
		float best = 1e9f;
		for (int s = 0; s < nc.segN; s++) {
			const float* G = &nc.segG[s][0][0];
			const V3 q = v3(p[0] - G[0], p[1] - G[1], p[2] - G[2]);
			const V3 ax = v3(G[4], G[5], G[6]);
			const float t = Dot(q, ax) / G[3];
			if (t < G[7] || t > 1.15f) continue;
			const V3 rv = q - ax * (t * G[3]);
			const float phi = atan2f(Dot(rv, v3(G[12], G[13], G[14])), Dot(rv, v3(G[8], G[9], G[10])));
			const float k = G[11] > 0.f ? Smooth01((t - G[7]) / G[11]) : 1.f;
			best = fminf(best, Len(rv) - ClothProf(&prof[s * PN], t, phi) * k);
		}
		return best;
	};
	// 脚から 2cm 以内の布は脚に付いていき、10cm 離れると腰に付いて揺れるだけ。
	auto followOf = [&](const float* p) { return 1.f - Smooth01((clearance(p) - 0.02f) / 0.08f); };

	// スカート状の部品。最上段（腰に固定）は太ももの付け根より少し下。
	const float y0 = hipY - 0.03f;
	Buf<BYTE> isSk;
	isSk.arena = 1;
	isSk.fill(n, 0);
	if (isSk.size() < n) return;
	float yMin = 1e9f, czSum = 0.f;
	int czN = 0;
	for (int i = 0; i < n; i++) {
		if (!skirtComp(UfFind(par, root[i]))) continue;
		isSk[i] = 1;
		const float* p = V[nb + i].p;
		if (p[1] < yMin) yMin = p[1];
		if (p[1] < y0) { czSum += p[2]; czN++; }
	}
	const float span = y0 - yMin;
	if (czN < 32 || span < 0.10f) return;
	const float cz = czSum / (float)czN;
	const int K = Renderer::CLOTH_SECT;
	const int M = Clampi((int)(span / 0.05f + 0.5f), 3, Renderer::CLOTH_NODES / K - 1);
	const int NN = K * (M + 1);
	const float PI2 = 6.2831853f;
	// 周の角度と高さの段から、近い 4 点とその重み。
	auto lattice = [&](const float* p, int* id, float* w) {
		const float s = (atan2f(p[0] - cx, p[2] - cz) / PI2 + 0.5f) * (float)K;
		int s0 = (int)floorf(s);
		const float fs = s - (float)s0;
		s0 = ((s0 % K) + K) % K;
		const int s1 = (s0 + 1) % K;
		const float r = Clampf((y0 - p[1]) / span * (float)M, 0.f, (float)M);
		int r0 = (int)r;
		if (r0 > M - 1) r0 = M - 1;
		const float fr = r - (float)r0;
		id[0] = r0 * K + s0; id[1] = r0 * K + s1; id[2] = (r0 + 1) * K + s0; id[3] = (r0 + 1) * K + s1;
		w[0] = (1.f - fs) * (1.f - fr); w[1] = fs * (1.f - fr); w[2] = (1.f - fs) * fr; w[3] = fs * fr;
	};
	// 点の半径と、脚に沿わせるときの骨の重みは、その点へ寄る頂点の平均。
	enum { NJW = 64 };
	Buf<float> rs, rw, jw;
	rs.arena = rw.arena = jw.arena = 1;
	rs.fill(NN, 0.f); rw.fill(NN, 0.f); jw.fill(NN * NJW, 0.f);
	if (rs.size() < NN || rw.size() < NN || jw.size() < NN * NJW) return;
	for (int i = 0; i < n; i++) {
		if (!isSk[i] || root[i] != i) continue;
		const Renderer::CSkinV& s = V[nb + i];
		if (s.p[1] > y0 + span / (float)M * 0.5f) continue;
		int id[4]; float w[4];
		lattice(s.p, id, w);
		const float rad = sqrtf((s.p[0] - cx) * (s.p[0] - cx) + (s.p[2] - cz) * (s.p[2] - cz));
		for (int e = 0; e < 4; e++) {
			if (w[e] <= 0.f) continue;
			rs[id[e]] += rad * w[e];
			rw[id[e]] += w[e];
			for (int c = 0; c < 4; c++) if (s.w[c] && s.j[c] < NJW) jw[id[e] * NJW + s.j[c]] += s.w[c] / 255.f * w[e];
		}
	}
	// 頂点の来ない点（前の開いた上着など）は、同じ段の近い点、無ければ上か下の段から写す。
	Buf<int> from;
	from.arena = 1;
	from.fill(NN, -1);
	if (from.size() < NN) return;
	for (int q = 0; q < NN; q++) if (rw[q] > 1e-4f) from[q] = q;
	for (int r = 0; r <= M; r++) for (int s = 0; s < K; s++) {
		const int q = r * K + s;
		for (int d = 1; d <= K / 2 && from[q] < 0; d++) {
			const int a = r * K + (s + d) % K, b = r * K + (s - d + K) % K;
			if (rw[a] > 1e-4f) from[q] = a;
			else if (rw[b] > 1e-4f) from[q] = b;
		}
	}
	for (int pass = 0; pass < 2; pass++) for (int r = 0; r <= M; r++) for (int s = 0; s < K; s++) {
		const int q = r * K + s;
		if (from[q] >= 0) continue;
		const int up = r > 0 ? from[(r - 1) * K + s] : -1, dn = r < M ? from[(r + 1) * K + s] : -1;
		from[q] = up >= 0 ? up : (pass ? dn : -1);
	}
	for (int q = 0; q < NN; q++) if (from[q] < 0) return;
	// 骨の重みは格子の上でならす。隣の点で付く脚が入れ替わると、脚を開いたときに布が裂ける。
	{
		Buf<float> tmp;
		tmp.arena = 1;
		tmp.fill(NN * NJW, 0.f);
		if (tmp.size() < NN * NJW) return;
		for (int q = 0; q < NN; q++) {
			const float s = rw[from[q]];
			for (int j = 0; j < NJW; j++) tmp[q * NJW + j] = jw[from[q] * NJW + j] / s;
		}
		for (int it = 0; it < 6; it++) {
			for (int r = 0; r <= M; r++) for (int s = 0; s < K; s++) {
				const int q = r * K + s;
				const int nq[4] = { r > 0 ? q - K : q, r < M ? q + K : q, r * K + (s + K - 1) % K, r * K + (s + 1) % K };
				for (int j = 0; j < NJW; j++) {
					float a = tmp[q * NJW + j] * 0.5f;
					for (int e = 0; e < 4; e++) a += tmp[nq[e] * NJW + j] * 0.125f;
					jw[q * NJW + j] = a;
				}
			}
			memcpy(tmp.data(), jw.data(), sizeof(float) * NN * NJW);
		}
	}

	nodes.resize(NN);
	if (nodes.size() < NN) return;
	for (int r = 0; r <= M; r++) for (int s = 0; s < K; s++) {
		const int q = r * K + s, f = from[q];
		ClothGpuN& o = nodes[q];
		memset(&o, 0, sizeof(o));
		const float R = rs[f] / rw[f];
		const float th = ((float)s / (float)K - 0.5f) * PI2;
		o.x[0] = cx + R * sinf(th);
		o.x[1] = y0 - span * (float)r / (float)M;
		o.x[2] = cz + R * cosf(th);
		// 最上段も脚に付いていく（上の頂点は衣装の元の重みなので、継ぎ目で動きを揃える）。
		o.q[0] = followOf(o.x);
		o.q[1] = clearance(o.x);   // 下の allowOf で許す深さに直す
		// 裾ほど遠くまで離れてよく、ゆっくり戻り、重力が効く。
		const float t = (float)r / (float)M;
		o.p[0] = 0.02f + 0.25f * t;
		o.p[1] = 0.30f - 0.22f * t;
		o.p[2] = 0.3f + 0.7f * t;
		o.p[3] = r == 0 ? 1.f : 0.f;
		const float* J = &jw[q * NJW];
		float sum = 0.f;
		for (int e = 0; e < 4; e++) {
			int best = -1;
			for (int j = 0; j < NJW; j++) {
				if (J[j] <= 0.f) continue;
				BOOL used = FALSE;
				for (int u = 0; u < e; u++) if ((int)o.j[u] == j) used = TRUE;
				if (!used && (best < 0 || J[j] > J[best])) best = j;
			}
			if (best < 0) break;
			o.j[e] = (unsigned)best;
			o.w[e] = J[best];
			sum += J[best];
		}
		if (sum > 1e-6f) for (int e = 0; e < 4; e++) o.w[e] /= sum;
		else { memset(o.j, 0, sizeof(o.j)); memset(o.w, 0, sizeof(o.w)); o.j[0] = (unsigned)jHips; o.w[0] = 1.f; }
	}
	for (int r = 0; r <= M; r++) for (int s = 0; s < K; s++) {
		const int q = r * K + s;
		const int nq[4] = { r > 0 ? q - K : -1, r < M ? q + K : -1, r * K + (s + K - 1) % K, r * K + (s + 1) % K };
		for (int e = 0; e < 4; e++) nodes[q].nb[e] = nq[e] >= 0 ? (unsigned)nq[e] : 0xFFFFFFFFu;
	}
	// 脚に当たった布は頂点だけ外へ出すので、頂点の間の三角形の面は脚の丸みに食い込む（弦と弧の差）。
	// 頂点ごとにまわりの辺の長さから、太さ 7cm の脚に掛けたときの食い込みを出し、その分だけ脚から離す。
	Buf<float> edge;
	edge.arena = 1;
	edge.fill(n, 0.f);
	if (edge.size() < n) return;
	for (int t = 0; t + 2 < cosTri.size(); t += 3) {
		const int id[3] = { cosTri[t], cosTri[t + 1], cosTri[t + 2] };
		if (id[0] >= n || id[1] >= n || id[2] >= n) continue;
		for (int e = 0; e < 3; e++) {
			const float* a = V[nb + id[e]].p;
			const float* b = V[nb + id[(e + 1) % 3]].p;
			const float l = sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
			for (int k = 0; k < 2; k++) {
				float& m = edge[root[id[(e + k) % 3]]];
				if (l > m) m = l;
			}
		}
	}
	auto allowOf = [&](float c, float e) {
		const float r = 0.07f, h = fminf(e * 0.5f, r);
		const float margin = Clampf(r - sqrtf(r * r - h * h), 0.004f, 0.03f);
		// 作りの段階で脚の形へ食い込んでいる分は許す（押し出すと静止しているのに布が膨らむ）。
		// 元から脚に近い布は、静止時に膨らむのを margin の半分までにする。
		if (c < 0.f) return -c;
		return -fminf(margin, fmaxf(c, margin * 0.5f));
	};
	for (int q = 0; q < NN; q++) nodes[q].q[1] = allowOf(nodes[q].q[1], span / (float)M);

	// 脚の骨（太ももから下）。
	BYTE legJ[256] = {};
	for (int j = 0; j < md.nJ && j < 256; j++) {
		for (int a = j, g = 0; a >= 0 && a < md.nJ && g < 64; a = md.parent[a], g++) {
			if (a == jUL[0] || a == jUL[1]) { legJ[j] = 1; break; }
		}
	}
	// 動かす頂点。最上段の 2cm 上から 20cm かけて、衣装の元の重みから格子へ移る。
	// 股のすぐ下は、元の重みで太ももに半分付いているから前へ出した脚を覆える。ここを腰だけで動かすと、
	// 前へ出した側の太ももが布を突き抜ける（ダンプで前の布が元より 12cm 奥にあった）。
	for (int i = 0; i < n; i++) {
		if (!isSk[i]) continue;
		const Renderer::CSkinV& s = V[nb + i];
		const float m = Smooth01((y0 + 0.02f - s.p[1]) / 0.20f);
		if (m <= 0.001f) continue;
		ClothGpuV o;
		memset(&o, 0, sizeof(o));
		int id[4];
		lattice(s.p, id, o.w);
		o.a[0] = (unsigned)(nb + i);
		o.a[1] = (unsigned)id[0] | ((unsigned)id[1] << 16);
		o.a[2] = (unsigned)id[2] | ((unsigned)id[3] << 16);
		memcpy(o.p, s.p, 12);
		o.p[3] = m;
		memcpy(o.n, s.n, 12);
		o.n[3] = allowOf(clearance(s.p), edge[root[i]]);
		verts.push_back(o);
	}
	if (verts.size() < 16) { nodes.clear(); verts.clear(); nc.segN = 0; return; }

	// 脚の素体。靴・タイツ・スカートのどれも 8mm 以内に無ければ見せてよい（ぴったりの所は布が脚に付いていくので隠したまま）。
	expose.fill(nb, 0);
	if (expose.size() == nb) {
		const float CELL = 0.02f;
		auto cellOf = [&](const float* p, int dx, int dy, int dz) -> unsigned {
			int x = (int)floorf(p[0] / CELL) + dx + 512, y = (int)floorf(p[1] / CELL) + dy + 512, z = (int)floorf(p[2] / CELL) + dz + 512;
			return (unsigned)(x & 1023) | ((unsigned)(y & 1023) << 10) | ((unsigned)(z & 1023) << 20);
		};
		Buf<unsigned long long> ck;
		ck.arena = 1;
		for (int i = 0; i < n; i++) if (root[i] == i) ck.push_back(((unsigned long long)cellOf(V[nb + i].p, 0, 0, 0) << 32) | (unsigned)i);
		const int nk = ck.size();
		if (nk > 0) qsort(ck.data(), nk, sizeof(unsigned long long), CmpU64);
		for (int k = 0; k < nb; k++) {
			const int d = DomJoint(V[k]);
			if (d != jUL[0] && d != jUL[1] && d != jLL[0] && d != jLL[1] && d != jFt[0] && d != jFt[1]) continue;
			const float* p = V[k].p;
			// 見せてよいのは当たりが全部の太さになった所だけ。太ももの当たりは股より下（hipY-0.07）から 12cm かけて
			// 太くなり、腰と混ぜた頂点は当たりの形に入れていないので、どちらも布を突き抜ける。2cm の余裕を取って隠す。
			if (p[1] > hipY - 0.21f) continue;
			int lw = 0;
			for (int c = 0; c < 4; c++) if (V[k].w[c] && V[k].j[c] < 256 && legJ[V[k].j[c]]) lw += V[k].w[c];
			if (lw < 204) continue;
			float dO = 1e9f, dS = 1e9f;
			for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
				const unsigned long long cell = cellOf(p, dx, dy, dz);
				int lo = 0, hi = nk;
				while (lo < hi) { int m = (lo + hi) >> 1; if ((ck[m] >> 32) < cell) lo = m + 1; else hi = m; }
				for (int q = lo; q < nk && (ck[q] >> 32) == cell; q++) {
					const int i = (int)(ck[q] & 0xFFFFFFFFu);
					const float* b = V[nb + i].p;
					const float d2 = (b[0] - p[0]) * (b[0] - p[0]) + (b[1] - p[1]) * (b[1] - p[1]) + (b[2] - p[2]) * (b[2] - p[2]);
					float& dd = isSk[i] ? dS : dO;
					if (d2 < dd) dd = d2;
				}
			}
			expose[k] = (dO > 0.008f * 0.008f && dS > 0.008f * 0.008f) ? 2 : 1;
		}
		// 腰から下でスカートの内側にある素体（3）。衣装の作りで残してある腰まわりの素体は元の重みで脚に付いて
		// ねじれるので、布を腰に付けて動かすと布を突き抜ける。周 32 × 高さ 2cm の区画ごとの布の一番外の半径より
		// 内にあれば隠す。腕は布より外なので残る。
		enum { HK = 32, HB = 64 };
		const float hTop = y0 + 0.15f, hBot = hipY - 0.30f, hDy = (hTop - hBot) / (float)HB;
		float rOut[HB][HK];
		for (int b = 0; b < HB; b++) for (int s = 0; s < HK; s++) rOut[b][s] = -1.f;
		auto hBin = [&](const float* p, int* b, int* s, float* r) {
			const float dx = p[0] - cx, dz = p[2] - cz;
			*r = sqrtf(dx * dx + dz * dz);
			*b = (int)floorf((p[1] - hBot) / hDy);
			*s = ((int)floorf((atan2f(dx, dz) / PI2 + 0.5f) * (float)HK) % HK + HK) % HK;
			return *b >= 0 && *b < HB;
		};
		for (int i = 0; i < n; i++) {
			if (!isSk[i]) continue;
			int b, s; float r;
			if (hBin(V[nb + i].p, &b, &s, &r) && r > rOut[b][s]) rOut[b][s] = r;
		}
		for (int k = 0; k < nb; k++) {
			if (expose[k]) continue;
			int b, s; float r;
			if (!hBin(V[k].p, &b, &s, &r)) continue;
			float cov = 1e9f;
			for (int e = -1; e <= 1; e++) cov = fminf(cov, rOut[b][(s + e + HK) % HK]);
			if (cov > 0.f && r < cov - 0.005f) expose[k] = 3;
		}
	}
	nc.clothN = NN;
	nc.clothVN = verts.size();
	nc.clothHip = jHips;
	nc.clothAxis[0] = cx;
	nc.clothAxis[1] = cz;
}

BOOL Renderer::DrawAvatar(int model, int avatar, int cup, const Pose& pose, const M4& world, const DrawParams& dp, const void* owner)
{
	if (model < 0 || model >= (int)models.size() || avatar <= 0 || m_avaSegN <= 0) return FALSE;
	const Model& md = models[model];
	const AvaModel* am = NULL;
	for (const AvaModel& x : m_avaModels) if (!strcmp(x.name, md.name)) { am = &x; break; }
	if (!am || md.rig < 0 || md.rig >= (int)rigs.size()) return FALSE;
	int ci = -1;
	for (int k = 0, n = 0; k < (int)m_avaCos.size(); k++) if (m_avaCos[k].gender == am->gender && ++n == avatar) { ci = k; break; }
	if (ci < 0) return FALSE;
	if (!EnsureAvaCos(m_avaCos[ci])) return FALSE;
	// 胸の差分が無い体（男性）は大きさで形が変わらないので、一つだけ作る。
	const int cupK = (!m_avaBody[am->gender].m.dmax.empty() && !m_avaCos[ci].m.dmax.empty()) ? Clampi(cup, 0, BUST_N - 1) : 0;
	AvaCache* ac = NULL;
	for (AvaCache& x : m_avaCache) if (x.model == model && x.cos == ci && x.cup == cupK) { ac = &x; break; }
	if (!ac) {
		if (g_avaNeedReset || m_avaCache.size() >= 32 || TacAvaBad()) { g_avaNeedReset = 1; return FALSE; }
		const AvaBody& body = m_avaBody[am->gender];
		const AvaCos& cos = m_avaCos[ci];
		const Rig& rg = rigs[md.rig];
		const int ns = m_avaSegN;
		Buf<int> segJ;
		segJ.arena = 1;
		segJ.resize(ns);
		if (segJ.size() < ns) { g_avaNeedReset = 1; return FALSE; }
		// 腕・肩・手・首・頭の骨。胸の山はここから 3cm 以内で弱める。
		BYTE armJ[256] = {};
		for (int s = 0; s < ns; s++) {
			int j = rg.Find(s < am->nJoint ? am->joint[s] : m_avaSeg[s]);
			segJ[s] = j >= 0 ? j : (md.hipsJ >= 0 ? md.hipsJ : 0);
			static const char* kArm[] = { "arm", "shoulder", "hand", "index", "middle", "ring", "pinky", "thumb", "neck", "head" };
			char lo[24];
			int q = 0;
			for (; q < 23 && m_avaSeg[s][q]; q++) lo[q] = (char)tolower((unsigned char)m_avaSeg[s][q]);
			lo[q] = 0;
			for (const char* k : kArm) if (j >= 0 && j < 256 && strstr(lo, k)) armJ[j] = 1;
		}
		AvaCache nc;
		nc.skin.bind.arena = 1;
		nc.skin.cloth.arena = 1;
		nc.skin.headTri.arena = 1;
		nc.skin.meshHair.arena = 1;
		nc.jig.arena = 1;
		nc.model = model;
		nc.cos = ci;
		nc.cup = cupK;
		Buf<float> bx, bn;
		bx.arena = bn.arena = 1;
		CreatureSkin& sk = nc.skin;
		sk.model = model;
		// 顔検出が途中で戻っても、範囲が未初期化のまま残らないようにする。残すと男性の頭で描画が落ちる。
		sk.faceSet = -1;
		sk.faceStart = 0;
		sk.faceN = 0;
		sk.faceSynth = 0;
		sk.faceGot = -1;
		const size_t nb = body.m.v.size(), nv = nb + cos.m.v.size();
		sk.bind.resize(nv);
		sk.cloth.resize(nv);
		const BOOL hasBust = !body.m.dmax.empty() && !cos.m.dmax.empty();
		if (hasBust) { bx.resize(nv * 3); bn.resize(nv * 3); }
		if ((size_t)sk.bind.size() < nv || (size_t)sk.cloth.size() < nv || (hasBust && ((size_t)bx.size() < nv * 3 || (size_t)bn.size() < nv * 3))) {
			g_avaNeedReset = 1;
			return FALSE;
		}
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
				lin(&src.dmax[q * 3], &bx[k * 3]);
				lin(&src.dmin[q * 3], &bn[k * 3]);
			}
		}
		int headSeg = -1;
		for (int s = 0; s < ns; s++) if (!strcmp(m_avaSeg[s], "Head")) { headSeg = s; break; }
		const CreatureSkin* own = NULL;
		BOOL mhFace = FALSE;
		for (const CreatureSkin& hs : m_skins) if (hs.model == model) {
			if (!own && hs.headTri.size() >= 9) own = &hs;
			if (hs.faceSet == am->gender && hs.faceN == body.nFace) mhFace = TRUE;
		}
		Buf<BYTE> mhHead;
		Buf<int> headMap;
		mhHead.arena = 1;
		headMap.arena = 1;
		int nAdd = 0;
		if (own && !mhFace && headSeg >= 0) {
			headMap.fill((int)own->bind.size(), -1);
			if (headMap.size() == own->bind.size()) {
				for (size_t t = 0; t + 2 < own->headTri.size(); t++) {
					int v = own->headTri[t];
					if (v >= 0 && v < (int)own->bind.size() && headMap[v] < 0) headMap[v] = nAdd++;
				}
			}
		}
		// 胸の周りの体と服の三角形を細かく割る。並びは [体][体の新][服][服の新]（元の体の番号は変わらない）。
		// 頭を継ぐ分は残す（継げなくなると顔が変わる）。
		static Buf<int> bOff;
		static Buf<WORD> bTri, cTri;
		BustRef br;
		int nbA = (int)nb, nvA = (int)nv;
		{
			const int reserve = (own && !mhFace && nAdd > 30 && (int)nv + nAdd <= 64000) ? nAdd : 0;
			const int maxAdd = (reserve ? 64000 - reserve : 65000) - (int)nv;
			if (!RefineBust(sk, bx, bn, (int)nb, (int)nv, body.bodyTri, cos.keep, cos.tri, armJ, maxAdd, bOff, bTri, cTri, nbA, nvA, br)) {
				g_avaNeedReset = 1;
				return FALSE;
			}
		}
		const BOOL graft = own && !mhFace && nAdd > 30 && nvA + nAdd <= 64000;
		if (graft) {
			mhHead.fill(nb, 0);
			for (size_t k = 0; k < nb && k < mhHead.size(); k++) {
				const CSkinV& s = body.m.v[k];
				int best = 0, bw = -1;
				for (int c = 0; c < 4; c++) if ((int)s.w[c] > bw) { bw = (int)s.w[c]; best = s.j[c]; }
				mhHead[k] = (best == headSeg) ? 1 : 0;
			}
		}
		Buf<WORD> idx;
		idx.arena = 1;
		idx.reserve(bTri.size() + body.extraTri.size() + cTri.size() + (graft ? own->headTri.size() : 0));
		for (size_t t = 0; t * 3 < body.bodyTri.size(); t++) {
			if (t / 8 >= cos.keep.size() || !((cos.keep[t / 8] >> (t & 7)) & 1)) continue;
			for (int ch = bOff[(int)t]; ch < bOff[(int)t + 1]; ch++) {
				int a = bTri[ch * 3], b = bTri[ch * 3 + 1], c = bTri[ch * 3 + 2];
				if (graft && a < (int)mhHead.size() && b < (int)mhHead.size() && c < (int)mhHead.size()
					&& mhHead[a] && mhHead[b] && mhHead[c]) continue;
				idx.push_back((WORD)a); idx.push_back((WORD)b); idx.push_back((WORD)c);
			}
		}
		if (!graft) for (size_t k = 0; k < body.extraTri.size(); k++) idx.push_back(body.extraTri[k]);
		for (WORD w : cTri) idx.push_back((WORD)(w + nbA));
		if (graft) {
			DWORD skinC = 0;
			double sr = 0, sg = 0, sb = 0; int nsamp = 0;
			for (int v = 0; v < (int)headMap.size(); v++) if (headMap[v] >= 0) {
				DWORD c = own->bind[v].c;
				float r = (c & 255) / 255.f, g = ((c >> 8) & 255) / 255.f, b = ((c >> 16) & 255) / 255.f;
				float y = 0.30f * r + 0.59f * g + 0.11f * b;
				if (y < 0.12f || y > 0.82f || r + 0.02f < b) continue;
				sr += r; sg += g; sb += b; nsamp++;
			}
			if (nsamp >= 20) {
				auto q8 = [](double x) { int v = (int)(x * 255.0 + 0.5); return v < 0 ? 0 : (v > 255 ? 255 : v); };
				skinC = RGBA8(q8(sr / nsamp), q8(sg / nsamp), q8(sb / nsamp));
				for (size_t k = 0; k < (size_t)nbA && k < sk.cloth.size(); k++)
					if (!sk.cloth[k] && (k >= mhHead.size() || !mhHead[k])) sk.bind[k].c = skinC;
			}
			const int base = (int)sk.bind.size();
			sk.bind.resize(base + nAdd);
			sk.cloth.resize(base + nAdd);
			sk.meshHair.fill(base + nAdd, 0);
			if (hasBust) {
				size_t z0 = bx.size();
				bx.resize((base + nAdd) * 3);
				bn.resize((base + nAdd) * 3);
				for (size_t i = z0; i < bx.size(); i++) { bx[i] = 0.f; bn[i] = 0.f; }
			}
			for (int v = 0; v < (int)headMap.size(); v++) if (headMap[v] >= 0) {
				CSkinV& d = sk.bind[base + headMap[v]];
				d = own->bind[v];
				sk.cloth[base + headMap[v]] = 0;
				if (v < (int)own->meshHair.size() && own->meshHair[v]) sk.meshHair[base + headMap[v]] = 1;
			}
			for (size_t t = 0; t + 2 < own->headTri.size(); t += 3) {
				int a = own->headTri[t], b = own->headTri[t + 1], c = own->headTri[t + 2];
				if (a < 0 || b < 0 || c < 0 || a >= (int)headMap.size() || b >= (int)headMap.size() || c >= (int)headMap.size()
					|| headMap[a] < 0 || headMap[b] < 0 || headMap[c] < 0) continue;
				idx.push_back((WORD)(base + headMap[a]));
				idx.push_back((WORD)(base + headMap[b]));
				idx.push_back((WORD)(base + headMap[c]));
			}
			BindFaceSynth(sk, base, nAdd);
		}
		if (hasBust) {
			// 0=A〜7=H。形の寸法は kBustW などに。
			const int nAll = (int)sk.bind.size();
			nc.jig.resize(nAll * 3);
			if (nc.jig.size() < nAll * 3 || bn.size() < nAll * 3) { g_avaNeedReset = 1; return FALSE; }
			ShapeCup(sk.bind.data(), nAll, nbA, nvA, bn.data(), cupK, br,
				bTri.data(), bTri.size() / 3, armJ, idx.data(), (int)idx.size(), nc.jig.data());
		}
		Buf<ClothGpuN> clothN;
		Buf<ClothGpuV> clothV;
		Buf<float> clothP;
		Buf<BYTE> expose;
		clothN.arena = clothV.arena = clothP.arena = expose.arena = 1;
		if (m_cbCloth) PrepCloth(nc, nbA, nvA, cTri, segJ.data(), m_avaSeg, ns, md, nc.jig.empty() ? NULL : nc.jig.data(), clothN, clothV, clothP, expose);
		if (nc.clothN > 0 && expose.size() == nbA) {
			// スカートの内側の素体（3）は、衣装の作りで残してあっても描かない。
			{
				size_t o = 0;
				for (size_t t = 0; t + 2 < idx.size(); t += 3) {
					const int a = idx[t], b = idx[t + 1], c = idx[t + 2];
					if (a < nbA && b < nbA && c < nbA && expose[a] == 3 && expose[b] == 3 && expose[c] == 3) continue;
					idx[o] = idx[t]; idx[o + 1] = idx[t + 1]; idx[o + 2] = idx[t + 2];
					o += 3;
				}
				idx.resize((int)o);
			}
			// スカートの下で消してあった脚を戻す。裾が脚から離れても、ふくらはぎが欠けないように。
			auto legOk = [](int e) { return e == 1 || e == 2; };
			for (size_t t = 0; t * 3 + 2 < body.bodyTri.size(); t++) {
				if (t / 8 < cos.keep.size() && ((cos.keep[t / 8] >> (t & 7)) & 1)) continue;
				for (int ch = bOff[(int)t]; ch < bOff[(int)t + 1]; ch++) {
					const int a = bTri[ch * 3], b = bTri[ch * 3 + 1], c = bTri[ch * 3 + 2];
					if (a >= nbA || b >= nbA || c >= nbA) continue;
					const int ea = expose[a], eb = expose[b], ec = expose[c];
					if (!legOk(ea) || !legOk(eb) || !legOk(ec) || (ea < 2 && eb < 2 && ec < 2)) continue;
					idx.push_back((WORD)a); idx.push_back((WORD)b); idx.push_back((WORD)c);
				}
			}
		}
		// 長さゼロの骨や、16bit に収まらない索引は、ここで落として素体に戻す。GPU へ渡すと装置が落ちる。
		for (size_t k = 0; k < sk.bind.size(); k++) {
			const float* p = sk.bind[k].p;
			if (p[0] != p[0] || p[1] != p[1] || p[2] != p[2]) return FALSE;
			if (fabsf(p[0]) > 12.f || fabsf(p[1]) > 12.f || fabsf(p[2]) > 12.f) return FALSE;
		}
		{
			Buf<WORD> kept;
			kept.arena = 1;
			kept.reserve(idx.size());
			const int nvm = (int)sk.bind.size();
			for (size_t t = 0; t + 2 < idx.size(); t += 3) {
				int a = idx[t], b = idx[t + 1], c = idx[t + 2];
				if ((unsigned)a >= (unsigned)nvm || (unsigned)b >= (unsigned)nvm || (unsigned)c >= (unsigned)nvm) continue;
				kept.push_back((WORD)a); kept.push_back((WORD)b); kept.push_back((WORD)c);
			}
			idx.swap(kept);
		}
		if (TacAvaBad()) { g_avaNeedReset = 1; return FALSE; }
		if (idx.size() < 3 || sk.bind.size() > 65535) return FALSE;
		if (!graft) for (const CreatureSkin& hs : m_skins) if (hs.model == model && hs.faceSet == am->gender && hs.faceN == body.nFace) {
			sk.faceSet = hs.faceSet;
			sk.faceStart = 0;
			sk.faceN = body.nFace;
			memcpy(sk.faceLin, hs.faceLin, sizeof(sk.faceLin));
			break;
		}
		static Buf<VtxS> pv;
		pv.resize((int)sk.bind.size());
		if (!pv.data() || pv.size() < (int)sk.bind.size()) return FALSE;
		for (size_t k = 0; k < sk.bind.size(); k++) {
			const CSkinV& b = sk.bind[k];
			VtxS& s = pv[k];
			s.p[0] = b.p[0]; s.p[1] = b.p[1]; s.p[2] = b.p[2];
			s.n[0] = b.n[0]; s.n[1] = b.n[1]; s.n[2] = b.n[2];
			s.uv[0] = s.uv[1] = 0;
			s.c = b.c;
		}
		FillClothBack(nc, sk, nbA, nvA);
		BuildBaldIB(m_dev, sk, idx.data(), (UINT)idx.size());
		if (!MakeMesh(nc.gm, pv.data(), sizeof(VtxS), (UINT)pv.size(), idx.data(), (UINT)idx.size())) return FALSE;
		nc.gm.tex = TEX_WHITE;
		if (nc.clothN > 0 && nc.clothN <= CLOTH_NODES && clothN.size() == nc.clothN && clothV.size() == nc.clothVN
			&& clothP.size() == CLOTH_SEGS * CLOTH_PT * CLOTH_PF) {
			const void* src[3] = { clothN.data(), clothV.data(), clothP.data() };
			const UINT cnt[3] = { (UINT)nc.clothN, (UINT)nc.clothVN, (UINT)clothP.size() };
			const UINT stride[3] = { (UINT)sizeof(ClothGpuN), (UINT)sizeof(ClothGpuV), 4u };
			for (int b = 0; b < 3 && nc.clothN > 0; b++) {
				D3D11_BUFFER_DESC bd = {};
				bd.ByteWidth = cnt[b] * stride[b];
				bd.Usage = D3D11_USAGE_IMMUTABLE;
				bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
				bd.StructureByteStride = stride[b];
				D3D11_SUBRESOURCE_DATA sd = { src[b], 0, 0 };
				D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
				sv.Format = DXGI_FORMAT_UNKNOWN;
				sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
				sv.Buffer.NumElements = cnt[b];
				if (FAILED(m_dev->CreateBuffer(&bd, &sd, &nc.clothSB[b])) || FAILED(m_dev->CreateShaderResourceView(nc.clothSB[b], &sv, &nc.clothSRV[b]))) {
					for (int k = 0; k < 3; k++) { TAC_REL(nc.clothSRV[k]); TAC_REL(nc.clothSB[k]); }
					nc.clothN = nc.clothVN = 0;
				}
			}
		} else nc.clothN = nc.clothVN = 0;
		m_avaCache.push_back(std::move(nc));
		ac = &m_avaCache.back();
	}
	// 揺れの倍率。A で 0.55、C でおよそ 1、H で 1.95。
	SkinAndDraw(ac->skin, ac->gm, ac->jig.empty() ? NULL : ac->jig.data(), 0.55f + 0.2f * (float)ac->cup, pose, world, dp,
		ac->clothN > 0 ? ac : NULL, owner);
	return TRUE;
}

void Renderer::RunCloth(AvaCache& ac, const void* owner, CreatureSkin& src, const Pose& pose, const M4& world, const DrawParams& dp)
{
	if (m_inPortrait || !owner || !ac.clothSRV[0] || !ac.clothSRV[1] || !ac.clothSRV[2] || ac.clothN <= 0 || ac.clothN > CLOTH_NODES || ac.clothVN <= 0
		|| !src.outUAV || !m_cbCloth || !m_cbSkinBone || !m_csCloth[0] || !m_csCloth[1] || !m_csCloth[2]) return;
	if (ac.clothHip < 0 || ac.clothHip >= pose.nJ) return;
	ClothInst* ci = NULL;
	for (ClothInst& x : m_clothInst) if (x.owner == owner && x.model == ac.model && x.cos == ac.cos && x.cup == ac.cup) { ci = &x; break; }
	if (!ci) {
		// 空きが無ければ、いちばん長く描いていないキャラの分を使い回す。
		if (m_clothInst.size() < CLOTH_INST) { m_clothInst.push_back(ClothInst()); ci = &m_clothInst.back(); }
		else { ci = &m_clothInst[0]; for (ClothInst& x : m_clothInst) if (x.frame < ci->frame) ci = &x; }
		ci->owner = owner; ci->model = ac.model; ci->cos = ac.cos; ci->cup = ac.cup; ci->valid = 0;
	}
	// 今・前・作業・目標からのずれを CLOTH_NODES 個ずつ。
	if (!ci->uav) {
		TAC_REL(ci->sb);
		ci->n = 0;
		ci->valid = 0;
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (UINT)CLOTH_NODES * 4u * 16u;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = 16;
		if (FAILED(m_dev->CreateBuffer(&bd, NULL, &ci->sb))) return;
		D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
		ud.Format = DXGI_FORMAT_UNKNOWN;
		ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		ud.Buffer.NumElements = (UINT)CLOTH_NODES * 4u;
		if (FAILED(m_dev->CreateUnorderedAccessView(ci->sb, &ud, &ci->uav))) { TAC_REL(ci->sb); return; }
		ci->n = CLOTH_NODES;
	}
	const float dtRaw = m_time - ci->time;
	int reset = !ci->valid;
	M4 delta = MIdent();
	if (!reset) {
		delta = MMul(ci->world, MInverse(world));
		const float* t = &delta.m[12];
		// 場面の切り替えや瞬間移動は、揺れを持ち越さずに置き直す。
		if (t[0] * t[0] + t[1] * t[1] + t[2] * t[2] > 1.5f * 1.5f || dtRaw > 0.25f || dtRaw < 0.f) reset = 1;
	}
	const BOOL step = reset || (ci->frame != m_frame && dtRaw > 1e-4f);
	const float hs = dp.bodyScale > 0.05f ? dp.bodyScale : 1.f;
	const float dt = Clampf(dtRaw, 1.f / 240.f, 1.f / 20.f);
	struct CB { M4 delta, hips, hipInv; float seg[CLOTH_SEGS][4][4]; M4 pal[CLOTH_SEGS], inv[CLOTH_SEGS]; unsigned cnt[4]; float sim[4]; float mode[4]; } cb;
	static_assert(sizeof(CB) == 192 + 64 * CLOTH_SEGS + 128 * CLOTH_SEGS + 48, "ClothParm");
	memset(&cb, 0, sizeof(cb));
	cb.delta = delta;
	cb.hips = pose.palette[ac.clothHip];
	cb.hipInv = MInverse(cb.hips);
	cb.mode[1] = ac.clothAxis[0];
	cb.mode[2] = ac.clothAxis[1];
	// 太ももが腰に対して大きく上がる（座る・しゃがむ）ほど、衣装の元の重みへ戻して脚に沿わせる。歩く程度では離したまま。
	{
		const M4 hipInv = MInverse(pose.palette[ac.clothHip]);
		float bend = 0.f;
		for (int s = 0; s < 2 && s < ac.segN; s++) {
			const int ja = ac.segJ[s][0], jb = ac.segJ[s][1];
			if (ja < 0 || jb < 0 || ja >= pose.nJ || jb >= pose.nJ) continue;
			const float* G = &ac.segG[s][0][0];
			const V3 A0 = v3(G[0], G[1], G[2]), B0 = A0 + v3(G[4], G[5], G[6]) * G[3];
			const V3 d = MXformDir(hipInv, MXformPt(pose.palette[jb], B0) - MXformPt(pose.palette[ja], A0));
			bend = fmaxf(bend, acosf(Clampf(Dot(Norm(d), Norm(B0 - A0)), -1.f, 1.f)));
		}
		cb.mode[0] = Smooth01((bend - 0.65f) / 0.6f);
	}
	// 脚の形はバインドの空間で持つ。布を根元の骨の逆行列で戻して比べ、押し出したら同じ骨で戻す。
	int nseg = 0;
	for (int s = 0; s < ac.segN && s < CLOTH_SEGS; s++) {
		const int ja = ac.segJ[s][0];
		if (ja < 0 || ja >= pose.nJ) break;
		memcpy(cb.seg[s], ac.segG[s], sizeof(cb.seg[s]));
		cb.pal[s] = pose.palette[ja];
		cb.inv[s] = MInverse(pose.palette[ja]);
		nseg++;
	}
	cb.cnt[0] = (unsigned)ac.clothN;
	cb.cnt[1] = (unsigned)nseg;
	cb.cnt[2] = (unsigned)ac.clothVN;
	cb.sim[0] = dt;
	cb.sim[1] = dt * 60.f;
	cb.sim[2] = reset ? 1.f : 0.f;
	cb.sim[3] = hs;
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(m_ctx->Map(m_cbCloth, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
	memcpy(ms.pData, &cb, sizeof(cb));
	m_ctx->Unmap(m_cbCloth, 0);
	// b1 は直前の CSSkin で書いた骨の行列（座ったときに脚へ沿わせる分）。
	ID3D11Buffer* cbs[2] = { m_cbCloth, m_cbSkinBone };
	m_ctx->CSSetConstantBuffers(0, 2, cbs);
	m_ctx->CSSetShaderResources(0, 3, ac.clothSRV);
	ID3D11UnorderedAccessView* uavs[2] = { src.outUAV, ci->uav };
	UINT cnts[2] = { 0, 0 };
	m_ctx->CSSetUnorderedAccessViews(0, 2, uavs, cnts);
	if (step) {
		m_ctx->CSSetShader(m_csCloth[0], NULL, 0);
		m_ctx->Dispatch(((UINT)ac.clothN + 63u) / 64u, 1, 1);
		m_ctx->CSSetShader(m_csCloth[1], NULL, 0);
		m_ctx->Dispatch(1, 1, 1);
	}
	m_ctx->CSSetShader(m_csCloth[2], NULL, 0);
	m_ctx->Dispatch(((UINT)ac.clothVN + 63u) / 64u, 1, 1);
	ID3D11UnorderedAccessView* nou[2] = { NULL, NULL };
	ID3D11ShaderResourceView* nos[3] = { NULL, NULL, NULL };
	ID3D11Buffer* nob[2] = { NULL, NULL };
	m_ctx->CSSetUnorderedAccessViews(0, 2, nou, NULL);
	m_ctx->CSSetShaderResources(0, 3, nos);
	m_ctx->CSSetConstantBuffers(0, 2, nob);
	m_ctx->CSSetShader(NULL, NULL, 0);
	ci->frame = m_frame;
	if (step) {
		ci->time = m_time;
		ci->world = world;
		ci->valid = 1;
	}
}

void Renderer::ReleaseHair()
{
	TAC_REL(m_hairUAV);
	TAC_REL(m_hairBind);
	TAC_REL(m_hairSB);
	m_hairDraw.vb = NULL;
	TAC_REL(m_hairVB);
	m_hairDraw.Release();
	TAC_REL(m_hairSrv);
	TAC_REL(m_hairTex);
	m_hairNv = 0;
	m_scalpModel = -1;
	m_capBind.clear();
	m_capDraw.Release();
}

void Renderer::LoadHairPack()
{
	ReleaseHair();
	if (!m_dev) return;
	HMODULE hm = AfxGetResourceHandle();
	HRSRC rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_HAIR), RT_RCDATA);
	if (!rs) { hm = GetModuleHandle(NULL); rs = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_HAIR), RT_RCDATA); }
	if (!rs) return;
	HGLOBAL hg = LoadResource(hm, rs);
	const BYTE* data = hg ? (const BYTE*)LockResource(hg) : NULL;
	DWORD size = SizeofResource(hm, rs);
	if (!data || size < 16 || memcmp(data, "TACH", 4) != 0) return;
	UINT nv = 0, ni = 0, ns = 0;
	memcpy(&nv, data + 4, 4);
	memcpy(&ni, data + 8, 4);
	memcpy(&ns, data + 12, 4);
	if (nv < 30 || nv > 65000 || ni < 9 || ni > 400000 || (ni % 3) != 0 || ns < 1) return;
	LoadPulse(1, 4);
	size_t need = 16ull + (size_t)nv * 40ull + (size_t)ni * 2ull;
	if (need > size) return;
	const float* rec = (const float*)(data + 16);
	const WORD* idx = (const WORD*)(data + 16 + (size_t)nv * 40);
	struct GpuSkinIn { float p[4], n[4], bx[4], bn[4]; unsigned pack[4]; };
	static_assert(sizeof(GpuSkinIn) == 80, "hair SkinIn");
	static Buf<GpuSkinIn> up;
	up.resize(nv);
	if (!up.data() || up.size() < nv) return;
	for (UINT i = 0; i < nv; i++) {
		const float* s = rec + i * 10;
		GpuSkinIn& g = up[(size_t)i];
		memset(&g, 0, sizeof(g));
		g.p[0] = s[0]; g.p[1] = s[1]; g.p[2] = s[2]; g.p[3] = s[8];
		g.n[0] = s[3]; g.n[1] = s[4]; g.n[2] = s[5]; g.n[3] = s[9];
		g.bx[0] = s[6]; g.bx[1] = s[7];
		g.pack[0] = 0xFFFFFFFFu;
	}
	memset(m_hairCap, 0, sizeof(m_hairCap));
	memset(m_hairCapN, 0, sizeof(m_hairCapN));
	{
		float top[16];
		double sx[16], sz[16];
		for (int s = 0; s < 16; s++) { top[s] = -1e9f; sx[s] = sz[s] = 0; }
		for (UINT i = 0; i < nv; i++) {
			int s = (int)(rec[i * 10 + 9] + 0.5f);
			if (s < 1 || s > 15) continue;
			if (rec[i * 10 + 1] > top[s]) top[s] = rec[i * 10 + 1];
		}
		for (UINT i = 0; i < nv; i++) {
			int s = (int)(rec[i * 10 + 9] + 0.5f);
			if (s < 1 || s > 15 || rec[i * 10 + 1] < top[s] - 0.14f) continue;
			sx[s] += rec[i * 10];
			sz[s] += rec[i * 10 + 2];
			m_hairCapN[s]++;
		}
		for (int s = 1; s <= 15; s++) if (m_hairCapN[s] >= 8) {
			m_hairCap[s][0] = (float)(sx[s] / m_hairCapN[s]);
			m_hairCap[s][1] = top[s];
			m_hairCap[s][2] = (float)(sz[s] / m_hairCapN[s]);
		}
	}
	LoadPulse(2, 4);
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = nv * 80u;
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = 80;
	D3D11_SUBRESOURCE_DATA sd = { up.data(), 0, 0 };
	if (FAILED(m_dev->CreateBuffer(&bd, &sd, &m_hairSB))) return;
	D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.Format = DXGI_FORMAT_UNKNOWN;
	sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	sv.Buffer.NumElements = nv;
	if (FAILED(m_dev->CreateShaderResourceView(m_hairSB, &sv, &m_hairBind))) { ReleaseHair(); return; }
	bd = {};
	bd.ByteWidth = nv * 36u;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_hairVB))) { ReleaseHair(); return; }
	D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
	ud.Format = DXGI_FORMAT_R32_TYPELESS;
	ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	ud.Buffer.NumElements = nv * 36u / 4u;
	ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	if (FAILED(m_dev->CreateUnorderedAccessView(m_hairVB, &ud, &m_hairUAV))) { ReleaseHair(); return; }
	bd = {};
	bd.ByteWidth = ((ni * 2u) + 3u) & ~3u;
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	static Buf<WORD> ib;
	ib.assign(idx, idx + ni);
	ib.resize(bd.ByteWidth / 2, 0);
	sd.pSysMem = ib.data();
	if (FAILED(m_dev->CreateBuffer(&bd, &sd, &m_hairDraw.ib))) { ReleaseHair(); return; }
	m_hairDraw.nIdx = ni;
	m_hairDraw.stride = 36;
	m_hairDraw.i32 = 0;
	m_hairDraw.tex = -2;
	HRSRC rt = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_HAIRTEX), RT_RCDATA);
	if (!rt) { hm = GetModuleHandle(NULL); rt = FindResource(hm, MAKEINTRESOURCE(IDR_TAC_HAIRTEX), RT_RCDATA); }
	if (rt) {
		HGLOBAL ht = LoadResource(hm, rt);
		const BYTE* td = ht ? (const BYTE*)LockResource(ht) : NULL;
		DWORD tl = SizeofResource(hm, rt);
		if (td && tl > 32) {
			IWICImagingFactory* f = NULL;
			if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) && f) {
				IWICStream* st = NULL; IWICBitmapDecoder* dec = NULL; IWICBitmapFrameDecode* fr = NULL; IWICFormatConverter* cv = NULL;
				if (SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)td, tl))
					&& SUCCEEDED(f->CreateDecoderFromStream(st, NULL, WICDecodeMetadataCacheOnLoad, &dec))
					&& SUCCEEDED(dec->GetFrame(0, &fr)) && SUCCEEDED(f->CreateFormatConverter(&cv))
					&& SUCCEEDED(cv->Initialize(fr, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
					UINT w = 0, h = 0;
					cv->GetSize(&w, &h);
					if (w > 0 && h > 0 && w <= 4096 && h <= 4096) {
						static Buf<DWORD> px;
						px.resize((int)((size_t)w * h));
						if (px.data() && SUCCEEDED(cv->CopyPixels(NULL, w * 4, w * h * 4, (BYTE*)px.data()))) {
							// カードは髪色 × この明るさで出る（平均はおよそ 0.345）。笠が白のままだと、カードの隙間で頭頂だけ明るく抜ける。
							// 平均の明るさを根元の影で 8 割にした灰色を笠の頂点色にする。
							double sum = 0;
							for (int i = 0; i < px.size(); i++) { DWORD c = px[i]; sum += 0.11 * (c & 255) + 0.59 * ((c >> 8) & 255) + 0.30 * ((c >> 16) & 255); }
							const int g = Clampi((int)(sum / px.size() * 0.8 + 0.5), 16, 255);
							m_capC = RGBA8(g, g, g);
							MakeTex(m_dev, m_ctx, px.data(), (int)w, (int)h, TRUE, &m_hairTex, &m_hairSrv);
						}
					}
				}
				TAC_REL(cv); TAC_REL(fr); TAC_REL(dec); TAC_REL(st); TAC_REL(f);
			}
		}
	}
	if (!m_hairSrv) { ReleaseHair(); return; }
	m_hairNv = nv;
	LoadPulse(4, 4);
}

void Renderer::EnsureScalp(int model, int headJ)
{
	if (model == m_scalpModel && m_capDraw.ib) return;
	m_scalpModel = -1;
	m_capDraw.Release();
	m_capBind.clear();
	if (!m_dev || !m_ctx || headJ < 0) return;
	const CreatureSkin* sk = NULL;
	for (int k = 0; k < (int)m_skins.size(); k++)
		if (m_skins[k].model == model && !m_skins[k].bind.empty() && m_skins[k].crownOk) { sk = &m_skins[k]; break; }
	if (!sk || sk->skullC[1] < 0.4f) return;
	const int nv = (int)sk->bind.size();
	if (sk->headTri.size() >= 9) {
		const float yTop = sk->crown[1];
		const float capCx = sk->skullC[0], capCy = sk->skullC[1], capCz = sk->skullC[2];
		// モデルが変わるたびに新しい塊を切ると、本体の arena が数秒で埋まり、次に開いたとき落ちる。
		static Buf<int> map;
		map.fill(nv, -1);
		if (map.size() == nv) {
			static Buf<WORD> ib;
			ib.clear();
			for (int t = 0; t + 2 < (int)sk->headTri.size(); t += 3) {
				int ix[3] = { sk->headTri[t], sk->headTri[t + 1], sk->headTri[t + 2] };
				if (ix[0] < 0 || ix[1] < 0 || ix[2] < 0 || ix[0] >= nv || ix[1] >= nv || ix[2] >= nv) continue;
				float cy = 0.f, cz = 0.f;
				for (int c = 0; c < 3; c++) {
					cy += sk->bind[ix[c]].p[1];
					cz += sk->bind[ix[c]].p[2];
				}
				// 笠は地肌だけ。前は眉の上 7.5cm まで、横は耳の上、後ろは襟足まで。
				// 目や額まで包むと、カードの隙間から髪色の兜が顔に被って見える。
				const float dy = cy * (1.f / 3.f) - yTop, dz = cz * (1.f / 3.f) - capCz;
				if (!(dy > -0.075f || (dy > -0.11f && dz > -0.03f) || (dy > -0.17f && dz > 0.f))) continue;
				for (int c = 0; c < 3; c++) {
					int v = ix[c];
					if (map[v] < 0) {
						if (m_capBind.size() >= 60000) { map[v] = 0; continue; }
						const CSkinV& src = sk->bind[v];
						// メッシュ法線は内側を向いていることがある。頭蓋の中心から外へ 4mm、髪のカード（6mm）の下。
						float nx = src.p[0] - capCx, ny = src.p[1] - capCy, nz = src.p[2] - capCz;
						float nl = sqrtf(nx * nx + ny * ny + nz * nz);
						if (nl > 1e-6f) { nx /= nl; ny /= nl; nz /= nl; }
						else { nx = 0.f; ny = 1.f; nz = 0.f; }
						const float push = 0.004f;
						VtxS s;
						memset(&s, 0, sizeof(s));
						s.p[0] = src.p[0] + nx * push;
						s.p[1] = src.p[1] + ny * push;
						s.p[2] = src.p[2] + nz * push;
						s.n[0] = nx; s.n[1] = ny; s.n[2] = nz;
						s.c = m_capC;
						map[v] = m_capBind.size();
						m_capBind.push_back(s);
					}
					ib.push_back((WORD)map[v]);
				}
			}
			if (ib.size() >= 9 && m_capBind.size() >= 3) {
				UINT nIdx = (UINT)ib.size();
				if (ib.size() & 1) ib.push_back(0);
				D3D11_BUFFER_DESC bd = {};
				bd.ByteWidth = (UINT)ib.size() * 2u;
				bd.Usage = D3D11_USAGE_IMMUTABLE;
				bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
				D3D11_SUBRESOURCE_DATA sd = { ib.data(), 0, 0 };
				if (SUCCEEDED(m_dev->CreateBuffer(&bd, &sd, &m_capDraw.ib))) {
					m_capDraw.nIdx = nIdx;
					m_capDraw.stride = sizeof(VtxS);
					m_capDraw.tex = -1;
					m_capDraw.i32 = 0;
					m_scalpModel = model;
				}
			}
		}
	}
}

BOOL Renderer::DrawHumanHair(int model, int style, int headJ, const Pose& pose, const M4& world, const DrawParams& dp, int avatar)
{
	const BOOL capOnly = style < 1;
	if (!capOnly && (m_hairNv < 30 || !m_csSkin || !m_hairBind || !m_hairUAV || !m_hairDraw.ib || !m_cbSkinBone || !m_cbSkinParm || !m_cbHairCloth)) return FALSE;
	if (style < 0 || model < 0 || model >= (int)models.size() || headJ < 0 || headJ >= pose.nJ) return FALSE;
	if (capOnly) {
		EnsureScalp(model, headJ);
		if (!(m_capDraw.ib && m_capBind.size() >= 3)) return FALSE;
		const M4& B = pose.palette[headJ];
		static Buf<VtxS> capPosed;
		capPosed.resize(m_capBind.size());
		if (capPosed.size() != m_capBind.size()) return FALSE;
		for (int i = 0; i < m_capBind.size(); i++) {
			const VtxS& s = m_capBind[i];
			VtxS d = s;
			d.p[0] = s.p[0] * B.m[0] + s.p[1] * B.m[4] + s.p[2] * B.m[8] + B.m[12];
			d.p[1] = s.p[0] * B.m[1] + s.p[1] * B.m[5] + s.p[2] * B.m[9] + B.m[13];
			d.p[2] = s.p[0] * B.m[2] + s.p[1] * B.m[6] + s.p[2] * B.m[10] + B.m[14];
			float nx = s.n[0] * B.m[0] + s.n[1] * B.m[4] + s.n[2] * B.m[8];
			float ny = s.n[0] * B.m[1] + s.n[1] * B.m[5] + s.n[2] * B.m[9];
			float nz = s.n[0] * B.m[2] + s.n[1] * B.m[6] + s.n[2] * B.m[10];
			float nl = sqrtf(nx * nx + ny * ny + nz * nz);
			if (nl > 1e-6f) { nx /= nl; ny /= nl; nz /= nl; }
			d.n[0] = nx; d.n[1] = ny; d.n[2] = nz;
			d.c = dp.tint;
			capPosed[i] = d;
		}
		DrawParams dc = dp;
		dc.outline = 0.f;
		dc.twoSided = 1;
		dc.sway = 0.f;
		dc.rim = 0.02f;
		DrawStaticVerts(m_capDraw, capPosed.data(), (UINT)capPosed.size(), world, dc);
		return TRUE;
	}
	const Model& md = models[model];
	const AvaModel* am = NULL;
	for (const AvaModel& x : m_avaModels) if (!strcmp(x.name, md.name)) { am = &x; break; }
	int headSeg = -1;
	for (int s = 0; s < m_avaSegN; s++) if (!strcmp(m_avaSeg[s], "Head")) { headSeg = s; break; }
	if (!am || headSeg < 0 || am->M.size() < (size_t)(headSeg + 1) * 12) return FALSE;
	int nj = pose.nJ;
	if (nj > 63) nj = 63;
	if (headJ >= nj) return FALSE;
	EnsureScalp(model, headJ);
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(m_ctx->Map(m_cbSkinBone, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return FALSE;
	memset(ms.pData, 0, 64u * (unsigned)MAXJ);
	if (nj) memcpy(ms.pData, pose.palette, sizeof(M4) * (size_t)nj);
	float* slot = ((float*)ms.pData) + 63 * 16;
	const float* seg = &am->M[(size_t)headSeg * 12];
	// 区間行列は列ベクトル（平行移動は 3,7,11）。髪のコンピュートは行ベクトルなので転置して載せる。
	slot[0] = seg[0]; slot[1] = seg[4]; slot[2] = seg[8]; slot[3] = 0.f;
	slot[4] = seg[1]; slot[5] = seg[5]; slot[6] = seg[9]; slot[7] = 0.f;
	slot[8] = seg[2]; slot[9] = seg[6]; slot[10] = seg[10]; slot[11] = 0.f;
	slot[12] = seg[3]; slot[13] = seg[7]; slot[14] = seg[11]; slot[15] = 1.f;
	// 男性の頭区間は顔の天辺に合わせてあり、髪の天辺はその分だけ首側に残る。
	// 天辺は読み込み時に測って skin.crown に置く。描画のたびにバインドを読み直すと、
	// 戦闘用の領域を巻き戻したあと別の確保が頭の頂点を潰し、髪だけ顎へ落ちる。
	// 頭頂の型は輪で、天辺を頭蓋の天辺に揃えると渦の穴から頭が出る。1cm 上に置き、
	// 内側に入った分はシェーダが頭蓋の外へ出す。左右前後は頭蓋の外形の中心。
	const CreatureSkin* hs = NULL;
	for (int k = 0; k < (int)m_skins.size(); k++)
		if (m_skins[k].model == model && m_skins[k].crownOk) { hs = &m_skins[k]; break; }
	if (style <= 15 && m_hairCapN[style] >= 8 && hs) {
		const float yTop = hs->crown[1] + 0.010f, hx = hs->skullC[0], hz = hs->skullC[2];
		if (yTop > 0.4f) {
			// 顔合わせで縮んだ頭だけ、髪を同じ倍率に戻す。地肌の半径で拡げると正中が割れる。
			float sc = 1.f;
			float lin = sqrtf(seg[0] * seg[0] + seg[4] * seg[4] + seg[8] * seg[8]);
			if (lin > 0.2f && lin < 1.f) sc = 1.f / lin;
			if (sc > 1.45f) sc = 1.45f;
			float cx = m_hairCap[style][0] * sc, cy = m_hairCap[style][1] * sc, cz = m_hairCap[style][2] * sc;
			float rx = seg[0] * cx + seg[1] * cy + seg[2] * cz;
			float ry = seg[4] * cx + seg[5] * cy + seg[6] * cz;
			float rz = seg[8] * cx + seg[9] * cy + seg[10] * cz;
			slot[0] *= sc; slot[1] *= sc; slot[2] *= sc;
			slot[4] *= sc; slot[5] *= sc; slot[6] *= sc;
			slot[8] *= sc; slot[9] *= sc; slot[10] *= sc;
			slot[12] = hx - rx;
			slot[13] = yTop - ry;
			slot[14] = hz - rz;
		}
	}
	m_ctx->Unmap(m_cbSkinBone, 0);
	float cloth[36 + 4 + SKULL_NA * SKULL_NE];
	memset(cloth, 0, sizeof(cloth));
	if (hs && hs->skullC[1] > 0.4f) {
		cloth[36] = hs->skullC[0]; cloth[37] = hs->skullC[1]; cloth[38] = hs->skullC[2]; cloth[39] = 1.f;
		memcpy(cloth + 40, hs->skullR, sizeof(hs->skullR));
	}
	if (avatar > 0) {
		int ci = -1;
		for (int k = 0, n = 0; k < (int)m_avaCos.size(); k++) if (m_avaCos[k].gender == am->gender && ++n == avatar) { ci = k; break; }
		for (const AvaCache& c : m_avaCache) if (c.model == model && c.cos == ci && c.backN > 1 && c.backDy > 1e-5f) {
			cloth[0] = c.backY0;
			cloth[1] = c.backDy;
			cloth[2] = (float)c.backN;
			cloth[3] = 0.020f;
			int n = c.backN < 32 ? c.backN : 32;
			for (int i = 0; i < n; i++) cloth[4 + i] = c.backZ[i];
			break;
		}
	}
	if (FAILED(m_ctx->Map(m_cbHairCloth, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return FALSE;
	memcpy(ms.pData, cloth, sizeof(cloth));
	m_ctx->Unmap(m_cbHairCloth, 0);
	struct Parm { float nip0[4], nip1[4], bust[4], info[4]; unsigned extra[4]; float jig[4]; };
	Parm prm;
	memset(&prm, 0, sizeof(prm));
	prm.nip0[0] = (float)style;
	prm.nip0[1] = (float)headJ;
	prm.nip0[2] = m_time;
	prm.bust[0] = -1.f;
	prm.info[1] = (float)nj;
	prm.extra[1] = m_hairNv;
	if (FAILED(m_ctx->Map(m_cbSkinParm, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return FALSE;
	memcpy(ms.pData, &prm, sizeof(prm));
	m_ctx->Unmap(m_cbSkinParm, 0);
	ID3D11Buffer* noneVB = NULL;
	UINT zero = 0;
	m_ctx->IASetVertexBuffers(0, 1, &noneVB, &zero, &zero);
	ID3D11Buffer* cbs[3] = { m_cbSkinBone, m_cbSkinParm, m_cbHairCloth };
	m_ctx->CSSetConstantBuffers(0, 3, cbs);
	ID3D11ShaderResourceView* srvs[2] = { m_hairBind, m_faceDummySRV };
	m_ctx->CSSetShaderResources(0, 2, srvs);
	UINT cnt = 0;
	m_ctx->CSSetUnorderedAccessViews(0, 1, &m_hairUAV, &cnt);
	m_ctx->CSSetShader(m_csSkin, NULL, 0);
	m_ctx->Dispatch((m_hairNv + 63u) / 64u, 1, 1);
	ID3D11UnorderedAccessView* nou = NULL;
	ID3D11ShaderResourceView* nos[3] = { NULL, NULL, NULL };
	ID3D11Buffer* nob[3] = { NULL, NULL, NULL };
	m_ctx->CSSetUnorderedAccessViews(0, 1, &nou, NULL);
	m_ctx->CSSetShaderResources(0, 3, nos);
	m_ctx->CSSetConstantBuffers(0, 3, nob);
	m_ctx->CSSetShader(NULL, NULL, 0);
	if (m_capDraw.ib && m_capBind.size() >= 3 && headJ < pose.nJ) {
		const M4& B = pose.palette[headJ];
		static Buf<VtxS> posed;
		posed.resize(m_capBind.size());
		if (posed.size() == m_capBind.size()) {
			for (int i = 0; i < m_capBind.size(); i++) {
				const VtxS& s = m_capBind[i];
				VtxS d = s;
				d.p[0] = s.p[0] * B.m[0] + s.p[1] * B.m[4] + s.p[2] * B.m[8] + B.m[12];
				d.p[1] = s.p[0] * B.m[1] + s.p[1] * B.m[5] + s.p[2] * B.m[9] + B.m[13];
				d.p[2] = s.p[0] * B.m[2] + s.p[1] * B.m[6] + s.p[2] * B.m[10] + B.m[14];
				float nx = s.n[0] * B.m[0] + s.n[1] * B.m[4] + s.n[2] * B.m[8];
				float ny = s.n[0] * B.m[1] + s.n[1] * B.m[5] + s.n[2] * B.m[9];
				float nz = s.n[0] * B.m[2] + s.n[1] * B.m[6] + s.n[2] * B.m[10];
				float nl = sqrtf(nx * nx + ny * ny + nz * nz);
				if (nl > 1e-6f) { nx /= nl; ny /= nl; nz /= nl; }
				d.n[0] = nx; d.n[1] = ny; d.n[2] = nz;
				posed[i] = d;
			}
			DrawParams dc = dp;
			dc.outline = 0.f;
			dc.twoSided = 1;
			dc.sway = 0.f;
			dc.rim = 0.02f;
			DrawStaticVerts(m_capDraw, posed.data(), (UINT)posed.size(), world, dc);
		}
	}
	ID3D11Buffer* saved = m_hairDraw.vb;
	m_hairDraw.vb = m_hairVB;
	DrawStatic(m_hairDraw, world, dp);
	m_hairDraw.vb = saved;
	return TRUE;
}

float Renderer::AvatarClothBack(int model, int avatar, float y)
{
	if (model < 0 || model >= (int)models.size() || avatar <= 0 || m_avaSegN <= 0) return -1e9f;
	const AvaModel* am = NULL;
	for (const AvaModel& x : m_avaModels) if (!strcmp(x.name, models[model].name)) { am = &x; break; }
	if (!am) return -1e9f;
	int ci = -1;
	for (int k = 0, n = 0; k < (int)m_avaCos.size(); k++) if (m_avaCos[k].gender == am->gender && ++n == avatar) { ci = k; break; }
	if (ci < 0) return -1e9f;
	const AvaCache* ac = NULL;
	for (const AvaCache& x : m_avaCache) if (x.model == model && x.cos == ci) { ac = &x; break; }
	if (!ac || ac->backN < 2 || ac->backDy < 1e-5f) return -1e9f;
	float t = (y - ac->backY0) / ac->backDy;
	if (t < 0.f || t > (float)(ac->backN - 1)) return -1e9f;
	int i = (int)t;
	if (i < 0) return ac->backZ[0];
	if (i >= ac->backN - 1) return ac->backZ[ac->backN - 1];
	float f = t - (float)i;
	return ac->backZ[i] * (1.f - f) + ac->backZ[i + 1] * f;
}

void Renderer::DrawGear(int i, const M4& world, const DrawParams& dp)
{
	if (i < 0 || i >= (int)m_gear.size()) return;
	DrawStatic(m_gear[i], world, dp);
}

int Renderer::FindModel(const char* n) const
{
	if (!n) return -1;
	for (int i = 0; i < (int)models.size(); i++) if (models[i].live && !strcmp(models[i].name, n)) return i;
	return -1;
}
int Renderer::FindAttach(const char* n) const { for (int i = 0; i < (int)attaches.size(); i++) if (!strcmp(attaches[i].name, n)) return i; return -1; }
int Renderer::FindStatic(const char* n) const { for (int i = 0; i < (int)statics.size(); i++) if (!strcmp(statics[i].name, n)) return i; return -1; }

int Renderer::AddProcModel(const Model& m, const Buf<VtxK>& v, const Buf<WORD>& idx)
{
	Model mm = m;
	mm.live = 1;
	mm.parts.reset();
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
	if (g_avaNeedReset) {
		g_avaNeedReset = 0;
		ReleaseAvatarCache();
		TacAvaReset();
	}
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
	m_fcb.fogColor[0] = (c & 255) / 255.f; m_fcb.fogColor[1] = ((c >> 8) & 255) / 255.f; m_fcb.fogColor[2] = ((c >> 16) & 255) / 255.f; m_fcb.fogColor[3] = (float)m_theme;
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
		// 枠の色は消さないと、選択順で枠が入れ替わったとき前の髪が残る。
		ID3D11DeviceContext1* c1 = NULL;
		if (SUCCEEDED(m_ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&c1)) && c1) {
			float zc[4] = { 0, 0, 0, 0 };
			D3D11_RECT rc;
			rc.left = sx; rc.top = sy; rc.right = sx + PORTRAIT_PX; rc.bottom = sy + PORTRAIT_PX;
			c1->ClearView(m_ptRtv, zc, &rc, 1);
			c1->Release();
		}
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
	if (m_groundSrv) m_ctx->PSSetShaderResources(3, 1, &m_groundSrv);
	if (m_matSrv) m_ctx->PSSetShaderResources(4, 1, &m_matSrv);
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

static void ProjectSun(const M4& vp, V3 eye, const float* light, float& u, float& v, float& vis)
{
	V3 p = v3(eye.x + light[0] * 90.f, eye.y + light[1] * 90.f, eye.z + light[2] * 90.f);
	const float* m = vp.m;
	float x = p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12];
	float y = p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13];
	float w = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
	u = .5f; v = .15f; vis = 0;
	if (w < .25f) return;
	u = x / w * .5f + .5f;
	v = .5f - y / w * .5f;
	if (u > -.45f && u < 1.45f && v > -.35f && v < 1.3f) vis = 1.f;
}

void Renderer::DrawSky(DWORD top, DWORD bottom, int lines, DWORD lineColor)
{
	DrawParams dp;
	dp.tint = top;
	dp.mode = (float)lines;
	dp.dissolve = (bottom & 255) / 255.f; dp.flash = ((bottom >> 8) & 255) / 255.f; dp.rim = ((bottom >> 16) & 255) / 255.f;
	dp.outlineColor = lineColor;
	float su, sv, svis;
	ProjectSun(m_vp, m_eye, m_fcb.light, su, sv, svis);
	dp.emissive = su; dp.hue = sv; dp.sway = svis;
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

void Renderer::DrawStaticVerts(const GpuMesh& m, const VtxS* v, UINT nv, const M4& world, const DrawParams& dp)
{
	if (!m.ib || !v || !nv || !m_vbDyn || nv * sizeof(VtxS) > m_vbDynBytes) { DrawStatic(m, world, dp); return; }
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(m_ctx->Map(m_vbDyn, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
	memcpy(ms.pData, v, nv * sizeof(VtxS));
	m_ctx->Unmap(m_vbDyn, 0);
	GpuMesh d = m;
	d.vb = m_vbDyn;
	d.stride = sizeof(VtxS);
	DrawStatic(d, world, dp);
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
	ID3D11ShaderResourceView* srv = (m.tex == -2 && m_hairSrv) ? m_hairSrv : SrvFor(m.tex);
	m_ctx->PSSetShaderResources(0, 1, &srv);
	BOOL alpha = ((dp.tint >> 24) & 255) < 255;
	m_ctx->OMSetBlendState(alpha ? m_bsAlpha : m_bsOpaque, NULL, 0xFFFFFFFF);
	if (dp.outline > 0 && !alpha) {
		SetObj(world, dp, dp.outline);
		BindForPass(FALSE, TRUE);
		// 輪郭は表を捨てて膨らませた裏面だけ残す。両面にすると殻が本体を覆い、敵が黒く見える。
		m_ctx->DrawIndexed(m.nIdx, 0, 0);
	}
	SetObj(world, dp, 0);
	BindForPass(FALSE, FALSE);
	if (m_psOverride) m_ctx->PSSetShader(m_psOverride, NULL, 0);
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
	dp.rim = 0;
	m_psOverride = (m_theme >= 0 && m_theme < 8 && m_psGround[m_theme]) ? m_psGround[m_theme] : NULL;
	if (m_terrain.vb) DrawStatic(m_terrain, MIdent(), dp);
	m_psOverride = m_psProp;
	for (auto& b : m_props) {
		dp.emissive = b.mat;
		dp.hue = 0;
		dp.rim = 0.02f;
		dp.outline = 0;
		DrawStatic(b.mesh, MIdent(), dp);
	}
	m_psOverride = m_psLava;
	if (m_lava.vb) {
		DrawParams lp;
		lp.mode = 2.f;
		DrawStatic(m_lava, MIdent(), lp);
	}
	m_psOverride = NULL;
}

void Renderer::DrawWater()
{
	if (!m_water.vb || m_pass == PASS_SHADOW) return;
	DrawParams dp;
	dp.mode = 1.f;
	dp.tint = (m_waterColor & 0xFFFFFF) | 0xF0000000;
	m_ctx->OMSetDepthStencilState(m_dsRead, 0);
	m_psOverride = m_psWater;
	DrawStatic(m_water, MIdent(), dp);
	m_psOverride = NULL;
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

void Renderer::DrawRibbon(const Buf<VtxP>& v)
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
	Buf<VtxS> v;
	Buf<UINT> i;
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
	m_theme = Clampi(m.theme, 0, 7);
	m_terrain.Release(); m_water.Release(); m_lava.Release();
	for (auto& b : m_props) b.mesh.Release();
	m_props.clear();
	static MeshBuild tb, wb, lb, wood;
	tb.v.clear(); tb.i.clear(); wb.v.clear(); wb.i.clear(); lb.v.clear(); lb.i.clear(); wood.v.clear(); wood.i.clear();
	tb.v.reserve(80000); tb.i.reserve(120000);
	const float bottom = -3.2f;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		const Cell& c = m.At(x, z);
		if (c.t == T_VOID) continue;
		float y = m.TopY(x, z);
		DWORD col = (c.t == T_CHASM) ? RGBAf(.35f, .32f, .33f) : 0xFFFFFFFF;
		if (c.t == T_WATER) col = 0xFEFFFFFF;
		if (c.t != T_BRIDGE) {
			tb.Quad(v3((float)x, y, (float)z), v3((float)x, y, (float)z + 1), v3((float)x + 1, y, (float)z + 1), v3((float)x + 1, y, (float)z), v3(0, 1, 0),
				0, 0, 1, 1, -1, col);
		} else {
			wood.Quad(v3((float)x, y, (float)z), v3((float)x, y, (float)z + 1), v3((float)x + 1, y, (float)z + 1), v3((float)x + 1, y, (float)z), v3(0, 1, 0),
				0, 0, 1, 1, -1, 0xFFFFFFFF);
			int wxn = 0, wzn = 0;
			const Cell& xp = Map::In(x + 1, z) ? m.At(x + 1, z) : c;
			const Cell& xn = Map::In(x - 1, z) ? m.At(x - 1, z) : c;
			const Cell& zp = Map::In(x, z + 1) ? m.At(x, z + 1) : c;
			const Cell& zn = Map::In(x, z - 1) ? m.At(x, z - 1) : c;
			if (xp.t == T_WATER || xp.t == T_CHASM || xp.t == T_LAVA) wxn++;
			if (xn.t == T_WATER || xn.t == T_CHASM || xn.t == T_LAVA) wxn++;
			if (zp.t == T_WATER || zp.t == T_CHASM || zp.t == T_LAVA) wzn++;
			if (zn.t == T_WATER || zn.t == T_CHASM || zn.t == T_LAVA) wzn++;
			float cx = x + .5f, cz = z + .5f;
			if (wzn >= wxn) {
				AddBox(wood, v3(cx, y + .18f, (float)z + .08f), v3(.46f, .16f, .07f), 0xFFFFFFFF, -1);
				AddBox(wood, v3(cx, y + .18f, (float)z + .92f), v3(.46f, .16f, .07f), 0xFFFFFFFF, -1);
			} else {
				AddBox(wood, v3((float)x + .08f, y + .18f, cz), v3(.07f, .16f, .46f), 0xFFFFFFFF, -1);
				AddBox(wood, v3((float)x + .92f, y + .18f, cz), v3(.07f, .16f, .46f), 0xFFFFFFFF, -1);
			}
		}
		static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
		for (int k = 0; k < 4; k++) {
			int nx = x + dx[k], nz = z + dz[k];
			float ny = Map::In(nx, nz) ? m.TopY(nx, nz) : bottom;
			if (ny >= y - 1e-3f) continue;
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
				DWORD sc = 0xFFFFFFFF;
				float v0 = 0, v1 = (yt - yb) / (kHStep * 2.f);
				tb.Quad(v3(ex0, yt, ez0), v3(ex1, yt, ez1), v3(ex1, yb, ez1), v3(ex0, yb, ez0), n, 0, v0, 1, v1, -1, sc);
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

	// 小物。木・岩・橋は写真テクスチャ、旗やランタンはモデル自身のテクスチャ。
	static MeshBuild treeMb, rockMb, proc, tentMb, flagMb, lilyMb, plantMb, lantMb, pumpMb;
	treeMb.v.clear(); treeMb.i.clear(); rockMb.v.clear(); rockMb.i.clear(); proc.v.clear(); proc.i.clear();
	tentMb.v.clear(); tentMb.i.clear(); flagMb.v.clear(); flagMb.i.clear(); lilyMb.v.clear(); lilyMb.i.clear();
	plantMb.v.clear(); plantMb.i.clear(); lantMb.v.clear(); lantMb.i.clear(); pumpMb.v.clear(); pumpMb.i.clear();
	auto flush = [&](MeshBuild& mb, int tex, float mat) {
		if (mb.v.empty()) return;
		PropBatch pb;
		pb.hue = 0;
		pb.mat = mat;
		if (MakeMesh32(pb.mesh, mb.v.data(), sizeof(VtxS), (UINT)mb.v.size(), mb.i.data(), (UINT)mb.i.size())) {
			pb.mesh.tex = tex;
			m_props.push_back(pb);
		}
	};
	if (m_assetsOk) {
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
				place(treeMb, si, cx + jx, cy, cz + jz, 1.35f + (c.var % 5) * .08f, rot, tint, 1.f);
				if ((c.var & 6) == 2) place(treeMb, (c.var & 8) ? sTreeA : sTreeB, cx - .28f, cy, cz + .25f, 1.0f, rot * 1.7f, tint, 1.f);
				break; }
			case PK_TREES: {
				int si = (c.var % 3 == 0) ? sTreesA : ((c.var % 3 == 1) ? sTreesB : sTreesL);
				place(treeMb, si, cx, cy, cz, .56f, rot, tint, .8f);
				break; }
			case PK_HILLTREES: place(treeMb, sHillT, cx, cy, cz, .55f, rot, tint, .5f); break;
			case PK_PINE: {
				int si = (c.var & 1) ? sPineO : sPineY;
				float sc = si == sPineO ? .22f : .27f;
				place(treeMb, si, cx + jx, cy, cz + jz, sc + (c.var % 4) * .01f, rot, tint, 1.f);
				break; }
			case PK_DEAD: {
				int si = (c.var & 1) ? sDeadL : sDeadM;
				place(treeMb, si, cx + jx, cy - .05f, cz + jz, si == sDeadL ? .3f : .34f, rot, tint, .15f);
				break; }
			case PK_ROCK: {
				int si = (c.var % 3 == 0) ? sRockA : ((c.var % 3 == 1) ? sRockC : sRockE);
				place(rockMb, si, cx, cy, cz, si == sRockA ? 3.2f : 2.6f, rot, tint, 0);
				break; }
			case PK_MOUNT: {
				int si = (m.theme == TH_PLAINS || m.theme == TH_FOREST) && (c.var & 3) == 0 ? sMtG : ((c.var % 3 == 0) ? sMtA : (c.var % 3 == 1 ? sMtB : sMtC));
				place(rockMb, si, cx, cy - .05f, cz, .62f + (c.var % 4) * .03f, rot, tint, 0);
				break; }
			case PK_LILY:
				place((c.var & 1) ? lilyMb : plantMb, (c.var & 1) ? sLily : sPlant, cx + jx * 3, m.StandY(x, z) - .01f, cz + jz * 3, 3.f, rot, tint, 0);
				break;
			case PK_PILLAR: {
				float hgt = .9f + (c.var % 5) * .18f;
				AddBox(proc, v3(cx, cy + .06f, cz), v3(.36f, .06f, .36f), tint, -1);
				AddBox(proc, v3(cx, cy + .12f + hgt * .5f, cz), v3(.24f, hgt * .5f, .24f), tint, -1);
				if ((c.var & 3) != 0) AddBox(proc, v3(cx, cy + .12f + hgt + .06f, cz), v3(.32f, .06f, .32f), tint, -1);
				break; }
			case PK_GRAVE: place(rockMb, sGrave, cx, cy, cz, .42f, rot, tint, 0); break;
			case PK_TENT: place(tentMb, sTent, cx, cy, cz, 1.9f, rot, tint, 0); break;
			case PK_FLAG: place(flagMb, sFlag, cx, cy, cz, 3.f, rot, tint, .6f); break;
			case PK_BARREL: place(wood, sBarrel, cx, cy, cz, 2.6f, rot, tint, 0); place(wood, sBarrel, cx + .25f, cy, cz - .2f, 2.2f, rot, tint, 0); break;
			case PK_CRATE: place(wood, sCrate, cx, cy, cz, 2.6f, rot, tint, 0); break;
			case PK_LANTERN: place(lantMb, sLant, cx + .3f, cy, cz + .3f, .6f, rot, tint, 0); break;
			case PK_PUMPKIN: place(pumpMb, sPump, cx, cy, cz, .45f, rot, tint, 0); break;
			}
		}
		auto texOf = [&](int si) { return si >= 0 ? statics[si].tex : TEX_WHITE; };
		flush(treeMb, TEX_WHITE, 0);
		flush(rockMb, TEX_WHITE, 1);
		flush(proc, TEX_WHITE, 4);
		flush(tentMb, texOf(sTent), 3);
		flush(flagMb, texOf(sFlag), 3);
		flush(lilyMb, texOf(sLily), 3);
		flush(plantMb, texOf(sPlant), 3);
		flush(lantMb, texOf(sLant), 3);
		flush(pumpMb, texOf(sPump), 3);
	}
	flush(wood, TEX_WHITE, 2);
	BuildMinimap(m);
	return TRUE;
}

void Renderer::BuildMinimap(const Map& m)
{
	TAC_REL(m_srvMini); TAC_REL(m_texMini);
	static Buf<DWORD> px;
	px.resize(MAPN * MAPN);
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
BOOL Renderer::CreatureExtent(int model, float& cx, float& cy, float& cz, float& rad) const
{
	for (const auto& s : m_skins) {
		if (s.model != model || s.extR <= 0.02f) continue;
		cx = s.extX; cy = s.extY; cz = s.extZ; rad = s.extR;
		return TRUE;
	}
	return FALSE;
}

BOOL Renderer::CreaturePosedExtent(int model, const Pose& pose, float& cx, float& cy, float& cz, float& rad) const
{
	if (pose.nJ <= 0) return FALSE;
	int nj = pose.nJ;
	if (nj > MAXJ) nj = MAXJ;
	float mn[3] = { 1e9f, 1e9f, 1e9f };
	float mx[3] = { -1e9f, -1e9f, -1e9f };
	int hit = 0;
	for (const auto& s : m_skins) {
		if (s.model != model || s.bind.empty()) continue;
		for (const CSkinV& v : s.bind) {
			M4 acc;
			memset(&acc, 0, sizeof(acc));
			float ws = 0.f;
			for (int c = 0; c < 4; c++) {
				int j = v.j[c];
				float w = v.w[c] / 255.f;
				if (w <= 0.f || j < 0 || j >= nj) continue;
				const float* bm = pose.palette[j].m;
				for (int k = 0; k < 16; k++) acc.m[k] += bm[k] * w;
				ws += w;
			}
			if (ws < 0.01f) continue;
			V3 p = MXformPt(acc, v3(v.p[0], v.p[1], v.p[2]));
			if (p.x < mn[0]) mn[0] = p.x;
			if (p.y < mn[1]) mn[1] = p.y;
			if (p.z < mn[2]) mn[2] = p.z;
			if (p.x > mx[0]) mx[0] = p.x;
			if (p.y > mx[1]) mx[1] = p.y;
			if (p.z > mx[2]) mx[2] = p.z;
			hit++;
		}
	}
	if (hit < 8 || mx[0] <= mn[0]) return FALSE;
	float hx = (mx[0] - mn[0]) * .5f, hy = (mx[1] - mn[1]) * .5f, hz = (mx[2] - mn[2]) * .5f;
	cx = (mn[0] + mx[0]) * .5f;
	cy = (mn[1] + mx[1]) * .5f;
	cz = (mn[2] + mx[2]) * .5f;
	rad = max(hx, max(hy, hz));
	return rad > 1e-4f;
}

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
BOOL Renderer::ApplyPost()
{
	if (m_inPortrait || !m_psBloom || !m_psBlur || !m_psGrade || !m_sceneTex || !m_blA || !m_blB || !m_bb) return FALSE;
	ID3D11RenderTargetView* nullRT = NULL;
	m_ctx->OMSetRenderTargets(0, &nullRT, NULL);
	ID3D11ShaderResourceView* unb[2] = { NULL, NULL };
	m_ctx->PSSetShaderResources(0, 2, unb);
	if (m_msaa > 1 && m_msTex)
		m_ctx->ResolveSubresource(m_sceneTex, 0, m_msTex, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
	else
		m_ctx->CopyResource(m_sceneTex, m_bb);

	auto fs = [&](ID3D11PixelShader* ps, ID3D11ShaderResourceView* t0, ID3D11ShaderResourceView* t1, ID3D11RenderTargetView* dst, int w, int h, const DrawParams& dp, float p0) {
		m_ctx->OMSetRenderTargets(0, &nullRT, NULL);
		m_ctx->PSSetShaderResources(0, 2, unb);
		m_ctx->OMSetRenderTargets(1, &dst, NULL);
		D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
		m_ctx->RSSetViewports(1, &vp);
		SetObj(MIdent(), dp, p0);
		ID3D11ShaderResourceView* srv[2] = { t0, t1 };
		m_ctx->PSSetShaderResources(0, 2, srv);
		m_ctx->VSSetShader(m_vsQ, NULL, 0);
		m_ctx->PSSetShader(ps, NULL, 0);
		m_ctx->IASetInputLayout(NULL);
		m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_ctx->RSSetState(m_rsNone);
		m_ctx->OMSetDepthStencilState(m_dsOff, 0);
		m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
		m_ctx->PSSetSamplers(0, 1, &m_sClamp);
		m_ctx->Draw(3, 0);
	};
	DrawParams ex;
	ex.mode = 0.72f;
	fs(m_psBloom, m_sceneSrv, NULL, m_blARtv, m_blW, m_blH, ex, 0);
	for (int pass = 0; pass < 2; pass++) {
		float rad = pass ? 2.2f : 1.f;
		DrawParams bh; bh.mode = 0; bh.emissive = 1.f / m_blW; bh.hue = rad;
		fs(m_psBlur, m_blASrv, NULL, m_blBRtv, m_blW, m_blH, bh, 1.f);
		DrawParams bv; bv.mode = 1.f; bv.emissive = 1.f / m_blH; bv.hue = rad;
		fs(m_psBlur, m_blBSrv, NULL, m_blARtv, m_blW, m_blH, bv, 0);
	}
	float su, sv, svis;
	ProjectSun(m_vp, m_eye, m_fcb.light, su, sv, svis);
	DrawParams gr;
	gr.mode = 0.62f; gr.emissive = su; gr.hue = sv; gr.rim = 1.f; gr.sway = svis;
	fs(m_psGrade, m_sceneSrv, m_blASrv, m_bbRtv, m_w, m_h, gr, 0);
	m_ctx->PSSetShaderResources(0, 2, unb);
	ID3D11SamplerState* ss[2] = { m_sLin, m_sCmp };
	m_ctx->PSSetSamplers(0, 2, ss);
	return TRUE;
}

void Renderer::BeginHud()
{
	if (!ApplyPost() && m_msaa > 1 && m_msTex && m_bb)
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
	HudGlass(x, y, w, h, fill, border, alpha);
}

void Renderer::HudGlass(float x, float y, float w, float h, DWORD fill, DWORD border, float alpha)
{
	if (!m_psUi || !m_vbHud || w < 4.f || h < 4.f) {
		HudRectGrad(x + 2, y + 2, w - 4, h - 4, MulA(Shade(fill, 1.12f), alpha), MulA(Shade(fill, .82f), alpha));
		DWORD b = MulA(border, alpha);
		HudRect(x + 3, y, w - 6, 2, b); HudRect(x + 3, y + h - 2, w - 6, 2, b);
		HudRect(x, y + 3, 2, h - 6, b); HudRect(x + w - 2, y + 3, 2, h - 6, b);
		HudRect(x + 1, y + 1, 2, 2, b); HudRect(x + w - 3, y + 1, 2, 2, b); HudRect(x + 1, y + h - 3, 2, 2, b); HudRect(x + w - 3, y + h - 3, 2, 2, b);
		HudRect(x + 4, y + 4, w - 8, 1, MulA(RGBA8(255, 255, 255, 60), alpha));
		return;
	}
	FlushHud();
	float rad = (w < h ? w : h) * 0.16f;
	if (rad > 14.f) rad = 14.f;
	if (rad < 5.f) rad = 5.f;
	float a = alpha * ((fill >> 24) & 255) / 255.f;
	DrawParams dp;
	dp.tint = border;
	dp.mode = h;
	dp.emissive = rad;
	dp.hue = a;
	SetObj(MIdent(), dp, w);
	DWORD fc = (fill & 0x00FFFFFF) | 0xFF000000;
	VtxH v[6] = {
		{ x, y, 0, 0, fc }, { x + w, y, 1, 0, fc }, { x + w, y + h, 1, 1, fc },
		{ x, y, 0, 0, fc }, { x + w, y + h, 1, 1, fc }, { x, y + h, 0, 1, fc },
	};
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(m_ctx->Map(m_vbHud, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
	memcpy(ms.pData, v, sizeof(v));
	m_ctx->Unmap(m_vbHud, 0);
	m_ctx->IASetInputLayout(m_ilH);
	m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	UINT st = sizeof(VtxH), o = 0;
	m_ctx->IASetVertexBuffers(0, 1, &m_vbHud, &st, &o);
	m_ctx->VSSetShader(m_vsH, NULL, 0);
	m_ctx->PSSetShader(m_psUi, NULL, 0);
	m_ctx->RSSetState(m_rsHud);
	m_ctx->OMSetDepthStencilState(m_dsOff, 0);
	m_ctx->OMSetBlendState(m_bsPremul, NULL, 0xFFFFFFFF);
	m_ctx->Draw(6, 0);
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
	wchar_t key[TAC_TEXTK];
	_snwprintf_s(key, _TRUNCATE, L"%d|%d|%d|%d|%d|%s", (int)(px * 4), bold, shadow, box ? (int)boxW : 0, box ? (int)boxH : 0, s);
	int slot = -1, oldest = -1;
	DWORD oldestF = 0;
	for (int ti = 0; ti < TAC_TEXTN; ti++) {
		if (!m_text[ti].live) { if (slot < 0) slot = ti; continue; }
		if (!wcscmp(m_text[ti].key, key)) { m_text[ti].sp.lastFrame = m_frame; return &m_text[ti].sp; }
		if (oldest < 0 || m_text[ti].sp.lastFrame < oldestF) { oldest = ti; oldestF = m_text[ti].sp.lastFrame; }
	}
	if (slot < 0) slot = oldest >= 0 ? oldest : 0;
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
	if (m_text[slot].live) { TAC_REL(m_text[slot].sp.srv); TAC_REL(m_text[slot].sp.tex); }
	m_text[slot].live = 1;
	wcsncpy_s(m_text[slot].key, key, _TRUNCATE);
	m_text[slot].sp = ts;
	return &m_text[slot].sp;
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
	/* Present(1) は未合成の HWND でフレームが退役せず、UI スレッドがクリックまで止まる。
	   キャプションの WM_PAINT もその間来ない。常に待たない。 */
	HRESULT hr = m_swap->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
	if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
		const DWORD now = GetTickCount();
		if (vis && now - m_lastRebind > 150) {
			m_lastRebind = now;
			m_presented = FALSE;
			m_waitStuck = 0;
			RefreshSurface();
		}
		return;
	}
	if (hr == DXGI_STATUS_OCCLUDED) return;
	if (SUCCEEDED(hr) && vis) {
		m_presented = TRUE;
		m_waitStuck = 0;
	}
	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) { m_lost = TRUE; m_lastHr = hr; }
}

void Renderer::EndFrameTextGc()
{
	int live = 0;
	for (int ti = 0; ti < TAC_TEXTN; ti++) if (m_text[ti].live) live++;
	if (live < 400) return;
	for (int ti = 0; ti < TAC_TEXTN; ti++) {
		if (!m_text[ti].live) continue;
		if (m_frame - m_text[ti].sp.lastFrame > 300) { TAC_REL(m_text[ti].sp.srv); TAC_REL(m_text[ti].sp.tex); m_text[ti].live = 0; }
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
