// OmakeTacticsRender.cpp — おまけタクティクス: DirectX11 描画

#include "stdafx.h"
#include "resource.h"
#include "OmakeTacticsRender.h"
#include "OmakeTacticsText.h"
#include "Soft3DTexRes.h"
#include "Soft3DTextD2D.h"
#include "simd_kernels.h"
#include <d3dcompiler.h>
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <wincodec.h>
#include <algorithm>
#include <stdio.h>
#include <stdarg.h>
#include <immintrin.h>

#pragma comment(lib, "windowscodecs.lib")

#define TAC_REL(p) do { if (p) { (p)->Release(); (p) = NULL; } } while (0)

namespace otac {

static void TacBindCacheDrop();

// 描画コマンドは固定配列。new / delete / std::vector は使わない。
enum { TAC_DRAWQ = 4096 };
struct TacObjCB { M4 w; float tint[4], params[4], extra[4], oc[4]; };
struct TacDrawCmd {
	ID3D11Buffer* vb;
	ID3D11Buffer* ib;
	ID3D11ShaderResourceView* srv;
	ID3D11VertexShader* vs;
	ID3D11HullShader* hs;
	ID3D11DomainShader* dom;
	ID3D11PixelShader* ps;
	ID3D11InputLayout* il;
	ID3D11BlendState* blend;
	ID3D11RasterizerState* rs;
	ID3D11DepthStencilState* depth;
	UINT nIdx;
	UINT iBase;
	UINT stride;
	UINT vbOff;
	DXGI_FORMAT ibFmt;
	D3D11_PRIMITIVE_TOPOLOGY topo;
	TacObjCB obj;
	unsigned seq;
	unsigned pair;
	unsigned char pre;
	unsigned char alpha;
};
static unsigned g_drawPair = 1;
static unsigned g_drawSeq = 1;
static TacDrawCmd g_drawQ[TAC_DRAWQ];
static int g_drawN = 0;
static BOOL g_drawFlush = FALSE;
static TacObjCB g_objGPU;
static BOOL g_objValid = FALSE;
enum { TAC_CB_POOL = 4096 };
static ID3D11Buffer* g_cbPool[TAC_CB_POOL];
static int g_cbPoolN = 0;
static int g_cbPoolI = 0;
// 同じ動的定数を描画の途中で DISCARD すると、GPU が読み終わるまで CPU が止まり、その間 GPU も空く。
// 枠を回して、飛行中のバッファは触らない。
enum { TAC_CB_RING = 256 };
struct TacCbRing { ID3D11Buffer* b[TAC_CB_RING]; int n, i; ID3D11Buffer* cur; };
static TacCbRing g_frameR, g_airR, g_boneR, g_skinBoneR, g_skinParmR, g_clothR, g_softR, g_hairSimR, g_hairClothR;
struct ClothPend {
	Renderer::AvaCache* ac;
	void* ci;
	ID3D11UnorderedAccessView* vert;
	ID3D11Buffer* clothB;
	ID3D11Buffer* boneB;
	ID3D11Buffer* softB;
	unsigned char parm[1536];
	int cloth, soft;
};
static ClothPend g_pend[Renderer::CLOTH_INST];
static int g_pendN, g_pendBusy;
static ID3D11Buffer* g_clothGate;
static ID3D11Buffer* g_clStB; static ID3D11UnorderedAccessView* g_clStU;
static ID3D11Buffer* g_clCnB; static ID3D11ShaderResourceView* g_clCnS;
static ID3D11Buffer* g_clPfB; static ID3D11ShaderResourceView* g_clPfS;
static ID3D11Buffer* g_clPmB; static ID3D11ShaderResourceView* g_clPmS;
struct HairPend {
	void* hi;
	ID3D11UnorderedAccessView* vert;
	ID3D11Buffer* simB;
	ID3D11Buffer* clothB;
	unsigned char parm[608];
	unsigned char cloth[672];
	int vn;
};
static HairPend g_hair[Renderer::HAIR_INST];
static int g_hairN, g_hairBusy;
static ID3D11Buffer* g_hStB; static ID3D11UnorderedAccessView* g_hStU;
static ID3D11Buffer* g_hPmB; static ID3D11ShaderResourceView* g_hPmS;
static void TacCbRingFree(TacCbRing& r)
{
	for (int i = 0; i < r.n; i++) TAC_REL(r.b[i]);
	r.n = 0; r.i = 0; r.cur = NULL;
}
static void TacCbRingMake(ID3D11Device* dev, TacCbRing& r, UINT bytes)
{
	TacCbRingFree(r);
	if (!dev || !bytes) return;
	D3D11_BUFFER_DESC bd = {};
	bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.ByteWidth = (bytes + 15u) & ~15u;
	for (int i = 0; i < TAC_CB_RING; i++) {
		if (FAILED(dev->CreateBuffer(&bd, NULL, &r.b[i]))) break;
		r.n++;
	}
}
static ID3D11Buffer* TacCbAcquire(TacCbRing& r, ID3D11DeviceContext* ctx, ID3D11Buffer* fallback, D3D11_MAPPED_SUBRESOURCE& ms)
{
	ID3D11Buffer* b = (r.n > 0) ? r.b[r.i] : fallback;
	if (!ctx || !b) return NULL;
	if (FAILED(ctx->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)) || !ms.pData) return NULL;
	if (r.n > 0) { r.i++; if (r.i >= r.n) r.i = 0; }
	r.cur = b;
	return b;
}
static ID3D11Buffer* TacCbCur(const TacCbRing& r, ID3D11Buffer* fallback)
{
	return r.cur ? r.cur : fallback;
}
static ID3D11Buffer* g_cbBound = NULL;

static BOOL TacQueueUses(ID3D11Buffer* vb)
{
	if (!vb) return FALSE;
	for (int i = 0; i < g_drawN; ++i) if (g_drawQ[i].vb == vb) return TRUE;
	return FALSE;
}

static void TacFillObj(TacObjCB& o, const M4& world, const DrawParams& dp, float outlineW)
{
	o.w = world;
	o.tint[0] = (dp.tint & 255) / 255.f; o.tint[1] = ((dp.tint >> 8) & 255) / 255.f; o.tint[2] = ((dp.tint >> 16) & 255) / 255.f; o.tint[3] = ((dp.tint >> 24) & 255) / 255.f;
	o.params[0] = outlineW; o.params[1] = dp.mode; o.params[2] = dp.emissive; o.params[3] = dp.hue;
	o.extra[0] = dp.dissolve; o.extra[1] = dp.flash; o.extra[2] = dp.rim; o.extra[3] = dp.sway;
	o.oc[0] = (dp.outlineColor & 255) / 255.f; o.oc[1] = ((dp.outlineColor >> 8) & 255) / 255.f; o.oc[2] = ((dp.outlineColor >> 16) & 255) / 255.f; o.oc[3] = ((dp.outlineColor >> 24) & 255) / 255.f;
	// 輪郭パスも同じ印。滝の mode が残っても、体・装備・髪の頂点は流さない。
	if (dp.noTess && dp.mode < 0.5f) o.oc[3] = 2.f;
}

static void TacBindObjBuf(ID3D11DeviceContext* ctx, ID3D11Buffer* b, BOOL tessOn)
{
	if (!ctx || !b || g_cbBound == b) return;
	g_cbBound = b;
	ctx->VSSetConstantBuffers(1, 1, &b);
	ctx->PSSetConstantBuffers(1, 1, &b);
	if (tessOn) {
		ctx->HSSetConstantBuffers(1, 1, &b);
		ctx->DSSetConstantBuffers(1, 1, &b);
	}
}

static void TacUploadObj(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, const TacObjCB& o, BOOL tessOn = FALSE)
{
	if (!ctx || !cb) return;
	if (g_objValid && g_cbBound && memcmp(&g_objGPU, &o, sizeof(o)) == 0) return;
	ID3D11Buffer* dst = cb;
	if (g_cbPoolN > 0) {
		dst = g_cbPool[g_cbPoolI];
		g_cbPoolI++;
		if (g_cbPoolI >= g_cbPoolN) g_cbPoolI = 0;
	}
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(ctx->Map(dst, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
	memcpy(ms.pData, &o, sizeof(o));
	ctx->Unmap(dst, 0);
	TacBindObjBuf(ctx, dst, tessOn);
	g_objGPU = o;
	g_objValid = TRUE;
}

void GpuMesh::Release() { TAC_REL(vb); TAC_REL(ib); nIdx = 0; iBase = 0; }

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
	memset(m_skinOut, 0, sizeof(m_skinOut));
	memset(m_hairOut, 0, sizeof(m_hairOut));
	m_outStamp = 0;
	m_frame = 0;
	m_view = m_proj = m_vp = m_sceneShift = MIdent();
	m_eye = v3(0, 10, -10);
	m_time = 0;
	m_hwnd = NULL; m_wait = NULL; m_lost = FALSE; m_presented = FALSE; m_presentBusy = FALSE; m_occluded = FALSE; m_waitStuck = 0; m_lastRebind = 0;
	m_qDisjoint = m_qTs0 = m_qTs1 = NULL; m_gpuMs = 0; m_gpuArmed = FALSE;
	m_fadeAbove = -1;
	m_cutDeck = -1;
	m_cutActors = FALSE;
	m_stageWind = TRUE;
	m_previewPost = FALSE;
	m_cutActorGen = 0;
	m_fadeX = m_fadeZ = -1;
	m_w = m_h = 0; m_quality = 1; m_qPlus = 0; m_gpu = 0; m_allowTess = TRUE; m_tessOn = FALSE; m_bindKey = -1; m_hudPipe = 0; m_lastPsSrv = NULL; m_dsWant = NULL; m_msaa = 1; m_lastHr = S_OK; m_failStage = 0; m_assetsOk = FALSE; m_loadFn = NULL; m_loadUser = NULL; m_pass = PASS_MAIN;
	m_dev = NULL; m_ctx = NULL; m_ctx1 = NULL; m_swap = NULL; m_bbRtv = NULL; m_bb = NULL; m_loadSky = NULL; m_loadSkyW = m_loadSkyH = 0; m_avaPlate = NULL; m_avaPlateW = m_avaPlateH = 0; m_msTex = NULL; m_msRtv = NULL; m_dsTex = NULL; m_dsv = NULL;
	m_sceneTex = NULL; m_sceneRtv = NULL; m_sceneSrv = NULL;
	m_refTex = NULL; m_refRtv = NULL; m_refSrv = NULL; m_refDs = NULL; m_refDsv = NULL;
	m_refW = m_refH = 0; m_refBlur = 0; m_reflecting = 0;
	m_refSavedVp = MIdent(); m_refSavedEyeY = 0; m_refSavedSx = m_refSavedSy = 0;
	m_refSavedNear0 = m_refSavedNear1 = 0; m_refSavedDeck = -1;
	m_blA = m_blB = NULL; m_blARtv = m_blBRtv = NULL; m_blASrv = m_blBSrv = NULL; m_blW = m_blH = 0;
	m_zTex = NULL; m_zRtv = NULL; m_zRes = NULL; m_zSrv = NULL;
	m_dofTex = NULL; m_dofRtv = NULL; m_dofSrv = NULL;
	m_shTex = NULL; m_shDsv = NULL; m_shSrv = NULL; m_shSize = 0;
	m_ptTex = NULL; m_ptRtv = NULL; m_srvPortrait = NULL; m_ptDs = NULL; m_ptDsv = NULL;
	m_vsS = m_vsSh = m_vsB = m_vsBSh = m_vsK = m_vsKSh = m_vsH = m_vsP = m_vsPT = m_vsQ = NULL;
	m_gsP = NULL;
	m_instOn = FALSE;
	m_psM = m_psO = m_psOV = m_psH = m_psP = m_psSky = NULL;
	for (int si = 0; si < 10; si++) m_psSkyT[si] = NULL;
	m_psBloom = m_psBlur = m_psGrade = m_psGradeTiny = m_psDofH = m_psDofU = m_psUi = NULL;
	m_vsT = NULL; m_hsT = NULL; m_dsT = NULL;
	m_psProp = m_psWater = m_psLava = m_psOverride = NULL;
	for (int gi = 0; gi < TH_COUNT; gi++) m_psGround[gi] = NULL;
	m_psTheme = NULL;
	m_groundTex = NULL; m_groundSrv = NULL; m_matTex = NULL; m_matSrv = NULL;
	m_photoTex = NULL; m_photoSrv = NULL; m_sphereTex = NULL; m_sphereSrv = NULL;
	m_envTex = NULL; m_envSrv = NULL;
	m_envTheme = -1; m_envBucket = -1; m_photoSide = 0; m_photoLive = 0;
	m_csSkin = NULL; m_cbSkinBone = m_cbSkinParm = m_cbHairCloth = m_faceDummy = NULL; m_faceDummySRV = NULL;
	m_csCloth[0] = m_csCloth[1] = m_csCloth[2] = NULL; m_cbCloth = NULL;
	m_csSoft[0] = m_csSoft[1] = NULL; m_cbSoft = NULL;
	m_hairSB = m_hairVB = NULL; m_hairBind = m_hairSrv = NULL; m_hairUAV = NULL; m_hairTex = NULL; m_hairNv = 0;
	memset(m_hairCap, 0, sizeof(m_hairCap));
	memset(m_hairCapN, 0, sizeof(m_hairCapN));
	m_csHair[0] = m_csHair[1] = NULL; m_cbHairSim = NULL;
	m_hairGuideSB = NULL; m_hairGuide = NULL; m_hairVertSB = NULL; m_hairVert = NULL;
	m_hairScratchSB = NULL; m_hairScratchUAV = NULL; m_hairScratchSRV = NULL;
	memset(m_hairV0, 0, sizeof(m_hairV0));
	memset(m_hairVN, 0, sizeof(m_hairVN));
	memset(m_hairI0, 0, sizeof(m_hairI0));
	memset(m_hairIN, 0, sizeof(m_hairIN));
	memset(m_hairVMin, 0, sizeof(m_hairVMin));
	memset(m_hairVMax, 0, sizeof(m_hairVMax));
	m_scalpModel = -1;
	m_capC = RGBA8(70, 70, 70);
	for (int s = 0; s < TAC_SCALP_CACHE; s++) m_scalps[s].model = -1;
	m_ilS = m_ilB = m_ilK = m_ilH = m_ilP = m_ilPT = NULL;
	m_cbFrame = m_cbAir = m_cbRipple = m_cbObj = m_cbBones = m_vbHud = m_vbDyn = m_vbInst = m_ibQuad = NULL; m_vbDynBytes = 0; m_dynPos = 0xFFFFFFF0u; m_dynOff = 0; m_hudPos = 0xFFFFFFF0u;
	m_sLin = m_sCmp = m_sClamp = NULL;
	m_rsBack = m_rsFront = m_rsNone = m_rsShadow = m_rsHud = NULL;
	m_dsWrite = m_dsRead = m_dsOff = NULL;
	m_bsOpaque = m_bsAlpha = m_bsPremul = m_bsAdd = NULL;
	for (int i = 0; i < TEX_MAX; i++) { m_tex[i] = NULL; m_srv[i] = NULL; }
	m_miniN = 1;
	for (int i = 0; i < LAY_N; i++) { m_texMiniL[i] = NULL; m_srvMiniL[i] = NULL; }
	m_waterColor = RGBA8(60, 130, 190);
	m_theme = 0;
	m_stageId = 0;
	m_wx = 0;
	m_hour = 0;
	m_puffN = 0;
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
	m_ptClear[0] = m_ptClear[1] = m_ptClear[2] = 0.f; m_ptClear[3] = 1.f;
	m_avaSegN = 0;
	m_avaHide = m_avaLoading = m_avaBuildOnly = m_avaLoadFull = m_avaPct = 0;
	m_avaShown = -1;
	m_avaBatchN = 1;
	m_avaBatchI = 0;
	m_avaBatchFrame = -1;
	m_avaFlashed = FALSE;
	memset(m_avaWait, 0xFF, sizeof(m_avaWait));
	memset(m_text, 0, sizeof(m_text));
	memset(m_textHash, 0, sizeof(m_textHash));
	for (int i = 0; i < TAC_TEXTN; i++) m_textNext[i] = -1;
	for (int i = 0; i < TAC_TEXTH; i++) m_textHead[i] = -1;
}

Renderer::~Renderer() { Release(); }

void Renderer::ReleaseTargets()
{
	DropTargetCache();
	if (m_ctx) m_ctx->OMSetRenderTargets(0, NULL, NULL);
	if (m_msRtv == m_bbRtv) m_msRtv = NULL;
	TAC_REL(m_msRtv); TAC_REL(m_msTex);
	TAC_REL(m_bbRtv); TAC_REL(m_bb); TAC_REL(m_loadSky); m_loadSkyW = m_loadSkyH = 0; TAC_REL(m_avaPlate); m_avaPlateW = m_avaPlateH = 0;
	TAC_REL(m_sceneSrv); TAC_REL(m_sceneRtv); TAC_REL(m_sceneTex);
	TAC_REL(m_refSrv); TAC_REL(m_refRtv); TAC_REL(m_refTex); TAC_REL(m_refDsv); TAC_REL(m_refDs);
	TAC_REL(m_blASrv); TAC_REL(m_blARtv); TAC_REL(m_blA);
	TAC_REL(m_blBSrv); TAC_REL(m_blBRtv); TAC_REL(m_blB);
	TAC_REL(m_dofSrv); TAC_REL(m_dofRtv); TAC_REL(m_dofTex);
	TAC_REL(m_zSrv); TAC_REL(m_zRtv); TAC_REL(m_zRes); TAC_REL(m_zTex);
	TAC_REL(m_dsv); TAC_REL(m_dsTex);
}

static void ClearMonPack();

void Renderer::Release()
{
	m_gearShown = 0;
	m_avaReady = 0;
	ClearMonPack();
	for (int ti = 0; ti < TAC_TEXTN; ti++) if (m_text[ti].live) { TAC_REL(m_text[ti].sp.srv); TAC_REL(m_text[ti].sp.tex); m_text[ti].live = 0; m_textHash[ti] = 0; }
	for (int ti = 0; ti < TAC_TEXTN; ti++) m_textNext[ti] = -1;
	for (int ti = 0; ti < TAC_TEXTH; ti++) m_textHead[ti] = -1;
	m_terrain.Release();
	for (int i = 0; i < 16; i++) {
		m_terrainChunk[i].Release();
		m_waterChunk[i].mesh.Release();
		m_waterChunk[i].rad = 0;
		m_lavaChunk[i].mesh.Release();
		m_lavaChunk[i].rad = 0;
	}
	for (int i = 0; i < 16; i++) { m_cover1[i].Release(); m_cover2[i].Release(); m_cover3[i].Release(); m_cover4[i].Release(); }
	m_water.Release(); m_lava.Release();
	for (auto& b : m_props) b.mesh.Release();
	m_props.clear();
	for (auto& m : models) for (auto& p : m.parts) p.Release();
	models.clear();
	for (auto& a : attaches) a.mesh.Release();
	attaches.clear();
	for (int i = 0; i < 192; i++) {
		for (int c = 0; c < 40; c++) {
			if (rigs.v[i].clips.v[c].q.arena == 2) rigs.v[i].clips.v[c].q.reset();
			else rigs.v[i].clips.v[c].q.clear();
			if (rigs.v[i].clips.v[c].hips.arena == 2) rigs.v[i].clips.v[c].hips.reset();
			else rigs.v[i].clips.v[c].hips.clear();
		}
	}
	rigs.clear();
	statics.clear();
	for (auto& c : m_creatures) c.Release();
	m_creatures.clear();
	for (int i = 0; i < 160; i++) {
		CreatureSkin& s = m_skins.v[i];
		ReleaseSkinGpu(s);
		TAC_REL(s.baldIB);
		s.baldN = 0;
		if (s.bind.arena == 2) s.bind.reset(); else s.bind.clear();
		if (s.cloth.arena == 2) s.cloth.reset(); else s.cloth.clear();
		if (s.headTri.arena == 2) s.headTri.reset(); else s.headTri.clear();
		if (s.meshHair.arena == 2) s.meshHair.reset(); else s.meshHair.clear();
	}
	m_skins.clear();
	for (int i = 0; i < TAC_SKIN_OUT; i++) { TAC_REL(m_skinOut[i].uav); TAC_REL(m_skinOut[i].vb); m_skinOut[i].owner = NULL; m_skinOut[i].src = NULL; m_skinOut[i].nv = 0; m_skinOut[i].frame = 0; }
	for (int i = 0; i < TAC_HAIR_OUT; i++) { TAC_REL(m_hairOut[i].uav); TAC_REL(m_hairOut[i].vb); m_hairOut[i].owner = NULL; m_hairOut[i].nv = 0; m_hairOut[i].frame = 0; }
	m_monN = 0; m_monBytes = NULL; m_monBytesN = 0;
	ReleaseAvatarCache();
	for (auto& c : m_gear) c.Release();
	m_gear.clear();
	m_gearGrip.clear();
	m_assetsOk = FALSE;
	ReleaseTargets();
	TAC_REL(m_shSrv); TAC_REL(m_shDsv); TAC_REL(m_shTex);
	TAC_REL(m_srvPortrait); TAC_REL(m_ptRtv); TAC_REL(m_ptTex); TAC_REL(m_ptDsv); TAC_REL(m_ptDs);
	TAC_REL(m_vsS); TAC_REL(m_vsSh); TAC_REL(m_vsB); TAC_REL(m_vsBSh); TAC_REL(m_vsK); TAC_REL(m_vsKSh); TAC_REL(m_vsH); TAC_REL(m_vsP); TAC_REL(m_vsPT); TAC_REL(m_vsQ);
	TAC_REL(m_gsP);
	TAC_REL(m_psM); TAC_REL(m_psO); TAC_REL(m_psOV); TAC_REL(m_psH); TAC_REL(m_psP); TAC_REL(m_psSky);
	for (int si = 0; si < 10; si++) TAC_REL(m_psSkyT[si]);
	TAC_REL(m_psBloom); TAC_REL(m_psBlur); TAC_REL(m_psGrade); TAC_REL(m_psGradeTiny);
	TAC_REL(m_psDofH); TAC_REL(m_psDofU); TAC_REL(m_vsT); TAC_REL(m_hsT); TAC_REL(m_dsT); TAC_REL(m_psUi);
	TAC_REL(m_psProp); TAC_REL(m_psWater); TAC_REL(m_psLava);
	for (int gi = 0; gi < TH_COUNT; gi++) TAC_REL(m_psGround[gi]);
	TAC_REL(m_psTheme);
	m_psOverride = NULL;
	TAC_REL(m_groundSrv); TAC_REL(m_groundTex); TAC_REL(m_matSrv); TAC_REL(m_matTex);
	DropLooks();
	TAC_REL(m_csSkin); TAC_REL(m_cbSkinBone); TAC_REL(m_cbSkinParm); TAC_REL(m_cbHairCloth); TAC_REL(m_faceDummySRV); TAC_REL(m_faceDummy);
	TacCbRingFree(g_skinBoneR); TacCbRingFree(g_skinParmR); TacCbRingFree(g_hairClothR);
	for (int ci = 0; ci < 3; ci++) TAC_REL(m_csCloth[ci]);
	TAC_REL(m_cbCloth); TacCbRingFree(g_clothR);
	TAC_REL(g_clothGate);
	TAC_REL(g_clStU); TAC_REL(g_clStB); TAC_REL(g_clCnS); TAC_REL(g_clCnB); TAC_REL(g_clPfS); TAC_REL(g_clPfB); TAC_REL(g_clPmS); TAC_REL(g_clPmB);
	g_pendN = 0;
	TAC_REL(g_hStU); TAC_REL(g_hStB); TAC_REL(g_hPmS); TAC_REL(g_hPmB);
	g_hairN = 0;
	TAC_REL(m_csSoft[0]); TAC_REL(m_csSoft[1]); TAC_REL(m_cbSoft); TacCbRingFree(g_softR);
	TAC_REL(m_csHair[0]); TAC_REL(m_csHair[1]); TAC_REL(m_cbHairSim); TacCbRingFree(g_hairSimR);
	ReleaseHair();
	TAC_REL(m_ilS); TAC_REL(m_ilB); TAC_REL(m_ilK); TAC_REL(m_ilH); TAC_REL(m_ilP); TAC_REL(m_ilPT);
	TAC_REL(m_cbFrame); TacCbRingFree(g_frameR); TAC_REL(m_cbAir); TacCbRingFree(g_airR); TAC_REL(m_cbRipple); TAC_REL(m_cbObj); TAC_REL(m_cbBones); TacCbRingFree(g_boneR); TAC_REL(m_vbHud); TAC_REL(m_vbDyn); TAC_REL(m_vbInst); TAC_REL(m_ibQuad);
	for (int i = 0; i < g_cbPoolN; i++) TAC_REL(g_cbPool[i]);
	g_cbPoolN = 0;
	g_cbPoolI = 0;
	g_cbBound = NULL;
	g_objValid = FALSE;
	TAC_REL(m_sLin); TAC_REL(m_sCmp); TAC_REL(m_sClamp);
	TAC_REL(m_rsBack); TAC_REL(m_rsFront); TAC_REL(m_rsNone); TAC_REL(m_rsShadow); TAC_REL(m_rsHud);
	TAC_REL(m_dsWrite); TAC_REL(m_dsRead); TAC_REL(m_dsOff);
	TAC_REL(m_bsOpaque); TAC_REL(m_bsAlpha); TAC_REL(m_bsPremul); TAC_REL(m_bsAdd);
	for (int i = 0; i < TEX_MAX; i++) { TAC_REL(m_srv[i]); TAC_REL(m_tex[i]); }
	for (int i = 0; i < LAY_N; i++) { TAC_REL(m_srvMiniL[i]); TAC_REL(m_texMiniL[i]); }
	if (m_wait) { CloseHandle(m_wait); m_wait = NULL; }
	TAC_REL(m_qDisjoint); TAC_REL(m_qTs0); TAC_REL(m_qTs1); m_gpuArmed = FALSE;
	TAC_REL(m_swap);
	if (m_ctx) { m_ctx->ClearState(); m_ctx->Flush(); }
	TacBindCacheDrop();
	TAC_REL(m_ctx1);
	TAC_REL(m_ctx);
	TAC_REL(m_dev);
	m_lost = FALSE;
	TacAvaReset();
	TacMonRestart();
}

BOOL Renderer::Init(HWND hwnd, int quality)
{
	Release();
	m_presented = FALSE;
	m_occluded = FALSE;
	m_waitStuck = 0;
	m_lastRebind = 0;
	m_hwnd = hwnd;
	m_quality = Clampi(quality, 0, 6);
	m_failStage = 1;
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	D3D_FEATURE_LEVEL req = D3D_FEATURE_LEVEL_11_0, got = (D3D_FEATURE_LEVEL)0;
	HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, &req, 1, D3D11_SDK_VERSION, &m_dev, &got, &m_ctx);
	if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, flags, &req, 1, D3D11_SDK_VERSION, &m_dev, &got, &m_ctx);
	if (FAILED(hr) || !m_dev) { m_lastHr = hr; return FALSE; }
	m_ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&m_ctx1);
	m_failStage = 2;
	IDXGIDevice* xd = NULL; IDXGIAdapter* xa = NULL; IDXGIFactory2* f2 = NULL;
	if (FAILED(hr = m_dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&xd)) || FAILED(hr = xd->GetAdapter(&xa))) {
		TAC_REL(xd); m_lastHr = hr; return FALSE;
	}
	{
		DXGI_ADAPTER_DESC ad = {};
		if (SUCCEEDED(xa->GetDesc(&ad))) {
			if (ad.VendorId == 0x10DE) m_gpu = 1;
			else if (ad.VendorId == 0x1002) m_gpu = 2;
			else if (ad.VendorId == 0x8086) m_gpu = 3;
		}
	}
	xa->GetParent(__uuidof(IDXGIFactory2), (void**)&f2);
	RECT rc; ::GetClientRect(hwnd, &rc);
	m_w = max(8, (int)(rc.right - rc.left)); m_h = max(8, (int)(rc.bottom - rc.top));
	DXGI_SWAP_CHAIN_DESC1 sd = {};
	sd.Width = m_w; sd.Height = m_h; sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; sd.SampleDesc.Count = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 3; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Scaling = DXGI_SCALING_STRETCH; sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
	hr = f2 ? f2->CreateSwapChainForHwnd(m_dev, hwnd, &sd, NULL, NULL, &m_swap) : E_FAIL;
	if (FAILED(hr) && f2) { sd.BufferCount = 2; hr = f2->CreateSwapChainForHwnd(m_dev, hwnd, &sd, NULL, NULL, &m_swap); }
	if (FAILED(hr) && f2) { sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; sd.BufferCount = 1; hr = f2->CreateSwapChainForHwnd(m_dev, hwnd, &sd, NULL, NULL, &m_swap); }
	if (f2) f2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
	if (xd) {
		IDXGIDevice1* d1 = NULL;
		if (SUCCEEDED(xd->QueryInterface(__uuidof(IDXGIDevice1), (void**)&d1)) && d1) {
			d1->SetMaximumFrameLatency(3);
			d1->Release();
		}
	}
	TAC_REL(f2); TAC_REL(xa); TAC_REL(xd);
	if (FAILED(hr) || !m_swap) { m_lastHr = hr; return FALSE; }
	m_failStage = 3;
	if (!CreateShaders()) return FALSE;
	m_failStage = 4;
	if (!CreateStates()) return FALSE;
	m_failStage = 5;
	if (!CreateProcTextures()) return FALSE;
	m_failStage = 6;
	if (!CreateTargets()) return FALSE;
	WarmTargets();
	m_failStage = 0;
	return TRUE;
}

static BOOL TacQRich(int q, int plus) { return q == 2 || q == 3 || q == 5 || q == 6 || (q == 1 && plus >= 2); }
static BOOL TacQFx(int q) { return q == 1 || q == 2 || q == 3 || q == 5 || q == 6; }
static BOOL TacQUltra(int q) { return q == 3 || q == 5 || q == 6; }

static int TacQFootprint(int q)
{
	(void)q;
	// 画質はシェーダとパス回数だけ変える。RTの形を揃えて切替の作り直しを止める。
	const int sh = 4096, msaa = 2, blur = 2, ref = 2, fx = 1;
	return sh | (msaa << 16) | (blur << 20) | (ref << 24) | (fx << 28);
}

enum { TAC_RT_CACHE = 4, TAC_ENV_CACHE = 6, TAC_PH_CACHE = 4 };
struct TacRtPack {
	int used, fp, w, h, msaa, shSize, blW, blH, refW, refH, stamp;
	float refBlur;
	ID3D11Texture2D* msTex; ID3D11RenderTargetView* msRtv;
	ID3D11Texture2D* dsTex; ID3D11DepthStencilView* dsv;
	ID3D11Texture2D* shTex; ID3D11DepthStencilView* shDsv; ID3D11ShaderResourceView* shSrv;
	ID3D11Texture2D* sceneTex; ID3D11RenderTargetView* sceneRtv; ID3D11ShaderResourceView* sceneSrv;
	ID3D11Texture2D* blA; ID3D11RenderTargetView* blARtv; ID3D11ShaderResourceView* blASrv;
	ID3D11Texture2D* blB; ID3D11RenderTargetView* blBRtv; ID3D11ShaderResourceView* blBSrv;
	ID3D11Texture2D* refTex; ID3D11RenderTargetView* refRtv; ID3D11ShaderResourceView* refSrv;
	ID3D11Texture2D* refDs; ID3D11DepthStencilView* refDsv;
	ID3D11Texture2D* zTex; ID3D11Texture2D* zRes; ID3D11RenderTargetView* zRtv; ID3D11ShaderResourceView* zSrv;
	ID3D11Texture2D* dofTex; ID3D11RenderTargetView* dofRtv; ID3D11ShaderResourceView* dofSrv;
};
struct TacEnvPack {
	int used, theme, face, sphere, stamp;
	ID3D11Texture2D* envTex; ID3D11ShaderResourceView* envSrv;
	ID3D11Texture2D* sphereTex; ID3D11ShaderResourceView* sphereSrv;
};
struct TacPhotoPack {
	int used, side, live, stamp;
	ID3D11Texture2D* tex; ID3D11ShaderResourceView* srv;
};
static TacRtPack g_rt[TAC_RT_CACHE];
static TacEnvPack g_env[TAC_ENV_CACHE];
static TacPhotoPack g_ph[TAC_PH_CACHE];
static int g_rtStamp = 1;
static int g_lookStamp = 1;
static int g_warmTheme = -999;
#define TAC_MOVE(dst, src) do { (dst) = (src); (src) = NULL; } while (0)

static void RtFree(TacRtPack& p)
{
	TAC_REL(p.msRtv); TAC_REL(p.msTex);
	TAC_REL(p.dsv); TAC_REL(p.dsTex);
	TAC_REL(p.shSrv); TAC_REL(p.shDsv); TAC_REL(p.shTex);
	TAC_REL(p.sceneSrv); TAC_REL(p.sceneRtv); TAC_REL(p.sceneTex);
	TAC_REL(p.blASrv); TAC_REL(p.blARtv); TAC_REL(p.blA);
	TAC_REL(p.blBSrv); TAC_REL(p.blBRtv); TAC_REL(p.blB);
	TAC_REL(p.refSrv); TAC_REL(p.refRtv); TAC_REL(p.refTex); TAC_REL(p.refDsv); TAC_REL(p.refDs);
	TAC_REL(p.zSrv); TAC_REL(p.zRtv); TAC_REL(p.zRes); TAC_REL(p.zTex);
	TAC_REL(p.dofSrv); TAC_REL(p.dofRtv); TAC_REL(p.dofTex);
	memset(&p, 0, sizeof(p));
}

void Renderer::DropTargetCache()
{
	for (int i = 0; i < TAC_RT_CACHE; i++) if (g_rt[i].used) RtFree(g_rt[i]);
}

void Renderer::ParkTargets(int fp)
{
	if (!m_dsTex && !m_shTex && !m_sceneTex && !m_msTex) return;
	int slot = -1, empty = -1, old = 0;
	for (int i = 0; i < TAC_RT_CACHE; i++) {
		if (g_rt[i].used && g_rt[i].fp == fp && g_rt[i].w == m_w && g_rt[i].h == m_h) { slot = i; break; }
		if (!g_rt[i].used && empty < 0) empty = i;
		if (g_rt[i].stamp < g_rt[old].stamp) old = i;
	}
	if (slot < 0) slot = (empty >= 0) ? empty : old;
	if (g_rt[slot].used) RtFree(g_rt[slot]);
	TacRtPack& p = g_rt[slot];
	memset(&p, 0, sizeof(p));
	p.used = 1; p.fp = fp; p.w = m_w; p.h = m_h; p.stamp = ++g_rtStamp;
	p.msaa = m_msaa; p.shSize = m_shSize;
	p.blW = m_blW; p.blH = m_blH; p.refW = m_refW; p.refH = m_refH; p.refBlur = m_refBlur;
	if (m_msRtv && m_msRtv != m_bbRtv) { TAC_MOVE(p.msRtv, m_msRtv); TAC_MOVE(p.msTex, m_msTex); }
	else { m_msRtv = NULL; m_msTex = NULL; }
	TAC_MOVE(p.dsv, m_dsv); TAC_MOVE(p.dsTex, m_dsTex);
	TAC_MOVE(p.shSrv, m_shSrv); TAC_MOVE(p.shDsv, m_shDsv); TAC_MOVE(p.shTex, m_shTex);
	TAC_MOVE(p.sceneSrv, m_sceneSrv); TAC_MOVE(p.sceneRtv, m_sceneRtv); TAC_MOVE(p.sceneTex, m_sceneTex);
	TAC_MOVE(p.blASrv, m_blASrv); TAC_MOVE(p.blARtv, m_blARtv); TAC_MOVE(p.blA, m_blA);
	TAC_MOVE(p.blBSrv, m_blBSrv); TAC_MOVE(p.blBRtv, m_blBRtv); TAC_MOVE(p.blB, m_blB);
	TAC_MOVE(p.refSrv, m_refSrv); TAC_MOVE(p.refRtv, m_refRtv); TAC_MOVE(p.refTex, m_refTex);
	TAC_MOVE(p.refDsv, m_refDsv); TAC_MOVE(p.refDs, m_refDs);
	TAC_MOVE(p.zSrv, m_zSrv); TAC_MOVE(p.zRtv, m_zRtv); TAC_MOVE(p.zRes, m_zRes); TAC_MOVE(p.zTex, m_zTex);
	TAC_MOVE(p.dofSrv, m_dofSrv); TAC_MOVE(p.dofRtv, m_dofRtv); TAC_MOVE(p.dofTex, m_dofTex);
	m_shSize = 0;
}

BOOL Renderer::TakeTargets(int fp)
{
	for (int i = 0; i < TAC_RT_CACHE; i++) {
		TacRtPack& p = g_rt[i];
		if (!p.used || p.fp != fp || p.w != m_w || p.h != m_h) continue;
		m_msaa = p.msaa; m_shSize = p.shSize;
		m_blW = p.blW; m_blH = p.blH; m_refW = p.refW; m_refH = p.refH; m_refBlur = p.refBlur;
		m_msTex = p.msTex; m_msRtv = (p.msaa <= 1 || !p.msRtv) ? m_bbRtv : p.msRtv;
		m_dsTex = p.dsTex; m_dsv = p.dsv;
		m_shTex = p.shTex; m_shDsv = p.shDsv; m_shSrv = p.shSrv;
		m_sceneTex = p.sceneTex; m_sceneRtv = p.sceneRtv; m_sceneSrv = p.sceneSrv;
		m_blA = p.blA; m_blARtv = p.blARtv; m_blASrv = p.blASrv;
		m_blB = p.blB; m_blBRtv = p.blBRtv; m_blBSrv = p.blBSrv;
		m_refTex = p.refTex; m_refRtv = p.refRtv; m_refSrv = p.refSrv; m_refDs = p.refDs; m_refDsv = p.refDsv;
		m_zTex = p.zTex; m_zRes = p.zRes; m_zRtv = p.zRtv; m_zSrv = p.zSrv;
		m_dofTex = p.dofTex; m_dofRtv = p.dofRtv; m_dofSrv = p.dofSrv;
		memset(&p, 0, sizeof(p));
		return TRUE;
	}
	return FALSE;
}

void Renderer::WarmTargets()
{
	if (!m_dev || !m_swap) return;
	static const int rep[4] = { 4, 1, 3, 6 };
	int keep = m_quality;
	for (int i = 0; i < 4; i++) {
		int fp = TacQFootprint(rep[i]);
		BOOL cached = FALSE;
		for (int k = 0; k < TAC_RT_CACHE; k++)
			if (g_rt[k].used && g_rt[k].fp == fp && g_rt[k].w == m_w && g_rt[k].h == m_h) cached = TRUE;
		if (cached) continue;
		if (TacQFootprint(m_quality) == fp && (m_dsTex || m_shTex || m_sceneTex)) continue;
		if (m_dsTex || m_shTex || m_sceneTex || m_msTex) ParkTargets(TacQFootprint(m_quality));
		m_quality = rep[i];
		CreateTargets();
	}
	if (TacQFootprint(m_quality) != TacQFootprint(keep)) {
		if (m_dsTex || m_shTex || m_sceneTex || m_msTex) ParkTargets(TacQFootprint(m_quality));
		m_quality = keep;
		if (!TakeTargets(TacQFootprint(keep))) CreateTargets();
	} else m_quality = keep;
}

void Renderer::DropLooks()
{
	for (int i = 0; i < TAC_ENV_CACHE; i++) {
		TAC_REL(g_env[i].envSrv); TAC_REL(g_env[i].envTex); TAC_REL(g_env[i].sphereSrv); TAC_REL(g_env[i].sphereTex);
		memset(&g_env[i], 0, sizeof(g_env[i]));
	}
	for (int i = 0; i < TAC_PH_CACHE; i++) {
		TAC_REL(g_ph[i].srv); TAC_REL(g_ph[i].tex);
		memset(&g_ph[i], 0, sizeof(g_ph[i]));
	}
	m_envSrv = NULL; m_envTex = NULL; m_sphereSrv = NULL; m_sphereTex = NULL;
	m_photoSrv = NULL; m_photoTex = NULL;
	m_envTheme = -1; m_envBucket = -1; m_photoSide = 0; m_photoLive = 0;
	g_warmTheme = -999;
}

BOOL Renderer::SetQuality(int q)
{
	q = Clampi(q, 0, 6);
	if (q == m_quality) return FALSE;
	int old = m_quality;
	m_quality = q;
	BOOL rebuilt = FALSE;
	if (m_dev) {
		int of = TacQFootprint(old);
		int nf = TacQFootprint(q);
		if (of != nf) {
			if (m_ctx) m_ctx->OMSetRenderTargets(0, NULL, NULL);
			if (m_dsTex || m_shTex || m_sceneTex || m_msTex) ParkTargets(of);
			if (!TakeTargets(nf)) {
				CreateTargets();
				rebuilt = TRUE;
			}
		}
		TAC_REL(m_sLin);
		D3D11_SAMPLER_DESC ss = {};
		int an = (m_quality == 4) ? 1 : ((m_quality >= 1) ? 16 : 8);
		if (m_gpu == 3) an = min(an, m_quality >= 5 ? 8 : 4);
		else if (m_gpu == 2 && an > 8) an = 8;
		ss.Filter = (an <= 1) ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_ANISOTROPIC;
		ss.MaxAnisotropy = an;
		ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		ss.MaxLOD = D3D11_FLOAT32_MAX;
		m_dev->CreateSamplerState(&ss, &m_sLin);
	}
	return rebuilt;
}

void Renderer::SetQualityPlus(int plus)
{
	// 細段の + 数。超軽量の下は負、限界突破は +10 を超えて伸ばす。見た目だけでセーブには出さない。
	if (plus < TAC_NOTCH_FLOOR) plus = TAC_NOTCH_FLOOR;
	if (plus > TAC_NOTCH_OPEN - TAC_NOTCH_LIMIT) plus = TAC_NOTCH_OPEN - TAC_NOTCH_LIMIT;
	m_qPlus = plus;
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
	const int rq = m_quality;
	HRESULT hr = S_OK;
	if (!m_bb) {
		hr = m_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&m_bb);
		if (FAILED(hr)) { m_lastHr = hr; return FALSE; }
		if (FAILED(hr = m_dev->CreateRenderTargetView(m_bb, NULL, &m_bbRtv))) { m_lastHr = hr; return FALSE; }
	}
	int want = 2;
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
		int ss = 4096;
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
	int div = 2;
	m_blW = max(8, m_w / div); m_blH = max(8, m_h / div);
	if (!MakeColorRt(m_dev, m_w, m_h, &m_sceneTex, &m_sceneRtv, &m_sceneSrv)
		|| !MakeColorRt(m_dev, m_blW, m_blH, &m_blA, &m_blARtv, &m_blASrv)
		|| !MakeColorRt(m_dev, m_blW, m_blH, &m_blB, &m_blBRtv, &m_blBSrv)) {
		TAC_REL(m_sceneSrv); TAC_REL(m_sceneRtv); TAC_REL(m_sceneTex);
		TAC_REL(m_blASrv); TAC_REL(m_blARtv); TAC_REL(m_blA);
		TAC_REL(m_blBSrv); TAC_REL(m_blBRtv); TAC_REL(m_blB);
	}
	{
		int rmul = 1, rdiv = 2;
		if (rdiv > 0) {
			m_refW = max(8, m_w * rmul / rdiv);
			m_refH = max(8, m_h * rmul / rdiv);
			if (m_refW > 7680) { m_refH = max(8, m_refH * 7680 / m_refW); m_refW = 7680; }
			if (m_refH > 4320) { m_refW = max(8, m_refW * 4320 / m_refH); m_refH = 4320; }
			float px = 0.f;
			if (rq == 1) px = 1.15f;
			else if (rq == 2) px = 0.55f;
			else if (rq == 3) px = 0.28f;
			m_refBlur = px / (float)m_refW;
			if (!MakeColorRt(m_dev, m_refW, m_refH, &m_refTex, &m_refRtv, &m_refSrv)) {
				TAC_REL(m_refSrv); TAC_REL(m_refRtv); TAC_REL(m_refTex);
			} else {
				D3D11_TEXTURE2D_DESC rd = {};
				rd.Width = (UINT)m_refW; rd.Height = (UINT)m_refH; rd.MipLevels = 1; rd.ArraySize = 1; rd.SampleDesc.Count = 1;
				rd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; rd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
				if (FAILED(m_dev->CreateTexture2D(&rd, NULL, &m_refDs)) || FAILED(m_dev->CreateDepthStencilView(m_refDs, NULL, &m_refDsv))) {
					TAC_REL(m_refDsv); TAC_REL(m_refDs); TAC_REL(m_refSrv); TAC_REL(m_refRtv); TAC_REL(m_refTex);
				}
			}
		}
	}
	{
		D3D11_TEXTURE2D_DESC zt = {};
		zt.Width = m_w; zt.Height = m_h; zt.MipLevels = 1; zt.ArraySize = 1;
		zt.Format = DXGI_FORMAT_R16_FLOAT; zt.SampleDesc.Count = m_msaa;
		zt.BindFlags = D3D11_BIND_RENDER_TARGET;
		if (m_msaa == 1) zt.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
		if (SUCCEEDED(m_dev->CreateTexture2D(&zt, NULL, &m_zTex)) && SUCCEEDED(m_dev->CreateRenderTargetView(m_zTex, NULL, &m_zRtv))) {
			if (m_msaa == 1) {
				if (FAILED(m_dev->CreateShaderResourceView(m_zTex, NULL, &m_zSrv))) { TAC_REL(m_zRtv); TAC_REL(m_zTex); }
			} else {
				zt.SampleDesc.Count = 1; zt.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				if (FAILED(m_dev->CreateTexture2D(&zt, NULL, &m_zRes)) || FAILED(m_dev->CreateShaderResourceView(m_zRes, NULL, &m_zSrv))) {
					TAC_REL(m_zSrv); TAC_REL(m_zRes); TAC_REL(m_zRtv); TAC_REL(m_zTex);
				}
			}
		} else { TAC_REL(m_zRtv); TAC_REL(m_zTex); }
		if (!MakeColorRt(m_dev, m_w, m_h, &m_dofTex, &m_dofRtv, &m_dofSrv)) {
			TAC_REL(m_dofSrv); TAC_REL(m_dofRtv); TAC_REL(m_dofTex);
		}
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
	TacBindCacheDrop();
	DXGI_SWAP_CHAIN_DESC1 d = {};
	m_swap->GetDesc1(&d);
	HRESULT hr = m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, d.Flags);
	if (FAILED(hr)) { m_lastHr = hr; m_lost = TRUE; return FALSE; }
	m_w = w; m_h = h;
	if (!CreateTargets()) return FALSE;
	WarmTargets();
	return TRUE;
}

BOOL Renderer::RefreshSurface()
{
	if (!m_swap || !m_ctx) return FALSE;
	ReleaseTargets();
	m_ctx->ClearState();
	TacBindCacheDrop();
	DXGI_SWAP_CHAIN_DESC1 d = {};
	m_swap->GetDesc1(&d);
	HRESULT hr = m_swap->ResizeBuffers(0, m_w, m_h, DXGI_FORMAT_UNKNOWN, d.Flags);
	m_presented = FALSE;
	m_waitStuck = 0;
	if (FAILED(hr)) { m_lastHr = hr; m_lost = TRUE; return FALSE; }
	if (!CreateTargets()) return FALSE;
	WarmTargets();
	return TRUE;
}

BOOL Renderer::FrameReady()
{
	// 前の絵がまだ画面に出ていない。同じ場面をもう一度積むと GPU が遅れ、次の Present も失敗する。
	// ここは出し直しだけ。まだ描き中なら、この拍のシーン記録はしない。ゲームの更新は呼び出し側で済んでいる。
	if (!m_presentBusy) return TRUE;
	if (!m_swap) { m_presentBusy = FALSE; return TRUE; }
	Present(0, 0);
	return !m_presentBusy;
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
			{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 2, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		D3D11_INPUT_ELEMENT_DESC lp[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		ok = ok && SUCCEEDED(m_dev->CreateInputLayout(ls, 4, bS->GetBufferPointer(), bS->GetBufferSize(), &m_ilS));
		ok = ok && SUCCEEDED(m_dev->CreateInputLayout(lk, 5, bK->GetBufferPointer(), bK->GetBufferSize(), &m_ilK));
		ok = ok && SUCCEEDED(m_dev->CreateInputLayout(lh, 5, bH->GetBufferPointer(), bH->GetBufferSize(), &m_ilH));
		ok = ok && SUCCEEDED(m_dev->CreateInputLayout(lp, 3, bP->GetBufferPointer(), bP->GetBufferSize(), &m_ilP));
	}
	{
		ID3DBlob* bVS = NULL;
		ID3DBlob* bGS = NULL;
		if (SUCCEEDED(LoadBlob("VSPT", "vs_5_0", IDR_CSO_TAC_VSPT, &bVS)) && bVS
			&& SUCCEEDED(LoadBlob("GSP", "gs_5_0", IDR_CSO_TAC_GSP, &bGS)) && bGS
			&& SUCCEEDED(m_dev->CreateVertexShader(bVS->GetBufferPointer(), bVS->GetBufferSize(), NULL, &m_vsPT))
			&& SUCCEEDED(m_dev->CreateGeometryShader(bGS->GetBufferPointer(), bGS->GetBufferSize(), NULL, &m_gsP))) {
			D3D11_INPUT_ELEMENT_DESC lpt[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 44, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};
			if (FAILED(m_dev->CreateInputLayout(lpt, 4, bVS->GetBufferPointer(), bVS->GetBufferSize(), &m_ilPT))) {
				TAC_REL(m_vsPT); TAC_REL(m_gsP); m_ilPT = NULL;
			}
		} else { TAC_REL(m_vsPT); TAC_REL(m_gsP); }
		TAC_REL(bVS); TAC_REL(bGS);
	}
	TAC_REL(bS); TAC_REL(bK); TAC_REL(bH); TAC_REL(bP); TAC_REL(bQ);
	{
		ID3DBlob* bB = NULL;
		if (SUCCEEDED(LoadBlob("VSSB", "vs_5_0", IDR_CSO_TAC_VSSB, &bB)) && bB) {
			if (SUCCEEDED(m_dev->CreateVertexShader(bB->GetBufferPointer(), bB->GetBufferSize(), NULL, &m_vsB))) {
				D3D11_INPUT_ELEMENT_DESC lb[] = {
					{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
					{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
					{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
					{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
					{ "TEXCOORD", 4, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
					{ "TEXCOORD", 5, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
					{ "TEXCOORD", 6, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
					{ "TEXCOORD", 7, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
				};
				if (FAILED(m_dev->CreateInputLayout(lb, 8, bB->GetBufferPointer(), bB->GetBufferSize(), &m_ilB))) {
					TAC_REL(m_vsB); m_ilB = NULL;
				}
			} else m_vsB = NULL;
		TAC_REL(bB);
	}
	{
		ID3DBlob* b = NULL;
		if (SUCCEEDED(LoadBlob("VSSSh", "vs_5_0", IDR_CSO_TAC_VSSSH, &b)) && b) {
			if (FAILED(m_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_vsSh))) m_vsSh = NULL;
			TAC_REL(b);
		}
		if (SUCCEEDED(LoadBlob("VSSBSh", "vs_5_0", IDR_CSO_TAC_VSSBSH, &b)) && b) {
			if (FAILED(m_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_vsBSh))) m_vsBSh = NULL;
			TAC_REL(b);
		}
		if (SUCCEEDED(LoadBlob("VSKSh", "vs_5_0", IDR_CSO_TAC_VSKSH, &b)) && b) {
			if (FAILED(m_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_vsKSh))) m_vsKSh = NULL;
			TAC_REL(b);
		}
	}
	}
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
		{ "PSG8", IDR_CSO_TAC_PSG8, &m_psGround[8] }, { "PSG9", IDR_CSO_TAC_PSG9, &m_psGround[9] },
		{ "PSGX", IDR_CSO_TAC_PSGX, &m_psTheme },
		{ "PSProp", IDR_CSO_TAC_PSPROP, &m_psProp }, { "PSWater", IDR_CSO_TAC_PSWATER, &m_psWater }, { "PSLava", IDR_CSO_TAC_PSLAVA, &m_psLava },
	};
	for (auto& d : ps) {
		ID3DBlob* b = NULL;
		if (FAILED(hr = LoadBlob(d.e, "ps_5_0", d.rid, &b))) { m_lastHr = hr; return FALSE; }
		hr = m_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, d.ps);
		TAC_REL(b);
		if (FAILED(hr)) { m_lastHr = hr; return FALSE; }
	}
	{
		ID3DBlob* b = NULL;
		if (FAILED(Soft3DLoadCso(L"tac", "VST", "vs_5_0", IDR_CSO_TAC_VST, L"omake_tactics_tess.hlsl", (void**)&b)) || !b) { m_lastHr = E_FAIL; return FALSE; }
		hr = m_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_vsT); TAC_REL(b);
		if (FAILED(hr)) { m_lastHr = hr; return FALSE; }
		if (FAILED(Soft3DLoadCso(L"tac", "HST", "hs_5_0", IDR_CSO_TAC_HST, L"omake_tactics_tess.hlsl", (void**)&b)) || !b) { m_lastHr = E_FAIL; return FALSE; }
		hr = m_dev->CreateHullShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_hsT); TAC_REL(b);
		if (FAILED(hr)) { m_lastHr = hr; return FALSE; }
		if (FAILED(Soft3DLoadCso(L"tac", "DST", "ds_5_0", IDR_CSO_TAC_DST, L"omake_tactics_tess.hlsl", (void**)&b)) || !b) { m_lastHr = E_FAIL; return FALSE; }
		hr = m_dev->CreateDomainShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_dsT); TAC_REL(b);
		if (FAILED(hr)) { m_lastHr = hr; return FALSE; }
		if (FAILED(Soft3DLoadCso(L"tac", "PSGradeTiny", "ps_5_0", IDR_CSO_TAC_PSGRTINY, L"omake_tactics.hlsl", (void**)&b)) || !b) { m_lastHr = E_FAIL; return FALSE; }
		hr = m_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_psGradeTiny); TAC_REL(b);
		if (FAILED(hr)) { m_lastHr = hr; return FALSE; }
		if (FAILED(Soft3DLoadCso(L"tac", "PSDofH", "ps_5_0", IDR_CSO_TAC_PSDOFH, L"omake_tactics_dof.hlsl", (void**)&b)) || !b) { m_lastHr = E_FAIL; return FALSE; }
		hr = m_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_psDofH); TAC_REL(b);
		if (FAILED(hr)) { m_lastHr = hr; return FALSE; }
		if (FAILED(Soft3DLoadCso(L"tac", "PSDofU", "ps_5_0", IDR_CSO_TAC_PSDOFU, L"omake_tactics_dof.hlsl", (void**)&b)) || !b) { m_lastHr = E_FAIL; return FALSE; }
		hr = m_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_psDofU); TAC_REL(b);
		if (FAILED(hr)) { m_lastHr = hr; return FALSE; }
	}
	{
		static const int skyRid[10] = {
			IDR_CSO_TAC_PSSKY0, IDR_CSO_TAC_PSSKY1, IDR_CSO_TAC_PSSKY2, IDR_CSO_TAC_PSSKY3, IDR_CSO_TAC_PSSKY4,
			IDR_CSO_TAC_PSSKY5, IDR_CSO_TAC_PSSKY6, IDR_CSO_TAC_PSSKY7, IDR_CSO_TAC_PSSKY8, IDR_CSO_TAC_PSSKY9
		};
		static const char* skyEnt[10] = { "PSSKY0", "PSSKY1", "PSSKY2", "PSSKY3", "PSSKY4", "PSSKY5", "PSSKY6", "PSSKY7", "PSSKY8", "PSSKY9" };
		for (int t = 0; t < 10; t++) {
			ID3DBlob* b = NULL;
			if (SUCCEEDED(LoadBlob(skyEnt[t], "ps_5_0", skyRid[t], &b)) && b) {
				if (FAILED(m_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_psSkyT[t]))) m_psSkyT[t] = NULL;
				TAC_REL(b);
			}
		}
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
		else TacCbRingMake(m_dev, g_skinBoneR, bd.ByteWidth);
		bd.ByteWidth = 96;
		if (m_csSkin && FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbSkinParm))) { TAC_REL(m_csSkin); TAC_REL(m_cbSkinBone); TacCbRingFree(g_skinBoneR); }
		else if (m_csSkin) TacCbRingMake(m_dev, g_skinParmR, bd.ByteWidth);
		bd.ByteWidth = (9 + 1 + SKULL_NA * SKULL_NE / 4) * 16;
		if (m_csSkin && FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbHairCloth))) { TAC_REL(m_csSkin); TAC_REL(m_cbSkinBone); TAC_REL(m_cbSkinParm); TacCbRingFree(g_skinBoneR); TacCbRingFree(g_skinParmR); }
		else if (m_csSkin) TacCbRingMake(m_dev, g_hairClothR, bd.ByteWidth);
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
		bd.ByteWidth = 1536;
		if (!all || FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbCloth))) {
			for (int i = 0; i < 3; i++) TAC_REL(m_csCloth[i]);
			m_cbCloth = NULL;
		} else TacCbRingMake(m_dev, g_clothR, bd.ByteWidth);
		bd.ByteWidth = 16;
		if (FAILED(m_dev->CreateBuffer(&bd, NULL, &g_clothGate))) g_clothGate = NULL;
		static const struct { const char* e; int rid; } sf[2] = {
			{ "CSSoft", IDR_CSO_TAC_CSSOFT }, { "CSSoftWrite", IDR_CSO_TAC_CSSOFTW } };
		for (int i = 0; i < 2; i++) {
			ID3DBlob* b = NULL;
			if (SUCCEEDED(Soft3DLoadCso(L"tac", sf[i].e, "cs_5_0", sf[i].rid, L"omake_tactics_soft.hlsl", (void**)&b)) && b) {
				if (FAILED(m_dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_csSoft[i]))) m_csSoft[i] = NULL;
				TAC_REL(b);
			}
		}
		bd.ByteWidth = 736;
		if (!m_csSoft[0] || !m_csSoft[1] || FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbSoft))) {
			TAC_REL(m_csSoft[0]); TAC_REL(m_csSoft[1]); TAC_REL(m_cbSoft);
		} else TacCbRingMake(m_dev, g_softR, bd.ByteWidth);
		static const struct { const char* e; int rid; } hr[2] = {
			{ "CSHair", IDR_CSO_TAC_CSHAIR }, { "CSHairWrite", IDR_CSO_TAC_CSHAIRW } };
		for (int i = 0; i < 2; i++) {
			ID3DBlob* b = NULL;
			if (SUCCEEDED(Soft3DLoadCso(L"tac", hr[i].e, "cs_5_0", hr[i].rid, L"omake_tactics_hair.hlsl", (void**)&b)) && b) {
				if (FAILED(m_dev->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), NULL, &m_csHair[i]))) m_csHair[i] = NULL;
				TAC_REL(b);
			}
		}
		bd.ByteWidth = 608;
		if (!m_csHair[0] || !m_csHair[1] || FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbHairSim))) {
			TAC_REL(m_csHair[0]); TAC_REL(m_csHair[1]); TAC_REL(m_cbHairSim);
		} else TacCbRingMake(m_dev, g_hairSimR, bd.ByteWidth);
	}
	return TRUE;
}

BOOL Renderer::CreateStates()
{
	D3D11_BUFFER_DESC bd = {};
	bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.ByteWidth = (sizeof(FrameCB) + 15) & ~15;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbFrame))) return FALSE;
	TacCbRingMake(m_dev, g_frameR, bd.ByteWidth);
	bd.ByteWidth = 32;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbAir))) return FALSE;
	TacCbRingMake(m_dev, g_airR, bd.ByteWidth);
	bd.ByteWidth = 48 * 16;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbRipple))) return FALSE;
	bd.ByteWidth = 64 + 16 * 4;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbObj))) return FALSE;
	bd.ByteWidth = 256;
	g_cbPoolN = 0;
	g_cbPoolI = 0;
	for (int i = 0; i < TAC_CB_POOL; i++) {
		if (FAILED(m_dev->CreateBuffer(&bd, NULL, &g_cbPool[i]))) {
			for (int j = 0; j < i; j++) TAC_REL(g_cbPool[j]);
			g_cbPoolN = 0;
			break;
		}
	}
	if (g_cbPool[0]) g_cbPoolN = TAC_CB_POOL;
	bd.ByteWidth = 64 * MAXJ;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_cbBones))) return FALSE;
	TacCbRingMake(m_dev, g_boneR, bd.ByteWidth);
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.ByteWidth = sizeof(VtxH) * 6 * 4096 * 4;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_vbHud))) return FALSE;
	m_vbDynBytes = 16u * 1024u * 1024u;
	bd.ByteWidth = m_vbDynBytes;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_vbDyn))) return FALSE;
	m_dynPos = 0xFFFFFFF0u;
	m_dynOff = 0;
	m_hudPos = 0xFFFFFFF0u;
	bd.ByteWidth = 64u * 64u;
	if (FAILED(m_dev->CreateBuffer(&bd, NULL, &m_vbInst))) return FALSE;
	{
		enum { QN = 8192 };
		static WORD qix[QN * 6];
		static int qok = 0;
		if (!qok) {
			for (int q = 0; q < QN; q++) {
				WORD b = (WORD)(q * 4);
				qix[q * 6 + 0] = b; qix[q * 6 + 1] = (WORD)(b + 1); qix[q * 6 + 2] = (WORD)(b + 2);
				qix[q * 6 + 3] = b; qix[q * 6 + 4] = (WORD)(b + 2); qix[q * 6 + 5] = (WORD)(b + 3);
			}
			qok = 1;
		}
		D3D11_BUFFER_DESC ib = {};
		ib.ByteWidth = sizeof(qix);
		ib.Usage = D3D11_USAGE_IMMUTABLE;
		ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
		D3D11_SUBRESOURCE_DATA sd = { qix, 0, 0 };
		if (FAILED(m_dev->CreateBuffer(&ib, &sd, &m_ibQuad))) return FALSE;
	}

	D3D11_SAMPLER_DESC ss = {};
	int an = (m_quality == 4) ? 1 : ((m_quality >= 1) ? 16 : 8);
	if (m_gpu == 3) an = min(an, m_quality >= 5 ? 8 : 4);
	else if (m_gpu == 2 && an > 8) an = 8;
	ss.Filter = (an <= 1) ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_ANISOTROPIC;
	ss.MaxAnisotropy = an;
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
	bl.IndependentBlendEnable = TRUE;
	bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	bl.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	m_dev->CreateBlendState(&bl, &m_bsOpaque);
	auto& t = bl.RenderTarget[0];
	t.BlendEnable = TRUE; t.BlendOp = t.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	t.SrcBlend = D3D11_BLEND_SRC_ALPHA; t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; t.SrcBlendAlpha = D3D11_BLEND_ONE; t.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	m_dev->CreateBlendState(&bl, &m_bsAlpha);
	t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	m_dev->CreateBlendState(&bl, &m_bsPremul);
	t.SrcBlend = D3D11_BLEND_ONE; t.DestBlend = D3D11_BLEND_ONE; t.SrcBlendAlpha = D3D11_BLEND_ZERO; t.DestBlendAlpha = D3D11_BLEND_ONE;
	m_dev->CreateBlendState(&bl, &m_bsAdd);
	{
		D3D11_QUERY_DESC qd = {};
		qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
		m_dev->CreateQuery(&qd, &m_qDisjoint);
		qd.Query = D3D11_QUERY_TIMESTAMP;
		m_dev->CreateQuery(&qd, &m_qTs0);
		m_dev->CreateQuery(&qd, &m_qTs1);
	}
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

static IWICImagingFactory* TacWicFactory()
{
	static IWICImagingFactory* s_f = NULL;
	if (!s_f) {
		HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_f));
		if (FAILED(hr) || !s_f) {
			CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
			CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_f));
		}
	}
	return s_f;
}

static BOOL LockRc(int rid, const BYTE** data, DWORD* len);

// 縦長ストリップ（正方形 × slices）を配列にする。16 スロット表には入れない。
static BOOL LoadStrip(ID3D11Device* dev, int rid, int slices, ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv)
{
	*tex = NULL; *srv = NULL;
	if (slices < 1 || slices > 16) return FALSE;
	DWORD len = 0;
	const BYTE* data = NULL;
	if (!LockRc(rid, &data, &len) || !data || len < 32) return FALSE;
	IWICImagingFactory* f = TacWicFactory();
	if (!f) return FALSE;
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
	TAC_REL(cv); TAC_REL(fr); TAC_REL(dec); TAC_REL(st);
	return ok;
}
// 平原〜滝。実写は tacticsbgm\resource\tactics の画像。画質で解像度と反射の段を変える。
static int StageTierQ(int q)
{
	// 軽量・標準・高は同じ環境マップ。切替のたびに作り直すとカクつく。
	if (q == 0 || q == 2) q = 1;
	if (q == 4) return 0;
	if (q <= 0) return 1;
	if (q == 1) return 2;
	if (q == 2) return 3;
	if (q == 3) return 4;
	if (q == 5) return 5;
	return 6;
}
static int PhotoSideFor(int q)
{
	int t = StageTierQ(q);
	if (t <= 0) return 256;
	if (t == 1) return 256;
	if (t == 2) return 512;
	return 1024;
}
static int EnvFaceFor(int t)
{
	if (t <= 1) return 32;
	if (t == 2) return 128;
	if (t == 3) return 128;
	return 256;
}
static int SphereSideFor(int t)
{
	if (t <= 0) return 32;
	if (t == 1) return 64;
	return 256;
}

static BOOL LockRc(int rid, const BYTE** data, DWORD* len)
{
	*data = NULL; *len = 0;
	if (BonusImageMap(rid, data, len) && *data) return TRUE;
	static BYTE* hold = NULL;
	free(hold); hold = NULL;
	if (BonusImageRead(rid, &hold, len)) { *data = hold; return TRUE; }
	HMODULE hm = AfxGetResourceHandle();
	HRSRC rs = FindResource(hm, MAKEINTRESOURCE(rid), RT_RCDATA);
	if (!rs) { hm = GetModuleHandle(NULL); rs = FindResource(hm, MAKEINTRESOURCE(rid), RT_RCDATA); }
	if (!rs) return FALSE;
	HGLOBAL hg = LoadResource(hm, rs);
	DWORD n = SizeofResource(hm, rs);
	const BYTE* p = (const BYTE*)LockResource(hg);
	if (!p || n < 32) return FALSE;
	*data = p; *len = n;
	return TRUE;
}

static BOOL LockPack(int rid, const BYTE** data, DWORD* len)
{
	*data = NULL; *len = 0;
	if (BonusImageMap(rid, data, len) && *data) return TRUE;
	struct Slot { int id; BYTE* p; DWORD n; };
	static Slot slot[8];
	static int nslot = 0;
	for (int i = 0; i < nslot; i++) if (slot[i].id == rid) {
		*data = slot[i].p; *len = slot[i].n; return TRUE;
	}
	BYTE* owned = NULL; DWORD n = 0;
	if (BonusImageRead(rid, &owned, &n) && owned) {
		if (nslot < 8) { slot[nslot].id = rid; slot[nslot].p = owned; slot[nslot].n = n; nslot++; }
		*data = owned; *len = n;
		return TRUE;
	}
	free(owned);
	HMODULE hm = AfxGetResourceHandle();
	HRSRC rs = FindResource(hm, MAKEINTRESOURCE(rid), RT_RCDATA);
	if (!rs) { hm = GetModuleHandle(NULL); rs = FindResource(hm, MAKEINTRESOURCE(rid), RT_RCDATA); }
	if (!rs) return FALSE;
	HGLOBAL hg = LoadResource(hm, rs);
	const BYTE* p = hg ? (const BYTE*)LockResource(hg) : NULL;
	DWORD sz = rs ? SizeofResource(hm, rs) : 0;
	if (!p || sz < 8) return FALSE;
	*data = p; *len = sz;
	return TRUE;
}

static BOOL DecodeScaled(const BYTE* data, int len, int side, DWORD* dst)
{
	IWICImagingFactory* f = TacWicFactory();
	if (!f) return FALSE;
	IWICStream* st = NULL; IWICBitmapDecoder* dec = NULL; IWICBitmapFrameDecode* fr = NULL;
	IWICBitmapScaler* sc = NULL; IWICFormatConverter* cv = NULL;
	BOOL ok = FALSE;
	if (SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromMemory((BYTE*)data, (DWORD)len))
		&& SUCCEEDED(f->CreateDecoderFromStream(st, NULL, WICDecodeMetadataCacheOnLoad, &dec))
		&& SUCCEEDED(dec->GetFrame(0, &fr))) {
		UINT w = 0, h = 0;
		fr->GetSize(&w, &h);
		IWICBitmapSource* src = fr;
		if ((int)w != side || (int)h != side) {
			if (SUCCEEDED(f->CreateBitmapScaler(&sc)) && SUCCEEDED(sc->Initialize(fr, (UINT)side, (UINT)side, WICBitmapInterpolationModeLinear)))
				src = sc;
			else src = NULL;
		}
		if (src && SUCCEEDED(f->CreateFormatConverter(&cv))
			&& SUCCEEDED(cv->Initialize(src, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
			ok = SUCCEEDED(cv->CopyPixels(NULL, (UINT)side * 4, (UINT)side * (UINT)side * 4, (BYTE*)dst));
		}
	}
	if (ok) {
		int n = side * side;
		for (int i = 0; i < n; i++) dst[i] |= 0xFF000000u;
	}
	TAC_REL(cv); TAC_REL(sc); TAC_REL(fr); TAC_REL(dec); TAC_REL(st);
	return ok;
}

static BOOL MakeGpuArray(ID3D11Device* dev, ID3D11DeviceContext* ctx, int side, int slices, const DWORD* px, BOOL cube,
	ID3D11Texture2D** tex, ID3D11ShaderResourceView** srv)
{
	*tex = NULL; *srv = NULL;
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = (UINT)side; td.Height = (UINT)side; td.MipLevels = 0; td.ArraySize = (UINT)slices;
	td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	if (cube) td.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
	if (FAILED(dev->CreateTexture2D(&td, NULL, tex)) || !*tex) return FALSE;
	(*tex)->GetDesc(&td);
	for (int s = 0; s < slices; s++) {
		UINT sub = D3D11CalcSubresource(0, (UINT)s, td.MipLevels);
		ctx->UpdateSubresource(*tex, sub, NULL, px + (size_t)s * side * side, (UINT)side * 4, 0);
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.Format = td.Format;
	if (cube) {
		sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
		sv.TextureCube.MipLevels = td.MipLevels;
	} else {
		sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		sv.Texture2DArray.MipLevels = td.MipLevels;
		sv.Texture2DArray.ArraySize = (UINT)slices;
	}
	if (FAILED(dev->CreateShaderResourceView(*tex, &sv, srv)) || !*srv) {
		TAC_REL(*srv); TAC_REL(*tex); return FALSE;
	}
	ctx->GenerateMips(*srv);
	return TRUE;
}

static void CubeDir(int face, float u, float v, float& x, float& y, float& z)
{
	float su = u * 2.f - 1.f, sv = v * 2.f - 1.f;
	switch (face) {
	case 0: x = 1.f; y = -sv; z = -su; break;
	case 1: x = -1.f; y = -sv; z = su; break;
	case 2: x = su; y = 1.f; z = sv; break;
	case 3: x = su; y = -1.f; z = -sv; break;
	case 4: x = su; y = -sv; z = 1.f; break;
	default: x = -su; y = -sv; z = -1.f; break;
	}
}

static void ShadeEnv(int theme, float x, float y, float z, float& r, float& g, float& b)
{
	static const BYTE kTop[TH_COUNT][3] = {
		{ 84, 146, 228 }, { 112, 146, 214 }, { 66, 128, 172 }, { 128, 160, 214 }, { 98, 162, 232 },
		{ 40, 24, 34 }, { 70, 92, 82 }, { 58, 56, 120 }, { 36, 110, 150 }, { 150, 188, 214 },
		{ 18, 24, 58 }, { 18, 22, 36 }, { 48, 32, 72 }, { 40, 48, 90 }
	};
	static const BYTE kHor[TH_COUNT][3] = {
		{ 206, 228, 246 }, { 246, 206, 164 }, { 176, 214, 194 }, { 232, 238, 248 }, { 252, 228, 176 },
		{ 170, 66, 40 }, { 156, 166, 132 }, { 206, 170, 204 }, { 140, 206, 214 }, { 226, 232, 236 },
		{ 48, 62, 110 }, { 70, 90, 110 }, { 140, 110, 150 }, { 210, 200, 230 }
	};
	static const BYTE kGnd[TH_COUNT][3] = {
		{ 110, 170, 80 }, { 170, 120, 80 }, { 70, 120, 60 }, { 230, 236, 246 }, { 220, 194, 140 },
		{ 80, 70, 68 }, { 80, 96, 60 }, { 160, 156, 140 }, { 48, 110, 100 }, { 90, 140, 80 },
		{ 70, 80, 108 }, { 90, 98, 108 }, { 168, 158, 176 }, { 186, 190, 210 }
	};
	static const float kSun[TH_COUNT][3] = {
		{ .45f, .82f, .35f }, { .55f, .7f, .3f }, { .35f, .85f, .4f }, { .4f, .7f, .5f }, { .5f, .85f, .2f },
		{ .3f, .75f, .55f }, { .35f, .8f, .45f }, { .6f, .6f, .35f }, { .4f, .62f, .28f }, { .4f, .8f, .3f },
		{ .2f, .35f, .9f }, { .55f, .7f, .25f }, { .35f, .55f, .7f }, { .25f, .8f, .4f }
	};
	theme = Clampi(theme, 0, TH_COUNT - 1);
	float len = sqrtf(x * x + y * y + z * z);
	if (len < 1e-4f) len = 1.f;
	x /= len; y /= len; z /= len;
	if (theme == TH_SEABED) {
		float up = Clampf(y * 0.5f + 0.5f, 0, 1);
		r = Lerpf(0.02f, 0.20f, up); g = Lerpf(0.10f, 0.48f, up); b = Lerpf(0.16f, 0.55f, up);
		float cau = 0.5f + 0.5f * sinf(x * 18.f + z * 9.f) * sinf(z * 14.f - x * 4.f);
		r += cau * cau * 0.08f; g += cau * 0.12f; b += cau * 0.16f;
		return;
	}
	float up = Clampf(y * 0.5f + 0.55f, 0, 1);
	up = up * up * (3.f - 2.f * up);
	r = Lerpf(kHor[theme][0] / 255.f, kTop[theme][0] / 255.f, up);
	g = Lerpf(kHor[theme][1] / 255.f, kTop[theme][1] / 255.f, up);
	b = Lerpf(kHor[theme][2] / 255.f, kTop[theme][2] / 255.f, up);
	float hor = powf(1.f - fabsf(y), 4.f);
	r = Lerpf(r, kHor[theme][0] / 255.f, hor * 0.45f);
	g = Lerpf(g, kHor[theme][1] / 255.f, hor * 0.45f);
	b = Lerpf(b, kHor[theme][2] / 255.f, hor * 0.45f);
	if (y < 0) {
		float k = Clampf(-y * 1.3f, 0, 1);
		r = Lerpf(r, kGnd[theme][0] / 255.f, k);
		g = Lerpf(g, kGnd[theme][1] / 255.f, k);
		b = Lerpf(b, kGnd[theme][2] / 255.f, k);
	}
	float sx = kSun[theme][0], sy = kSun[theme][1], sz = kSun[theme][2];
	float sl = sqrtf(sx * sx + sy * sy + sz * sz);
	sx /= sl; sy /= sl; sz /= sl;
	float d = x * sx + y * sy + z * sz;
	float disk = expf((d - 1.f) * (theme == TH_VOLCANO ? 10.f : 28.f));
	float glow = expf((d - 1.f) * 5.5f);
	float sr = 1.f, sg = 0.94f, sb = 0.78f;
	if (theme == TH_VOLCANO) { sr = 1.f; sg = 0.35f; sb = 0.08f; }
	else if (theme == TH_SNOW) { sg = 0.97f; sb = 1.f; }
	else if (theme == TH_FALLS) { sg = 0.98f; sb = 1.f; }
	r += sr * (disk * 1.4f + glow * 0.28f);
	g += sg * (disk * 1.4f + glow * 0.28f);
	b += sb * (disk * 1.4f + glow * 0.28f);
	if (theme != TH_VOLCANO && theme != TH_ORBIT && theme != TH_STATION && y > 0.05f) {
		float cl = TNoise(theme * 19 + 4, x * 3.4f + 8.f, z * 3.4f);
		if (cl > 0.62f) {
			float a = (cl - 0.62f) * 1.8f * Clampf(y * 2.f, 0, 1);
			r = Lerpf(r, 0.96f, a); g = Lerpf(g, 0.97f, a); b = Lerpf(b, 1.f, a);
		}
	}
	if (theme == TH_VOLCANO && y > 0.15f && TNoise(9, x * 40.f, z * 40.f) > 0.96f) { r += 0.8f; g += 0.75f; b += 0.6f; }
	if (theme == TH_FALLS && fabsf(y) < 0.18f) { r = Lerpf(r, 0.92f, 0.35f); g = Lerpf(g, 0.95f, 0.35f); b = Lerpf(b, 1.f, 0.35f); }
	if (theme == TH_RUINS && y > 0.35f && TNoise(3, x * 30.f, z * 30.f) > 0.97f) { r += 0.45f; g += 0.45f; b += 0.7f; }
	if (theme == TH_ORBIT || theme == TH_STATION) {
		if (y > 0.02f && TNoise(theme * 3, x * 80.f, z * 80.f) > 0.97f) { r += 0.9f; g += 0.95f; b += 1.f; }
		if (theme == TH_ORBIT && y > 0.2f) {
			float neb = TNoise(21, x * 2.2f + 3.f, z * 2.2f);
			if (neb > 0.55f) { r = Lerpf(r, 0.52f, 0.28f); g = Lerpf(g, 0.32f, 0.22f); b = Lerpf(b, 0.82f, 0.38f); }
		}
		if (theme == TH_STATION && y > 0.15f && TNoise(8, x * 18.f, z * 6.f) > 0.82f) { r += 0.15f; g += 0.45f; b += 0.55f; }
	}
	if (theme == TH_CITADEL && y > 0.25f && TNoise(11, x * 12.f, z * 12.f) > 0.72f) { r = Lerpf(r, 0.72f, 0.28f); g = Lerpf(g, 0.48f, 0.18f); b = Lerpf(b, 0.88f, 0.32f); }
	if (theme == TH_LANTERN && y > 0.05f) {
		float moon = expf((y - 0.85f) * 8.f);
		r += moon * 0.35f; g += moon * 0.4f; b += moon * 0.55f;
		if (TNoise(15, x * 6.f, z * 6.f) > 0.7f) { r = Lerpf(r, 0.85f, 0.2f); g = Lerpf(g, 0.88f, 0.2f); b = Lerpf(b, 1.f, 0.25f); }
	}
}

static void FillCube(int theme, int face, DWORD* dst)
{
	int n = face * face;
	for (int i = 0; i < 6; i++) {
		DWORD* f = dst + (size_t)i * n;
		for (int y = 0; y < face; y++) for (int x = 0; x < face; x++) {
			float dx, dy, dz, r, g, b;
			CubeDir(i, (x + 0.5f) / face, (y + 0.5f) / face, dx, dy, dz);
			ShadeEnv(theme, dx, dy, dz, r, g, b);
			f[y * face + x] = BGRA((int)(Clampf(r, 0, 1) * 255.f), (int)(Clampf(g, 0, 1) * 255.f), (int)(Clampf(b, 0, 1) * 255.f));
		}
	}
}

static void FillSphere(int theme, int side, DWORD* dst)
{
	for (int y = 0; y < side; y++) for (int x = 0; x < side; x++) {
		float mx = ((x + 0.5f) / side) * 2.f - 1.f;
		float my = ((y + 0.5f) / side) * 2.f - 1.f;
		float r2 = mx * mx + my * my;
		float dx, dy, dz, r, g, b;
		if (r2 > 1.f) {
			dx = mx; dy = 0; dz = 0.15f;
		} else {
			float nz = sqrtf(1.f - r2);
			dx = 2.f * nz * mx; dy = 2.f * nz * my; dz = 2.f * nz * nz - 1.f;
		}
		ShadeEnv(theme, dx, dy, dz, r, g, b);
		dst[y * side + x] = BGRA((int)(Clampf(r, 0, 1) * 255.f), (int)(Clampf(g, 0, 1) * 255.f), (int)(Clampf(b, 0, 1) * 255.f));
	}
}

static int EnvSlot(int theme, int face, int sphere)
{
	int empty = -1, old = 0;
	for (int i = 0; i < TAC_ENV_CACHE; i++) {
		if (g_env[i].used && g_env[i].theme == theme && g_env[i].face == face && g_env[i].sphere == sphere) return i;
		if (!g_env[i].used && empty < 0) empty = i;
		if (g_env[i].stamp < g_env[old].stamp) old = i;
	}
	return (empty >= 0) ? empty : old;
}

static BOOL BuildEnv(ID3D11Device* dev, ID3D11DeviceContext* ctx, int theme, int face, int sphere, TacEnvPack& p)
{
	static DWORD cube[256 * 256 * 6];
	static DWORD sph[256 * 256];
	if (face <= 0 || face > 256 || sphere <= 0 || sphere > 256) return FALSE;
	ID3D11Texture2D* ct = NULL, *st = NULL;
	ID3D11ShaderResourceView* cs = NULL, *ssrv = NULL;
	FillCube(theme, face, cube);
	FillSphere(theme, sphere, sph);
	BOOL ok = MakeGpuArray(dev, ctx, face, 6, cube, TRUE, &ct, &cs);
	ok = ok && MakeTex(dev, ctx, sph, sphere, sphere, TRUE, &st, &ssrv);
	if (!ok) { TAC_REL(cs); TAC_REL(ct); TAC_REL(ssrv); TAC_REL(st); return FALSE; }
	TAC_REL(p.envSrv); TAC_REL(p.envTex); TAC_REL(p.sphereSrv); TAC_REL(p.sphereTex);
	p.used = 1; p.theme = theme; p.face = face; p.sphere = sphere; p.stamp = ++g_lookStamp;
	p.envTex = ct; p.envSrv = cs; p.sphereTex = st; p.sphereSrv = ssrv;
	return TRUE;
}

static BOOL BuildPhoto(ID3D11Device* dev, ID3D11DeviceContext* ctx, int side, TacPhotoPack& p)
{
	if (side <= 0 || side > 1024) return FALSE;
	size_t pix = (size_t)side * side;
	static DWORD px[1024 * 1024];
	int live = 0;
	D3D11_TEXTURE2D_DESC td = {};
	td.Width = (UINT)side; td.Height = (UINT)side; td.MipLevels = 0; td.ArraySize = (UINT)(TH_COUNT * 2);
	td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
	ID3D11Texture2D* t = NULL; ID3D11ShaderResourceView* s = NULL;
	if (FAILED(dev->CreateTexture2D(&td, NULL, &t)) || !t) return FALSE;
	t->GetDesc(&td);
	for (int theme = 0; theme < TH_COUNT; theme++) {
		for (int nrm = 0; nrm < 2; nrm++) {
			const BYTE* data = NULL; DWORD n = 0;
			int slice = theme + nrm * TH_COUNT;
			if (LockRc(IDR_TAC_PH0 + slice, &data, &n) && DecodeScaled(data, (int)n, side, px)) {
				if (!nrm) live++;
			} else memset(px, 0, pix * sizeof(DWORD));
			UINT sub = D3D11CalcSubresource(0, (UINT)slice, td.MipLevels);
			ctx->UpdateSubresource(t, sub, NULL, px, (UINT)side * 4, 0);
		}
	}
	if (live <= 0) { TAC_REL(t); return FALSE; }
	D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
	sv.Format = td.Format;
	sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	sv.Texture2DArray.MipLevels = td.MipLevels;
	sv.Texture2DArray.ArraySize = (UINT)(TH_COUNT * 2);
	if (FAILED(dev->CreateShaderResourceView(t, &sv, &s)) || !s) {
		TAC_REL(s); TAC_REL(t); return FALSE;
	}
	ctx->GenerateMips(s);
	TAC_REL(p.srv); TAC_REL(p.tex);
	p.used = 1; p.side = side; p.live = live; p.stamp = ++g_lookStamp;
	p.tex = t; p.srv = s;
	return TRUE;
}

void Renderer::WarmLooks(int theme)
{
	if (!m_dev || !m_ctx) return;
	int tier = StageTierQ(m_quality);
	int face = EnvFaceFor(tier);
	int sph = SphereSideFor(tier);
	int eslot = EnvSlot(theme, face, sph);
	if (g_env[eslot].used && g_env[eslot].theme == theme && g_env[eslot].face == face && g_env[eslot].sphere == sph)
		g_env[eslot].stamp = ++g_lookStamp;
	else
		BuildEnv(m_dev, m_ctx, theme, face, sph, g_env[eslot]);
	int side = PhotoSideFor(m_quality);
	int pslot = -1, empty = -1, old = 0;
	for (int k = 0; k < TAC_PH_CACHE; k++) {
		if (g_ph[k].used && g_ph[k].side == side) { pslot = k; break; }
		if (!g_ph[k].used && empty < 0) empty = k;
		if (g_ph[k].stamp < g_ph[old].stamp) old = k;
	}
	if (pslot >= 0) {
		g_ph[pslot].stamp = ++g_lookStamp;
	} else {
		pslot = (empty >= 0) ? empty : old;
		BuildPhoto(m_dev, m_ctx, side, g_ph[pslot]);
	}
}

void Renderer::BindLooks()
{
	int tier = StageTierQ(m_quality);
	int face = EnvFaceFor(tier);
	int sph = SphereSideFor(tier);
	bool envHit = false;
	for (int i = 0; i < TAC_ENV_CACHE; i++) {
		if (!g_env[i].used || g_env[i].theme != m_theme || g_env[i].face != face || g_env[i].sphere != sph) continue;
		m_envTex = g_env[i].envTex; m_envSrv = g_env[i].envSrv;
		m_sphereTex = g_env[i].sphereTex; m_sphereSrv = g_env[i].sphereSrv;
		m_envTheme = m_theme; m_envBucket = tier * 1000 + face;
		envHit = true;
		break;
	}
	if (!envHit && m_dev && m_ctx) {
		int eslot = EnvSlot(m_theme, face, sph);
		if (BuildEnv(m_dev, m_ctx, m_theme, face, sph, g_env[eslot])) {
			m_envTex = g_env[eslot].envTex; m_envSrv = g_env[eslot].envSrv;
			m_sphereTex = g_env[eslot].sphereTex; m_sphereSrv = g_env[eslot].sphereSrv;
			m_envTheme = m_theme; m_envBucket = tier * 1000 + face;
		}
	}
	int side = PhotoSideFor(m_quality);
	bool phHit = false;
	for (int i = 0; i < TAC_PH_CACHE; i++) {
		if (!g_ph[i].used || g_ph[i].side != side) continue;
		m_photoTex = g_ph[i].tex; m_photoSrv = g_ph[i].srv;
		m_photoSide = side; m_photoLive = g_ph[i].live;
		phHit = true;
		break;
	}
	if (!phHit && m_dev && m_ctx) {
		int empty = -1, old = 0;
		for (int k = 0; k < TAC_PH_CACHE; k++) {
			if (!g_ph[k].used && empty < 0) empty = k;
			if (g_ph[k].stamp < g_ph[old].stamp) old = k;
		}
		int pslot = (empty >= 0) ? empty : old;
		if (BuildPhoto(m_dev, m_ctx, side, g_ph[pslot])) {
			m_photoTex = g_ph[pslot].tex; m_photoSrv = g_ph[pslot].srv;
			m_photoSide = side; m_photoLive = g_ph[pslot].live;
		}
	}
}

void Renderer::PollStageLook()
{
	if (!m_dev || !m_ctx) return;
	if (g_warmTheme != m_theme) {
		WarmLooks(m_theme);
		g_warmTheme = m_theme;
	}
	BindLooks();
	if (!m_photoSrv) {
		DWORD blank[28];
		for (int i = 0; i < 28; i++) blank[i] = 0;
		ID3D11Texture2D* t = NULL; ID3D11ShaderResourceView* s = NULL;
		if (MakeGpuArray(m_dev, m_ctx, 1, 28, blank, FALSE, &t, &s)) { m_photoTex = t; m_photoSrv = s; }
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
		static DWORD p[64 * 64];
		static int palReady = 0;
		if (!palReady) {
			for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
				DWORD c = PaletteColor((y / 4) * 16 + (x / 4));
				p[y * 64 + x] = BGRA(c & 255, (c >> 8) & 255, (c >> 16) & 255);
			}
			palReady = 1;
		}
		if (!MakeTex(m_dev, m_ctx, p, 64, 64, FALSE, &m_tex[TEX_PAL], &m_srv[TEX_PAL])) return FALSE;
	}
	{
		const int S = 512, T = 128;
		static DWORD p[512 * 512];
		static int atlasReady = 0;
		if (!atlasReady) {
			for (int tile = 0; tile < 16; tile++) {
				int ox = (tile % 4) * T, oy = (tile / 4) * T;
				for (int y = 0; y < T; y++) for (int x = 0; x < T; x++) {
					float r, g, b;
					TileColor(tile, x, y, r, g, b);
					p[(oy + y) * S + ox + x] = BGRA((int)(r * 255), (int)(g * 255), (int)(b * 255));
				}
			}
			atlasReady = 1;
		}
		if (!MakeTex(m_dev, m_ctx, p, S, S, TRUE, &m_tex[TEX_ATLAS], &m_srv[TEX_ATLAS])) return FALSE;
	}
	if (!LoadStrip(m_dev, IDR_TAC_DETAIL, 16, &m_groundTex, &m_groundSrv)) return FALSE;
	if (!LoadStrip(m_dev, IDR_TAC_MAT, 8, &m_matTex, &m_matSrv)) return FALSE;
	PollStageLook();
	return TRUE;
}

BOOL Renderer::LoadPngTex(const BYTE* data, UINT len, int slot)
{
	IWICImagingFactory* f = TacWicFactory();
	if (!f) return FALSE;
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
	TAC_REL(cv); TAC_REL(fr); TAC_REL(dec); TAC_REL(st);
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

int Renderer::CountAssetPulses() const
{
	const BYTE* data = NULL;
	DWORD size = 0;
	if (!LockPack(IDR_TAC_ASSETS, &data, &size)) return 40;
	if (!data || size < 32 || memcmp(data, "TAC1", 4) != 0) return 40;
	Reader rd(data + 4, size - 4);
	UINT ver = rd.U32(), nTex = rd.U32(), nRig = rd.U32(), nMod = rd.U32(), nAtt = rd.U32(), nSt = rd.U32();
	if (ver != 1 || nTex > 12 || nRig > 8 || nMod > 32 || nAtt > 64 || nSt > 48) return 40;
	int nJnt[8] = {};
	int n = 0;
	for (UINT i = 0; i < nTex; i++) {
		UINT len = rd.U32();
		if (!rd.Bytes(len)) return n > 0 ? n : 40;
		rd.Align4();
		n++;
	}
	for (UINT r = 0; r < nRig && r < 8; r++) {
		UINT nj = rd.U32();
		if (nj < 1 || nj > MAXJ) return n > 0 ? n : 40;
		nJnt[r] = (int)nj;
		if (!rd.Bytes((size_t)nj * 28)) return n > 0 ? n : 40;
		UINT nc = rd.U32();
		if (nc > 40) return n > 0 ? n : 40;
		if (nc == 0) n++;
		for (UINT c = 0; c < nc; c++) {
			if (!rd.Bytes(32)) return n > 0 ? n : 40;
			UINT nf = rd.U32();
			rd.F32(); rd.U32();
			if (nf == 0 || nf > 4000) return n > 0 ? n : 40;
			if (!rd.Bytes((size_t)nf * nj * 8)) return n > 0 ? n : 40;
			if (!rd.Bytes((size_t)nf * 12)) return n > 0 ? n : 40;
			n++;
		}
	}
	for (UINT mi = 0; mi < nMod; mi++) {
		if (!rd.Bytes(32)) return n > 0 ? n : 40;
		UINT rig = rd.U32(); rd.U32();
		int nj = (rig < 8) ? nJnt[rig] : 0;
		if (nj < 1) return n > 0 ? n : 40;
		if (!rd.Bytes((size_t)nj * 40 + 64 + (size_t)nj * 64)) return n > 0 ? n : 40;
		UINT np = rd.U32();
		if (np == 0) n++;
		for (UINT p = 0; p < np; p++) {
			if (!rd.Bytes(32)) return n > 0 ? n : 40;
			rd.U32();
			UINT nv = rd.U32(), ni = rd.U32();
			if (!rd.Bytes((size_t)nv * 40)) return n > 0 ? n : 40;
			if (!rd.Bytes((size_t)ni * 2)) return n > 0 ? n : 40;
			rd.Align4();
			n++;
		}
	}
	for (UINT ai = 0; ai < nAtt; ai++) {
		if (!rd.Bytes(32 + 24)) return n > 0 ? n : 40;
		rd.U32();
		UINT nv = rd.U32(), ni = rd.U32();
		if (!rd.Bytes((size_t)nv * 32)) return n > 0 ? n : 40;
		if (!rd.Bytes((size_t)ni * 2)) return n > 0 ? n : 40;
		rd.Align4();
		n++;
	}
	for (UINT si = 0; si < nSt; si++) {
		if (!rd.Bytes(32)) return n > 0 ? n : 40;
		rd.U32();
		UINT nv = rd.U32(), ni = rd.U32();
		if (!rd.Bytes(24)) return n > 0 ? n : 40;
		if (!rd.Bytes((size_t)nv * 32)) return n > 0 ? n : 40;
		if (!rd.Bytes((size_t)ni * 2)) return n > 0 ? n : 40;
		rd.Align4();
		n++;
	}
	n++;
	return n > 0 ? n : 40;
}

int Renderer::CountGearMeshes() const
{
	const BYTE* data = NULL;
	DWORD size = 0;
	if (!LockPack(IDR_TAC_GEAR, &data, &size)) return 1;
	if (!data || size < 12) return 1;
	if (memcmp(data, "TACQ", 4) != 0 && memcmp(data, "TACG", 4) != 0) return 1;
	UINT n = 0;
	memcpy(&n, data + 4, 4);
	if (n < 1) return 1;
	if (n > 256) n = 256;
	return (int)n;
}

void Renderer::NoteAvaPhase(int phase, int phases, int i, int n)
{
	if (phases < 1) phases = 1;
	if (n < 1) n = 1;
	if (phase < 0) phase = 0;
	if (phase >= phases) phase = phases - 1;
	if (i < 0) i = 0;
	if (i > n) i = n;
	int local = (int)(((long long)phase * n + i) * 99 / ((long long)phases * n));
	if (local < 1) local = 1;
	if (local > 99) local = 99;
	FlashAvaLocal(local);
}

void Renderer::FlashAvaLocal(int local)
{
	int n = m_avaBatchN < 1 ? 1 : m_avaBatchN;
	int i = m_avaBatchI;
	if (i < 0) i = 0;
	if (i >= n) i = n - 1;
	if (local < 0) local = 0;
	if (local > 100) local = 100;
	int pct;
	if (local >= 100 && i + 1 >= n) pct = 100;
	else {
		int piece = local > 99 ? 99 : local;
		pct = (int)(((long long)i * 100 + piece) / n);
		if (pct < 1 && local > 0) pct = 1;
		if (pct > 99) pct = 99;
	}
	if (pct > m_avaPct || pct >= 100) FlashAvaPct(pct);
}

void Renderer::BeginAvaBatch(int n)
{
	m_avaBatchN = n < 1 ? 1 : n;
	m_avaBatchI = 0;
	m_avaBatchFrame = m_frame;
}

int Renderer::CountPendingAvatars(int n, const int* model, const int* avatar, const int* cup) const
{
	if (!model || !avatar || !cup || n < 1 || m_avaSegN <= 0) return 0;
	int keys[64][3];
	int nk = 0;
	int cnt = 0;
	for (int i = 0; i < n; i++) {
		int mdI = model[i], av = avatar[i], cp = cup[i];
		if (mdI < 0 || mdI >= (int)models.size() || av <= 0) continue;
		const Model& md = models[mdI];
		const AvaModel* am = NULL;
		for (const AvaModel& x : m_avaModels) if (!strcmp(x.name, md.name)) { am = &x; break; }
		if (!am) {
			const BOOL fem = strncmp(md.name, "HumF", 4) == 0;
			const BOOL mal = strncmp(md.name, "HumM", 4) == 0;
			if (fem || mal) {
				const char* ref = fem ? "HumF0" : "HumM0";
				for (const AvaModel& x : m_avaModels) if (!strcmp(x.name, ref)) { am = &x; break; }
			}
		}
		if (!am) continue;
		int ci = -1;
		for (int k = 0, c = 0; k < (int)m_avaCos.size(); k++) if (m_avaCos[k].gender == am->gender && ++c == av) { ci = k; break; }
		if (ci < 0) continue;
		int cupK = 0;
		if (am->gender >= 0 && am->gender < 2 && !m_avaBody[am->gender].m.dmax.empty() && !m_avaCos[ci].m.dmax.empty())
			cupK = Clampi(cp, 0, BUST_N - 1);
		BOOL seen = FALSE;
		for (int k = 0; k < nk; k++) if (keys[k][0] == mdI && keys[k][1] == ci && keys[k][2] == cupK) seen = TRUE;
		if (seen) continue;
		if (nk < 64) { keys[nk][0] = mdI; keys[nk][1] = ci; keys[nk][2] = cupK; nk++; }
		BOOL have = FALSE;
		for (const AvaCache& x : m_avaCache) if (x.model == mdI && x.cos == ci && x.cup == cupK) { have = TRUE; break; }
		if (!have) cnt++;
	}
	return cnt;
}

void Renderer::GrabAvaPlate()
{
	if (!m_bb || !m_dev || !m_ctx) return;
	D3D11_TEXTURE2D_DESC d = {};
	m_bb->GetDesc(&d);
	if (!m_avaPlate || m_avaPlateW != (int)d.Width || m_avaPlateH != (int)d.Height) {
		TAC_REL(m_avaPlate);
		d.BindFlags = 0;
		d.MiscFlags = 0;
		d.CPUAccessFlags = 0;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.MipLevels = 1;
		d.ArraySize = 1;
		d.SampleDesc.Count = 1;
		d.SampleDesc.Quality = 0;
		if (FAILED(m_dev->CreateTexture2D(&d, NULL, &m_avaPlate)) || !m_avaPlate) return;
		m_avaPlateW = (int)d.Width;
		m_avaPlateH = (int)d.Height;
	}
	ID3D11RenderTargetView* oldRT = NULL;
	ID3D11DepthStencilView* oldDS = NULL;
	m_ctx->OMGetRenderTargets(1, &oldRT, &oldDS);
	ID3D11RenderTargetView* none = NULL;
	m_ctx->OMSetRenderTargets(1, &none, NULL);
	m_ctx->CopyResource(m_avaPlate, m_bb);
	m_ctx->OMSetRenderTargets(1, oldRT ? &oldRT : &m_bbRtv, oldDS);
	if (oldRT) oldRT->Release();
	if (oldDS) oldDS->Release();
}

void Renderer::RestoreAvaPlate()
{
	if (!m_avaPlate || !m_ctx || !m_bb) return;
	ID3D11RenderTargetView* none = NULL;
	m_ctx->OMSetRenderTargets(1, &none, NULL);
	m_ctx->CopyResource(m_bb, m_avaPlate);
}

void Renderer::FlashAvaPct(int pct)
{
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	m_avaPct = pct;
	if (m_loadFn) {
		if (pct < m_avaShown && pct < 100) return;
		m_avaShown = pct;
		m_loadFn(pct, 100, m_loadUser);
		return;
	}
	if (m_avaBuildOnly) {
		if (pct > m_avaShown || pct >= 100) m_avaShown = pct;
		return;
	}
	if (m_inPortrait || !m_ctx || !m_bbRtv || !m_swap || m_w < 8 || m_h < 8) return;
	if (pct == m_avaShown) return;
	m_avaShown = pct;
	m_avaFlashed = TRUE;
	M4 oldView = m_view, oldProj = m_proj, oldVp = m_vp;
	V3 oldEye = m_eye;
	float oldTime = m_time;
	const BOOL full = m_avaLoadFull != 0;
	if (full) {
		V3 dir = v3(0.f, sinf(0.3f), cosf(0.3f));
		V3 eye = dir * 10.f;
		SetCamera(MLookAtLH(eye, v3(0, 0, 0), v3(0, 1, 0)), MPerspLH(0.8f, (float)m_w / (float)max(1, m_h), 0.1f, 400.f), eye, m_time);
		BeginMainPass();
		DrawSky(RGBA8(30, 34, 70), RGBA8(90, 70, 110), 0, 0);
		BeginHud();
	} else {
		if (!m_avaPlate) GrabAvaPlate();
		else RestoreAvaPlate();
		m_ctx->OMSetRenderTargets(1, &m_bbRtv, NULL);
		D3D11_VIEWPORT vp = { 0, 0, (float)m_w, (float)m_h, 0, 1 };
		m_ctx->RSSetViewports(1, &vp);
		m_fcb.screen[0] = (float)m_w; m_fcb.screen[1] = (float)m_h;
		m_fcb.screen[2] = 1.f / (float)m_w; m_fcb.screen[3] = 1.f / (float)m_h;
		UpdateFrameCB();
		m_hud.clear();
		m_hudSrv = m_srv[TEX_WHITE];
		m_hudPipe = 0;
	}
	float ui = (float)m_w / 1280.f;
	float uiH = (float)m_h / 720.f;
	if (uiH < ui) ui = uiH;
	if (ui < 0.62f) ui = 0.62f;
	if (ui > 2.2f) ui = 2.2f;
	if (full) {
		const wchar_t* title = TT(TX_TITLE);
		float th = 40.f * ui;
		float maxW = (float)m_w * .86f;
		while (th > 12.f && TextWidth(title, th, 1) > maxW) th *= 0.92f;
		HudText(title, m_w * .5f, m_h * .38f, th, RGBA8(255, 230, 160), 1, 1);
	}
	wchar_t line[80];
	swprintf_s(line, L"%s  %d%%/100%%", TT(TX_MISC_LOADING), pct);
	HudText(line, m_w * .5f, m_h * .50f, 20.f * ui, RGBA8(255, 255, 255), 1, 1, 1);
	float bw = 520.f * ui;
	if (bw > m_w * .62f) bw = m_w * .62f;
	float bh = 22.f * ui;
	float bx = ((float)m_w - bw) * .5f;
	float by = m_h * .56f;
	HudRect(bx, by, bw, bh, RGBA8(40, 28, 18, 220));
	float fill = bw * (pct / 100.f);
	if (pct > 0 && fill < 2.f) fill = 2.f;
	if (fill > 0.f) HudRect(bx, by, fill, bh, RGBA8(255, 214, 120, 255));
	EndHud();
	Present();
	if (full) {
		SetCamera(oldView, oldProj, oldEye, oldTime);
		m_vp = oldVp;
	}
}

BOOL Renderer::LoadAssets()
{
	if (m_assetsOk) return TRUE;
	const BYTE* data = NULL;
	DWORD size = 0;
	if (!LockPack(IDR_TAC_ASSETS, &data, &size)) return FALSE;
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
	src.gpuSkinFrame = 0;
	src.gpuSkinKey = 0;
	src.gpuSkinOwner = NULL;
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
	// 目録は足幅を測らない。姿勢も頂点も中身を読まず、同じバイト数だけ進める。
	if (!foot) {
		if (!rd.Bytes((size_t)nJ * 40 + 64 + (size_t)nJ * 64)) return FALSE;
		UINT nv = rd.U32(), ni = rd.U32();
		if (!nv || !ni || nv > 70000 || ni > 200000) return FALSE;
		if (!rd.Bytes(24)) return FALSE;
		if (!rd.Bytes((size_t)nv * 23)) return FALSE;
		rd.Align4();
		if (!rd.Bytes((size_t)ni * 2)) return FALSE;
		rd.Align4();
		return !rd.bad;
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
				R.LoadPulse((int)c + 1, (int)nc + 8);
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
				if ((k & 2047u) == 0) {
					int denom = (int)nc + 8;
					int at = (int)nc + 1 + (int)((nv > 0) ? ((unsigned long long)k * 6 / nv) : 0);
					if (at > denom - 1) at = denom - 1;
					R.LoadPulse(at, denom);
				}
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
			R.LoadPulse((int)nc + 7, (int)nc + 8);
			if (!R.MakeMesh(gm, poseV.data(), sizeof(VtxS), nv, idx.data(), ni)) return Bail();
			R.LoadPulse((int)nc + 8, (int)nc + 8);
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
							if (r < 1e-5f || r > 0.20f) continue; // 翼や袖など頭の骨に付いた長い頂点は頭蓋にしない
							float s = dy / r;
							float el = asinf(s < -1.f ? -1.f : (s > 1.f ? 1.f : s));
							int ia = (int)floorf((atan2f(dx, dz) + 3.14159265f) * (SKULL_NA / 6.2831853f));
							int ie = (int)floorf((el + 0.6f) * (SKULL_NE / 2.1707963f));
							ia = ((ia % SKULL_NA) + SKULL_NA) % SKULL_NA;
							if (ie < 0 || ie >= SKULL_NE) continue;
							float& R = skin.skullR[ie * SKULL_NA + ia];
							if (r > R) R = r;
						}
						// 頭頂は三角形が粗くて空の区画ができる。0 のまま残ると CSSkin の補間で頭蓋の半径が 0 に落ち、
						// 髪が頭蓋の中へ沈んでハゲになる。隣の区画から埋める。
						for (int pass = 0; pass < 6; pass++) for (int ie = 0; ie < SKULL_NE; ie++) for (int ia = 0; ia < SKULL_NA; ia++) {
							float& R = skin.skullR[ie * SKULL_NA + ia];
							if (R > 0.02f) continue;
							float best = R;
							best = fmaxf(best, skin.skullR[ie * SKULL_NA + (ia + 1) % SKULL_NA]);
							best = fmaxf(best, skin.skullR[ie * SKULL_NA + (ia + SKULL_NA - 1) % SKULL_NA]);
							if (ie > 0) best = fmaxf(best, skin.skullR[(ie - 1) * SKULL_NA + ia]);
							if (ie < SKULL_NE - 1) best = fmaxf(best, skin.skullR[(ie + 1) * SKULL_NA + ia]);
							R = best;
						}
						// 眼窩など内側だけ測れた区画は隣より細い。その方位の風で毛が頭蓋へ入る。
						for (int pass = 0; pass < 2; pass++) for (int ie = 0; ie < SKULL_NE; ie++) for (int ia = 0; ia < SKULL_NA; ia++) {
							float& R = skin.skullR[ie * SKULL_NA + ia];
							float nb = R;
							nb = fmaxf(nb, skin.skullR[ie * SKULL_NA + (ia + 1) % SKULL_NA]);
							nb = fmaxf(nb, skin.skullR[ie * SKULL_NA + (ia + SKULL_NA - 1) % SKULL_NA]);
							if (ie > 0) nb = fmaxf(nb, skin.skullR[(ie - 1) * SKULL_NA + ia]);
							if (ie + 1 < SKULL_NE) nb = fmaxf(nb, skin.skullR[(ie + 1) * SKULL_NA + ia]);
							if (nb > 0.05f && R < nb - 0.025f) R = nb - 0.025f;
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
	for (int i = 0; i < 160; i++) {
		CreatureSkin& s = m_skins.v[i];
		ReleaseSkinGpu(s);
		TAC_REL(s.baldIB);
		s.baldN = 0;
		if (s.bind.arena == 2) s.bind.reset(); else s.bind.clear();
		if (s.cloth.arena == 2) s.cloth.reset(); else s.cloth.clear();
		if (s.headTri.arena == 2) s.headTri.reset(); else s.headTri.clear();
		if (s.meshHair.arena == 2) s.meshHair.reset(); else s.meshHair.clear();
	}
	m_skins.clear();
	ReleaseAvatarCache();
	const BYTE* data = NULL;
	DWORD size = 0;
	if (!LockPack(IDR_TAC_MON, &data, &size)) return;
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
	for (int c = m_keepCreatures; c < 160; c++) {
		m_creatures.v[c].Release();
		CreatureSkin& sk = m_skins.v[c];
		const int rm = sk.model;
		if (rm >= 0) {
			if (m_scalpModel == rm) { m_scalpModel = -1; m_capDraw.ib = NULL; m_capDraw.nIdx = 0; m_capBind.clear(); }
			for (int s = 0; s < TAC_SCALP_CACHE; s++) if (m_scalps[s].model == rm) { m_scalps[s].model = -1; m_scalps[s].draw.Release(); m_scalps[s].bind.clear(); }
		}
		ReleaseSkinGpu(sk);
		TAC_REL(sk.baldIB);
		sk.baldN = 0;
		sk.model = -1;
		sk.bind.arena = 2; sk.bind.reset();
		sk.cloth.arena = 2; sk.cloth.reset();
		sk.headTri.arena = 2; sk.headTri.reset();
		sk.meshHair.arena = 2; sk.meshHair.reset();
	}
	for (int i = m_keepRigs; i < 192; i++) {
		for (int c = 0; c < 40; c++) {
			rigs[i].clips.v[c].q.arena = 2; rigs[i].clips.v[c].q.reset();
			rigs[i].clips.v[c].hips.arena = 2; rigs[i].clips.v[c].hips.reset();
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
	DrawParams d = dp;
	d.noTess = 1;
	if (d.mode > 0.5f && d.mode < 2.5f) d.mode = 0.f;
	DrawStatic(m_creatures[i], world, d);
}

void Renderer::DrawSkinnedCreature(int i, const Pose& pose, const M4& world, const DrawParams& dp, const void* owner)
{
	int slot = -1;
	for (int k = 0; k < (int)m_skins.size(); k++) if (m_skins[k].model == i) { slot = k; break; }
	if (slot < 0 || m_skins[slot].bind.empty() || slot >= (int)m_creatures.size()) return;
	SkinAndDraw(m_skins[slot], m_creatures[slot], NULL, 0.f, pose, world, dp, NULL, owner);
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
	if (face < 0 || face > 5 || n <= 0) return;
	static Buf<GpuFaceIn> rec;
	rec.resize(n);
	if (!rec.data() || rec.size() < n) return;
	memset(rec.data(), 0, sizeof(GpuFaceIn) * (size_t)n);
	const BOOL fem = src.faceSet == 1;
	static const DWORD irisF[5] = { RGBA8(51, 31, 20), RGBA8(92, 112, 140), RGBA8(140, 96, 48), RGBA8(56, 140, 78), RGBA8(176, 100, 28) };
	static const DWORD irisM[5] = { RGBA8(40, 24, 16), RGBA8(86, 107, 133), RGBA8(56, 128, 72), RGBA8(92, 56, 26), RGBA8(158, 102, 30) };
	int fi = face >= 1 && face <= 5 ? face - 1 : 0;
	DWORD iris = fem ? irisF[fi] : irisM[fi];
	// 0 は形を変えず唇だけ。1 以降は 0 丸い大眼 1 細い眼 2 やわらか 3 尖った耳 4 大きい眼と広い口
	int kind = -1;
	if (face >= 1 && face <= 5) {
		kind = face - 1;
		if (!fem) { static const int mk[5] = { 1, 2, 3, 0, 4 }; kind = mk[kind]; }
	}
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
			if (kind == 0) grow = 0.16f;
			else if (kind == 1) { grow = 0.06f; flatten = 0.04f; }
			else if (kind == 2) { grow = 0.12f; lift = 0.001f; }
			else if (kind == 3) grow = 0.10f;
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
		if (kind == 3 && y < nose[1] && y > chin[1] - 0.01f && ax < half * 0.55f && z < EL[2] + 0.03f) {
			float t = (nose[1] - y) / (nose[1] - chin[1] + 0.001f);
			if (t > 1.f) t = 1.f;
			if (t < 0) t = 0;
			ox += (x >= cx ? 0.004f : -0.004f) * t;
		}
		if (ax > half * 0.28f && ax < half * 0.78f && y < EL[1] + 0.01f && y > chin[1] && z < EL[2] + 0.03f) {
			float t = 1.f - fabsf(y - (EL[1] + chin[1]) * 0.5f) / (EL[1] - chin[1] + 0.001f);
			if (t < 0) t = 0;
			ox += (x >= cx ? 0.005f : -0.005f) * t;
		}
		if (kind == 4 && y < nose[1] && y > chin[1] && ax < half * 0.42f && z < nose[2] + 0.025f) {
			float mid = (nose[1] + chin[1]) * 0.5f;
			float t = 1.f - fabsf(y - mid) / ((nose[1] - chin[1]) * 0.5f + 0.001f);
			if (t > 0 && ax > 0.004f) ox += (x >= cx ? 0.008f : -0.008f) * t;
		}
		if (!paint && lum > 0.22f) {
			const float mouthY = nose[1] * 0.40f + chin[1] * 0.60f;
			const float dy = y - mouthY, lipH = 0.011f, lipX = kind == 4 ? half * 0.34f : half * 0.30f;
			if (ax < lipX && z < nose[2] + 0.016f && z > chin[2] - 0.012f && dy > -lipH && dy < lipH * 0.75f) {
				const float tx = 1.f - ax / lipX, ty = 1.f - fabsf(dy) / lipH;
				const float lw = tx * tx * (ty > 0.f ? ty * ty : 0.f);
				if (lw > 0.12f) {
					const float a = 0.52f * lw;
					const float sr = (b.c & 255) / 255.f, sg = ((b.c >> 8) & 255) / 255.f, sbv = ((b.c >> 16) & 255) / 255.f;
					auto q8 = [](float v) { int i = (int)(v * 255.f + .5f); return i < 0 ? 0 : (i > 255 ? 255 : i); };
					float lr = fem ? 0.62f : 0.55f, lg = fem ? 0.28f : 0.30f, lb = fem ? 0.34f : 0.32f;
					float amt = fem ? a : a * 0.72f;
					eyeCol = RGBA8(q8(sr * (1.f - amt) + lr * amt), q8(sg * (1.f - amt) + lg * amt), q8(sbv * (1.f - amt) + lb * amt));
					paint = 1;
				}
			}
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
	if (src.faceN <= 0 || src.faceSet < 0 || src.faceSet > 1 || face < 0) return;
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

static unsigned TacPoseKey(const Pose& p)
{
	unsigned h = (unsigned)p.nJ * 2654435761u;
	int n = p.nJ;
	if (n > MAXJ) n = MAXJ;
	for (int i = 0; i < n; i++) {
		const float* m = p.palette[i].m;
		unsigned x, y, z, r;
		memcpy(&x, m + 12, 4);
		memcpy(&y, m + 13, 4);
		memcpy(&z, m + 14, 4);
		memcpy(&r, m, 4);
		h ^= x + 0x9e3779b9u + (h << 6) + (h >> 2);
		h ^= y + 0x9e3779b9u + (h << 6) + (h >> 2);
		h ^= z + 0x9e3779b9u + (h << 6) + (h >> 2);
		h ^= r + 0x9e3779b9u + (h << 6) + (h >> 2);
	}
	return h;
}

static BOOL TacMakeRawVB(ID3D11Device* dev, UINT nv, ID3D11Buffer** vb, ID3D11UnorderedAccessView** uav)
{
	if (uav) TAC_REL(*uav);
	if (vb) TAC_REL(*vb);
	if (!dev || !vb || !uav || nv < 1) return FALSE;
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = nv * 36u;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_UNORDERED_ACCESS;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	if (FAILED(dev->CreateBuffer(&bd, NULL, vb))) return FALSE;
	D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
	ud.Format = DXGI_FORMAT_R32_TYPELESS;
	ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	ud.Buffer.NumElements = nv * 36u / 4u;
	ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	if (FAILED(dev->CreateUnorderedAccessView(*vb, &ud, uav))) { TAC_REL(*vb); return FALSE; }
	return TRUE;
}

Renderer::SkinOut* Renderer::TakeSkinOut(const void* owner, CreatureSkin& src)
{
	if (!owner || src.gpuNv < 1 || !m_dev) return NULL;
	m_outStamp++;
	if (m_outStamp == 0) m_outStamp = 1;
	SkinOut* empty = NULL;
	SkinOut* old = &m_skinOut[0];
	for (int i = 0; i < TAC_SKIN_OUT; i++) {
		SkinOut& s = m_skinOut[i];
		if (s.owner == owner && s.src == &src && s.vb && s.nv == src.gpuNv) { s.stamp = m_outStamp; return &s; }
		if (!s.vb && !empty) empty = &s;
		if (s.stamp < old->stamp) old = &s;
	}
	SkinOut* s = empty ? empty : old;
	if (!s->vb || s->nv != src.gpuNv) {
		if (!TacMakeRawVB(m_dev, (UINT)src.gpuNv, &s->vb, &s->uav)) {
			s->owner = NULL; s->src = NULL; s->nv = 0; s->frame = 0; return NULL;
		}
		s->nv = src.gpuNv;
	}
	if (s->owner != owner || s->src != &src) { s->frame = 0; s->key = 0; }
	s->owner = owner;
	s->src = &src;
	s->stamp = m_outStamp;
	return s;
}

Renderer::HairOut* Renderer::TakeHairOut(const void* owner)
{
	if (!owner || m_hairNv < 30 || !m_dev) return NULL;
	m_outStamp++;
	if (m_outStamp == 0) m_outStamp = 1;
	HairOut* empty = NULL;
	HairOut* old = &m_hairOut[0];
	for (int i = 0; i < TAC_HAIR_OUT; i++) {
		HairOut& s = m_hairOut[i];
		if (s.owner == owner && s.vb && s.nv == (int)m_hairNv) { s.stamp = m_outStamp; return &s; }
		if (!s.vb && !empty) empty = &s;
		if (s.stamp < old->stamp) old = &s;
	}
	HairOut* s = empty ? empty : old;
	if (!s->vb || s->nv != (int)m_hairNv) {
		if (!TacMakeRawVB(m_dev, m_hairNv, &s->vb, &s->uav)) {
			s->owner = NULL; s->nv = 0; s->frame = 0; return NULL;
		}
		s->nv = (int)m_hairNv;
	}
	if (s->owner != owner) { s->frame = 0; s->key = 0; }
	s->owner = owner;
	s->stamp = m_outStamp;
	return s;
}

BOOL Renderer::GpuSkinAndDraw(CreatureSkin& src, const float* jig, float amt, const Pose& pose, const M4& world, const DrawParams& dp, GpuMesh& base,
	AvaCache* cloth, const void* owner)
{
	if (!m_csSkin || !m_cbSkinBone || !m_cbSkinParm || !m_faceDummySRV || !base.ib) return FALSE;
	if (!EnsureSkinGpu(src, jig)) return FALSE;
	unsigned key = TacPoseKey(pose);
	key ^= (unsigned)dp.face * 0x9E3779B1u;
	key ^= (unsigned)(dp.blink * 1024.f);
	key ^= (unsigned)dp.hideMeshHair;
	key ^= dp.clothTint;
	key ^= (unsigned)dp.skinTone;
	key ^= (unsigned)dp.hairJoint;
	key ^= dp.hairTint;
	{
		unsigned u;
		memcpy(&u, &dp.clothHue, 4); key ^= u;
		memcpy(&u, &dp.bodyScale, 4); key ^= u;
		float am = jig ? amt : 0.f;
		memcpy(&u, &am, 4); key ^= u;
		key ^= m_cutActorGen * 0x85EBCA6Bu;
	}
	SkinOut* so = owner ? TakeSkinOut(owner, src) : NULL;
	ID3D11Buffer* outVB = (so && so->vb) ? so->vb : src.outVB;
	ID3D11UnorderedAccessView* outUAV = (so && so->uav) ? so->uav : src.outUAV;
	auto drawOut = [&](ID3D11Buffer* vb) {
		if (!vb) return;
		ID3D11Buffer* saved = base.vb;
		UINT savedStride = base.stride;
		base.vb = vb;
		base.stride = sizeof(VtxS);
		DrawParams d = dp;
		d.noTess = 1;
		if (d.mode > 0.5f && d.mode < 2.5f) d.mode = 0.f;
		DrawStatic(base, world, d);
		base.vb = saved;
		base.stride = savedStride;
	};
	if (so && so->frame == m_frame && so->key == key) { drawOut(so->vb); return TRUE; }
	if (!so && owner && src.outVB && src.gpuSkinFrame == m_frame && src.gpuSkinOwner == owner && src.gpuSkinKey == key) {
		drawOut(src.outVB);
		return TRUE;
	}
	const BOOL facePack = dp.face > 0 && src.faceSet >= 0 && src.faceSet <= 1 && dp.face < m_faces[src.faceSet].nFace;
	const BOOL faceOn = src.faceN > 0 && src.faceSet >= 0 && src.faceSet <= 1 && (src.faceSynth ? dp.face >= 0 : facePack);
	if (faceOn) UpdateFaceGpu(src, dp.face);
	D3D11_MAPPED_SUBRESOURCE ms;
	ID3D11Buffer* skinBone = TacCbAcquire(g_skinBoneR, m_ctx, m_cbSkinBone, ms);
	if (!skinBone) return FALSE;
	int nj = pose.nJ;
	if (nj < 0) nj = 0;
	if (nj > MAXJ) nj = MAXJ;
	if (nj) memcpy(ms.pData, pose.palette, sizeof(M4) * (size_t)nj);
	if (nj < MAXJ) memset((M4*)ms.pData + nj, 0, sizeof(M4) * (size_t)(MAXJ - nj));
	m_ctx->Unmap(skinBone, 0);
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
					const float u = Clampf(amt / 7.f, 0.f, 1.f);
					// 女性は下着の無い柔らかい乳房。大きいほど重く、ゆっくり大きく揺れて長く残る。
					// 男性は胸板の筋肉。硬いばねで小さく速く戻る。厚いほど少し大きい。
					const BOOL pec = !strncmp(models[src.model].name, "HumM", 4);
					const float inert = pec ? Lerpf(0.22f, 0.50f, u) : Lerpf(0.58f, 1.35f, u);
					const float k = pec ? Lerpf(0.55f, 0.40f, u) : Lerpf(0.30f, 0.095f, u);
					const float damp = pec ? 0.76f : Lerpf(0.84f, 0.925f, u);
					const float maxL = pec ? Lerpf(0.003f, 0.010f, u) : Lerpf(0.010f, 0.036f, u);
					float tgt[3] = { -dx * inert, -dy * inert * 1.15f, -dz * inert };
					for (int a = 0; a < 3; a++) {
						src.bustVel[a] += tgt[a] + (0.f - src.bustLag[a]) * k;
						src.bustVel[a] *= damp;
						src.bustLag[a] += src.bustVel[a];
					}
					if (src.bustVel[1] > 0.f) src.bustVel[1] *= Lerpf(0.92f, 0.78f, u);
					float L2 = src.bustLag[0] * src.bustLag[0] + src.bustLag[1] * src.bustLag[1] + src.bustLag[2] * src.bustLag[2];
					if (L2 > maxL * maxL) {
						float s = maxL / sqrtf(L2);
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
	if (dp.blink > 0.001f && src.faceN > 0) {
		float ex = src.faceEye[0][0], ey = src.faceEye[0][1], ez = src.faceEye[0][2];
		float rx = src.faceEye[1][0], ry = src.faceEye[1][1], rz = src.faceEye[1][2];
		float el = ex * ex + ey * ey + ez * ez;
		float er = rx * rx + ry * ry + rz * rz;
		if (el > 1e-4f && er > 1e-4f) {
			float rad = src.faceHalf > 0.02f ? src.faceHalf * 0.28f : 0.016f;
			if (rad < 0.012f) rad = 0.012f;
			if (rad > 0.028f) rad = 0.028f;
			prm.nip0[0] = ex; prm.nip0[1] = ey; prm.nip0[2] = ez; prm.nip0[3] = dp.blink;
			prm.nip1[0] = rx; prm.nip1[1] = ry; prm.nip1[2] = rz; prm.nip1[3] = rad;
		}
	}
	prm.bust[0] = jig ? 1.f : amt;
	prm.bust[1] = jig ? Clampf(amt / 7.f, 0.f, 1.f) : 0.f;
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
	ID3D11Buffer* skinParm = TacCbAcquire(g_skinParmR, m_ctx, m_cbSkinParm, ms);
	if (!skinParm) return FALSE;
	memcpy(ms.pData, &prm, sizeof(prm));
	m_ctx->Unmap(skinParm, 0);
	ID3D11Buffer* noneVB = NULL;
	UINT zero = 0;
	m_ctx->IASetVertexBuffers(0, 1, &noneVB, &zero, &zero);
	ID3D11Buffer* cbs[3] = { skinBone, skinParm, TacCbCur(g_hairClothR, m_cbHairCloth) };
	m_ctx->CSSetConstantBuffers(0, 3, cbs);
	ID3D11ShaderResourceView* srvs[2] = { src.bindSRV, (faceOn && src.faceSRV) ? src.faceSRV : m_faceDummySRV };
	m_ctx->CSSetShaderResources(0, 2, srvs);
	if (TacQueueUses(outVB)) FlushQueuedDraws();
	UINT cnt = 0;
	m_ctx->CSSetUnorderedAccessViews(0, 1, &outUAV, &cnt);
	m_ctx->CSSetShader(m_csSkin, NULL, 0);
	m_ctx->Dispatch(((UINT)src.gpuNv + 63u) / 64u, 1, 1);
	if (cloth && owner && !m_inPortrait) {
		ID3D11Buffer* keepVB = src.outVB;
		ID3D11UnorderedAccessView* keepUAV = src.outUAV;
		if (so) { src.outVB = so->vb; src.outUAV = so->uav; }
		RunCloth(*cloth, owner, src, pose, world, dp);
		src.outVB = keepVB;
		src.outUAV = keepUAV;
	}
	{
		ID3D11UnorderedAccessView* nou[2] = { NULL, NULL };
		m_ctx->CSSetUnorderedAccessViews(0, 2, nou, NULL);
	}
	if (so) { so->frame = m_frame; so->key = key; }
	else {
		src.gpuSkinFrame = m_frame;
		src.gpuSkinKey = key;
		src.gpuSkinOwner = owner;
	}
	drawOut(outVB);
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
	if (GpuSkinAndDraw(src, jig, jig ? amt : 0.f, pose, world, dp, base, cloth, owner)) return;
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
	DrawParams d = dp;
	d.noTess = 1;
	if ((UINT)(v.size() * sizeof(VtxS)) > m_vbDynBytes || !m_vbDyn) {
		DrawStatic(base, world, d);
		return;
	}
	UINT dynOff = 0;
	void* dst = DynMap((UINT)(v.size() * sizeof(VtxS)), dynOff);
	if (!dst) return;
	memcpy(dst, v.data(), v.size() * sizeof(VtxS));
	m_ctx->Unmap(m_vbDyn, 0);
	m_dynOff = dynOff;
	ID3D11Buffer* saved = base.vb;
	base.vb = m_vbDyn;
	UINT savedStride = base.stride;
	base.stride = sizeof(VtxS);
	DrawStatic(base, world, d);
	base.vb = saved;
	base.stride = savedStride;
}

void Renderer::LoadGearPack()
{
	for (auto& c : m_gear) c.Release();
	m_gear.clear();
	m_gearGrip.clear();
	const BYTE* data = NULL;
	DWORD size = 0;
	if (!LockPack(IDR_TAC_GEAR, &data, &size)) return;
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
	const BYTE* data = NULL;
	DWORD size = 0;
	if (!LockPack(IDR_TAC_FACE, &data, &size)) return;
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
	for (ClothInst& c : m_clothInst) { TAC_REL(c.uav); TAC_REL(c.sb); TAC_REL(c.softUav); TAC_REL(c.softSb); }
	m_clothInst.clear();
}

static void ReleaseAvaEntry(Renderer::AvaCache& c)
{
		TAC_REL(c.skin.baldIB);
		c.skin.baldN = 0;
	DropSkinGpu(c.skin);
		c.gm.Release();
	for (int b = 0; b < 3; b++) { TAC_REL(c.clothSRV[b]); TAC_REL(c.clothSB[b]); }
	for (int b = 0; b < 2; b++) { TAC_REL(c.softSRV[b]); TAC_REL(c.softSB[b]); }
	c.clothN = c.clothVN = 0;
	c.softN = c.softVN = 0;
	}

void Renderer::ReleaseAvatarCache()
{
	for (AvaCache& c : m_avaCache) ReleaseAvaEntry(c);
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
	const BYTE* data = NULL;
	DWORD size = 0;
	if (!LockPack(IDR_TAC_AVA, &data, &size)) return;
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
struct EdgeTri { unsigned long long e; int t; };
static int CmpEdgeTri(const void* a, const void* b)
{
	unsigned long long x = ((const EdgeTri*)a)->e, y = ((const EdgeTri*)b)->e;
	return (x > y) - (x < y);
}

// 胸の形を作る基準。左右の乳首（体の頂点。0=-X 側）と、胴の縦軸（乳首の中点 x0、乳首の高さの胴の前後の中点 z0）。
// HumF1/F2 はバインドが +X へ寄るので、軸は x=0 ではなく左右の乳首の中点にする。
struct BustRef { int kn[2]; float z0, x0; };

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
static Renderer* g_avaPlate = NULL;

static BOOL RefineBust(Renderer::CreatureSkin& sk, Buf<float>& bx, Buf<float>& bn, int nb, int nv, const Buf<WORD>& bodyTri,
	const Buf<BYTE>& keep, const Buf<WORD>& cosTri, const BYTE* armJ, int maxAdd,
	Buf<int>& bOff, Buf<WORD>& bTri, Buf<WORD>& cTri, int& nbA, int& nvA, BustRef& br)
{
	typedef Renderer::CSkinV SV;
	(void)keep;
	const int nBT = bodyTri.size() / 3, nCT = cosTri.size() / 3;
	nbA = nb; nvA = nv;
	br.kn[0] = br.kn[1] = -1; br.z0 = br.x0 = 0.f;
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
		br.x0 = (V[br.kn[0]].p[0] + V[br.kn[1]].p[0]) * 0.5f;
		float lo = 1e9f, hi = -1e9f;
		const float yn = V[br.kn[1]].p[1];
		for (int k = 0; k < nb; k++) {
			const float* p = V[k].p;
			if (fabsf(p[1] - yn) >= 0.01f || fabsf(p[0] - br.x0) >= 0.03f || armOf(V[k])) continue;
			lo = fminf(lo, p[2]); hi = fmaxf(hi, p[2]);
		}
		if (lo < hi) br.z0 = (lo + hi) * 0.5f; else { ok = FALSE; br.kn[0] = br.kn[1] = -1; br.x0 = 0.f; }
	}
	float thN[2] = {}, rhN[2] = {}, yN[2] = {};
	for (int s = 0; ok && s < 2; s++) {
		const float* p = V[br.kn[s]].p;
		const float dx = p[0] - br.x0, dz = p[2] - br.z0;
		thN[s] = atan2f(dx, -dz); rhN[s] = sqrtf(dx * dx + dz * dz); yN[s] = p[1];
	}
	const float W7 = kBustW[BUST_N - 1];
	const float rao = .48f * W7 * 1.4f, rai = .42f * W7 * 1.4f, rbu = fminf(.5f * W7, .08f) * 1.4f, rbd = .4f * W7 * 1.4f;
	int added = 0;
	float L = 0.007f;
	for (int lev = 0; ok && lev < 2; lev++) {
		// すでに細かい服は 2 段目を割らない。作成で衣装を送ったときの止まりを短くする。
		if (lev == 1 && (int)V.size() > 16000) break;
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
			if (g_avaPlate && (k & 63) == 0) g_avaPlate->NoteAvaPhase(1, 5, k, n);
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
			const float th = atan2f(p[0] - br.x0, br.z0 - p[2]);
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
			// 印の有無は表で見る。閉じ終わってから一度だけ並べ、辺の番号は今までと同じ。
			int hcap = 1024;
			int need = nT > 0 ? nT * 8 + 8 : 1024;
			if (need < 1024) need = 1024;
			if (need > (1 << 22)) need = 1 << 22;
			while (hcap < need) hcap *= 2;
			static Buf<unsigned long long> hk;
			hk.fill(hcap, ~0ull);
			if (hk.size() < hcap) return FALSE;
			auto putK = [&](unsigned long long key) {
				unsigned h = (unsigned)(key * 11400714819323198485ull);
				for (int s = 0; s < hcap; s++) {
					int slot = (int)((h + (unsigned)s) & (unsigned)(hcap - 1));
					if (hk[slot] == ~0ull || hk[slot] == key) { hk[slot] = key; return; }
				}
			};
			auto hasK = [&](unsigned long long key) -> BOOL {
				unsigned h = (unsigned)(key * 11400714819323198485ull);
				for (int s = 0; s < hcap; s++) {
					int slot = (int)((h + (unsigned)s) & (unsigned)(hcap - 1));
					if (hk[slot] == ~0ull) return FALSE;
					if (hk[slot] == key) return TRUE;
				}
				return FALSE;
			};
			for (int i = 0; i < nM; i++) putK(M[i]);
			// 印の付いた辺に触る三角形だけを見る。閉じた辺の集合は、全三角形を波ごとになでるのと同じ。
			{
				static Buf<EdgeTri> adj;
				static Buf<unsigned long long> que;
				adj.clear();
				if (nT > 0 && nT < 10000000 / 3) adj.reserve(nT * 3);
				for (int i = 0; i < nT; i++) {
					const int* q = &T[i * 3];
					for (int e = 0; e < 3; e++) {
						EdgeTri et;
						et.e = EdgeKey(root[q[e]], root[q[(e + 1) % 3]]);
						et.t = i;
						adj.push_back(et);
					}
				}
				if (adj.size() >= nT * 3) {
					if (adj.size() > 1) qsort(adj.data(), adj.size(), sizeof(EdgeTri), CmpEdgeTri);
					que.clear();
					for (int i = 0; i < nM; i++) que.push_back(M[i]);
					for (int qi = 0; qi < que.size(); qi++) {
						const unsigned long long e = que[qi];
						int lo = 0, hi = adj.size();
						while (lo < hi) {
							const int mid = (lo + hi) >> 1;
							if (adj[mid].e < e) lo = mid + 1; else hi = mid;
						}
						for (int u = lo; u < adj.size() && adj[u].e == e; u++) {
							const int* q = &T[adj[u].t * 3];
							unsigned long long k3[3];
							int c = 0, miss = -1;
							for (int ee = 0; ee < 3; ee++) {
								k3[ee] = EdgeKey(root[q[ee]], root[q[(ee + 1) % 3]]);
								if (hasK(k3[ee])) c++; else miss = ee;
							}
							if (c == 2 && !hasK(k3[miss])) {
								putK(k3[miss]);
								M.push_back(k3[miss]);
								que.push_back(k3[miss]);
							}
						}
					}
				} else {
					int guard = 0;
					for (;;) {
						add.clear();
						if (++guard > nT + 2) break;
						for (int i = 0; i < nT; i++) {
							const int* q = &T[i * 3];
							unsigned long long k3[3];
							int c = 0, miss = -1;
							for (int e = 0; e < 3; e++) {
								k3[e] = EdgeKey(root[q[e]], root[q[(e + 1) % 3]]);
								if (hasK(k3[e])) c++; else miss = e;
							}
							if (c == 2 && !hasK(k3[miss])) add.push_back(k3[miss]);
						}
						if (add.empty()) break;
						for (int i = 0; i < add.size(); i++) { putK(add[i]); M.push_back(add[i]); }
					}
				}
			}
			nM = 0;
			if (M.size() > 1) qsort(M.data(), M.size(), sizeof(unsigned long long), CmpU64);
			for (int i = 0; i < M.size(); i++) if (nM == 0 || M[i] != M[nM - 1]) M[nM++] = M[i];
			M.resize(nM);
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
// ・服 [nb, ne) は、近い体（腹側・同じ左右）の山の動きだけをガウスで写し、中央の谷も胸の頂の高さに少し引き上げる。
// 半径以内の点だけを見る。セルは半径の半分、周囲は 3 セルまで見るので取りこぼさない。
// 作業バッファは使い回す。呼び出しごとに arena へ捨てない。
static Buf<int> gNearHead, gNearNext, gNearHit;
struct NearGrid {
	float org[3], inv, r2;
	int dim[3], ncell, stride;
	const float* p;
	const int* id;
	BOOL ok;
	void build(const float* pts, int str, const int* ids, int n, float radius, float limit2) {
		ok = FALSE;
		p = pts; stride = str; id = ids;
		if (n <= 0 || radius <= 1e-8f || !pts || !ids) return;
		inv = 2.f / radius;
		r2 = limit2;
		org[0] = org[1] = org[2] = 1e9f;
		float hi[3] = { -1e9f, -1e9f, -1e9f };
		for (int i = 0; i < n; i++) {
			const float* q = pts + (size_t)ids[i] * str;
			for (int a = 0; a < 3; a++) { if (q[a] < org[a]) org[a] = q[a]; if (q[a] > hi[a]) hi[a] = q[a]; }
		}
		ncell = 1;
		for (int a = 0; a < 3; a++) {
			int d = (int)((hi[a] - org[a]) * inv) + 1;
			if (d < 1) d = 1;
			if (d > 512) return;
			dim[a] = d;
			if (ncell > 2000000 / d) return;
			ncell *= d;
		}
		gNearHead.fill(ncell, -1);
		gNearNext.resize(n);
		if (gNearHead.size() < ncell || gNearNext.size() < n) return;
		for (int i = 0; i < n; i++) {
			const float* q = pts + (size_t)ids[i] * str;
			int c[3];
			for (int a = 0; a < 3; a++) {
				c[a] = (int)((q[a] - org[a]) * inv);
				if (c[a] < 0) c[a] = 0;
				if (c[a] >= dim[a]) c[a] = dim[a] - 1;
			}
			int cell = c[0] + dim[0] * (c[1] + dim[1] * c[2]);
			gNearNext[i] = gNearHead[cell];
			gNearHead[cell] = i;
		}
		ok = TRUE;
	}
	void gather(const float* q) {
		gNearHit.clear();
		if (!ok) return;
		int c[3];
		for (int a = 0; a < 3; a++) c[a] = (int)((q[a] - org[a]) * inv);
		for (int dz = -3; dz <= 3; dz++) for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) {
			int x = c[0] + dx, y = c[1] + dy, z = c[2] + dz;
			if (x < 0 || y < 0 || z < 0 || x >= dim[0] || y >= dim[1] || z >= dim[2]) continue;
			for (int i = gNearHead[x + dim[0] * (y + dim[1] * z)]; i >= 0; i = gNearNext[i]) {
				const float* b = p + (size_t)id[i] * stride;
				const float dxp = b[0] - q[0], dyp = b[1] - q[1], dzp = b[2] - q[2];
				if (dxp * dxp + dyp * dyp + dzp * dzp <= r2) gNearHit.push_back(id[i]);
			}
		}
	}
};

static_assert(sizeof(Renderer::CSkinV) == 36, "near grid stride");
static int CmpAscI(const void* a, const void* b)
{
	int x = *(const int*)a, y = *(const int*)b;
	return (x > y) - (x < y);
}

static void ShapeCup(Renderer::CSkinV* v, int nv, int nb, int ne, const float* bn, int cup, const BustRef& br,
	const WORD* bodyTri, int nBodyTri, const BYTE* armJ, const WORD* idx, int nIdx, float* jig)
{
	if (nb <= 0 || nb > ne || ne > nv || !bn || br.kn[0] < 0 || br.kn[1] < 0 || br.kn[0] >= nb || br.kn[1] >= nb) return;
	cup = Clampi(cup, 0, BUST_N - 1);
	static Buf<float> P, S, Wl, tw, D, U, L;
	static Buf<int> root;
	static Buf<BYTE> arm, in;
	static Buf<int> cand;
	P.resize(nv * 3); S.resize(nb * 3); Wl.resize(nb * 3); tw.resize(nb); D.resize(nv * 3);
	root.resize(nb); arm.resize(nb); in.resize(nb);
	if (P.size() < nv * 3 || S.size() < nb * 3 || Wl.size() < nb * 3 || tw.size() < nb || D.size() < nv * 3
		|| root.size() < nb || arm.size() < nb || in.size() < nb) return;
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
			if (g_avaPlate && (k & 63) == 0) g_avaPlate->NoteAvaPhase(2, 5, k, nb);
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
	const float z0 = br.z0, x0 = br.x0;
	const float dxR = S[br.kn[1] * 3] - x0, dzR = S[br.kn[1] * 3 + 2] - z0;
	const float dxL = S[br.kn[0] * 3] - x0, dzL = S[br.kn[0] * 3 + 2] - z0;
	const float rho0 = 0.5f * (sqrtf(dxR * dxR + dzR * dzR) + sqrtf(dxL * dxL + dzL * dzL));
	if (rho0 < 0.02f) return;
	for (int k = 0; k < nb; k++) {
		const Renderer::CSkinV& q = v[k];
		int m = 0;
		for (int c = 1; c < 4; c++) if (q.w[c] > q.w[m]) m = c;
		arm[k] = armJ[q.j[m]] ? 1 : 0;
	}
	// A カップ（S = p + bn）はすでに平らで裏返りもない胸板になっている。3 次曲面で胸壁を作り直すと、
	// 外の輪に混ざる肩の半径に引かれて胸の中央が 3.7cm 奥へ凹み、山を足しても平らな箱になる。S をそのまま土台にする。
	float thN[2], yN[2], rhN[2];
	for (int s = 0; s < 2; s++) {
		const float* p = &S[br.kn[s] * 3];
		thN[s] = atan2f(p[0] - x0, z0 - p[2]); yN[s] = p[1];
		rhN[s] = sqrtf((p[0] - x0) * (p[0] - x0) + (p[2] - z0) * (p[2] - z0));
	}
	const float W7 = kBustW[BUST_N - 1];
	auto rad = [&](float th, float y, int s, float W, float kk, float* uo, float* vo) {
		const float u = (th - thN[s]) * (s ? 1.f : -1.f) * rhN[s], vv = y - yN[s];
		const float a = (u > 0.f ? .48f * W : .42f * W) * kk, b = (vv > 0.f ? fminf(.5f * W, .08f) : .4f * W) * kk;
		if (uo) *uo = u;
		if (vo) *vo = vv;
		return sqrtf((u / a) * (u / a) + (vv / b) * (vv / b));
	};
	(void)bodyTri; (void)nBodyTri;
	// 腕らしさ: 上腕・手からの距離で 0→1（5cm）。肩は形だけ残し、距離には入れない。
	{
		float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
		for (int k = 0; k < nb; k++) {
			const float* p = &S[k * 3];
			const float th = atan2f(p[0] - x0, z0 - p[2]);
			in[k] = root[k] == k && fabsf(th) < 1.8f && (rad(th, p[1], 0, W7, 1.45f, 0, 0) < 1.f || rad(th, p[1], 1, W7, 1.45f, 0, 0) < 1.f);
			tw[k] = 0.f;
			if (in[k]) for (int a = 0; a < 3; a++) { lo[a] = fminf(lo[a], v[k].p[a]); hi[a] = fmaxf(hi[a], v[k].p[a]); }
		}
		cand.clear();
		for (int k = 0; k < nb; k++) {
			const float* p = v[k].p;
			int m = 0;
			for (int c = 1; c < 4; c++) if (v[k].w[c] > v[k].w[m]) m = c;
			if (arm[k] && armJ[v[k].j[m]] == 1 && root[k] == k && p[0] > lo[0] - .05f && p[0] < hi[0] + .05f && p[1] > lo[1] - .05f && p[1] < hi[1] + .05f && p[2] > lo[2] - .05f && p[2] < hi[2] + .05f) cand.push_back(k);
		}
		NearGrid armG;
		armG.build(v[0].p, (int)(sizeof(Renderer::CSkinV) / sizeof(float)), cand.data(), cand.size(), .05f, .05f * .05f);
		for (int k = 0; k < nb; k++) {
			if (!in[k]) continue;
			if (arm[k]) continue;
			float d2 = .05f * .05f;
			if (armG.ok) {
				armG.gather(v[k].p);
				for (int i = 0; i < gNearHit.size(); i++) {
					const float* q = v[gNearHit[i]].p;
					const float dx = q[0] - v[k].p[0], dy = q[1] - v[k].p[1], dz = q[2] - v[k].p[2];
					d2 = fminf(d2, dx * dx + dy * dy + dz * dz);
				}
			} else for (int i = 0; i < cand.size(); i++) {
				const float* q = v[cand[i]].p;
				const float dx = q[0] - v[k].p[0], dy = q[1] - v[k].p[1], dz = q[2] - v[k].p[2];
				d2 = fminf(d2, dx * dx + dy * dy + dz * dz);
			}
			tw[k] = Smooth01(sqrtf(d2) / .05f);
		}
		for (int k = 0; k < nb; k++) tw[k] = tw[root[k]];
	}
	// 胸壁 Wl。S = p + bn には乳首の周りだけ 4.5cm 突き出た狭い突起や乳房下の段が残っていて、そのまま山 D を足すと二重盛りや横皺になる。
	// 外の輪（|th| < 1.35, r = 1.10〜1.65）と胸骨（|th| < 0.14）に 2 次曲面 rW(u, v) を当て、体も服も既存の段を滑らかな胸板に戻してから山を載せる。
	double cfS[2][6] = {};
	BOOL cfOk[2] = { FALSE, FALSE };
	for (int s = 0; s < 2; s++) {
		const float sgn = s ? 1.f : -1.f;
		double A[6][7] = {};
		int nr = 0;
		for (int k = 0; k < nb; k++) {
			if (arm[k]) continue;
			const float* p = &S[k * 3];
			const float th = atan2f(p[0] - x0, z0 - p[2]), y = p[1];
			float u, vv;
			const float r = rad(th, y, s, W7, 1.f, &u, &vv);
			const BOOL outer = fabsf(th) < 1.35f && th * sgn > 0.f && r > 1.10f && r < 1.65f;
			const BOOL stern = fabsf(th) < 0.14f && p[2] < z0 && fabsf(vv) < 0.11f;
			if (!outer && !stern) continue;
			const double w = stern ? 16.0 : 1.0;
			const double f[6] = { 1., u, vv, (double)u * u, (double)u * vv, (double)vv * vv };
			const double rA = sqrt((double)(p[0] - x0) * (p[0] - x0) + ((double)p[2] - z0) * ((double)p[2] - z0));
			for (int i = 0; i < 6; i++) { for (int j = 0; j < 6; j++) A[i][j] += w * f[i] * f[j]; A[i][6] += w * f[i] * rA; }
			nr++;
		}
		if (nr < 16) continue;
		for (int i = 0; i < 6; i++) {
			int pv = i;
			for (int r = i + 1; r < 6; r++) if (fabs(A[r][i]) > fabs(A[pv][i])) pv = r;
			if (fabs(A[pv][i]) < 1e-24) { nr = 0; break; }
			if (pv != i) for (int c = 0; c < 7; c++) { const double t = A[i][c]; A[i][c] = A[pv][c]; A[pv][c] = t; }
			for (int r = 0; r < 6; r++) if (r != i) {
				const double f = A[r][i] / A[i][i];
				for (int c = i; c < 7; c++) A[r][c] -= f * A[i][c];
			}
		}
		if (!nr) continue;
		cfOk[s] = TRUE;
		for (int i = 0; i < 6; i++) cfS[s][i] = A[i][6] / A[i][i];
	}
	if (cfOk[0] || cfOk[1]) {
		const float rhM = 0.5f * (rhN[0] + rhN[1]);
		for (int pass = 0; pass < 2; pass++) for (int k = 0; k < nb; k++) {
			if (tw[k] <= 0.f) continue;
			const float* p = &S[k * 3];
			const float* q = pass ? &Wl[k * 3] : p;
			const float th = atan2f(q[0] - x0, z0 - q[2]), y = p[1];
			float u0, v0, u1, v1;
			const float r0 = rad(th, y, 0, W7, 1.f, &u0, &v0);
			const float r1 = rad(th, y, 1, W7, 1.f, &u1, &v1);
			const float r = fminf(r0, r1);
			const float wf = Smooth01((1.35f - r) / .35f) * tw[k];
			if (wf <= 0.f) continue;
			const float w1 = cfOk[0] && cfOk[1] ? Smooth01((th * rhM + 0.018f) / 0.036f) : (cfOk[1] ? 1.f : 0.f);
			const double* c0 = cfS[0]; const double* c1 = cfS[1];
			const float rw0 = (float)(c0[0] + c0[1] * u0 + c0[2] * v0 + c0[3] * u0 * u0 + c0[4] * u0 * v0 + c0[5] * v0 * v0);
			const float rw1 = (float)(c1[0] + c1[1] * u1 + c1[2] * v1 + c1[3] * u1 * u1 + c1[4] * u1 * v1 + c1[5] * v1 * v1);
			const float rW = rw0 + (rw1 - rw0) * w1;
			if (!(rW > 0.04f) || rW > 0.28f) continue;
			const float tg[3] = { x0 + rW * sinf(th), y, z0 - rW * cosf(th) };
			for (int a = 0; a < 3; a++) Wl[k * 3 + a] = p[a] + wf * (tg[a] - p[a]);
		}
	}
	for (int k = 0; k < nb; k++) for (int a = 0; a < 3; a++) Wl[k * 3 + a] = Wl[root[k] * 3 + a];
	// 山。平らな胸板 Wl の上に、カップに応じた幅 W の広いドームと、中心から外周への放射広がりを載せる。
	const float W = kBustW[cup], PP = kBustP[cup] * W * 0.72f, sag = kBustSag[cup];
	const float edc = .20f + .25f * (1.f - (float)cup / 7.f);
	const float spread = Clampf((W - kBustW[0]) / (kBustW[BUST_N - 1] - kBustW[0]), 0.f, 1.f);
	auto mound = [&](float th, float y, float t, float* d) {
		d[0] = d[1] = d[2] = 0.f;
		if (t <= 0.f) return;
		for (int s = 0; s < 2; s++) {
			const float sgn = s ? 1.f : -1.f;
			if (th * sgn <= 0.f) continue;
			const float u = (th - thN[s]) * sgn * rhN[s], vv = y - yN[s];
			// 内側の裾は胸骨の手前（th=0 の 4mm 外）まで広げ、反対側へは出さない。狭い二つの円錐にならず、中央に自然な谷間ができる。
			const float aIn = fminf(.58f * W, fmaxf(fabsf(thN[s]) * rhN[s] - 0.004f, 0.035f));
			const float a = u > 0.f ? .50f * W : aIn, b = vv > 0.f ? fminf(.56f * W, .095f) : .46f * W;
			const float ru = u / a, rv = vv / b;
			const float r = sqrtf(ru * ru + rv * rv);
			if (r >= 1.f) continue;
			const float tt = Smooth01(vv / b * 1.4f + .5f);
			float down = Clampf(-vv / b, 0.f, 1.f);
			down = fminf(down * down / fmaxf(r * r, 1e-9f), 1.f);
			const float ed = .56f + (edc - .56f) * down;
			// 先端を平らに丸めたドーム（1 - r^2 の 0.55 乗）。中心の曲率を緩やかにして尖りを消す。
			const float fu = powf(1.f - r * r, 0.55f) * Smooth01((1.f - r) / .55f);
			const float fd = (1.f - powf(r, 2.6f)) * Smooth01((1.f - r) / ed);
			float h = PP * (fd + (fu - fd) * tt);
			// 胸骨のすぐ横（3mm → 19mm）で滑らかに 0 へ落とし、はっきりした谷間にする。
			const float cleav = Smooth01((fabsf(th) * rhN[s] - 0.003f) / 0.016f);
			h *= t * cleav;
			if (h <= 0.f) continue;
			// 素体のメッシュ自体を乳房の中心から放射状に広げる。中心近くの頂点が外周へ広がり、広い半球面を作る。
			const float bell = (1.f - r) * (1.f + 1.4f * r) * (1.f - r) * t * cleav;
			const float expU = ru * (u > 0.f ? 0.36f : 0.24f) * (PP + 0.016f * spread) * bell;
			const float expV = rv * (vv < 0.f ? 0.42f : 0.26f) * (PP + 0.016f * spread) * bell;
			const float outer = u > 0.f ? Smooth01(u / fmaxf(0.50f * W, 1e-4f)) : 0.f;
			const float td = th * (0.12f + 0.16f * outer);
			d[0] = sinf(td) * h + sgn * expU;
			d[1] = expV - sag * h * (0.90f + 0.50f * Smooth01(-vv / b));
			d[2] = -cosf(td) * h;
		}
	};
	for (int k = 0; k < nb; k++) {
		const float* p = &Wl[k * 3];
		mound(atan2f(p[0] - x0, z0 - p[2]), p[1], tw[k], &D[k * 3]);
		for (int a = 0; a < 3; a++) P[k * 3 + a] = Wl[k * 3 + a] + D[k * 3 + a];
	}
	// 肋骨の横を少し外へ。胸の下〜脇の下。正面の谷は触らない。
	{
		const float yRib = 0.5f * (yN[0] + yN[1]);
		for (int k = 0; k < nb; k++) {
			if (arm[k] || tw[k] <= 0.f) continue;
			const float* p = &S[k * 3];
			const float th = atan2f(p[0] - x0, z0 - p[2]), y = p[1], ath = fabsf(th);
			if (ath < 0.50f || ath > 1.20f || y > yRib + 0.015f || y < yRib - 0.13f) continue;
			const float g = Smooth01((ath - 0.50f) / 0.18f) * Smooth01((1.20f - ath) / 0.18f)
				* Smooth01((yRib + 0.015f - y) / 0.03f) * Smooth01((y - (yRib - 0.13f)) / 0.03f) * tw[k];
			if (g <= 0.f) continue;
			const float out = 0.020f * g, sx = P[k * 3] >= x0 ? 1.f : -1.f;
			D[k * 3] += sx * out; D[k * 3 + 2] -= 0.004f * g;
			P[k * 3] += sx * out; P[k * 3 + 2] -= 0.004f * g;
		}
	}
	if (ne > nb) {
		// 服の dmin は胸の上半分だけ引かれていて、乳房の下（y=1.26〜1.29）に元の膨らみの段が残る。
		// 体の S との差（P - S）を足すとその段がそのまま残り、胸の中央に横の折れ目が出る。
		// 服も外の輪の厚みを保った滑らかな胸壁へ一度戻し、その上から同じ滑らかな胸壁の上で揃えた山 D を載せる。
		float drC[2] = { 0.0065f, 0.0065f };
		for (int s = 0; s < 2; s++) if (cfOk[s]) {
			const float sgn = s ? 1.f : -1.f;
			const double* cf = cfS[s];
			double sum = 0., wsum = 0.;
			for (int k = nb; k < ne; k++) {
				int m = 0;
				for (int c = 1; c < 4; c++) if (v[k].w[c] > v[k].w[m]) m = c;
				if (armJ[v[k].j[m]]) continue;
				const float* p = &P[k * 3];
				if (p[2] > z0) continue;
				const float th = atan2f(p[0] - x0, z0 - p[2]), y = p[1];
				float u, vv;
				const float r = rad(th, y, s, W7, 1.f, &u, &vv);
				if (!(fabsf(th) < 1.25f && th * sgn > 0.f && r > 1.08f && r < 1.50f)) continue;
				const float rW = (float)(cf[0] + cf[1] * u + cf[2] * vv + cf[3] * u * u + cf[4] * u * vv + cf[5] * vv * vv);
				const float rC = sqrtf((p[0] - x0) * (p[0] - x0) + (p[2] - z0) * (p[2] - z0));
				const float d = rC - rW;
				if (d > 0.001f && d < 0.025f) { sum += d; wsum += 1.; }
			}
			if (wsum >= 8.) drC[s] = Clampf((float)(sum / wsum), 0.0060f, 0.013f);
		}
		const float TR = 0.020f, inv2 = 1.f / (2.f * TR * TR), cut = 20.f * TR * TR;
		const float yMid = 0.5f * (yN[0] + yN[1]);
		const float dzPeak = 0.5f * (D[br.kn[0] * 3 + 2] + D[br.kn[1] * 3 + 2]);
		const float dyPeak = 0.5f * (D[br.kn[0] * 3 + 1] + D[br.kn[1] * 3 + 1]);
		float cMin = 1e9f;
		for (int k = nb; k < ne; k++) if (P[k * 3 + 1] < cMin) cMin = P[k * 3 + 1];
		float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
		cand.clear();
		for (int k = 0; k < nb; k++) {
			if (!in[k] || Wl[k * 3 + 2] > z0 + 0.02f) continue;
			cand.push_back(k);
			for (int a = 0; a < 3; a++) { lo[a] = fminf(lo[a], Wl[k * 3 + a]); hi[a] = fmaxf(hi[a], Wl[k * 3 + a]); }
		}
		for (int a = 0; a < 3; a++) { lo[a] -= 3.f * TR; hi[a] += 3.f * TR; }
		NearGrid clothG;
		clothG.build(Wl.data(), 3, cand.data(), cand.size(), sqrtf(cut), cut);
		for (int k = nb; k < ne; k++) {
			float* c = &P[k * 3];
			if (!(c[0] > lo[0] && c[0] < hi[0] && c[1] > lo[1] && c[1] < hi[1] && c[2] > lo[2] && c[2] < hi[2])) continue;
			int m = 0;
			for (int j = 1; j < 4; j++) if (v[k].w[j] > v[k].w[m]) m = j;
			const float wArmC = armJ[v[k].j[m]] == 1 ? 0.f : 1.f;
			const float th0 = atan2f(c[0] - x0, z0 - c[2]);
			const float fade = Smooth01((1.32f - fabsf(th0)) / 0.28f) * Smooth01((z0 + 0.012f - c[2]) / 0.024f) * wArmC;
			if (fade <= 0.f) continue;
			const float pOrig[3] = { c[0], c[1], c[2] };
			if (cfOk[0] || cfOk[1]) {
				const float rhM = 0.5f * (rhN[0] + rhN[1]);
				for (int pass = 0; pass < 2; pass++) {
					const float th = atan2f(c[0] - x0, z0 - c[2]), y = pOrig[1];
					float u0, v0, u1, v1;
					const float r0 = rad(th, y, 0, W7, 1.f, &u0, &v0);
					const float r1 = rad(th, y, 1, W7, 1.f, &u1, &v1);
					const float r = fminf(r0, r1);
					const float wf = Smooth01((1.35f - r) / .35f) * fade;
					if (wf <= 0.f) continue;
					const float w1 = cfOk[0] && cfOk[1] ? Smooth01((th * rhM + 0.018f) / 0.036f) : (cfOk[1] ? 1.f : 0.f);
					const double* cf0 = cfS[0]; const double* cf1 = cfS[1];
					const float rw0 = (float)(cf0[0] + cf0[1] * u0 + cf0[2] * v0 + cf0[3] * u0 * u0 + cf0[4] * u0 * v0 + cf0[5] * v0 * v0) + drC[0];
					const float rw1 = (float)(cf1[0] + cf1[1] * u1 + cf1[2] * v1 + cf1[3] * u1 * u1 + cf1[4] * u1 * v1 + cf1[5] * v1 * v1) + drC[1];
					const float rW = rw0 + (rw1 - rw0) * w1;
					if (!(rW > 0.04f) || rW > 0.29f) continue;
					const float tg[3] = { x0 + rW * sinf(th), y, z0 - rW * cosf(th) };
					for (int a = 0; a < 3; a++) c[a] = pOrig[a] + wf * (tg[a] - pOrig[a]);
				}
			}
			const float sx = c[0] - x0;
			float sw = 0.f, d3[3] = {};
			auto take = [&](int q) {
				if (sx * (Wl[q * 3] - x0) < 0.f && fabsf(sx) > 0.012f) return;
				const float dx = Wl[q * 3] - c[0], dy = Wl[q * 3 + 1] - c[1], dz = Wl[q * 3 + 2] - c[2];
				const float d2 = dx * dx + dy * dy + dz * dz;
				if (d2 > cut) return;
				const float g = expf(-d2 * inv2);
				sw += g;
				for (int a = 0; a < 3; a++) d3[a] += g * D[q * 3 + a];
			};
			if (clothG.ok) {
				clothG.gather(c);
				if (gNearHit.size() > 1) qsort(gNearHit.data(), gNearHit.size(), sizeof(int), CmpAscI);
				for (int i = 0; i < gNearHit.size(); i++) take(gNearHit[i]);
			} else for (int i = 0; i < cand.size(); i++) take(cand[i]);
			if (sw < 1e-6f) continue;
			const float inv = fade / sw;
			for (int a = 0; a < 3; a++) d3[a] *= inv;
			// 水着は胸から股で終わる（yMin > 0.70）ので谷間を見せる。シャツやドレスは谷の布も前へ渡す。
			const float wMid = (cMin < 0.65f ? 1.f : 0.f) * (1.f - Smooth01(fabsf(sx) / 0.048f))
				* Smooth01((c[1] - (yMid - 0.44f * W)) / 0.035f) * Smooth01((yMid + 0.055f - c[1]) / 0.035f) * fade;
			if (wMid > 0.f && dzPeak < 0.f) {
				const float zSpan = dzPeak * 0.58f * wMid;
				if (zSpan < d3[2]) {
					d3[2] = zSpan;
					d3[1] = fminf(d3[1], dyPeak * 0.50f * wMid);
				}
			}
			for (int a = 0; a < 3; a++) {
				D[k * 3 + a] = d3[a];
				P[k * 3 + a] = c[a] + d3[a];
			}
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
struct SoftGpuN { float x[4]; float p[4]; unsigned nb[4]; float q[4]; };
struct SoftGpuV { unsigned a[4]; float w[4]; float p[4]; };
static_assert(sizeof(SoftGpuN) == 64 && sizeof(SoftGpuV) == 48, "SoftGpu");

// 脚の形の半径（PT 段 × PF 分割）を t（骨に沿った割合。区画と同じ (t+0.08)/1.16）と φ で引く。
static float ClothProf(const float* P, float t, float phi)
{
	enum { PT = Renderer::CLOTH_PT, PF = Renderer::CLOTH_PF };
	const float ft = Clampf((t + 0.08f) / 1.16f, 0.f, 1.f) * (float)(PT - 1);
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

static int SegJoint(const int* segJ, const char (*segName)[24], int ns, const char* name)
{
	for (int s = 0; s < ns; s++) if (!strcmp(segName[s], name)) return segJ[s];
	return -1;
}

// ゆるい胸と袖だけを格子にする。体から 1.2cm 以内の布はタイトなので載せない。
static void BuildSoft(Renderer::AvaCache& nc, int nb, int n, Renderer::CSkinV* V, const float* moved, const int* root,
	const BYTE* isSk, const int* segJ, const char (*segName)[24], int ns, const Model& md, float* jig,
	Buf<SoftGpuN>& nodes, Buf<SoftGpuV>& verts)
{
	nodes.clear();
	verts.clear();
	nc.softN = nc.softVN = 0;
	for (int i = 0; i < 6; i++) nc.softBone[i] = -1;
	for (int i = 0; i < 4; i++) { nc.armJ[i][0] = nc.armJ[i][1] = -1; nc.armA[i][3] = 0.f; nc.armB[i][3] = -1.f; }
	if (!V || n < 8 || nb <= 0) return;
	auto joint = [&](const char* name) { return SegJoint(segJ, segName, ns, name); };
	auto bindPos = [&](int j, float* o) -> BOOL {
		if (j < 0 || j >= md.nJ) return FALSE;
		M4 b = MInverse(md.invBind[j]);
		o[0] = b.m[12]; o[1] = b.m[13]; o[2] = b.m[14];
		if (j < 64) { o[0] += nc.jOff[j][0]; o[1] += nc.jOff[j][1]; o[2] += nc.jOff[j][2]; }
		return TRUE;
	};
	const int jChest = joint("Chest") >= 0 ? joint("Chest") : joint("Torso");
	const int jUA[2] = { joint("UpperArm.L"), joint("UpperArm.R") };
	const int jLA[2] = { joint("LowerArm.L"), joint("LowerArm.R") };
	const int jHand[2] = { joint("Hand.L"), joint("Hand.R") };
	nc.softBone[0] = jChest;
	nc.softBone[1] = jUA[0];
	nc.softBone[2] = jLA[0] >= 0 ? jLA[0] : jUA[0];
	nc.softBone[3] = jUA[1];
	nc.softBone[4] = jLA[1] >= 0 ? jLA[1] : jUA[1];
	// 腕の太さは端ごとに測る（t0〜t1 の帯）。区間全体の最大だと、肩の太さで肘や手首の袖まで押し広げてこぶになる。
	auto armRadius = [&](int ja, int jb, const float* A, const float* B, float t0, float t1) {
		V3 ax = v3(B[0] - A[0], B[1] - A[1], B[2] - A[2]);
		const float L = Len(ax);
		if (L < 0.05f) return 0.042f;
		ax = ax * (1.f / L);
		float r = 0.f;
		int got = 0;
		for (int k = 0; k < nb; k++) {
			const int d = DomJoint(V[k]);
			if (d != ja && d != jb) continue;
			int wl = 0;
			for (int c = 0; c < 4; c++) if ((int)V[k].j[c] == ja || (int)V[k].j[c] == jb) wl += V[k].w[c];
			if (wl < 180) continue;
			const V3 q = v3(V[k].p[0] - A[0], V[k].p[1] - A[1], V[k].p[2] - A[2]);
			const float t = Clampf(Dot(q, ax) / L, 0.f, 1.f);
			if (t < t0 || t > t1) continue;
			const float dist = Len(q - ax * (t * L));
			if (dist > 0.12f) continue;
			if (dist > r) r = dist;
			got++;
		}
		return got >= 4 ? Clampf(r + 0.008f, 0.030f, 0.080f) : 0.042f;
	};
	for (int side = 0; side < 2; side++) {
		float A[3], B[3], H[3];
		if (jUA[side] >= 0 && jLA[side] >= 0 && bindPos(jUA[side], A) && bindPos(jLA[side], B)) {
			const int c = side * 2;
			nc.armJ[c][0] = jUA[side]; nc.armJ[c][1] = jLA[side];
			nc.armA[c][0] = A[0]; nc.armA[c][1] = A[1]; nc.armA[c][2] = A[2]; nc.armA[c][3] = armRadius(jUA[side], jLA[side], A, B, 0.f, 0.35f);
			nc.armB[c][0] = B[0]; nc.armB[c][1] = B[1]; nc.armB[c][2] = B[2]; nc.armB[c][3] = armRadius(jUA[side], jLA[side], A, B, 0.65f, 1.f);
		}
		if (jLA[side] >= 0 && jHand[side] >= 0 && bindPos(jLA[side], B) && bindPos(jHand[side], H)) {
			const int c = side * 2 + 1;
			nc.armJ[c][0] = jLA[side]; nc.armJ[c][1] = jHand[side];
			nc.armA[c][0] = B[0]; nc.armA[c][1] = B[1]; nc.armA[c][2] = B[2]; nc.armA[c][3] = armRadius(jLA[side], jHand[side], B, H, 0.f, 0.35f);
			nc.armB[c][0] = H[0]; nc.armB[c][1] = H[1]; nc.armB[c][2] = H[2]; nc.armB[c][3] = armRadius(jLA[side], jHand[side], B, H, 0.65f, 1.f);
		}
	}
	const int GX = 8, GY = 5, GN = GX * GY;
	int chestBase = -1;
	if (jig && jChest >= 0 && moved && root) {
		float hipY = 0.9f, cx = 0.f;
		float pL[3], pR[3];
		const int ul0 = joint("UpperLeg.L"), ul1 = joint("UpperLeg.R");
		if (ul0 >= 0 && ul1 >= 0 && bindPos(ul0, pL) && bindPos(ul1, pR)) {
			hipY = (pL[1] + pR[1]) * 0.5f;
			cx = (pL[0] + pR[0]) * 0.5f;
		}
		float apex[2][3] = {}, apexJ[2][3] = {};
		int ok[2] = { 0, 0 };
		for (int k = 0; k < nb; k++) {
			const float* J = &jig[(size_t)k * 3];
			if (J[0] * J[0] + J[1] * J[1] + J[2] * J[2] < 0.004f * 0.004f) continue;
			const float* p = V[k].p;
			if (p[1] < hipY + 0.15f) continue;
			const int s = p[0] < cx ? 0 : 1;
			if (!ok[s] || p[2] < apex[s][2]) {
				for (int a = 0; a < 3; a++) { apex[s][a] = p[a]; apexJ[s][a] = J[a]; }
				ok[s] = 1;
			}
		}
		int looseN = 0;
		float yBot = 1e9f, x0 = 1e9f, x1 = -1e9f;
		for (int i = 0; i < n; i++) {
			if (isSk && isSk[i]) continue;
			const float* p = V[nb + i].p;
			if (!moved || moved[root[i]] < 0.028f) continue;
			looseN++;
			if (p[1] < yBot) yBot = p[1];
			if (p[0] < x0) x0 = p[0];
			if (p[0] > x1) x1 = p[0];
		}
		if (ok[0] && ok[1]) {
			for (int s = 0; s < 2; s++) {
				const float jl = sqrtf(apexJ[s][0] * apexJ[s][0] + apexJ[s][1] * apexJ[s][1] + apexJ[s][2] * apexJ[s][2]);
				nc.bustA[s][0] = apex[s][0]; nc.bustA[s][1] = apex[s][1]; nc.bustA[s][2] = apex[s][2];
				nc.bustA[s][3] = Clampf(0.038f + jl * 0.45f, 0.04f, 0.085f);
			}
		}
		// 1/2 系は格子に載せると面が折れるので、胸下は CPU の垂らしだけ。当たりはあとで頂点 CS。
		if (0 && ok[0] && ok[1] && looseN >= 20 && x1 > x0 + 0.04f) {
			float yTop = (apex[0][1] + apex[1][1]) * 0.5f + 0.015f;
			if (yBot > yTop - 0.06f) yBot = yTop - 0.08f;
			x0 -= 0.02f; x1 += 0.02f;
			Buf<float> acc, wgt;
			acc.arena = wgt.arena = 1;
			acc.fill(GN * 3, 0.f); wgt.fill(GN, 0.f);
			if (acc.size() == GN * 3 && wgt.size() == GN) {
				for (int i = 0; i < n; i++) {
					if (isSk && isSk[i]) continue;
					const float* p = V[nb + i].p;
					if (!moved || moved[root[i]] < 0.028f) continue;
					float u = Clampf((p[0] - x0) / (x1 - x0), 0.f, 1.f) * (float)(GX - 1);
					float v = Clampf((yTop - p[1]) / (yTop - yBot), 0.f, 1.f) * (float)(GY - 1);
					int u0 = (int)u; if (u0 > GX - 2) u0 = GX - 2;
					int v0 = (int)v; if (v0 > GY - 2) v0 = GY - 2;
					float fu = u - (float)u0, fv = v - (float)v0;
					const int id[4] = { v0 * GX + u0, v0 * GX + u0 + 1, (v0 + 1) * GX + u0, (v0 + 1) * GX + u0 + 1 };
					const float w[4] = { (1 - fu) * (1 - fv), fu * (1 - fv), (1 - fu) * fv, fu * fv };
					for (int e = 0; e < 4; e++) if (w[e] > 0.f) {
						wgt[id[e]] += w[e];
						for (int a = 0; a < 3; a++) acc[id[e] * 3 + a] += p[a] * w[e];
					}
				}
				for (int pass = 0; pass < 4; pass++) for (int q = 0; q < GN; q++) if (wgt[q] < 1e-4f) {
					const int u = q % GX, v = q / GX;
					const int nbh[4] = { u > 0 ? q - 1 : -1, u < GX - 1 ? q + 1 : -1, v > 0 ? q - GX : -1, v < GY - 1 ? q + GX : -1 };
					for (int e = 0; e < 4; e++) if (nbh[e] >= 0 && wgt[nbh[e]] > 1e-4f) {
						wgt[q] = wgt[nbh[e]];
						for (int a = 0; a < 3; a++) acc[q * 3 + a] = acc[nbh[e] * 3 + a];
						break;
					}
				}
				chestBase = nodes.size();
				int chestOk = 1;
				for (int q = 0; q < GN && nodes.size() < Renderer::SOFT_MAX; q++) {
					if (wgt[q] < 1e-4f) { chestOk = 0; break; }
					SoftGpuN o;
					memset(&o, 0, sizeof(o));
					const int gy = q / GX;
					const float t = (float)gy / (float)(GY - 1);
					o.x[0] = acc[q * 3] / wgt[q]; o.x[1] = acc[q * 3 + 1] / wgt[q]; o.x[2] = acc[q * 3 + 2] / wgt[q];
					o.x[3] = gy == 0 ? 1.f : 0.f;
					o.p[0] = 0.02f + 0.045f * t;
					o.p[1] = 0.11f - 0.07f * t;
					o.p[2] = 0.30f + 0.70f * t;
					o.p[3] = 0.f;
					o.q[0] = 0.026f;
					o.q[1] = 0.f;
					const int u = q % GX, v = gy;
					const int nbh[4] = { v > 0 ? q - GX : -1, v < GY - 1 ? q + GX : -1, u > 0 ? q - 1 : -1, u < GX - 1 ? q + 1 : -1 };
					for (int e = 0; e < 4; e++) o.nb[e] = nbh[e] >= 0 ? (unsigned)(chestBase + nbh[e]) : 0xFFFFFFFFu;
					nodes.push_back(o);
				}
				if (!chestOk) { nodes.resize(chestBase); chestBase = -1; }
				if (chestBase >= 0 && nodes.size() >= chestBase + GN) {
					for (int i = 0; i < n; i++) {
						if (isSk && isSk[i]) continue;
						const float* p = V[nb + i].p;
						if (!moved || moved[root[i]] < 0.028f) continue;
						float u = Clampf((p[0] - x0) / (x1 - x0), 0.f, 1.f) * (float)(GX - 1);
						float v = Clampf((yTop - p[1]) / (yTop - yBot), 0.f, 1.f) * (float)(GY - 1);
						int u0 = (int)u; if (u0 > GX - 2) u0 = GX - 2;
						int v0 = (int)v; if (v0 > GY - 2) v0 = GY - 2;
						float fu = u - (float)u0, fv = v - (float)v0;
						SoftGpuV o;
						memset(&o, 0, sizeof(o));
						o.a[0] = (unsigned)(nb + i);
						const int id[4] = { chestBase + v0 * GX + u0, chestBase + v0 * GX + u0 + 1, chestBase + (v0 + 1) * GX + u0, chestBase + (v0 + 1) * GX + u0 + 1 };
						o.a[1] = (unsigned)id[0] | ((unsigned)id[1] << 16);
						o.a[2] = (unsigned)id[2] | ((unsigned)id[3] << 16);
						o.a[3] = 0;
						o.w[0] = (1 - fu) * (1 - fv); o.w[1] = fu * (1 - fv); o.w[2] = (1 - fu) * fv; o.w[3] = fu * fv;
						o.p[0] = p[0]; o.p[1] = p[1]; o.p[2] = p[2];
						o.p[3] = Clampf(moved[root[i]] / 0.02f, 0.35f, 1.f);
						verts.push_back(o);
						if (jig && o.p[3] > 0.65f) memset(&jig[(size_t)(nb + i) * 3], 0, 12);
					}
				} else chestBase = -1;
			}
		}
	}
	for (int side = 0; side < 2 && nodes.size() + 8 <= Renderer::SOFT_MAX; side++) {
		float A[3], H[3];
		if (jUA[side] < 0 || jHand[side] < 0 || !bindPos(jUA[side], A) || !bindPos(jHand[side], H)) continue;
		const int armJ[3] = { jUA[side], jLA[side] >= 0 ? jLA[side] : jUA[side], jHand[side] };
		Buf<int> ids;
		Buf<float> dist;
		ids.arena = dist.arena = 1;
		V3 ax = v3(H[0] - A[0], H[1] - A[1], H[2] - A[2]);
		const float L = Len(ax);
		if (L < 0.08f) continue;
		ax = ax * (1.f / L);
		for (int i = 0; i < n; i++) {
			if (isSk && isSk[i]) continue;
			const Renderer::CSkinV& s = V[nb + i];
			const int d = DomJoint(s);
			if (d != armJ[0] && d != armJ[1] && d != armJ[2]) continue;
			int wl = 0;
			for (int c = 0; c < 4; c++) if ((int)s.j[c] == d) wl += s.w[c];
			if (wl < 180) continue;
			const V3 q = v3(s.p[0] - A[0], s.p[1] - A[1], s.p[2] - A[2]);
			const float t = Dot(q, ax) / L;
			if (t < -0.05f || t > 1.15f) continue;
			const float dd = Len(q - ax * (Clampf(t, 0.f, 1.f) * L));
			if (dd > 0.16f) continue;
			ids.push_back(i);
			dist.push_back(dd);
		}
		if (ids.size() < 24) continue;
		Buf<float> sorted;
		sorted.arena = 1;
		sorted.resize(dist.size());
		if (sorted.size() < dist.size()) continue;
		memcpy(sorted.data(), dist.data(), sizeof(float) * dist.size());
		qsort(sorted.data(), sorted.size(), sizeof(float), CmpFloat);
		const float p70 = sorted[sorted.size() * 7 / 10];
		if (p70 < 0.070f) continue;
		const int base = nodes.size();
		const int SN = 8;
		float sum[8][3];
		float sw[8];
		for (int k = 0; k < SN; k++) { sw[k] = 0.f; sum[k][0] = sum[k][1] = sum[k][2] = 0.f; }
		for (int q = 0; q < ids.size(); q++) {
			const float* p = V[nb + ids[q]].p;
			const float t = Clampf(Dot(v3(p[0] - A[0], p[1] - A[1], p[2] - A[2]), ax) / L, 0.f, 1.f);
			const int b = Clampi((int)(t * (float)(SN - 1) + 0.5f), 0, SN - 1);
			sw[b] += 1.f;
			for (int a = 0; a < 3; a++) sum[b][a] += p[a];
		}
		for (int k = 0; k < SN; k++) if (sw[k] < 1.f) {
			const float t = (float)k / (float)(SN - 1);
			sum[k][0] = A[0] + (H[0] - A[0]) * t;
			sum[k][1] = A[1] + (H[1] - A[1]) * t;
			sum[k][2] = A[2] + (H[2] - A[2]) * t;
			sw[k] = 1.f;
		}
		const int boneTop = side == 0 ? 1 : 3;
		const int boneLow = side == 0 ? 2 : 4;
		for (int k = 0; k < SN; k++) {
			SoftGpuN o;
			memset(&o, 0, sizeof(o));
			const float t = (float)k / (float)(SN - 1);
			o.x[0] = sum[k][0] / sw[k]; o.x[1] = sum[k][1] / sw[k]; o.x[2] = sum[k][2] / sw[k];
			o.x[3] = k == 0 ? 1.f : 0.f;
			o.p[0] = 0.015f + 0.055f * t;
			o.p[1] = 0.14f - 0.09f * t;
			o.p[2] = 0.25f + 0.75f * t;
			o.p[3] = 1.f;
			o.q[1] = (float)(t < 0.45f ? boneTop : boneLow);
			o.q[2] = (float)(side + 1);
			o.nb[0] = k > 0 ? (unsigned)(base + k - 1) : 0xFFFFFFFFu;
			o.nb[1] = k < SN - 1 ? (unsigned)(base + k + 1) : 0xFFFFFFFFu;
			o.nb[2] = o.nb[3] = 0xFFFFFFFFu;
			nodes.push_back(o);
		}
		for (int q = 0; q < ids.size(); q++) {
			const int i = ids[q];
			const float* p = V[nb + i].p;
			const float t = Clampf(Dot(v3(p[0] - A[0], p[1] - A[1], p[2] - A[2]), ax) / L, 0.f, 1.f);
			const float f = t * (float)(SN - 1);
			int i0 = (int)f; if (i0 > SN - 2) i0 = SN - 2;
			const float fr = f - (float)i0;
			SoftGpuV o;
			memset(&o, 0, sizeof(o));
			o.a[0] = (unsigned)(nb + i);
			o.a[1] = (unsigned)(base + i0) | ((unsigned)(base + i0 + 1) << 16);
			o.a[2] = o.a[1];
			o.a[3] = (unsigned)(t < 0.45f ? boneTop : boneLow);
			o.w[0] = 1.f - fr; o.w[1] = fr;
			o.p[0] = p[0]; o.p[1] = p[1]; o.p[2] = p[2];
			o.p[3] = Smooth01((dist[q] - 0.012f) / 0.02f);
			if (o.p[3] < 0.08f) continue;
			verts.push_back(o);
		}
	}
	if (nodes.size() >= 4 && verts.size() >= 8) {
		nc.softN = nodes.size();
		nc.softVN = verts.size();
	} else {
		nodes.clear();
		verts.clear();
		nc.softN = nc.softVN = 0;
	}
}

struct FlutOrd { int n; int c; int k; int kind; };
static int CmpFlutOrd(const void* a, const void* b)
{
	const FlutOrd* A = (const FlutOrd*)a;
	const FlutOrd* B = (const FlutOrd*)b;
	if (A->kind != B->kind) return A->kind - B->kind;
	return A->n - B->n;
}

// リボン・衿・エプロン・上着の裾・ワンピースの上半身。スカート格子と袖の点は触らない。
// 付け根は固定し、先だけ風で遅れる。胸の格子は使わない（面が折れる）。
static void BuildFlutter(Renderer::AvaCache& nc, int nb, int n, Renderer::CSkinV* V, const int* root, Buf<int>& par, const BYTE* isSk, const BYTE* tight,
	const int* segJ, const char (*segName)[24], int ns, const Model& md, float hipY, float cx,
	Buf<SoftGpuN>& nodes, Buf<SoftGpuV>& verts)
{
	if (!V || !root || n < 16 || nb <= 0 || par.size() < n) return;
	auto joint = [&](const char* name) { return SegJoint(segJ, segName, ns, name); };
	auto bindPos = [&](int j, float* o) -> BOOL {
		if (j < 0 || j >= md.nJ) return FALSE;
		M4 b = MInverse(md.invBind[j]);
		o[0] = b.m[12]; o[1] = b.m[13]; o[2] = b.m[14];
		if (j < 64) { o[0] += nc.jOff[j][0]; o[1] += nc.jOff[j][1]; o[2] += nc.jOff[j][2]; }
		return TRUE;
	};
	const int jChest = joint("Chest") >= 0 ? joint("Chest") : joint("Torso");
	if (jChest < 0) return;
	if (nc.softBone[0] < 0) nc.softBone[0] = jChest;
	const int jHead = joint("Head");
	const int jUL[2] = { joint("UpperLeg.L"), joint("UpperLeg.R") };
	const int jLL[2] = { joint("LowerLeg.L"), joint("LowerLeg.R") };
	const int jFt[2] = { joint("Foot.L"), joint("Foot.R") };
	const int jUA[2] = { joint("UpperArm.L"), joint("UpperArm.R") };
	const int jLA[2] = { joint("LowerArm.L"), joint("LowerArm.R") };
	const int jHand[2] = { joint("Hand.L"), joint("Hand.R") };
	const int jSh[2] = { joint("Shoulder.L"), joint("Shoulder.R") };
	const int armJ[8] = { jUA[0], jUA[1], jLA[0], jLA[1], jHand[0], jHand[1], jSh[0], jSh[1] };
	float pUA[2][3], pLA[2][3], pHd[2][3];
	BOOL hasUp[2] = {}, hasLo[2] = {};
	for (int s = 0; s < 2; s++) {
		hasUp[s] = bindPos(jUA[s], pUA[s]) && bindPos(jLA[s], pLA[s]);
		hasLo[s] = bindPos(jLA[s], pLA[s]) && bindPos(jHand[s], pHd[s]);
	}
	const int legJ[6] = { jUL[0], jUL[1], jLL[0], jLL[1], jFt[0], jFt[1] };
	float chest[3], hip[3] = { cx, hipY, 0.f };
	if (!bindPos(jChest, chest)) return;
	float l0[3], l1[3];
	if (bindPos(jUL[0], l0) && bindPos(jUL[1], l1)) hip[2] = (l0[2] + l1[2]) * 0.5f;
	else hip[2] = chest[2];
	auto wOn = [&](const Renderer::CSkinV& s, const int* js, int nj) -> int {
		int w = 0;
		for (int c = 0; c < 4; c++) {
			const int j = (int)s.j[c];
			for (int k = 0; k < nj; k++) if (js[k] >= 0 && j == js[k]) w += s.w[c];
		}
		return w;
	};
	Buf<BYTE> used;
	used.arena = 1;
	used.fill(n, 0);
	if (used.size() < n) return;
	for (int v = 0; v < verts.size(); v++) {
		const unsigned ix = verts[v].a[0];
		if (ix >= (unsigned)nb && ix < (unsigned)(nb + n)) used[ix - (unsigned)nb] = 1;
	}
	auto cand = [&](int i) -> BOOL {
		if (i < 0 || i >= n || used[i]) return FALSE;
		const Renderer::CSkinV& s = V[nb + i];
		if (tight && tight[i]) return FALSE;
		if (jHead >= 0 && wOn(s, &jHead, 1) >= 200) return FALSE;
		if (wOn(s, armJ, 8) >= 40) return FALSE;
		{
			float ad = 1e9f;
			for (int sd = 0; sd < 2; sd++) {
				const float* A[2] = { pUA[sd], pLA[sd] };
				const float* B[2] = { pLA[sd], pHd[sd] };
				const BOOL ok[2] = { hasUp[sd], hasLo[sd] };
				for (int g = 0; g < 2; g++) {
					if (!ok[g]) continue;
					const float ab[3] = { B[g][0] - A[g][0], B[g][1] - A[g][1], B[g][2] - A[g][2] };
					const float L2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
					if (L2 < 1e-6f) continue;
					float t = ((s.p[0] - A[g][0]) * ab[0] + (s.p[1] - A[g][1]) * ab[1] + (s.p[2] - A[g][2]) * ab[2]) / L2;
					if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
					const float rx = s.p[0] - (A[g][0] + ab[0] * t), ry = s.p[1] - (A[g][1] + ab[1] * t), rz = s.p[2] - (A[g][2] + ab[2] * t);
					const float d2 = rx * rx + ry * ry + rz * rz;
					if (d2 < ad) ad = d2;
				}
			}
			ad = sqrtf(ad);
			const float dx = s.p[0] - cx, dz = s.p[2] - hip[2];
			const float td = sqrtf(dx * dx + dz * dz);
			if (ad < 0.16f && ad + 0.02f < td) return FALSE;
		}
		if (fabsf(s.p[0] - cx) > 0.12f && s.p[1] > hipY - 0.05f) return FALSE;
		if (wOn(s, jFt, 2) >= 80 && s.p[1] < hipY - 0.15f) return FALSE;
		// スカートは格子側。腰の一点でも前後を分けて載せると、プリーツの背中が抜ける。
		if (isSk && isSk[i]) return FALSE;
		return TRUE;
	};
	Buf<int> comp, cmap, cN, cSk, cLeg;
	Buf<float> cX0, cX1, cY0, cY1, cZ0, cZ1;
	comp.arena = cmap.arena = cN.arena = cSk.arena = cLeg.arena = 1;
	cX0.arena = cX1.arena = cY0.arena = cY1.arena = cZ0.arena = cZ1.arena = 1;
	cmap.fill(n, -1);
	if (cmap.size() < n) return;
	// 袖の付いた上着は前後で別々に風へ載せると、面が割れて穴になる。部品ごとスキニングに残す。
	Buf<int> sleeveN;
	sleeveN.arena = 1;
	sleeveN.fill(n, 0);
	if (sleeveN.size() < n) return;
	for (int i = 0; i < n; i++) {
		if (V[nb + i].p[1] < hipY - 0.05f) continue;
		if (wOn(V[nb + i], armJ, 8) < 40) continue;
		const int c = UfFind(par, root[i]);
		if (c >= 0 && c < n) sleeveN[c]++;
	}
	for (int i = 0; i < n; i++) {
		if (!cand(i)) continue;
		const int c = UfFind(par, root[i]);
		if (c < 0 || c >= n) continue;
		if (cmap[c] < 0) {
			cmap[c] = comp.size();
			comp.push_back(c);
			cN.push_back(0);
			cSk.push_back(0);
			cLeg.push_back(0);
			cX0.push_back(1e9f); cX1.push_back(-1e9f);
			cY0.push_back(1e9f); cY1.push_back(-1e9f);
			cZ0.push_back(1e9f); cZ1.push_back(-1e9f);
			if (cZ1.size() != comp.size()) return;
		}
		const int id = cmap[c];
		const float* p = V[nb + i].p;
		cN[id]++;
		if (isSk && isSk[i]) cSk[id]++;
		if (wOn(V[nb + i], legJ, 6) >= 160) cLeg[id]++;
		if (p[0] < cX0[id]) cX0[id] = p[0];
		if (p[0] > cX1[id]) cX1[id] = p[0];
		if (p[1] < cY0[id]) cY0[id] = p[1];
		if (p[1] > cY1[id]) cY1[id] = p[1];
		if (p[2] < cZ0[id]) cZ0[id] = p[2];
		if (p[2] > cZ1[id]) cZ1[id] = p[2];
	}
	Buf<FlutOrd> ord;
	ord.arena = 1;
	for (int id = 0; id < comp.size(); id++) {
		if (cN[id] < 14) continue;
		// 靴も靴下も腰まで届かない。ここを風に載せると、ブーツと足が布と一緒に動く。
		if (cY1[id] < hipY - 0.18f) continue;
		const float dx = cX1[id] - cX0[id], dy = cY1[id] - cY0[id], dz = cZ1[id] - cZ0[id];
		if (dx < 0.04f && dy < 0.04f && dz < 0.04f) continue;
		const float mn = fminf(dx, fminf(dy, dz));
		const float mx = fmaxf(dx, fmaxf(dy, dz));
		int kind = 1;
		if (cSk[id] * 2 > cN[id]) kind = 2;
		else if (cLeg[id] * 2 > cN[id]) kind = 3;
		else if (cN[id] < 240 || (mn < 0.05f && cN[id] < 1500) || (mn < 0.10f && mn < mx * 0.38f && cN[id] < 900)) kind = 0;
		if (kind == 2 && cN[id] < 24) continue;
		if (kind != 2 && cY1[id] < hipY - 0.35f) continue;
		FlutOrd o;
		o.n = cN[id]; o.c = comp[id]; o.k = id; o.kind = kind;
		ord.push_back(o);
	}
	if (ord.size() > 1) qsort(ord.data(), ord.size(), sizeof(FlutOrd), CmpFlutOrd);
	int sheets = 0;
	for (int qi = 0; qi < ord.size(); qi++) {
		const FlutOrd& fo = ord[qi];
		if (fo.kind >= 1 && fo.kind != 3) {
			if (sheets >= 3) continue;
		}
		const int compId = fo.c;
		if (compId >= 0 && compId < n && sleeveN[compId] >= 24) continue;
		auto mine = [&](int i) -> BOOL {
			if (!cand(i)) return FALSE;
			return UfFind(par, root[i]) == compId;
		};
		const float amp = fo.kind == 0 ? 0.062f : (fo.kind == 2 ? 0.020f : (fo.kind == 3 ? 0.018f : 0.036f));
		const float sag = fo.kind == 0 ? 0.016f : 0.f;
		const int beforeN = nodes.size();
		const int beforeV = verts.size();
		auto revert = [&]() {
			nodes.resize(beforeN);
			verts.resize(beforeV);
		};
		if (fo.kind == 0) {
			const int SN = 5;
			if (nodes.size() + SN > Renderer::SOFT_MAX) break;
			int iPin = -1;
			float yMax = -1e9f, best = 1e9f;
			for (int i = 0; i < n; i++) if (mine(i) && V[nb + i].p[1] > yMax) yMax = V[nb + i].p[1];
			for (int i = 0; i < n; i++) if (mine(i)) {
				const float sc = SegDist(V[nb + i].p, chest, hip) + (yMax - V[nb + i].p[1]) * 0.22f;
				if (sc < best) { best = sc; iPin = i; }
			}
			if (iPin < 0) continue;
			int iTip = -1;
			float tipD = 0.f;
			const float* pp = V[nb + iPin].p;
			for (int i = 0; i < n; i++) if (mine(i)) {
				const float* q = V[nb + i].p;
				const float d = (q[0] - pp[0]) * (q[0] - pp[0]) + (q[1] - pp[1]) * (q[1] - pp[1]) + (q[2] - pp[2]) * (q[2] - pp[2]);
				if (d > tipD) { tipD = d; iTip = i; }
			}
			if (iTip < 0 || tipD < 0.05f * 0.05f) continue;
			float axis[3];
			for (int a = 0; a < 3; a++) axis[a] = V[nb + iTip].p[a] - pp[a];
			float L = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
			if (L < 0.05f) continue;
			for (int a = 0; a < 3; a++) axis[a] /= L;
			float sum[5][3], sw[5];
			for (int b = 0; b < SN; b++) { sw[b] = 0.f; sum[b][0] = sum[b][1] = sum[b][2] = 0.f; }
			for (int i = 0; i < n; i++) if (mine(i)) {
				const float* q = V[nb + i].p;
				float t = ((q[0] - pp[0]) * axis[0] + (q[1] - pp[1]) * axis[1] + (q[2] - pp[2]) * axis[2]) / L;
				t = Clampf(t, 0.f, 1.f);
				const int b = Clampi((int)(t * (float)(SN - 1) + 0.5f), 0, SN - 1);
				sw[b] += 1.f;
				for (int a = 0; a < 3; a++) sum[b][a] += q[a];
			}
			for (int b = 0; b < SN; b++) if (sw[b] < 1.f) {
				const float t = (float)b / (float)(SN - 1);
				sum[b][0] = pp[0] + axis[0] * L * t;
				sum[b][1] = pp[1] + axis[1] * L * t;
				sum[b][2] = pp[2] + axis[2] * L * t;
				sw[b] = 1.f;
			}
			const int base = nodes.size();
			for (int b = 0; b < SN; b++) {
				SoftGpuN o;
				memset(&o, 0, sizeof(o));
				const float t = (float)b / (float)(SN - 1);
				o.x[0] = sum[b][0] / sw[b]; o.x[1] = sum[b][1] / sw[b]; o.x[2] = sum[b][2] / sw[b];
				o.x[3] = b == 0 ? 1.f : 0.f;
				o.p[0] = 0.004f + amp * t;
				o.p[1] = 0.14f - 0.09f * t;
				o.p[2] = 0.20f + 0.80f * t;
				o.p[3] = 2.f;
				o.q[0] = sag * t;
				o.q[1] = 0.f;
				o.nb[0] = b > 0 ? (unsigned)(base + b - 1) : 0xFFFFFFFFu;
				o.nb[1] = b < SN - 1 ? (unsigned)(base + b + 1) : 0xFFFFFFFFu;
				o.nb[2] = o.nb[3] = 0xFFFFFFFFu;
				nodes.push_back(o);
			}
			if (nodes.size() != base + SN) { revert(); break; }
			for (int i = 0; i < n; i++) if (mine(i)) {
				const float* q = V[nb + i].p;
				float t = ((q[0] - pp[0]) * axis[0] + (q[1] - pp[1]) * axis[1] + (q[2] - pp[2]) * axis[2]) / L;
				t = Clampf(t, 0.f, 1.f);
				const float mv = Smooth01((t - 0.06f) / 0.20f);
				if (mv < 0.04f) continue;
				const float f = t * (float)(SN - 1);
				int r0 = (int)f; if (r0 > SN - 2) r0 = SN - 2;
				const float fr = f - (float)r0;
				SoftGpuV o;
				memset(&o, 0, sizeof(o));
				o.a[0] = (unsigned)(nb + i);
				o.a[1] = (unsigned)(base + r0) | ((unsigned)(base + r0 + 1) << 16);
				o.a[2] = o.a[1];
				o.w[0] = 1.f - fr; o.w[1] = fr;
				o.p[0] = q[0]; o.p[1] = q[1]; o.p[2] = q[2]; o.p[3] = mv;
				verts.push_back(o);
			}
		} else {
			const int SN = 4;
			const float x0 = cX0[fo.k], x1 = cX1[fo.k], y0 = cY1[fo.k], y1 = cY0[fo.k];
			if (y0 - y1 < 0.06f) continue;
			int cols = (x1 - x0) < 0.10f ? 1 : ((x1 - x0) < 0.18f ? 2 : 3);
			while (cols > 1 && nodes.size() + cols * SN > Renderer::SOFT_MAX) cols--;
			if (nodes.size() + cols * SN > Renderer::SOFT_MAX) break;
			float sum[3][4][3], sw[3][4], colN[3];
			for (int c = 0; c < 3; c++) { colN[c] = 0.f; for (int r = 0; r < 4; r++) { sw[c][r] = 0.f; sum[c][r][0] = sum[c][r][1] = sum[c][r][2] = 0.f; } }
			for (int i = 0; i < n; i++) if (mine(i)) {
				if (V[nb + i].n[2] > 0.45f) continue;
				const float* q = V[nb + i].p;
				const float u = (x1 - x0) > 1e-4f ? Clampf((q[0] - x0) / (x1 - x0), 0.f, 1.f) : 0.5f;
				int col = (int)(u * (float)cols); if (col > cols - 1) col = cols - 1;
				const float t = Clampf((y0 - q[1]) / (y0 - y1), 0.f, 1.f);
				const int row = Clampi((int)(t * (float)(SN - 1) + 0.5f), 0, SN - 1);
				sw[col][row] += 1.f;
				colN[col] += 1.f;
				for (int a = 0; a < 3; a++) sum[col][row][a] += q[a];
			}
			int live[3], nL = 0;
			for (int c = 0; c < cols; c++) if (colN[c] >= 6.f) live[nL++] = c;
			if (nL < 1) continue;
			for (int li = 0; li < nL; li++) {
				const int c = live[li];
				for (int r = 0; r < SN; r++) if (sw[c][r] < 1.f) {
					int src = -1;
					for (int d = 1; d < SN && src < 0; d++) {
						if (r - d >= 0 && sw[c][r - d] >= 1.f) src = r - d;
						else if (r + d < SN && sw[c][r + d] >= 1.f) src = r + d;
					}
					if (src < 0) continue;
					const float inv = 1.f / sw[c][src];
					sum[c][r][0] = sum[c][src][0] * inv;
					sum[c][r][2] = sum[c][src][2] * inv;
					sum[c][r][1] = y0 - (y0 - y1) * ((float)r / (float)(SN - 1));
					sw[c][r] = 1.f;
				}
			}
			int nid[3][4];
			for (int c = 0; c < 3; c++) for (int r = 0; r < 4; r++) nid[c][r] = -1;
			for (int li = 0; li < nL; li++) {
				const int c = live[li];
				for (int r = 0; r < SN; r++) {
					if (sw[c][r] < 1.f) continue;
					SoftGpuN o;
					memset(&o, 0, sizeof(o));
					const float t = (float)r / (float)(SN - 1);
					const float inf = fo.kind == 2 ? sinf(t * 3.14159265f) : t;
					o.x[0] = sum[c][r][0] / sw[c][r]; o.x[1] = sum[c][r][1] / sw[c][r]; o.x[2] = sum[c][r][2] / sw[c][r];
					o.x[3] = r == 0 ? 1.f : 0.f;
					o.p[0] = 0.004f + amp * inf;
					o.p[1] = 0.16f - 0.07f * t;
					o.p[2] = 0.22f + 0.78f * (fo.kind == 2 ? inf : t);
					o.p[3] = 2.f;
					o.q[1] = 0.f;
					o.nb[0] = o.nb[1] = o.nb[2] = o.nb[3] = 0xFFFFFFFFu;
					nid[c][r] = nodes.size();
					nodes.push_back(o);
				}
			}
			if (nodes.size() != beforeN + nL * SN || nodes.size() > Renderer::SOFT_MAX) { revert(); continue; }
			for (int li = 0; li < nL; li++) {
				const int c = live[li];
				const int left = li > 0 ? live[li - 1] : -1;
				const int right = li < nL - 1 ? live[li + 1] : -1;
				for (int r = 0; r < SN; r++) {
					SoftGpuN& o = nodes[nid[c][r]];
					if (r > 0) o.nb[0] = (unsigned)nid[c][r - 1];
					if (r < SN - 1) o.nb[1] = (unsigned)nid[c][r + 1];
					if (left >= 0) o.nb[2] = (unsigned)nid[left][r];
					if (right >= 0) o.nb[3] = (unsigned)nid[right][r];
				}
			}
			float cen[3];
			for (int li = 0; li < nL; li++) cen[li] = (x1 - x0) < 1e-4f ? 0.5f : ((float)live[li] + 0.5f) / (float)cols;
			for (int i = 0; i < n; i++) if (mine(i)) {
				if (V[nb + i].n[2] > 0.45f) continue;
				const float* q = V[nb + i].p;
				const float t = Clampf((y0 - q[1]) / (y0 - y1), 0.f, 1.f);
				const float mv = fo.kind == 2 ? sinf(t * 3.14159265f) * 0.90f : (0.12f + 0.88f * t);
				if (mv < 0.04f) continue;
				const float f = t * (float)(SN - 1);
				int r0 = (int)f; if (r0 > SN - 2) r0 = SN - 2;
				if (nid[live[0]][r0] < 0 || nid[live[0]][r0 + 1] < 0) continue;
				const float frr = f - (float)r0;
				const float u = (x1 - x0) > 1e-4f ? Clampf((q[0] - x0) / (x1 - x0), 0.f, 1.f) : 0.5f;
				int a = 0, b = 0;
				float frc = 0.f;
				if (nL >= 2 && u > cen[0] && u < cen[nL - 1]) {
					b = 0;
					while (b < nL - 1 && cen[b + 1] < u) b++;
					a = b;
					b = a + 1;
					if (b > nL - 1) b = nL - 1;
					const float den = cen[b] - cen[a];
					frc = den > 1e-5f ? Clampf((u - cen[a]) / den, 0.f, 1.f) : 0.f;
				} else if (nL >= 2 && u >= cen[nL - 1]) a = b = nL - 1;
				if (nid[live[a]][r0] < 0 || nid[live[a]][r0 + 1] < 0 || nid[live[b]][r0] < 0 || nid[live[b]][r0 + 1] < 0) continue;
				SoftGpuV o;
				memset(&o, 0, sizeof(o));
				o.a[0] = (unsigned)(nb + i);
				const unsigned ia = (unsigned)nid[live[a]][r0], ib = (unsigned)nid[live[b]][r0];
				const unsigned ic = (unsigned)nid[live[a]][r0 + 1], idn = (unsigned)nid[live[b]][r0 + 1];
				o.a[1] = ia | (ib << 16);
				o.a[2] = ic | (idn << 16);
				if (a == b) {
					o.w[0] = 1.f - frr; o.w[1] = frr;
				} else {
					o.w[0] = (1.f - frc) * (1.f - frr);
					o.w[1] = frc * (1.f - frr);
					o.w[2] = (1.f - frc) * frr;
					o.w[3] = frc * frr;
				}
				o.p[0] = q[0]; o.p[1] = q[1]; o.p[2] = q[2]; o.p[3] = mv;
				verts.push_back(o);
			}
		}
		if (fo.kind >= 1 && fo.kind != 3 && verts.size() > beforeV) sheets++;
		if (nodes.size() + 6 > Renderer::SOFT_MAX) break;
	}
	if (nodes.size() >= 4 && verts.size() >= 8) {
		nc.softN = nodes.size();
		nc.softVN = verts.size();
	}
}

// シミュレーションの点の次に、腰の断面を1点足す。点の数には入れない。
static void AppendTorso(Renderer::AvaCache& nc, Buf<SoftGpuN>& nodes)
{
	if (nc.softN < 4 || nodes.size() != nc.softN || nodes.size() >= Renderer::SOFT_MAX) return;
	SoftGpuN m;
	memset(&m, 0, sizeof(m));
	for (int i = 0; i < 4; i++) m.x[i] = fmaxf(nc.torsoR[i * 2], nc.torsoR[i * 2 + 1]);
	for (int i = 0; i < 4; i++) m.p[i] = fmaxf(nc.torsoR[8 + i * 2], nc.torsoR[8 + i * 2 + 1]);
	m.q[0] = nc.torsoC[0]; m.q[1] = nc.torsoC[1]; m.q[2] = nc.torsoC[2];
	m.q[3] = m.x[0] > 0.04f ? 1.f : 0.f;
	nodes.push_back(m);
}

// 衣装 [nb, nv) の布の決まりを作る。バインドを書き換えるので、GPU へ載せる前に呼ぶ。
// ・胸の頂から腰へ垂れる線より奥の布は前へ出し、胸の下に隙間を作る（jig があるとき）。
// ・股の中央を通って裾まで続く部品（ドレス・スカート・着物）は、腰を囲む細かい格子で動かす。頂点は近い 4 点の
//   ずれを混ぜて受けるので、布はいつもなめらか。脚に近い点は脚に付いていき、離れた点（広がった裾）は腰に付いて揺れる。
//   最後に頂点を脚の形（prof）の外へ出すので、布は体の線より内へ入らない。
// ・スカートの下で消してあった脚（素体）を戻す印を expose に付ける。1=脚だが他の服がぴったり覆う 2=見えてよい。
static void PrepCloth(Renderer::AvaCache& nc, int nb, int nv, const Buf<WORD>& cosTri, const int* segJ, const char (*segName)[24], int ns,
	const Model& md, float* jig, Buf<ClothGpuN>& nodes, Buf<ClothGpuV>& verts, Buf<float>& prof, Buf<BYTE>& expose,
	Buf<SoftGpuN>& softN, Buf<SoftGpuV>& softV, BOOL sewn)
{
	nodes.clear();
	softN.clear();
	softV.clear();
	nc.softN = nc.softVN = 0;
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
		if (j < 64) { o[0] += nc.jOff[j][0]; o[1] += nc.jOff[j][1]; o[2] += nc.jOff[j][2]; }
		return TRUE;
	};
	const int jHips = jointOf("Hips");
	const int jUL[2] = { jointOf("UpperLeg.L"), jointOf("UpperLeg.R") };
	const int jLL[2] = { jointOf("LowerLeg.L"), jointOf("LowerLeg.R") };
	const int jFt[2] = { jointOf("Foot.L"), jointOf("Foot.R") };
	const int jUA[2] = { jointOf("UpperArm.L"), jointOf("UpperArm.R") };
	const int jLA[2] = { jointOf("LowerArm.L"), jointOf("LowerArm.R") };
	const int jHd[2] = { jointOf("Hand.L"), jointOf("Hand.R") };
	const int jSh[2] = { jointOf("Shoulder.L"), jointOf("Shoulder.R") };
	const int jHead = jointOf("Head");
	float pUL[2][3], pLL[2][3], pFt[2][3], pUA[2][3] = {}, pLA[2][3] = {}, pHd[2][3] = {};
	for (int s = 0; s < 2; s++) {
		bindPos(jUA[s], pUA[s]);
		bindPos(jLA[s], pLA[s]);
		bindPos(jHd[s], pHd[s]);
	}
	if (jHips < 0) return;
	for (int s = 0; s < 2; s++) {
		if (jUL[s] == jHips || jLL[s] == jHips || jFt[s] == jHips) return;
		if (!bindPos(jUL[s], pUL[s]) || !bindPos(jLL[s], pLL[s]) || !bindPos(jFt[s], pFt[s])) return;
	}
	const float hipY = (pUL[0][1] + pUL[1][1]) * 0.5f, cx = (pUL[0][0] + pUL[1][0]) * 0.5f;
	const float torsoZ = (pUL[0][2] + pUL[1][2]) * 0.5f;
	// 胴の中心より腕の骨に近い布は袖。胸は胴の方が近いので、ここには入らない。
	auto nearerArm = [&](const float* p) -> BOOL {
		float ad = 1e9f;
		for (int s = 0; s < 2; s++) {
			const float* A[2] = { pUA[s], pLA[s] };
			const float* B[2] = { pLA[s], pHd[s] };
			for (int g = 0; g < 2; g++) {
				const float ab[3] = { B[g][0] - A[g][0], B[g][1] - A[g][1], B[g][2] - A[g][2] };
				const float L2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
				if (L2 < 1e-6f) continue;
				float t = ((p[0] - A[g][0]) * ab[0] + (p[1] - A[g][1]) * ab[1] + (p[2] - A[g][2]) * ab[2]) / L2;
				if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
				const float rx = p[0] - (A[g][0] + ab[0] * t), ry = p[1] - (A[g][1] + ab[1] * t), rz = p[2] - (A[g][2] + ab[2] * t);
				const float d2 = rx * rx + ry * ry + rz * rz;
				if (d2 < ad) ad = d2;
			}
		}
		ad = sqrtf(ad);
		const float dx = p[0] - cx, dz = p[2] - torsoZ;
		const float td = sqrtf(dx * dx + dz * dz);
		return ad < 0.16f && ad + 0.02f < td;
	};
	if (!(hipY > 0.3f && hipY < 2.5f)) return;
	nc.clothHip = jHips;
	float fitR[8][16];
	float fitLo = 0.f, fitHi = 0.f, fitCz = 0.f;
	int fitNS = 0;
	memset(fitR, 0, sizeof(fitR));
	// 腰から下腹。角度ごとのいちばん外。円にすると背中が腰の幅まで出る。
	{
		const float cz = (pUL[0][2] + pUL[1][2]) * 0.5f;
		const int armJ[6] = { jUA[0], jUA[1], jLA[0], jLA[1], jHd[0], jHd[1] };
		for (int i = 0; i < 16; i++) nc.torsoR[i] = 0.f;
		int got = 0;
		for (int k = 0; k < nb; k++) {
			const float* p = V[k].p;
			int aw = 0;
			for (int c = 0; c < 4; c++) {
				const int j = (int)V[k].j[c];
				if (jHead >= 0 && j == jHead) aw += V[k].w[c];
				for (int a = 0; a < 6; a++) if (armJ[a] >= 0 && j == armJ[a]) aw += V[k].w[c];
			}
			if (aw >= 160) continue;
			int lw = 0;
			for (int c = 0; c < 4; c++) {
				const int j = (int)V[k].j[c];
				for (int s = 0; s < 2; s++) if (j == jUL[s] || j == jLL[s] || j == jFt[s]) lw += V[k].w[c];
			}
			if (lw >= 140 && p[1] < hipY - 0.02f) continue;
			const float dx = p[0] - cx, dz = p[2] - cz;
			const float dist = sqrtf(dx * dx + dz * dz);
			// 腹は腰より前へ出る。前だけ上まで取る。尻の太さまで広げると、腰の輪が水着を後ろへ突き出す。
			const int belly = (dz < -0.015f && fabsf(dx) < 0.10f) ? 1 : 0;
			float yTop = hipY + 0.16f, dMax = 0.20f;
			if (belly) {
				yTop = hipY + 0.26f;
				dMax = 0.21f;
				if (p[1] > hipY + 0.16f && fabsf(dx) > 0.05f) continue;
			}
			if (p[1] < hipY - 0.05f || p[1] > yTop) continue;
			if (dist < 0.04f || dist > dMax) continue;
			const float phi = atan2f(dx, dz);
			const int pi = (int)floorf((phi / 6.2831853f + 0.5f) * 16.f + 0.5f) % 16;
			if (dist > nc.torsoR[pi]) nc.torsoR[pi] = dist;
			got++;
		}
		if (got >= 24) {
			for (int pass = 0; pass < 3; pass++) for (int i = 0; i < 16; i++) if (nc.torsoR[i] <= 0.f)
				nc.torsoR[i] = fmaxf(nc.torsoR[(i + 1) % 16], nc.torsoR[(i + 15) % 16]);
			int front[16];
			for (int i = 0; i < 16; i++) {
				const float phi = ((float)i / 16.f - 0.5f) * 6.2831853f;
				front[i] = cosf(phi) < -0.35f ? 1 : 0;
			}
			// 翼のような一本だけ太い角は潰す。腹は前が太いのが普通なので、中央値では切らない。
			for (int pass = 0; pass < 2; pass++) {
				float cur[16];
				for (int i = 0; i < 16; i++) cur[i] = nc.torsoR[i];
				for (int i = 0; i < 16; i++) {
					const float nbR = fmaxf(cur[(i + 15) % 16], cur[(i + 1) % 16]);
					if (nc.torsoR[i] > nbR * 1.40f && nc.torsoR[i] > 0.16f) nc.torsoR[i] = nbR * 1.15f;
				}
			}
			for (int i = 0; i < 16; i++) if (nc.torsoR[i] > 0.20f) nc.torsoR[i] = 0.20f;
			float raw[16], sm[16];
			for (int i = 0; i < 16; i++) raw[i] = nc.torsoR[i];
			for (int i = 0; i < 16; i++) {
				float sw = 0.f, sr = 0.f;
				for (int d = -2; d <= 2; d++) {
					const float w = d == 0 ? 1.f : (d == 1 || d == -1 ? 0.55f : 0.22f);
					sw += w;
					sr += w * nc.torsoR[(i + d + 16) % 16];
				}
				sm[i] = sr / sw + 0.008f;
				// ならすと腹の出っ張りが腰の幅まで戻る。前は測った面を残す。
				if (front[i] && sm[i] < raw[i] + 0.006f) sm[i] = raw[i] + 0.006f;
			}
			for (int i = 0; i < 16; i++) nc.torsoR[i] = sm[i];
		} else for (int i = 0; i < 16; i++) nc.torsoR[i] = 0.f;
		nc.torsoC[0] = cx; nc.torsoC[1] = hipY; nc.torsoC[2] = cz;
		// 腹は腰の一つの輪では届かない。高さごとに輪切りし、輪の中にいる服をその高さの外へ出す。
		{
			const int NS = 8;
			const float yLo = hipY - 0.20f, yHi = hipY + 0.52f;
			float rad[8][16];
			int cntS[8];
			for (int s = 0; s < NS; s++) { cntS[s] = 0; for (int i = 0; i < 16; i++) rad[s][i] = 0.f; }
			for (int k = 0; k < nb; k++) {
				const float* p = V[k].p;
				if (p[1] < yLo || p[1] > yHi) continue;
				int aw = 0;
				for (int c = 0; c < 4; c++) {
					const int j = (int)V[k].j[c];
					if (jHead >= 0 && j == jHead) aw += V[k].w[c];
					for (int a = 0; a < 6; a++) if (armJ[a] >= 0 && j == armJ[a]) aw += V[k].w[c];
				}
				if (aw >= 160) continue;
				int lw = 0;
				for (int c = 0; c < 4; c++) {
					const int j = (int)V[k].j[c];
					for (int s = 0; s < 2; s++) if (j == jUL[s] || j == jLL[s] || j == jFt[s]) lw += V[k].w[c];
				}
				if (lw >= 140 && p[1] < hipY - 0.02f) continue;
				const float dx = p[0] - cx, dz = p[2] - cz;
				if (p[1] > hipY + 0.34f && dz <= 0.02f) continue;
				const float dist = sqrtf(dx * dx + dz * dz);
				if (dist < 0.04f || dist > 0.22f) continue;
				int s = (int)((p[1] - yLo) / (yHi - yLo) * (float)NS);
				if (s < 0) s = 0;
				if (s > NS - 1) s = NS - 1;
				const float phi = atan2f(dx, dz);
				const int pi = (int)floorf((phi / 6.2831853f + 0.5f) * 16.f + 0.5f) % 16;
				if (dist > rad[s][pi]) rad[s][pi] = dist;
				cntS[s]++;
			}
			for (int s = 0; s < NS; s++) {
				if (cntS[s] < 12) continue;
				float rawM[16];
				for (int i = 0; i < 16; i++) rawM[i] = rad[s][i];
				for (int pass = 0; pass < 3; pass++) for (int i = 0; i < 16; i++) if (rad[s][i] <= 0.f)
					rad[s][i] = fmaxf(rad[s][(i + 1) % 16], rad[s][(i + 15) % 16]);
				for (int pass = 0; pass < 2; pass++) {
					float cur[16];
					for (int i = 0; i < 16; i++) cur[i] = rad[s][i];
					for (int i = 0; i < 16; i++) {
						const float nbR = fmaxf(cur[(i + 15) % 16], cur[(i + 1) % 16]);
						if (rad[s][i] > nbR * 1.40f && rad[s][i] > 0.16f) rad[s][i] = nbR * 1.15f;
					}
				}
				for (int i = 0; i < 16; i++) if (rad[s][i] > 0.20f) rad[s][i] = 0.20f;
				float sm[16];
				for (int i = 0; i < 16; i++) {
					float sw = 0.f, sr = 0.f;
					for (int d = -1; d <= 1; d++) {
						const float w = d == 0 ? 1.f : 0.45f;
						sw += w;
						sr += w * rad[s][(i + d + 16) % 16];
					}
					sm[i] = sr / sw + 0.003f;
					const float phi = ((float)i / 16.f - 0.5f) * 6.2831853f;
					const float czd = cosf(phi);
					if (czd < -0.35f && sm[i] < rad[s][i] + 0.004f) sm[i] = rad[s][i] + 0.004f;
					if (czd > 0.35f) {
						if (rawM[i] > 0.f) sm[i] = rawM[i] + 0.004f;
						else sm[i] = 0.f;
					}
				}
				for (int i = 0; i < 16; i++) rad[s][i] = sm[i];
			}
			fitNS = NS; fitLo = yLo; fitHi = yHi; fitCz = cz;
			for (int s = 0; s < NS && s < 8; s++) for (int i = 0; i < 16; i++) fitR[s][i] = rad[s][i];
			for (int i = 0; i < n; i++) {
				if (sewn) continue;
				float* p = V[nb + i].p;
				if (p[1] < yLo || p[1] > yHi) continue;
				int aw = 0;
				for (int c = 0; c < 4; c++) {
					const int j = (int)V[nb + i].j[c];
					if (jHead >= 0 && j == jHead) aw += V[nb + i].w[c];
					for (int a = 0; a < 6; a++) if (armJ[a] >= 0 && j == armJ[a]) aw += V[nb + i].w[c];
					for (int s = 0; s < 2; s++) if (jSh[s] >= 0 && j == jSh[s]) aw += V[nb + i].w[c];
				}
				if (aw >= 40) continue;
				BOOL onArm = FALSE;
				for (int s = 0; s < 2 && !onArm; s++) {
					const float* segA[2] = { pUA[s], pLA[s] };
					const float* segB[2] = { pLA[s], pHd[s] };
					for (int g = 0; g < 2; g++) {
						const float ab[3] = { segB[g][0] - segA[g][0], segB[g][1] - segA[g][1], segB[g][2] - segA[g][2] };
						const float L2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
						if (L2 < 1e-6f) continue;
						float t = ((p[0] - segA[g][0]) * ab[0] + (p[1] - segA[g][1]) * ab[1] + (p[2] - segA[g][2]) * ab[2]) / L2;
						if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
						const float rx = p[0] - (segA[g][0] + ab[0] * t), ry = p[1] - (segA[g][1] + ab[1] * t), rz = p[2] - (segA[g][2] + ab[2] * t);
						if (rx * rx + ry * ry + rz * rz < 0.11f * 0.11f) onArm = TRUE;
					}
				}
				if (onArm || nearerArm(p)) continue;
				int lw = 0;
				for (int c = 0; c < 4; c++) {
					const int j = (int)V[nb + i].j[c];
					for (int s = 0; s < 2; s++) if (j == jUL[s] || j == jLL[s] || j == jFt[s]) lw += V[nb + i].w[c];
				}
				if (lw >= 140 && p[1] < hipY - 0.04f) continue;
				const float dx = p[0] - cx, dz = p[2] - cz;
				// 袖は胴の中心から押すと腕から離れる。背中の中央だけ上まで出す。
				if (fabsf(dx) > 0.11f) continue;
				if (p[1] > hipY + 0.34f && !(dz > 0.04f && fabsf(dx) < 0.09f)) continue;
				const float dist = sqrtf(dx * dx + dz * dz);
				if (dist < 0.012f || dist > 0.28f) continue;
				const float u = (p[1] - yLo) / (yHi - yLo) * (float)(NS - 1);
				int s0 = (int)floorf(u);
				if (s0 < 0) s0 = 0;
				if (s0 > NS - 2) s0 = NS - 2;
				const float fu = u - (float)s0;
				const float phi = atan2f(dx, dz);
				const float fp = (phi / 6.2831853f + 0.5f) * 16.f;
				int b0 = (int)floorf(fp);
				const float fb = fp - (float)b0;
				b0 = (b0 % 16 + 16) % 16;
				const int b1 = (b0 + 1) % 16;
				const float r0 = rad[s0][b0] * (1.f - fb) + rad[s0][b1] * fb;
				const float r1 = rad[s0 + 1][b0] * (1.f - fb) + rad[s0 + 1][b1] * fb;
				float R = r0 * (1.f - fu) + r1 * fu;
				if (r0 < 0.05f) R = r1;
				if (r1 < 0.05f) R = r0;
				if (R < 0.05f || dist >= R) continue;
				float need = R - dist;
				const float cap = (dz > 0.04f && fabsf(dx) < 0.08f) ? 0.08f : 0.04f;
				if (need > cap) need = cap;
				const float sc = (dist + need) / dist;
				p[0] = cx + dx * sc;
				p[2] = cz + dz * sc;
			}
		}
	}

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
	BYTE armQ[256] = {};
	for (int j = 0; j < md.nJ && j < 256; j++) {
		for (int a = j, g = 0; a >= 0 && a < md.nJ && g < 64; a = md.parent[a], g++) {
			if (a == jUA[0] || a == jUA[1]) { armQ[j] = 1; break; }
		}
	}

	// 胸の下。頂（素体の胸でいちばん前の点）から垂らした布は、下の体（お腹・腰）に触れる所までまっすぐ張る。
	// 縦の列ごとに、頂から下の布の前面へ引いた接線（前面をどこも横切らない一番前の線）より奥の布を前へ出す。
	// 水着（衣装の下端が y > 0.65m）は肌に密着するので張らない。
	float cosYMin = 1e9f;
	for (int i = 0; i < n; i++) if (V[nb + i].p[1] < cosYMin) cosYMin = V[nb + i].p[1];
	// JK とスーツは作者の面のまま。胸の接線で手前へ引くと、腹のプリーツと袖の境が開く。
	if (jig && cosYMin < 0.65f && !sewn) {
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
			float S_stern = zFront + 0.03f;
			for (int k = 0; k < nb; k++) {
				const float* p = V[k].p;
				if (fabsf(p[0] - cx) < 0.015f && fabsf(p[1] - ya) < 0.025f && p[2] < 0.f) S_stern = fminf(S_stern, p[2]);
			}
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
			float slope[NBIN], yTan[NBIN], zTop[NBIN];
			for (int b = 0; b < NBIN; b++) { slope[b] = 0.f; yTan[b] = ya; zTop[b] = zFront; }
			if (fz.size() == NBIN * NY && ya - yLow > 0.08f) {
				for (int i = 0; i < n; i++) {
					if (root[i] != i) continue;
					const float* p = V[nb + i].p;
					const float* nr = V[nb + i].n;
					if (p[1] > ya + 0.02f || p[1] < yLow || p[2] > zFront + 0.40f || -nr[2] - 0.5f * nr[1] < 0.2f) continue;
					const int b = binOf(p[0]), yb = Clampi((int)((ya - p[1]) / 0.01f), 0, NY - 1);
					if (b < 0 || b >= NBIN) continue;
					float& z = fz[b * NY + yb];
					if (p[2] < z) z = p[2];
				}
				// 各列の始点は、素体の頂の包絡（左右はなだらかに下げ、谷も少し持ち上げる）と、その列の胸の前面のうち前にある方。
				// リボン等だけ出た列を横へ広げてから、ガウスで滑らかにする。
				const float zAvg = 0.5f * (apex[0][2] + apex[1][2]);
				const float zSpanC = zAvg + (S_stern - zAvg) * 0.35f;
				float zRaw[NBIN];
				for (int b = 0; b < NBIN; b++) {
					const float xb = cx + ((float)(b - NBIN / 2) + 0.5f) * 0.01f;
					float z0b = 0.f;
					if (xb >= apex[0][0] && xb <= apex[1][0]) {
						const float u0 = (xb - apex[0][0]) / (apex[1][0] - apex[0][0]);
						const float lin = apex[0][2] + (apex[1][2] - apex[0][2]) * u0;
						const float mid = sinf(u0 * 3.14159265f);
						z0b = lin + (zSpanC - lin) * (mid * mid) * 0.55f;
					} else {
						const int s = xb < cx ? 0 : 1;
						const float d = fabsf(xb - apex[s][0]);
						z0b = apex[s][2] + 0.032f * Smooth01(d / 0.065f);
					}
					z0b -= 0.004f;
					for (int k = 0; k <= 3 && k < NY; k++) if (fz[b * NY + k] < z0b) z0b = fz[b * NY + k];
					zRaw[b] = z0b;
				}
				float zDil[NBIN];
				for (int b = 0; b < NBIN; b++) {
					float m = zRaw[b];
					for (int d = -2; d <= 2; d++) if (b + d >= 0 && b + d < NBIN && zRaw[b + d] < m) m = zRaw[b + d];
					zDil[b] = m;
				}
				for (int b = 0; b < NBIN; b++) {
					float sw = 0.f, sz = 0.f;
					for (int d = -6; d <= 6; d++) {
						const int c = b + d;
						if (c < 0 || c >= NBIN) continue;
						const float w = expf(-0.5f * (float)(d * d) / 6.25f);
						sw += w; sz += w * zDil[c];
					}
					zTop[b] = sz / sw;
				}
				// 胸の下の着地は全列で揃える（頂より 16cm 下、傾き 0.14 = 16cm で 2.2cm 奥）。列ごとに別のお腹の点へ触れると縦の折れ目が出る。
				for (int b = 0; b < NBIN; b++) {
					yTan[b] = ya - 0.16f;
					float zBot = zTop[b] + 0.14f * 0.16f;
					for (int k = 14; k <= 18 && k < NY; k++) if (fz[b * NY + k] < zBot) zBot = fz[b * NY + k];
					slope[b] = (zBot - zTop[b]) / 0.16f;
				}
				float slSm[NBIN];
				for (int b = 0; b < NBIN; b++) {
					float sw = 0.f, ss = 0.f;
					for (int d = -5; d <= 5; d++) {
						const int c = b + d;
						if (c < 0 || c >= NBIN) continue;
						const float w = expf(-0.5f * (float)(d * d) / 4.f);
						sw += w; ss += w * slope[c];
					}
					slSm[b] = ss / sw;
				}
				for (int b = 0; b < NBIN; b++) slope[b] = slSm[b];
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
				(void)apexZ(x, &u);
				float wl = between ? 1.f : 1.f - Smooth01((fabsf(x - apex[s][0]) - 0.04f) / 0.07f);
				wl *= Smooth01((-nr[2] - 0.5f * nr[1] - 0.10f) / 0.30f);
				// 胸の横に下りた袖の内側も前を向く。腕の布を接線へ引くと、肘と脇に角が立つ。
				{
					int aw = 0;
					for (int c = 0; c < 4; c++) if (V[nb + i].w[c] && V[nb + i].j[c] < 256 && armQ[V[nb + i].j[c]]) aw += V[nb + i].w[c];
					wl *= 1.f - Smooth01((float)aw / 90.f);
				}
				if (nearerArm(p)) continue;
				if (wl <= 0.f) continue;
				// 頂のすぐ下から滑らかに張り始め、下の端（yTan）でも滑らかに 0 へ落としてお腹に段を作らない。
				const float bF = (x - cx) / 0.01f + (float)(NBIN / 2) - 0.5f;
				const int b = Clampi((int)floorf(bF), 0, NBIN - 2);
				const float wb = Clampf(bF - (float)b, 0.f, 1.f);
				const float yt = yTan[b] * (1.f - wb) + yTan[b + 1] * wb;
				if (yt >= ya || p[1] < yt) continue;
				const float t = (ya - p[1]) / (ya - yt);
				const float wv = Smooth01((ya - 0.005f - p[1]) / 0.032f) * Smooth01((1.f - t) / 0.25f);
				if (wv <= 0.f) continue;
				const float zt = zTop[b] * (1.f - wb) + zTop[b + 1] * wb;
				const float slb = slope[b] * (1.f - wb) + slope[b + 1] * wb;
				const float zl = zt + slb * (ya - p[1]) - 0.005f * sinf(t * 3.14159265f);
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
	// 裾の輪。部品の縁（面が片側にしかない辺）をつないだ輪のうち、いちばん低い縁の点を含む輪が
	// 左右の脚をまたいでいればスカート。短パンや水着は脚ごとに輪が分かれ、内股の裾が中央へ来ても脚をまたがない。
	Buf<int> lowB, ring;
	Buf<float> rLo, rHi;
	Buf<BYTE> onB;
	lowB.arena = ring.arena = rLo.arena = rHi.arena = onB.arena = 1;
	lowB.fill(n, -1); ring.resize(n); rLo.fill(n, 1e9f); rHi.fill(n, -1e9f); onB.fill(n, 0);
	if (lowB.size() < n || ring.size() < n || rLo.size() < n || rHi.size() < n || onB.size() < n) return;
	for (int i = 0; i < n; i++) ring[i] = i;
	{
		Buf<unsigned long long> ek;
		ek.arena = 1;
		ek.reserve(cosTri.size());
		for (int t = 0; t + 2 < cosTri.size(); t += 3) {
			int id[3] = { cosTri[t], cosTri[t + 1], cosTri[t + 2] };
			if (id[0] >= n || id[1] >= n || id[2] >= n) continue;
			for (int e = 0; e < 3; e++) {
				const unsigned a = (unsigned)root[id[e]], b = (unsigned)root[id[(e + 1) % 3]];
				if (a == b) continue;
				ek.push_back(a < b ? ((unsigned long long)a << 32) | b : ((unsigned long long)b << 32) | a);
			}
		}
		const int ne = ek.size();
		if (ne > 0) qsort(ek.data(), ne, sizeof(unsigned long long), CmpU64);
		for (int e = 0; e < ne; ) {
			int f = e + 1;
			while (f < ne && ek[f] == ek[e]) f++;
			if (f - e == 1) {
				const int a = (int)(ek[e] >> 32), b = (int)(ek[e] & 0xFFFFFFFFu);
				const int ra = UfFind(ring, a), rb = UfFind(ring, b);
				if (ra != rb) ring[ra] = rb;
				for (int k = 0; k < 2; k++) {
					const int v = k ? b : a;
					const int c = UfFind(par, v);
					if (lowB[c] < 0 || V[nb + v].p[1] < V[nb + lowB[c]].p[1]) lowB[c] = v;
					onB[v] = 1;
				}
			}
			e = f;
		}
		for (int v = 0; v < n; v++) {
			if (!onB[v]) continue;
			const int r = UfFind(ring, v);
			const float x = V[nb + v].p[0] - cx;
			if (x < rLo[r]) rLo[r] = x;
			if (x > rHi[r]) rHi[r] = x;
		}
	}
	auto hemRing = [&](int c) -> BOOL {
		if (lowB[c] < 0) return TRUE;
		const int r = UfFind(ring, lowB[c]);
		return rLo[r] < -0.04f && rHi[r] > 0.04f;
	};
	auto skirtComp = [&](int c) { return cMin[c] < hipY - 0.12f && cCen[c] < 1e8f && cCen[c] - cMin[c] < 0.10f && hemRing(c); };

	// 脚の形。太もも・すねごとに骨に沿って PT 段・まわり PF 分割し、体の頂点のいちばん外の半径に布の厚み 6mm を足す。
	// 前（-Z）を骨に直交させた向きを φ=0 にする。動いた布は骨の逆行列でこの空間へ戻して比べる。
	enum { PT = Renderer::CLOTH_PT, PF = Renderer::CLOTH_PF, PN = PT * PF };
	prof.fill(Renderer::CLOTH_SEGS * PN, 0.f);
	if (prof.size() < Renderer::CLOTH_SEGS * PN) return;
	auto addSeg = [&](int ja, int jb, int foot, float minR) {
		if (nc.segN >= Renderer::CLOTH_SEGS || ja < 0 || jb < 0 || (ja == jb && foot != 2)) return;
		float A[3], B[3];
		if (foot == 2) {
			float S[3];
			if (!bindPos(ja, A) || !bindPos(jb, S)) return;
			V3 d = v3(A[0] - S[0], A[1] - S[1], A[2] - S[2]);
			const float dl = Len(d);
			d = dl < 0.02f ? v3(0.f, -0.12f, 0.02f) : d * (0.12f / dl);
			B[0] = A[0] + d.x; B[1] = A[1] + d.y; B[2] = A[2] + d.z;
		} else if (!bindPos(ja, A) || !bindPos(jb, B)) return;
		V3 ax = v3(B[0] - A[0], B[1] - A[1], B[2] - A[2]);
		float L = Len(ax);
		if (L < 0.02f || (!foot && L < 0.05f)) return;
		ax = ax * (1.f / L);
		// 靴は足首の先まで続く。指の骨が短いとカプセルが甲で終わり、爪先が裾を突き抜ける。22cm まで足す。
		if (foot && L < 0.22f) L = 0.22f;
		V3 u = v3(0, 0, -1) - ax * (-ax.z);
		if (Len(u) < 0.2f) u = v3(1, 0, 0) - ax * ax.x;
		u = Norm(u);
		const V3 w = Cross(ax, u);
		const int s = nc.segN;
		float* P = &prof[s * PN];
		const int kend = foot ? nv : nb;
		const int side = (ja == jUL[0] || ja == jLL[0] || ja == jFt[0]) ? 0 : 1;
		const int oth = 1 - side;
		for (int k = 0; k < kend; k++) {
			const int d = DomJoint(V[k]);
			if (foot == 2) { if (d != ja) continue; }
			else if (foot) { if (d != ja && d != jb) continue; }
			if (k >= nb && !foot) continue;
			const V3 q = v3(V[k].p[0] - A[0], V[k].p[1] - A[1], V[k].p[2] - A[2]);
			const float t = Dot(q, ax) / L;
			const float tMax = foot ? 1.55f : 1.08f;
			if (t < -0.08f || t > tMax || (!foot && d == jb && t > 1.02f)) continue;
			const V3 rv = q - ax * (t * L);
			const float dist = Len(rv);
			// 太ももの外側は腰と重みを分けている。骨の重みだけでは円周が細くなり、スカートが肉の中に残る。
			if (dist < 0.012f || dist > 0.20f) continue;
			// 脛は子の骨（ねじり）も肉に入れる。太ももは子にすねと足がぶら下がるので、骨そのものだけ。
			bool legDom = (d == ja || d == jb);
			if (!legDom) {
				for (int a = d, g = 0; a >= 0 && a < md.nJ && g < 6; a = md.parent[a], g++) {
					if (ja == jUL[0] || ja == jUL[1]) {
						if (a == jb || a == jFt[0] || a == jFt[1]) break;
						if (a == ja) { legDom = true; break; }
					} else if (ja == jLL[0] || ja == jLL[1]) {
						if (a == ja || a == jb) { legDom = true; break; }
					}
				}
			}
			if (!legDom) {
				if (foot) continue;
				int ownW = 0, foreign = 0;
				for (int c = 0; c < 4; c++) {
					const int j = (int)V[k].j[c];
					if (j == ja || j == jb) ownW += V[k].w[c];
					if (j == jUL[oth] || j == jLL[oth] || j == jFt[oth]) foreign += V[k].w[c];
					if (j == jHead || j == jUA[0] || j == jUA[1] || j == jLA[0] || j == jLA[1] || j == jHd[0] || j == jHd[1]) foreign += V[k].w[c];
				}
				if (foreign >= 48) continue;
				if (t < 0.22f && ownW < 32) continue;
				if (d == jHips && dist > (t < 0.25f ? 0.125f : 0.14f)) continue;
			}
			const float phi = atan2f(Dot(rv, w), Dot(rv, u));
			const int ti = Clampi((int)floorf((t + 0.08f) / 1.16f * (float)(PT - 1) + 0.5f), 0, PT - 1);
			const int pi = (int)floorf((phi / 6.2831853f + 0.5f) * (float)PF + 0.5f) % PF;
			float& r = P[ti * PF + pi];
			if (dist > r) r = dist;
		}
		int got = 0;
		for (int i = 0; i < PN; i++) if (P[i] > 0.f) got++;
		if (got < PN / 4) {
			for (int i = 0; i < PN; i++) if (P[i] <= 0.f) P[i] = minR;
		}
		// 頂点の来なかった区画は、同じ段のまわり、次に上下の段から写す。
		for (int pass = 0; pass < 4; pass++) for (int ti = 0; ti < PT; ti++) for (int pi = 0; pi < PF; pi++) {
			float& r = P[ti * PF + pi];
			if (r > 0.f) continue;
			for (int d = 1; d <= PF / 2 && r <= 0.f; d++) r = fmaxf(P[ti * PF + (pi + d) % PF], P[ti * PF + (pi - d + PF) % PF]);
			if (r <= 0.f && pass > 0) r = fmaxf(ti > 0 ? P[(ti - 1) * PF + pi] : 0.f, ti < PT - 1 ? P[(ti + 1) * PF + pi] : 0.f);
		}
		if (!foot) {
			float Q[PN];
			for (int ti = 0; ti < PT; ti++) for (int pi = 0; pi < PF; pi++) {
				float m = P[ti * PF + pi];
				m = fmaxf(m, P[ti * PF + (pi + 1) % PF]);
				m = fmaxf(m, P[ti * PF + (pi + PF - 1) % PF]);
				Q[ti * PF + pi] = m;
			}
			memcpy(P, Q, sizeof(Q));
		}
		for (int i = 0; i < PN; i++) {
			if (P[i] < 0.02f) P[i] = minR;
			P[i] += 0.012f;
			// 太もものカプセルが尻・腰の肉を飲み込むと背面半径が 12cm 超になり、
			// 脚を振ったときスカートが大きく伸ばされて裂ける。現実的な脚の太さへ抑える。
			if (!foot && P[i] > 0.098f) P[i] = 0.098f;
		}
		nc.segJ[s][0] = ja; nc.segJ[s][1] = jb;
		float* G = &nc.segG[s][0][0];
		G[0] = A[0]; G[1] = A[1]; G[2] = A[2]; G[3] = L;
		G[4] = ax.x; G[5] = ax.y; G[6] = ax.z;
		// 付け根から足の中へ入れると、スカートが太ももの肉に残る。腰の帯はシェーダ側で外す。
		G[7] = -0.20f;
		G[8] = u.x; G[9] = u.y; G[10] = u.z; G[11] = 0.f;
		G[12] = w.x; G[13] = w.y; G[14] = w.z; G[15] = 0.f;
		nc.segN++;
	};
	for (int s = 0; s < 2; s++) addSeg(jUL[s], jLL[s], 0, 0.090f);
	if (nc.segN < 2) {
		if (!sewn) {
			BuildSoft(nc, nb, n, V, moved.data(), root.data(), NULL, segJ, segName, ns, md, jig, softN, softV);
			BuildFlutter(nc, nb, n, V, root.data(), par, NULL, NULL, segJ, segName, ns, md, hipY, cx, softN, softV);
		}
		AppendTorso(nc, softN);
		return;
	}
	for (int s = 0; s < 2; s++) addSeg(jLL[s], jFt[s], 0, 0.064f);
	for (int s = 0; s < 2; s++) {
		const int toe = jointOf(s == 0 ? "PT.L" : "PT.R");
		if (toe >= 0) addSeg(jFt[s], toe, 1, 0.050f);
		else addSeg(jFt[s], jLL[s], 2, 0.050f);
	}
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
	if (isSk.size() < n) {
		if (!sewn) {
			BuildSoft(nc, nb, n, V, moved.data(), root.data(), NULL, segJ, segName, ns, md, jig, softN, softV);
			BuildFlutter(nc, nb, n, V, root.data(), par, NULL, NULL, segJ, segName, ns, md, hipY, cx, softN, softV);
		}
		AppendTorso(nc, softN);
		return;
	}
	float yMin = 1e9f, czSum = 0.f;
	int czN = 0;
	for (int i = 0; i < n; i++) {
		if (!skirtComp(UfFind(par, root[i]))) continue;
		isSk[i] = 1;
		const float* p = V[nb + i].p;
		if (p[1] < yMin) yMin = p[1];
		if (p[1] < y0) { czSum += p[2]; czN++; }
	}
	// 作った時点で足の中にあるスカートは、正面を体に残しても足が出る。表面の外へ出してから格子を組む。
	// JK とスーツはプリーツが足の横まで上がっている。ここを一気に出すと裾が帯になるので、作者の位置のままにする。
	for (int pass = 0; pass < 4 && !sewn; pass++) for (int i = 0; i < n; i++) {
		if (!isSk[i]) continue;
		float* p = V[nb + i].p;
		if (p[1] > y0) continue;
		float pen = 0.f;
		V3 push = v3(0.f, 0.f, 0.f);
		for (int s = 0; s < nc.segN; s++) {
			const float* G = &nc.segG[s][0][0];
			const V3 q = v3(p[0] - G[0], p[1] - G[1], p[2] - G[2]);
			const V3 ax = v3(G[4], G[5], G[6]);
			const float t = Dot(q, ax) / G[3];
			if (t < -0.20f || t > 1.20f) continue;
			const float tc = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
			const V3 rv = q - ax * (tc * G[3]);
			const float dist = Len(rv);
			const float phi = atan2f(Dot(rv, v3(G[12], G[13], G[14])), Dot(rv, v3(G[8], G[9], G[10])));
			float R = ClothProf(&prof[(size_t)s * PN], tc, phi) + 0.012f;
			if (R < 0.045f) R = 0.045f;
			if (R - dist <= pen) continue;
			pen = R - dist;
			const V3 dir = dist > 1e-4f ? rv * (1.f / dist) : v3(G[8], G[9], G[10]);
			push = dir * pen;
		}
		if (pen > 0.f) { p[0] += push.x; p[1] += push.y; p[2] += push.z; }
	}
	Buf<BYTE> tight;
	Buf<float> yTopC, yBotC;
	tight.arena = yTopC.arena = yBotC.arena = 1;
	tight.fill(n, 0);
	yTopC.fill(n, -1e9f);
	yBotC.fill(n, 1e9f);
	if (tight.size() >= n && yTopC.size() >= n && yBotC.size() >= n) {
		for (int i = 0; i < n; i++) {
			const int c = UfFind(par, root[i]);
			const float y = V[nb + i].p[1];
			if (y > yTopC[c]) yTopC[c] = y;
			if (y < yBotC[c]) yBotC[c] = y;
		}
		for (int i = 0; i < n; i++) {
			const int c = UfFind(par, root[i]);
			// 水着。胴から下腹までで、膝まで垂れない。スカートの風には載せない。
			if (yTopC[c] > hipY + 0.12f && yBotC[c] < hipY - 0.06f && yBotC[c] > hipY - 0.32f) tight[i] = 1;
		}
		// ブーツは上半分がすね、下半分が足。骨は作者のまま（筒はすね、底は足）。足の骨へまとめると筒が甲の方向へ寝る。風の布からは外す。
		for (int i = 0; i < n; i++) {
			Renderer::CSkinV& s = V[nb + i];
			int best = -1, bw = 0;
			for (int k = 0; k < 4; k++) if (s.w[k] > bw) { bw = s.w[k]; best = (int)s.j[k]; }
			if (best < 0 || bw < 80) continue;
			int side = -1;
			for (int a = best, g = 0; a >= 0 && a < md.nJ && g < 8; a = md.parent[a], g++) {
				if (a == jFt[0] || a == jLL[0]) { side = 0; break; }
				if (a == jFt[1] || a == jLL[1]) { side = 1; break; }
			}
			if (side < 0) continue;
			const int c = UfFind(par, root[i]);
			const BOOL bootPiece = yTopC[c] < hipY - 0.18f;
			if (!bootPiece || s.p[1] > hipY - 0.20f) continue;
			isSk[i] = 0;
			tight[i] = 0;
		}
		auto sliceR = [&](const float* p) -> float {
			if (fitNS < 2 || p[1] < fitLo || p[1] > fitHi) return 0.f;
			const float dx = p[0] - cx, dz = p[2] - fitCz;
			const float u = (p[1] - fitLo) / (fitHi - fitLo) * (float)(fitNS - 1);
			int s0 = (int)floorf(u);
			if (s0 < 0) s0 = 0;
			if (s0 > fitNS - 2) s0 = fitNS - 2;
			const float fu = u - (float)s0;
			const float phi = atan2f(dx, dz);
			const float fp = (phi / 6.2831853f + 0.5f) * 16.f;
			int b0 = (int)floorf(fp);
			const float fb = fp - (float)b0;
			b0 = (b0 % 16 + 16) % 16;
			const int b1 = (b0 + 1) % 16;
			const float r0 = fitR[s0][b0] * (1.f - fb) + fitR[s0][b1] * fb;
			const float r1 = fitR[s0 + 1][b0] * (1.f - fb) + fitR[s0 + 1][b1] * fb;
			float R = r0 * (1.f - fu) + r1 * fu;
			if (r0 < 0.05f) R = r1;
			if (r1 < 0.05f) R = r0;
			return R;
		};
		for (int i = 0; i < n; i++) {
			if (sewn) continue;
			float* p = V[nb + i].p;
			const float dx = p[0] - cx, dz = p[2] - fitCz;
			const float dist = sqrtf(dx * dx + dz * dz);
			if (dist < 0.02f) continue;
			if (tight[i]) {
				isSk[i] = 0;
				const float R = sliceR(p);
				if (R < 0.05f) continue;
				float goal = dist;
				if (dist < R) goal = dist + fminf(R - dist, 0.04f);
				else if (dist > R + 0.008f) goal = dist - fminf(dist - (R + 0.004f), 0.04f);
				if (fabsf(goal - dist) < 1e-4f) continue;
				const float sc = goal / dist;
				p[0] = cx + dx * sc;
				p[2] = fitCz + dz * sc;
			}
		}
		// 上下が別の服。重なる高さではスカートを上着の内側、上着をその外へ。
		float topR[8][16], botR[8][16];
		memset(topR, 0, sizeof(topR));
		memset(botR, 0, sizeof(botR));
		const float layLo = hipY - 0.22f, layHi = hipY + 0.22f;
		auto layOf = [&](const float* p, int* s, int* b, float* dist) -> BOOL {
			if (p[1] < layLo || p[1] > layHi) return FALSE;
			const float dx = p[0] - cx, dz = p[2] - fitCz;
			*dist = sqrtf(dx * dx + dz * dz);
			if (*dist < 0.02f) return FALSE;
			const float u = (p[1] - layLo) / (layHi - layLo) * 7.f;
			*s = (int)u; if (*s < 0) *s = 0; if (*s > 7) *s = 7;
			const float phi = atan2f(dx, dz);
			*b = (int)floorf((phi / 6.2831853f + 0.5f) * 16.f + 0.5f);
			*b = (*b % 16 + 16) % 16;
			return TRUE;
		};
		for (int i = 0; i < n; i++) {
			const int c = UfFind(par, root[i]);
			const float* p = V[nb + i].p;
			int s, b; float dist;
			if (!layOf(p, &s, &b, &dist)) continue;
			if (fabsf(p[0] - cx) > 0.16f) continue;
			const BOOL top = !isSk[i] && !tight[i] && yTopC[c] > hipY + 0.10f && yBotC[c] > hipY - 0.42f;
			const BOOL bot = isSk[i] && yTopC[c] < hipY + 0.22f && yBotC[c] < hipY - 0.05f;
			if (top && dist > topR[s][b]) topR[s][b] = dist;
			if (bot && dist > botR[s][b]) botR[s][b] = dist;
		}
		auto shellR = [&](float tab[8][16], const float* p) -> float {
			int s, b; float dist;
			if (!layOf(p, &s, &b, &dist)) return 0.f;
			float R = tab[s][b];
			if (R < 0.05f) R = fmaxf(tab[s][(b + 1) % 16], tab[s][(b + 15) % 16]);
			return R;
		};
		for (int i = 0; i < n; i++) {
			if (sewn) continue;
			const int c = UfFind(par, root[i]);
			float* p = V[nb + i].p;
			const float dx = p[0] - cx, dz = p[2] - fitCz;
			const float dist = sqrtf(dx * dx + dz * dz);
			if (dist < 0.02f) continue;
			const BOOL bot = isSk[i] && yTopC[c] < hipY + 0.22f && yBotC[c] < hipY - 0.05f;
			float goal = dist;
			if (bot && fabsf(p[0] - cx) < 0.16f) {
				const float tR = shellR(topR, p);
				if (tR > 0.05f && dist > tR - 0.008f) {
					goal = tR - 0.008f;
					const float body = sliceR(p);
					if (body > 0.05f && goal < body) goal = body;
				}
			}
			if (fabsf(goal - dist) < 1e-4f) continue;
			float move = goal - dist;
			if (move > 0.05f) move = 0.05f;
			if (move < -0.05f) move = -0.05f;
			const float sc = (dist + move) / dist;
			p[0] = cx + dx * sc;
			p[2] = fitCz + dz * sc;
		}
		// 着物と長いスカートの裾が靴の中に入っている分だけ、靴の外へ出す。
		float shR[2] = {}, shY0[2] = { 1e9f, 1e9f }, shY1[2] = { -1e9f, -1e9f };
		for (int i = 0; i < n; i++) {
			if (isSk[i] || tight[i]) continue;
			const float* p = V[nb + i].p;
			if (p[1] > hipY - 0.40f) continue;
			const int s = p[0] < cx ? 0 : 1;
			const float dx = p[0] - pFt[s][0], dz = p[2] - pFt[s][2];
			const float dist = sqrtf(dx * dx + dz * dz);
			if (dist > shR[s]) shR[s] = dist;
			if (p[1] < shY0[s]) shY0[s] = p[1];
			if (p[1] > shY1[s]) shY1[s] = p[1];
		}
		for (int i = 0; i < n; i++) {
			if (sewn) continue;
			if (!isSk[i]) continue;
			float* p = V[nb + i].p;
			if (p[1] > hipY - 0.35f) continue;
			int leg = 0, best = -1, bw = 0;
			for (int k = 0; k < 4; k++) if (V[nb + i].w[k] > bw) { bw = V[nb + i].w[k]; best = (int)V[nb + i].j[k]; }
			if (best >= 0) for (int a = best, g = 0; a >= 0 && a < md.nJ && g < 8; a = md.parent[a], g++)
				if (a == jLL[0] || a == jLL[1] || a == jFt[0] || a == jFt[1] || a == jUL[0] || a == jUL[1]) { leg = 1; break; }
			if (leg) continue;
			const int s = p[0] < cx ? 0 : 1;
			if (shR[s] < 0.03f || p[1] > shY1[s] + 0.02f) continue;
			const float dx = p[0] - pFt[s][0], dz = p[2] - pFt[s][2];
			const float dist = sqrtf(dx * dx + dz * dz);
			if (dist < 0.01f || dist > shR[s] + 0.004f) continue;
			const float sc = (shR[s] + 0.012f) / dist;
			p[0] = pFt[s][0] + dx * sc;
			p[2] = pFt[s][2] + dz * sc;
			if (p[1] < shY1[s] && p[1] > shY0[s] - 0.01f) p[1] = shY1[s] + 0.008f;
		}
	}
	float span = y0 - yMin;
	// ミニスカートは靡かせない。裾が股の近くなので、動かすとカメラから股間が見える。
	const BOOL mini = span < 0.38f;
	// ミニスカートだけ裾を 9cm 足す。腰は動かさない。丈のあるドレスと水着の下は対象外。
	if (span > 0.12f && span < 0.36f) {
		const float extra = 0.09f;
		for (int i = 0; i < n; i++) {
			if (!isSk[i]) continue;
			float* p = V[nb + i].p;
			const float drop = y0 - p[1];
			if (drop <= 0.f) continue;
			const float u = drop / span;
			const float w = Smooth01((u - 0.08f) / 0.42f);
			p[1] -= extra * w;
		}
		yMin -= extra;
		span = y0 - yMin;
	}
	if (!sewn) {
		BuildSoft(nc, nb, n, V, moved.data(), root.data(), isSk.data(), segJ, segName, ns, md, jig, softN, softV);
		BuildFlutter(nc, nb, n, V, root.data(), par, isSk.data(), tight.size() >= n ? tight.data() : NULL, segJ, segName, ns, md, hipY, cx, softN, softV);
	}
	AppendTorso(nc, softN);
	if (czN < 32 || span < 0.10f) {
		// スカートが無くても、袖の下の素体は expose で隠す。ズボン＋シャツでも同じ。
		BYTE armBone[256] = {};
		for (int j = 0; j < md.nJ && j < 256; j++) {
			for (int a = j, g = 0; a >= 0 && a < md.nJ && g < 64; a = md.parent[a], g++) {
				if (a == jUA[0] || a == jUA[1]) { armBone[j] = 1; break; }
			}
		}
		expose.fill(nb, 0);
		if (expose.size() == nb) {
			const float CELL = 0.02f;
			auto cellOf = [&](const float* p, int dx, int dy, int dz) -> unsigned {
				int x = (int)floorf(p[0] / CELL) + dx + 512, y = (int)floorf(p[1] / CELL) + dy + 512, z = (int)floorf(p[2] / CELL) + dz + 512;
				return (unsigned)(x & 1023) | ((unsigned)(y & 1023) << 10) | ((unsigned)(z & 1023) << 20);
			};
			auto sleeveW = [&](int i) -> int {
				int aw = 0;
				const Renderer::CSkinV& cv = V[nb + i];
				for (int c = 0; c < 4; c++) if (cv.w[c] && cv.j[c] < 256 && armBone[cv.j[c]]) aw += cv.w[c];
				return aw;
			};
			Buf<unsigned long long> ck;
			ck.arena = 1;
			for (int i = 0; i < n; i++) if (root[i] == i) ck.push_back(((unsigned long long)cellOf(V[nb + i].p, 0, 0, 0) << 32) | (unsigned)i);
			const int nk = ck.size();
			if (nk > 0) qsort(ck.data(), nk, sizeof(unsigned long long), CmpU64);
			auto rayCover = [&](const float* p, float nx, float ny, float nz, float lim, BOOL wantArm) -> BOOL {
				for (float t = 0.002f; t <= lim; t += 0.003f) {
					const float q[3] = { p[0] + nx * t, p[1] + ny * t, p[2] + nz * t };
					for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
						const unsigned long long cell = cellOf(q, dx, dy, dz);
						int lo = 0, hi = nk;
						while (lo < hi) { int m = (lo + hi) >> 1; if ((ck[m] >> 32) < cell) lo = m + 1; else hi = m; }
						for (int u = lo; u < nk && (ck[u] >> 32) == cell; u++) {
							const int i = (int)(ck[u] & 0xFFFFFFFFu);
							const float* b = V[nb + i].p;
							const float d2 = (b[0] - q[0]) * (b[0] - q[0]) + (b[1] - q[1]) * (b[1] - q[1]) + (b[2] - q[2]) * (b[2] - q[2]);
							if (d2 < 0.005f * 0.005f) {
								const int aw = sleeveW(i);
								if (wantArm ? aw >= 96 : aw < 180) return TRUE;
							}
						}
					}
				}
				return FALSE;
			};
			for (int k = 0; k < nb; k++) {
				const int d = DomJoint(V[k]);
				const BOOL isArm = (d >= 0 && d < 256 && armBone[d]);
				const BOOL isTor = !isArm && d != jHead && V[k].p[1] > hipY - 0.08f;
				if (!isArm && !isTor) continue;
				const float* p = V[k].p;
				if (isArm) {
					int lw = 0;
					for (int c = 0; c < 4; c++) if (V[k].w[c] && V[k].j[c] < 256 && armBone[V[k].j[c]]) lw += V[k].w[c];
					if (lw < 204) continue;
				}
				float nx = V[k].n[0], ny = V[k].n[1], nz = V[k].n[2];
				float nl = sqrtf(nx * nx + ny * ny + nz * nz);
				BOOL hit = FALSE;
				if (nl > 1e-5f) {
					nx /= nl; ny /= nl; nz /= nl;
					if (isArm) {
						int s = (d == jUA[1] || d == jLA[1] || d == jHd[1]) ? 1 : 0;
						const float* A = (d == jLA[s] || d == jHd[s]) ? pLA[s] : pUA[s];
						const float* B = (d == jLA[s] || d == jHd[s]) ? pHd[s] : pLA[s];
						float ax = B[0] - A[0], ay = B[1] - A[1], az = B[2] - A[2];
						float aL = sqrtf(ax * ax + ay * ay + az * az);
						int nHit = 0;
						if (aL > 1e-5f) {
							ax /= aL; ay /= aL; az /= aL;
							const float dn = nx * ax + ny * ay + nz * az;
							float px = nx - ax * dn, py = ny - ay * dn, pz = nz - az * dn;
							float pL = sqrtf(px * px + py * py + pz * pz);
							if (pL > 1e-5f) {
								px /= pL; py /= pL; pz /= pL;
								const float bx = ay * pz - az * py, by = az * px - ax * pz, bz = ax * py - ay * px;
								const float dir[4][3] = { { px, py, pz }, { bx, by, bz }, { -px, -py, -pz }, { -bx, -by, -bz } };
								for (int e = 0; e < 4; e++)
									if (rayCover(p, dir[e][0], dir[e][1], dir[e][2], 0.030f, TRUE)) nHit++;
							}
						}
						hit = nHit >= 3;
					} else hit = rayCover(p, nx, ny, nz, 0.012f, FALSE);
				}
				expose[k] = hit ? 1 : 2;
			}
		}
		return;
	}
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
	// 両脇のスリット。段ごとに布の三角形が覆う角度を 64 分割で塗り、真横から ±40° に布の無い角度が裾から 2 段以上続けば、その脇は開いている。
	// 前と後ろの両方に布がある段だけ数える（前後で丈の違う裾を開きと見ない）。開いた段は前と後ろを別の布にし、
	// となりの点をつながず、頂点は自分の側の点だけから受ける。角度は atan2(x, z)：0 が背中、±π が前、負が左脇。
	enum { SLIT_AB = 64, SLIT_R = Renderer::CLOTH_NODES / Renderer::CLOTH_SECT };
	float gapA[SLIT_R][2];
	int slitTop[2] = { M + 1, M + 1 };
	for (int r = 0; r < SLIT_R; r++) gapA[r][0] = gapA[r][1] = 0.f;
	{
		Buf<BYTE> cov;
		cov.arena = 1;
		cov.fill((M + 1) * SLIT_AB, 0);
		if (cov.size() == (M + 1) * SLIT_AB && M + 1 <= SLIT_R) {
			const float rowH = span / (float)M;
			for (int t = 0; t + 2 < cosTri.size(); t += 3) {
				const int id3[3] = { cosTri[t], cosTri[t + 1], cosTri[t + 2] };
				if (id3[0] >= n || id3[1] >= n || id3[2] >= n) continue;
				if (!isSk[id3[0]] || !isSk[id3[1]] || !isSk[id3[2]]) continue;
				float a[3], ylo = 1e9f, yhi = -1e9f;
				for (int e = 0; e < 3; e++) {
					const float* p = V[nb + id3[e]].p;
					a[e] = atan2f(p[0] - cx, p[2] - cz);
					ylo = fminf(ylo, p[1]);
					yhi = fmaxf(yhi, p[1]);
				}
				float lo = a[0], hi = a[0];
				for (int e = 1; e < 3; e++) {
					float d = a[e] - a[0];
					if (d > PI2 * 0.5f) d -= PI2;
					else if (d < -PI2 * 0.5f) d += PI2;
					lo = fminf(lo, a[0] + d);
					hi = fmaxf(hi, a[0] + d);
				}
				// 軸のすぐ近くの三角形は角度が大きく開く。塗ると脇の切れ目まで埋まる。
				if (hi - lo > PI2 * 0.42f) continue;
				const int b0 = (int)floorf((lo / PI2 + 0.5f) * SLIT_AB), b1 = (int)floorf((hi / PI2 + 0.5f) * SLIT_AB);
				for (int r = 0; r <= M; r++) {
					const float yr = y0 - rowH * (float)r;
					if (yr < ylo - rowH * 0.25f || yr > yhi + rowH * 0.25f) continue;
					for (int b = b0; b <= b1; b++) cov[r * SLIT_AB + ((b % SLIT_AB) + SLIT_AB) % SLIT_AB] = 1;
				}
			}
			BYTE gap[SLIT_R][2] = {};
			for (int r = 1; r <= M; r++) {
				const BYTE* c = &cov[r * SLIT_AB];
				BOOL fr = FALSE, bk = FALSE;
				for (int b = 0; b < SLIT_AB; b++) {
					if (!c[b]) continue;
					const float ang = fabsf(((float)b + 0.5f) / (float)SLIT_AB - 0.5f) * PI2;
					if (ang > 2.3f) fr = TRUE;
					if (ang < 0.85f) bk = TRUE;
				}
				if (!fr || !bk) continue;
				for (int sd = 0; sd < 2; sd++) {
					const float sa = sd ? PI2 * 0.25f : -PI2 * 0.25f;
					float sumA = 0.f;
					int cntA = 0;
					for (int b = 0; b < SLIT_AB; b++) {
						if (c[b]) continue;
						const float a = (((float)b + 0.5f) / (float)SLIT_AB - 0.5f) * PI2;
						if (fabsf(a - sa) < 0.70f) { sumA += a; cntA++; }
					}
					if (cntA > 0) {
						gap[r][sd] = 1;
						gapA[r][sd] = sumA / (float)cntA;
					}
				}
			}
			// 裾の段は縁の三角形が覆うことや前後の丈の差で片側だけ残ることがあるので、一つ上から数え始めてよい。
			// 切れ目の無かった段にも一番近い開いた段の角度を入れておき、裾（r=M）や境の段で角度が 0 に飛ばないようにする。
			for (int sd = 0; sd < 2; sd++) {
				int top = M + 1, cnt = 0;
				for (int r = M; r >= 1; r--) {
					if (gap[r][sd]) { top = r; cnt++; }
					else if (cnt > 0 || r < M - 1) break;
				}
				if (cnt >= 2) {
					slitTop[sd] = top;
					for (int r = 0; r <= M; r++) {
						if (gap[r][sd]) continue;
						int bestR = -1, bestD = 999;
						for (int r2 = 1; r2 <= M; r2++) if (gap[r2][sd]) {
							const int d = r > r2 ? r - r2 : r2 - r;
							if (d < bestD) { bestD = d; bestR = r2; }
						}
						if (bestR >= 0) gapA[r][sd] = gapA[bestR][sd];
					}
				}
			}
		}
	}
	const int topBoth = slitTop[0] > slitTop[1] ? slitTop[0] : slitTop[1];
	const float ySlit = topBoth <= M ? y0 - span * (float)topBoth / (float)M : yMin;
	float slitAng[2] = { PI2 * 0.25f, PI2 * 0.25f };
	for (int sd = 0; sd < 2; sd++) {
		if (slitTop[sd] <= M) {
			float sum = 0.f;
			int cnt = 0;
			for (int r = slitTop[sd]; r <= M; r++) {
				const float g = fabsf(gapA[r][sd]);
				if (g > 0.20f) { sum += g; cnt++; }
			}
			if (cnt > 0) slitAng[sd] = sum / (float)cnt;
		}
	}
	auto gapOf = [&](int r, int sd) { (void)r; return slitAng[sd]; };
	auto cutRow = [&](int r, int sd) { return slitTop[sd] <= M && r >= slitTop[sd]; };
	auto nodeAng = [&](int s) { return ((float)s / (float)K - 0.5f) * PI2; };
	// 開いた脇をまたぐとなりか。
	auto cutBetween = [&](int r, int s, int s2) -> BOOL {
		const int sd = nodeAng(s) + nodeAng(s2) < 0.f ? 0 : 1;
		if (!cutRow(r, sd)) return FALSE;
		const float g = gapOf(r, sd);
		return (fabsf(nodeAng(s)) > g) != (fabsf(nodeAng(s2)) > g);
	};
	// 切れ目の向こうの点から受けている重みを、同じ側の点へ移す。
	auto slitFix = [&](const float* p, int* id, float* w) {
		const float ang = atan2f(p[0] - cx, p[2] - cz);
		const int sd = ang < 0.f ? 0 : 1;
		for (int h = 0; h < 2; h++) {
			const int r = id[h * 2] / K;
			if (!cutRow(r, sd)) continue;
			const float g = gapOf(r, sd);
			const BOOL fv = fabsf(ang) > g;
			const BOOL f0 = fabsf(nodeAng(id[h * 2] % K)) > g, f1 = fabsf(nodeAng(id[h * 2 + 1] % K)) > g;
			if (f0 != fv && f1 == fv) { w[h * 2 + 1] += w[h * 2]; w[h * 2] = 0.f; id[h * 2] = id[h * 2 + 1]; }
			else if (f1 != fv && f0 == fv) { w[h * 2] += w[h * 2 + 1]; w[h * 2 + 1] = 0.f; id[h * 2 + 1] = id[h * 2]; }
		}
	};
	BYTE armBone[256] = {}, footL[256] = {}, footR[256] = {};
	for (int j = 0; j < md.nJ && j < 256; j++) {
		for (int a = j, g = 0; a >= 0 && a < md.nJ && g < 64; a = md.parent[a], g++) {
			if (a == jUA[0] || a == jUA[1] || a == jHd[0] || a == jHd[1]) { armBone[j] = 1; break; }
		}
		for (int a = j, g = 0; a >= 0 && a < md.nJ && g < 64; a = md.parent[a], g++) {
			if (a == jFt[0]) { footL[j] = 1; break; }
			if (a == jFt[1]) { footR[j] = 1; break; }
		}
	}
	// 長いスカートの裾に足首・つま先の骨が混ざっていると、飛行でつま先を伸ばしたときに踵側の裾が上へ折れ、前も靴に貼り付く。
	// 靴は別の部品（isSk==0）なので触らず、スカートの頂点（isSk==1）はすべてすねの骨へ移す。
	for (int i = 0; i < n; i++) {
		if (!isSk[i]) continue;
		Renderer::CSkinV& s = V[nb + i];
		for (int c = 0; c < 4; c++) {
			if (!s.w[c] || s.j[c] >= 256) continue;
			int rep = -1;
			if (footL[s.j[c]] && jLL[0] >= 0 && jLL[0] < 256) rep = jLL[0];
			else if (footR[s.j[c]] && jLL[1] >= 0 && jLL[1] < 256) rep = jLL[1];
			if (rep < 0) continue;
			int exist = -1;
			for (int k = 0; k < 4; k++) if (k != c && s.j[k] == (unsigned char)rep && s.w[k]) exist = k;
			if (exist >= 0) {
				int sum = (int)s.w[exist] + (int)s.w[c];
				if (sum > 255) sum = 255;
				s.w[exist] = (unsigned char)sum;
				s.w[c] = 0;
			} else s.j[c] = (unsigned char)rep;
		}
	}
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
		slitFix(s.p, id, w);
		const float rad = sqrtf((s.p[0] - cx) * (s.p[0] - cx) + (s.p[2] - cz) * (s.p[2] - cz));
		for (int e = 0; e < 4; e++) {
			if (w[e] <= 0.f) continue;
			if (rad > rs[id[e]]) rs[id[e]] = rad;
			rw[id[e]] += w[e];
			for (int c = 0; c < 4; c++) {
				if (!s.w[c] || s.j[c] >= NJW) continue;
				if (s.j[c] < 256 && armBone[s.j[c]]) continue;
				jw[id[e] * NJW + s.j[c]] += s.w[c] / 255.f * w[e];
			}
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
		for (int passSame = 0; passSame < 2 && from[q] < 0; passSame++) {
			for (int d = 1; d <= K / 2 && from[q] < 0; d++) {
				const int sa = (s + d) % K, sb = (s - d + K) % K;
				const int a = r * K + sa, b = r * K + sb;
				const BOOL okA = passSame || !cutBetween(r, s, sa);
				const BOOL okB = passSame || !cutBetween(r, s, sb);
				if (okA && rw[a] > 1e-4f) from[q] = a;
				else if (okB && rw[b] > 1e-4f) from[q] = b;
			}
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
				int nq[4] = { r > 0 ? q - K : q, r < M ? q + K : q, r * K + (s + K - 1) % K, r * K + (s + 1) % K };
				if (cutBetween(r, s, (s + K - 1) % K)) nq[2] = q;
				if (cutBetween(r, s, (s + 1) % K)) nq[3] = q;
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
		const float R = rs[f];
		const float th = ((float)s / (float)K - 0.5f) * PI2;
		o.x[0] = cx + R * sinf(th);
		o.x[1] = y0 - span * (float)r / (float)M;
		o.x[2] = cz + R * cosf(th);
		// 最上段も脚に付いていく（上の頂点は衣装の元の重みなので、継ぎ目で動きを揃える）。
		o.q[0] = followOf(o.x);
		o.q[1] = clearance(o.x);   // 下の allowOf で許す深さに直す
		// 裾ほど遠くまで離れてよく、ゆっくり戻り、重力が効く。
		const float t = (float)r / (float)M;
		// 脚に近い点は、当たりとばねが押し合いしてちらつかないよう、離れ幅を足す。
		// 戻りは裾ほど遅い。足を戻した布は重力と風で降りる。
		o.p[0] = 0.022f + 0.09f * t + 0.03f * o.q[0];
		o.p[1] = 0.12f - 0.09f * t;
		o.p[2] = 0.55f + 0.45f * t;
		o.p[3] = (r == 0 || mini) ? 1.f : 0.f;
		// 両脇スリット：前（1）と後ろ（2）を一貫した脇の角度で分け、スリット上端での段差を作らない。
		if (topBoth <= M && r > 0) {
			const BOOL isFront = fabsf(th) > gapOf(r, th < 0.f ? 0 : 1);
			if (isFront) {
				o.q[2] = 1.f;
				o.q[3] = 1.f;
			} else {
				o.q[2] = 2.f + fmaxf(y0 - ySlit, 0.f);
				o.q[3] = Smooth01((float)(r - topBoth + 1) / 2.f);
			}
		}
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
		int nq[4] = { r > 0 ? q - K : -1, r < M ? q + K : -1, r * K + (s + K - 1) % K, r * K + (s + 1) % K };
		if (cutBetween(r, s, (s + K - 1) % K)) nq[2] = -1;
		if (cutBetween(r, s, (s + 1) % K)) nq[3] = -1;
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
	// 頂点の間の三角形の面が脚の丸みに食い込まない分だけ脚から離す。筒の中へ沈むのを許すと太ももで肌が出る。
	auto allowOf = [&](float c, float e) {
		(void)c;
		const float r = 0.08f, h = fminf(e * 0.5f, r);
		const float margin = Clampf(r - sqrtf(r * r - h * h), 0.008f, 0.028f);
		return -margin;
	};
	for (int q = 0; q < NN; q++) nodes[q].q[1] = allowOf(nodes[q].q[1], span / (float)M);

	// 脚の骨（太ももから下）。袖から出た腕も同じ印を付ける。
	BYTE legJ[256] = {};
	for (int j = 0; j < md.nJ && j < 256; j++) {
		for (int a = j, g = 0; a >= 0 && a < md.nJ && g < 64; a = md.parent[a], g++) {
			if (a == jUL[0] || a == jUL[1]) { legJ[j] = 1; break; }
		}
		for (int a = j, g = 0; a >= 0 && a < md.nJ && g < 64; a = md.parent[a], g++) {
			if (a == jUA[0] || a == jUA[1]) { armBone[j] = 1; break; }
		}
	}
	// 腰より下のスカート頂点に腕・手の骨が混ざっていると、腕を振ったときにその頂点だけ外れてメッシュが裂ける。腰の骨へ移す。
	for (int i = 0; i < n; i++) {
		if (!isSk[i]) continue;
		Renderer::CSkinV& s = V[nb + i];
		if (s.p[1] > y0 + 0.02f) continue;
		for (int c = 0; c < 4; c++) {
			if (!s.w[c] || s.j[c] >= 256 || !armBone[s.j[c]]) continue;
			int exist = -1;
			for (int k = 0; k < 4; k++) if (k != c && s.j[k] == (unsigned char)jHips && s.w[k]) exist = k;
			if (exist >= 0) {
				int sum = (int)s.w[exist] + (int)s.w[c];
				s.w[exist] = (unsigned char)(sum > 255 ? 255 : sum);
				s.w[c] = 0;
			} else s.j[c] = (unsigned char)jHips;
		}
	}
	// 腰のすぐ下から、スキニングしたドレスに格子のたるみを足す。足の骨の重みは残す。
	for (int i = 0; i < n; i++) {
		if (!isSk[i]) continue;
		const Renderer::CSkinV& s = V[nb + i];
		int aw = 0;
		for (int c = 0; c < 4; c++) if (s.w[c] && s.j[c] < 256 && armBone[s.j[c]]) aw += s.w[c];
		if (aw >= 96 && s.p[1] > y0 - 0.02f) continue;
		const float m = Smooth01((y0 + 0.01f - s.p[1]) / 0.05f);
		if (m <= 0.001f) continue;
		ClothGpuV o;
		memset(&o, 0, sizeof(o));
		int id[4];
		lattice(s.p, id, o.w);
		slitFix(s.p, id, o.w);
		// a.w の下 2bit は前 1・後ろ 2、次の 8bit は重み、上の 16bit はスリット上端までの丈（mm）。
		if (topBoth <= M) {
			const float ang = atan2f(s.p[0] - cx, s.p[2] - cz);
			const int r = Clampi((int)floorf((y0 - s.p[1]) / span * (float)M + 0.5f), 0, M);
			const unsigned lab = fabsf(ang) > gapOf(r, ang < 0.f ? 0 : 1) ? 1u : 2u;
			const float pw = lab == 1u ? Smooth01((y0 - s.p[1]) / 0.12f) : Smooth01((ySlit - s.p[1]) / 0.14f);
			const unsigned slitMm = (unsigned)Clampi((int)(fmaxf(y0 - ySlit, 0.f) * 1000.f + 0.5f), 0, 65535);
			o.a[3] = lab | ((unsigned)Clampi((int)(pw * 255.f + 0.5f), 0, 255) << 8) | (slitMm << 16);
		}
		o.a[0] = (unsigned)(nb + i);
		o.a[1] = (unsigned)id[0] | ((unsigned)id[1] << 16);
		o.a[2] = (unsigned)id[2] | ((unsigned)id[3] << 16);
		memcpy(o.p, s.p, 12);
		o.p[3] = m;
		memcpy(o.n, s.n, 12);
		const float rj = 0.08f, hj = fminf(edge[root[i]] * 0.5f, rj);
		o.n[3] = -Clampf(rj - sqrtf(rj * rj - hj * hj), 0.008f, 0.028f);
		verts.push_back(o);
	}
	if (verts.size() < 16) { nodes.clear(); verts.clear(); nc.clothN = nc.clothVN = 0; nc.segN = 0; if (!sewn) return; }

	// 脚の素体。靴・タイツ・スカートのどれも 8mm 以内に無ければ見せてよい（ぴったりの所は布が脚に付いていくので隠したまま）。
	expose.fill(nb, 0);
	if (expose.size() == nb) {
		const float CELL = 0.02f;
		auto cellOf = [&](const float* p, int dx, int dy, int dz) -> unsigned {
			int x = (int)floorf(p[0] / CELL) + dx + 512, y = (int)floorf(p[1] / CELL) + dy + 512, z = (int)floorf(p[2] / CELL) + dz + 512;
			return (unsigned)(x & 1023) | ((unsigned)(y & 1023) << 10) | ((unsigned)(z & 1023) << 20);
		};
		auto sleeveW = [&](int i) -> int {
			int aw = 0;
			const Renderer::CSkinV& cv = V[nb + i];
			for (int c = 0; c < 4; c++) if (cv.w[c] && cv.j[c] < 256 && armBone[cv.j[c]]) aw += cv.w[c];
			return aw;
		};
		Buf<unsigned long long> ck;
		ck.arena = 1;
		for (int i = 0; i < n; i++) if (root[i] == i) ck.push_back(((unsigned long long)cellOf(V[nb + i].p, 0, 0, 0) << 32) | (unsigned)i);
		const int nk = ck.size();
		if (nk > 0) qsort(ck.data(), nk, sizeof(unsigned long long), CmpU64);
		auto rayCover = [&](const float* p, float nx, float ny, float nz, float lim, BOOL wantArm) -> BOOL {
			for (float t = 0.002f; t <= lim; t += 0.003f) {
				const float q[3] = { p[0] + nx * t, p[1] + ny * t, p[2] + nz * t };
				for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
					const unsigned long long cell = cellOf(q, dx, dy, dz);
					int lo = 0, hi = nk;
					while (lo < hi) { int m = (lo + hi) >> 1; if ((ck[m] >> 32) < cell) lo = m + 1; else hi = m; }
					for (int u = lo; u < nk && (ck[u] >> 32) == cell; u++) {
						const int i = (int)(ck[u] & 0xFFFFFFFFu);
						const float* b = V[nb + i].p;
						const float d2 = (b[0] - q[0]) * (b[0] - q[0]) + (b[1] - q[1]) * (b[1] - q[1]) + (b[2] - q[2]) * (b[2] - q[2]);
						if (d2 < 0.005f * 0.005f) {
							const int aw = sleeveW(i);
							if (wantArm ? aw >= 96 : aw < 180) return TRUE;
						}
					}
				}
			}
			return FALSE;
		};
		for (int k = 0; k < nb; k++) {
			const int d = DomJoint(V[k]);
			const BOOL isLeg = (d == jUL[0] || d == jUL[1] || d == jLL[0] || d == jLL[1] || d == jFt[0] || d == jFt[1]);
			const BOOL isArm = d >= 0 && d < 256 && armBone[d];
			if (!isLeg && !isArm) continue;
			const float* p = V[k].p;
			// 股のすぐ下は素体のまま隠す。それより下の太ももとすねは消さない。
			// 丈でまとめて隠すと、布が横へずれても足のメッシュが戻らず、スカートに食い込まれて見える。
			if (isLeg) {
				if (d == jFt[0] || d == jFt[1]) {
					if (p[1] > hipY - 0.06f) continue;
				} else {
					expose[k] = 2;
					continue;
				}
			}
			int lw = 0;
			const BYTE* bone = isLeg ? legJ : armBone;
			for (int c = 0; c < 4; c++) if (V[k].w[c] && V[k].j[c] < 256 && bone[V[k].j[c]]) lw += V[k].w[c];
			if (lw < 180) continue;
			float nx = V[k].n[0], ny = V[k].n[1], nz = V[k].n[2];
			float nl = sqrtf(nx * nx + ny * ny + nz * nz);
			BOOL hit = FALSE;
			if (nl > 1e-5f) {
				nx /= nl; ny /= nl; nz /= nl;
				if (isArm) {
					int s = (d == jUA[1] || d == jLA[1] || d == jHd[1]) ? 1 : 0;
					const float* A = (d == jLA[s] || d == jHd[s]) ? pLA[s] : pUA[s];
					const float* B = (d == jLA[s] || d == jHd[s]) ? pHd[s] : pLA[s];
					float ax = B[0] - A[0], ay = B[1] - A[1], az = B[2] - A[2];
					float aL = sqrtf(ax * ax + ay * ay + az * az);
					int nHit = 0;
					if (aL > 1e-5f) {
						ax /= aL; ay /= aL; az /= aL;
						const float dn = nx * ax + ny * ay + nz * az;
						float px = nx - ax * dn, py = ny - ay * dn, pz = nz - az * dn;
						float pL = sqrtf(px * px + py * py + pz * pz);
						if (pL > 1e-5f) {
							px /= pL; py /= pL; pz /= pL;
							const float bx = ay * pz - az * py, by = az * px - ax * pz, bz = ax * py - ay * px;
							const float dir[4][3] = { { px, py, pz }, { bx, by, bz }, { -px, -py, -pz }, { -bx, -by, -bz } };
							for (int e = 0; e < 4; e++)
								if (rayCover(p, dir[e][0], dir[e][1], dir[e][2], 0.030f, TRUE)) nHit++;
						}
					}
					hit = nHit >= 3;
				} else hit = rayCover(p, nx, ny, nz, 0.008f, FALSE);
			}
			expose[k] = hit ? 1 : 2;
		}
		for (int k = 0; k < nb; k++) {
			if (expose[k]) continue;
			const int d = DomJoint(V[k]);
			if (d == jHead) continue;
			const float* p = V[k].p;
			if (p[1] < hipY - 0.08f) continue;
			const float limT = 0.012f;
			float nx = V[k].n[0], ny = V[k].n[1], nz = V[k].n[2];
			float nl = sqrtf(nx * nx + ny * ny + nz * nz);
			BOOL hit = FALSE;
			if (nl > 1e-5f) {
				nx /= nl; ny /= nl; nz /= nl;
				for (float t = 0.002f; t <= limT && !hit; t += 0.003f) {
					const float q[3] = { p[0] + nx * t, p[1] + ny * t, p[2] + nz * t };
					for (int dz = -1; dz <= 1 && !hit; dz++) for (int dy = -1; dy <= 1 && !hit; dy++) for (int dx = -1; dx <= 1; dx++) {
						const unsigned long long cell = cellOf(q, dx, dy, dz);
						int lo = 0, hi = nk;
						while (lo < hi) { int m = (lo + hi) >> 1; if ((ck[m] >> 32) < cell) lo = m + 1; else hi = m; }
						for (int u = lo; u < nk && (ck[u] >> 32) == cell; u++) {
							const int i = (int)(ck[u] & 0xFFFFFFFFu);
							const float* b = V[nb + i].p;
							const float d2 = (b[0] - q[0]) * (b[0] - q[0]) + (b[1] - q[1]) * (b[1] - q[1]) + (b[2] - q[2]) * (b[2] - q[2]);
							if (d2 < 0.005f * 0.005f) {
								if (sleeveW(i) < 180) { hit = TRUE; break; }
							}
						}
					}
				}
			}
			expose[k] = hit ? 1 : 2;
		}
	}
	// 高さ 13 段は ProfR と同じ式で先に補間する。当たる布は角度の 2 点だけ読む。
	// 並びは omake_tactics_cloth.hlsl の RAD0。胴 16 の直後、足 4 × 13 × PF、最後にその最大。
	prof.reserve(Renderer::CLOTH_SEGS * PN + 16 + 4 * 13 * PF + 4);
	for (int i = 0; i < 16; i++) prof.push_back(nc.torsoR[i]);
	float radMax[4] = { 0, 0, 0, 0 };
	for (int s = 0; s < 4; s++) {
		for (int k = 0; k < 13; k++) {
			const float t = (float)k / 12.f;
			float ft = (t + 0.08f) / 1.16f;
			if (ft < 0.f) ft = 0.f;
			if (ft > 1.f) ft = 1.f;
			ft *= (float)(PT - 1);
			int t0 = (int)ft;
			if (t0 > PT - 2) t0 = PT - 2;
			if (t0 < 0) t0 = 0;
			const float a = ft - (float)t0;
			for (int p = 0; p < PF; p++) {
				const float r0 = prof[s * PN + t0 * PF + p];
				const float r1 = prof[s * PN + (t0 + 1) * PF + p];
				const float r = r0 + (r1 - r0) * a;
				if (r > radMax[s]) radMax[s] = r;
				prof.push_back(r);
			}
		}
	}
	for (int s = 0; s < 4; s++) prof.push_back(radMax[s]);
	if (sewn) { nodes.clear(); verts.clear(); nc.clothN = 0; nc.clothVN = 0; }
	else { nc.clothN = NN; nc.clothVN = verts.size(); }
	nc.clothHip = jHips;
	nc.clothAxis[0] = cx;
	nc.clothAxis[1] = cz;
	nc.clothY0 = y0;
}

// 髪の根は、今描く素体の頭と同じバインド位置に置く。カタログ側の頭蓋は素体より低いことがあり、
// そこへ髪を載せると根が顔へ残り、頭頂が禿げる。
static void MeasureHostSkull(Renderer::CreatureSkin& skin, int headJ, float neckY)
{
	skin.crownOk = 0;
	skin.crown[0] = skin.crown[1] = skin.crown[2] = 0.f;
	skin.skullC[0] = skin.skullC[1] = skin.skullC[2] = 0.f;
	memset(skin.skullR, 0, sizeof(skin.skullR));
	if (headJ < 0) return;
	const int nvb = (int)skin.bind.size();
	if (nvb < 8) return;
	const int nmh = (int)skin.meshHair.size();
	float yTop = -1e9f;
	for (int i = 0; i < nvb; i++) {
		const Renderer::CSkinV& v = skin.bind[i];
		if (i < nmh && skin.meshHair[i]) continue;
		int best = 0, bw = -1;
		for (int c = 0; c < 4; c++) if ((int)v.w[c] > bw) { bw = (int)v.w[c]; best = (int)v.j[c]; }
		if (best == headJ && bw >= 200 && v.p[1] > yTop) yTop = v.p[1];
	}
	if (yTop < 0.4f) return;
	double sx = 0, sz = 0;
	int n = 0;
	for (int i = 0; i < nvb; i++) {
		const Renderer::CSkinV& v = skin.bind[i];
		if (i < nmh && skin.meshHair[i]) continue;
		int best = 0, bw = -1;
		for (int c = 0; c < 4; c++) if ((int)v.w[c] > bw) { bw = (int)v.w[c]; best = (int)v.j[c]; }
		if (best != headJ || bw < 200 || v.p[1] < yTop - 0.035f) continue;
		sx += v.p[0]; sz += v.p[2]; n++;
	}
	if (n < 1) return;
	skin.crown[0] = (float)(sx / n);
	skin.crown[1] = yTop;
	skin.crown[2] = (float)(sz / n);
	skin.crownOk = 1;
	if (neckY < 0.f) neckY = yTop - 0.30f;
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
	if (!(x0 < x1 && z0 < z1)) return;
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
		if (r < 1e-5f || r > 0.20f) continue;
		float s = dy / r;
		float el = asinf(s < -1.f ? -1.f : (s > 1.f ? 1.f : s));
		int ia = (int)floorf((atan2f(dx, dz) + 3.14159265f) * (SKULL_NA / 6.2831853f));
		int ie = (int)floorf((el + 0.6f) * (SKULL_NE / 2.1707963f));
		ia = ((ia % SKULL_NA) + SKULL_NA) % SKULL_NA;
		if (ie < 0 || ie >= SKULL_NE) continue;
		float& R = skin.skullR[ie * SKULL_NA + ia];
		if (r > R) R = r;
	}
	for (int pass = 0; pass < 6; pass++) for (int ie = 0; ie < SKULL_NE; ie++) for (int ia = 0; ia < SKULL_NA; ia++) {
		float& R = skin.skullR[ie * SKULL_NA + ia];
		if (R > 0.02f) continue;
		float best = R;
		best = fmaxf(best, skin.skullR[ie * SKULL_NA + (ia + 1) % SKULL_NA]);
		best = fmaxf(best, skin.skullR[ie * SKULL_NA + (ia + SKULL_NA - 1) % SKULL_NA]);
		if (ie > 0) best = fmaxf(best, skin.skullR[(ie - 1) * SKULL_NA + ia]);
		if (ie < SKULL_NE - 1) best = fmaxf(best, skin.skullR[(ie + 1) * SKULL_NA + ia]);
		R = best;
	}
	for (int pass = 0; pass < 2; pass++) for (int ie = 0; ie < SKULL_NE; ie++) for (int ia = 0; ia < SKULL_NA; ia++) {
		float& R = skin.skullR[ie * SKULL_NA + ia];
		float nb = R;
		nb = fmaxf(nb, skin.skullR[ie * SKULL_NA + (ia + 1) % SKULL_NA]);
		nb = fmaxf(nb, skin.skullR[ie * SKULL_NA + (ia + SKULL_NA - 1) % SKULL_NA]);
		if (ie > 0) nb = fmaxf(nb, skin.skullR[(ie - 1) * SKULL_NA + ia]);
		if (ie + 1 < SKULL_NE) nb = fmaxf(nb, skin.skullR[(ie + 1) * SKULL_NA + ia]);
		if (nb > 0.05f && R < nb - 0.025f) R = nb - 0.025f;
	}
}

BOOL Renderer::DrawAvatar(int model, int avatar, int cup, const Pose& pose, const M4& world, const DrawParams& dp, const void* owner)
{
	m_avaHide = 0;
	if (model < 0 || model >= (int)models.size() || avatar <= 0 || m_avaSegN <= 0) return FALSE;
	const Model& md = models[model];
	const AvaModel* am = NULL;
	for (const AvaModel& x : m_avaModels) if (!strcmp(x.name, md.name)) { am = &x; break; }
	// 区間行列を持たない素体（冒険者・王・中世・魔女）にも、同じ性別の 0 系の区間行列を借りて同じ手順で着せる。
	// 骨の並びは違うが、関節は名前で引くので区間番号のまま通る。
	if (!am) {
		const BOOL fem = strncmp(md.name, "HumF", 4) == 0;
		const BOOL mal = strncmp(md.name, "HumM", 4) == 0;
		if (fem || mal) {
			const char* ref = fem ? "HumF0" : "HumM0";
			for (const AvaModel& x : m_avaModels) if (!strcmp(x.name, ref)) { am = &x; break; }
		}
	}
	if (!am || md.rig < 0 || md.rig >= (int)rigs.size()) return FALSE;
	int ci = -1;
	for (int k = 0, n = 0; k < (int)m_avaCos.size(); k++) if (m_avaCos[k].gender == am->gender && ++n == avatar) { ci = k; break; }
	if (ci < 0) return FALSE;
	if (!EnsureAvaCos(m_avaCos[ci])) return FALSE;
	// 胸の差分が無い体は大きさで形が変わらないので、一つだけ作る。男性は胸板の厚み（筋肉の量）で作り分ける。
	const BOOL maleAva = !strncmp(md.name, "HumM", 4);
	const int cupK = ((!m_avaBody[am->gender].m.dmax.empty() && !m_avaCos[ci].m.dmax.empty()) || maleAva) ? Clampi(cup, 0, BUST_N - 1) : 0;
	AvaCache* ac = NULL;
	for (AvaCache& x : m_avaCache) if (x.model == model && x.cos == ci && x.cup == cupK) { ac = &x; break; }
	if (!ac) {
		// 同じ体・同じカップの衣装は残す。別の体やカップを先に外し、arena が溢れたときだけ全部捨てる。
		auto holdBare = [&]() -> BOOL {
			m_avaHide = 1;
			m_avaLoading = 1;
			if (m_avaPct < 0) m_avaPct = 0;
			return FALSE;
		};
		int cap = 40;
		// 作る前の1フレームだけ体を出さない。待ちは衣装ごと。他のキャラが上書きすると、読み込み中が降りない。
		int slot = -1, freeSlot = -1;
		for (int w = 0; w < 32; w++) {
			if (m_avaWait[w].frame < 0) { if (freeSlot < 0) freeSlot = w; continue; }
			if (m_avaWait[w].model == model && m_avaWait[w].cos == ci && m_avaWait[w].cup == cupK) slot = w;
		}
		const BOOL armed = slot >= 0 && m_avaWait[slot].frame != m_frame;
		if (!armed) {
			if (m_avaPct < 1) m_avaPct = 1;
			if (slot < 0) slot = freeSlot >= 0 ? freeSlot : 0;
			m_avaWait[slot].model = model;
			m_avaWait[slot].cos = ci;
			m_avaWait[slot].cup = cupK;
			m_avaWait[slot].frame = m_frame;
			return holdBare();
		}
		struct PlateHold {
			PlateHold(Renderer* r) { g_avaPlate = r; }
			~PlateHold() { g_avaPlate = NULL; }
		} plateHold(this);
		if (TacAvaBad()) { g_avaNeedReset = 1; return holdBare(); }
		while ((int)m_avaCache.size() >= cap) {
			int drop = -1;
			for (int i = 0; i < (int)m_avaCache.size(); i++) if (m_avaCache[i].model != model) { drop = i; break; }
			if (drop < 0) for (int i = 0; i < (int)m_avaCache.size(); i++) if (m_avaCache[i].cup != cupK) { drop = i; break; }
			if (drop < 0) { g_avaNeedReset = 1; return holdBare(); }
			ReleaseAvaEntry(m_avaCache[drop]);
			m_avaCache.eraseAt(drop);
		}
		if (g_avaNeedReset) return holdBare();
		FlashAvaLocal(8);
		const AvaBody& body = m_avaBody[am->gender];
		const AvaCos& cos = m_avaCos[ci];
		const Rig& rg = rigs[md.rig];
		const int ns = m_avaSegN;
		int segJ[64] = {};
		if (ns > 64) { g_avaNeedReset = 1; return holdBare(); }
		// 1=腕・手（胸の減衰にも使う）2=肩（形は触らない。減衰に入れるとデコルテが死ぬ）。
		BYTE armJ[256] = {};
		for (int s = 0; s < ns; s++) {
			int j = rg.Find(s < am->nJoint ? am->joint[s] : m_avaSeg[s]);
			segJ[s] = j >= 0 ? j : (md.hipsJ >= 0 ? md.hipsJ : 0);
			static const char* kArm[] = { "arm", "hand", "index", "middle", "ring", "pinky", "thumb" };
			char lo[24];
			int q = 0;
			for (; q < 23 && m_avaSeg[s][q]; q++) lo[q] = (char)tolower((unsigned char)m_avaSeg[s][q]);
			lo[q] = 0;
			if (s < 256 && strstr(lo, "shoulder")) armJ[s] = 2;
			else for (const char* k : kArm) if (s < 256 && strstr(lo, k)) { armJ[s] = 1; break; }
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
		static Buf<float> bx, bn;
		bx.clear(); bn.clear();
		CreatureSkin& sk = nc.skin;
		sk.model = model;
		// 顔検出が途中で戻っても、範囲が未初期化のまま残らないようにする。残すと男性の頭で描画が落ちる。
		sk.faceSet = -1;
		sk.faceStart = 0;
		sk.faceN = 0;
		sk.faceSynth = 0;
		sk.faceGot = -1;
		const size_t nb = body.m.v.size(), nv = nb + cos.m.v.size();
		const int maxCap = 65000;
		sk.bind.reserve(maxCap);
		sk.cloth.reserve(maxCap);
		sk.bind.resize(nv);
		sk.cloth.resize(nv);
		const BOOL hasBust = !body.m.dmax.empty() && !cos.m.dmax.empty();
		if (hasBust) { nc.jig.reserve(maxCap * 3); bx.resize(nv * 3); bn.resize(nv * 3); }
		else if (maleAva) nc.jig.reserve(maxCap * 3);
		if ((size_t)sk.bind.size() < nv || (size_t)sk.cloth.size() < nv || (hasBust && ((size_t)bx.size() < nv * 3 || (size_t)bn.size() < nv * 3))) {
			g_avaNeedReset = 1;
			return holdBare();
		}
		const size_t avaScratchMark = TacAvaMark();
		struct AvaScratchGuard {
			size_t mark;
			AvaScratchGuard(size_t m) : mark(m) {}
			~AvaScratchGuard() { TacAvaRewind(mark); }
		} avaScratch(avaScratchMark);
		int headSeg = -1;
		float headOpen = 1.f;
		float hjc[3] = {};
		BOOL headFit = FALSE;
		for (int s = 0; s < ns; s++) if (!strcmp(m_avaSeg[s], "Head")) { headSeg = s; break; }
		if (headSeg >= 0) {
			const float* H = &am->M[(size_t)headSeg * 12];
			const float lx = sqrtf(H[0] * H[0] + H[4] * H[4] + H[8] * H[8]);
			const float ly = sqrtf(H[1] * H[1] + H[5] * H[5] + H[9] * H[9]);
			const float lz = sqrtf(H[2] * H[2] + H[6] * H[6] + H[10] * H[10]);
			const float headSc = (lx + ly + lz) / 3.f;
			if (headSc > 0.40f && headSc < 0.96f) {
				headOpen = 0.96f / headSc;
				if (headOpen > 1.14f) headOpen = 1.14f;
			}
			const int hj = segJ[headSeg];
			if (hj >= 0 && hj < md.nJ) {
				const M4 hb = MInverse(md.invBind[hj]);
				hjc[0] = hb.m[12]; hjc[1] = hb.m[13]; hjc[2] = hb.m[14];
				headFit = TRUE;
			}
		}
		// 胸は HumF0 のバインドで作る。F1/F2 へ先に載せると胸が潰れる。関節番号は区間のまま残す。
		const AvaModel* amShape = am;
		if (hasBust) {
			for (const AvaModel& x : m_avaModels) if (!strcmp(x.name, "HumF0")) { amShape = &x; break; }
		}
		for (size_t k = 0; k < nv; k++) {
			if ((k & 63) == 0) NoteAvaPhase(0, 5, (int)k, (int)nv);
			const BOOL isBody = k < nb;
			const AvaMesh& src = isBody ? body.m : cos.m;
			const size_t q = isBody ? k : k - nb;
			const CSkinV& s = src.v[q];
			float A[12] = {};
			for (int c = 0; c < 4; c++) {
				float w = s.w[c] / 255.f;
				if (w <= 0.f || s.j[c] >= ns) continue;
				const float* Ms = &amShape->M[(size_t)s.j[c] * 12];
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
			for (int c = 0; c < 4; c++) { d.j[c] = s.j[c]; d.w[c] = s.w[c]; }
			sk.cloth[k] = src.cloth[q];
			if (hasBust) {
				lin(&src.dmax[q * 3], &bx[k * 3]);
				lin(&src.dmin[q * 3], &bn[k * 3]);
			}
		}
		const CreatureSkin* own = NULL;
		BOOL mhFace = TRUE;
		for (const CreatureSkin& hs : m_skins) if (hs.model == model) {
			if (!own && hs.headTri.size() >= 9) own = &hs;
		}
		// 共通素体（body）はもともと 0 番〜nFace-1 番に溶接済みの頭を持っていて、首の輪と頂点も色も法線も一続きになっている。
		// ここで頭を切り落として素体側の頭を継ぎ直すと、首と顎の境で三角形が全て消えて穴と黒い筋になる。
		int nAdd = 0;
		// 胸の周りの体と服の三角形を細かく割る。並びは [体][体の新][服][服の新]（元の体の番号は変わらない）。
		static Buf<int> bOff;
		static Buf<WORD> bTri, cTri;
		BustRef br;
		int nbA = (int)nb, nvA = (int)nv;
		{
			const int maxAdd = 65000 - (int)nv;
			FlashAvaLocal(28);
			if (!RefineBust(sk, bx, bn, (int)nb, (int)nv, body.bodyTri, cos.keep, cos.tri, armJ, maxAdd, bOff, bTri, cTri, nbA, nvA, br)) {
				g_avaNeedReset = 1;
				return holdBare();
			}
		}
		const BOOL graft = FALSE;
		int faceBase = 0;
		(void)mhFace; (void)own; (void)nAdd; (void)faceBase;
		static Buf<WORD> idx;
		idx.clear();
		idx.reserve(bTri.size() + body.extraTri.size() + cTri.size());
		for (size_t t = 0; t * 3 < body.bodyTri.size(); t++) {
			if (t / 8 >= cos.keep.size() || !((cos.keep[t / 8] >> (t & 7)) & 1)) continue;
			for (int ch = bOff[(int)t]; ch < bOff[(int)t + 1]; ch++) {
				int a = bTri[ch * 3], b = bTri[ch * 3 + 1], c = bTri[ch * 3 + 2];
				idx.push_back((WORD)a); idx.push_back((WORD)b); idx.push_back((WORD)c);
			}
		}
		for (size_t k = 0; k < body.extraTri.size(); k++) idx.push_back(body.extraTri[k]);
		for (WORD w : cTri) idx.push_back((WORD)(w + nbA));
		static Buf<float> pF0;
		pF0.clear();
		int chestSeg = -1, neckSeg = -1;
		for (int s = 0; s < ns; s++) {
			if (!strcmp(m_avaSeg[s], "Chest")) chestSeg = s;
			else if (!strcmp(m_avaSeg[s], "Neck")) neckSeg = s;
		}
		if (hasBust && amShape != am) {
			pF0.resize(nvA * 3);
			if (pF0.size() < nvA * 3) { g_avaNeedReset = 1; return holdBare(); }
			for (int k = 0; k < nvA; k++) for (int a = 0; a < 3; a++) pF0[k * 3 + a] = sk.bind[k].p[a];
		}
		if (hasBust) {
			// 0=A〜7=H。形の寸法は kBustW などに。
			const int nAll = (int)sk.bind.size();
			nc.jig.resize(nAll * 3);
			if (nc.jig.size() < nAll * 3 || bn.size() < nAll * 3) { g_avaNeedReset = 1; return holdBare(); }
			FlashAvaLocal(52);
			ShapeCup(sk.bind.data(), nAll, nbA, nvA, bn.data(), cupK, br,
				bTri.data(), bTri.size() / 3, armJ, idx.data(), (int)idx.size(), nc.jig.data());
		} else if (maleAva && chestSeg >= 0 && chestSeg < ns && segJ[chestSeg] >= 0 && segJ[chestSeg] < md.nJ) {
			// 男性の胸板。0〜7 は筋肉の量で、未設定の 2 は形を変えない。胸の関節の前の左右の大胸筋に、体と服を同じだけ前へ盛る。
			// 1/2 系は区間行列で体が横へ寄っているので、中心 C は骨ではなく体の Chest 頂点から取る。
			static const float kPec[BUST_N] = { -0.004f, -0.002f, 0.f, 0.005f, 0.010f, 0.016f, 0.022f, 0.030f };
			const float depth = kPec[Clampi(cupK, 0, BUST_N - 1)];
			float cxSum = 0.f, cySum = 0.f, czLo = 1e9f, czHi = -1e9f;
			int nCh = 0;
			for (int k = 0; k < nbA; k++) {
				const CSkinV& d = sk.bind[k];
				if (DomJoint(d) != chestSeg) continue;
				cxSum += d.p[0]; cySum += d.p[1];
				if (d.p[2] < czLo) czLo = d.p[2];
				if (d.p[2] > czHi) czHi = d.p[2];
				nCh++;
			}
			const float C[3] = { nCh > 8 ? cxSum / (float)nCh : 0.f, nCh > 8 ? cySum / (float)nCh : 1.30f, nCh > 8 ? 0.5f * (czLo + czHi) : 0.f };
			// 左右の胸のいちばん前の点を、胸の骨に付いた体から探す。
			float P[2][3] = { { C[0] - 0.075f, C[1], C[2] - 0.10f }, { C[0] + 0.075f, C[1], C[2] - 0.10f } };
			BOOL got[2] = { FALSE, FALSE };
			for (int k = 0; k < nbA; k++) {
				const CSkinV& d = sk.bind[k];
				if (DomJoint(d) != chestSeg) continue;
				const float ax = fabsf(d.p[0] - C[0]);
				if (ax < 0.040f || ax > 0.12f || d.p[1] < C[1] - 0.08f || d.p[1] > C[1] + 0.14f || d.p[2] > C[2] - 0.03f) continue;
				const int s = d.p[0] < C[0] ? 0 : 1;
				if (!got[s] || d.p[2] < P[s][2]) { P[s][0] = d.p[0]; P[s][1] = d.p[1]; P[s][2] = d.p[2]; got[s] = TRUE; }
			}
			if (got[0] && got[1]) {
				const float py = 0.5f * (P[0][1] + P[1][1]);
				const float px = Clampf(0.5f * (P[1][0] - P[0][0]), 0.060f, 0.095f);
				const float mass = 0.020f + (depth > 0.f ? depth : 0.f);
				const int nAllM = (int)sk.bind.size();
				nc.jig.fill(nAllM * 3, 0.f);
				if (nc.jig.size() == nAllM * 3) {
					for (int k = 0; k < nAllM; k++) {
						CSkinV& d = sk.bind[k];
						int aw = 0;
						for (int c = 0; c < 4; c++) if (d.w[c] && d.j[c] < 256 && armJ[d.j[c]] == 1) aw += d.w[c];
						const float wArm = 1.f - Smooth01((float)aw / 120.f);
						const float wz = Smooth01((C[2] - 0.02f - d.p[2]) / 0.05f) * wArm;
						if (wz <= 0.f) continue;
						const int s = d.p[0] >= C[0] ? 1 : 0;
						const float cleav = Smooth01((fabsf(d.p[0] - C[0]) - 0.004f) / 0.018f);
						const float hy = d.p[1] < py ? 0.075f : 0.095f;
						const float ex = (d.p[0] - (C[0] + (s ? px : -px))) / 0.085f, ey = (d.p[1] - py) / hy;
						const float r2 = ex * ex + ey * ey;
						if (r2 >= 1.f || cleav <= 0.f) continue;
						const float q = 1.f - r2;
						const float f = q * q * cleav;
						const float fx = -4.f * q * ex / 0.085f * cleav;
						const float fy = -4.f * q * ey / hy * cleav;
						const float D = depth * f * wz;
						d.p[1] -= D * 0.12f;
						d.p[2] -= D;
						// 盛った面の傾きを法線へ。前を向いた面だけ。
						const float facing = Clampf(-d.n[2], 0.f, 1.f);
						d.n[0] -= depth * wz * fx * facing * 0.65f;
						d.n[1] -= depth * wz * fy * facing * 0.65f;
						const float nl = sqrtf(d.n[0] * d.n[0] + d.n[1] * d.n[1] + d.n[2] * d.n[2]);
						if (nl > 1e-6f) { d.n[0] /= nl; d.n[1] /= nl; d.n[2] /= nl; }
						const float jm = mass * f * wz;
						nc.jig[k * 3] = 0.f;
						nc.jig[k * 3 + 1] = -0.18f * jm;
						nc.jig[k * 3 + 2] = -jm;
					}
				}
			}
		}
		// 体は各バインド、胸の差分は Chest だけで載せる（首ウェイトでデコルテが潰れない）。
		float Lr[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		if (amShape != am && chestSeg >= 0 && chestSeg < ns) {
			const float* C0 = &amShape->M[(size_t)chestSeg * 12];
			const float* Cm = &am->M[(size_t)chestSeg * 12];
			const float det = C0[0] * (C0[5] * C0[10] - C0[6] * C0[9]) - C0[1] * (C0[4] * C0[10] - C0[6] * C0[8]) + C0[2] * (C0[4] * C0[9] - C0[5] * C0[8]);
			if (fabsf(det) > 1e-12f) {
				const float inv = 1.f / det;
				const float i00 = (C0[5] * C0[10] - C0[6] * C0[9]) * inv, i01 = (C0[2] * C0[9] - C0[1] * C0[10]) * inv, i02 = (C0[1] * C0[6] - C0[2] * C0[5]) * inv;
				const float i10 = (C0[6] * C0[8] - C0[4] * C0[10]) * inv, i11 = (C0[0] * C0[10] - C0[2] * C0[8]) * inv, i12 = (C0[2] * C0[4] - C0[0] * C0[6]) * inv;
				const float i20 = (C0[4] * C0[9] - C0[5] * C0[8]) * inv, i21 = (C0[1] * C0[8] - C0[0] * C0[9]) * inv, i22 = (C0[0] * C0[5] - C0[1] * C0[4]) * inv;
				Lr[0] = Cm[0] * i00 + Cm[1] * i10 + Cm[2] * i20; Lr[1] = Cm[0] * i01 + Cm[1] * i11 + Cm[2] * i21; Lr[2] = Cm[0] * i02 + Cm[1] * i12 + Cm[2] * i22;
				Lr[3] = Cm[4] * i00 + Cm[5] * i10 + Cm[6] * i20; Lr[4] = Cm[4] * i01 + Cm[5] * i11 + Cm[6] * i21; Lr[5] = Cm[4] * i02 + Cm[5] * i12 + Cm[6] * i22;
				Lr[6] = Cm[8] * i00 + Cm[9] * i10 + Cm[10] * i20; Lr[7] = Cm[8] * i01 + Cm[9] * i11 + Cm[10] * i21; Lr[8] = Cm[8] * i02 + Cm[9] * i12 + Cm[10] * i22;
			}
		}
		for (int k = 0; k < nvA; k++) {
			if ((k & 63) == 0) NoteAvaPhase(3, 5, k, nvA);
			CSkinV& d = sk.bind[k];
			if (amShape != am) {
				float A0[12] = {}, Am[12] = {};
				for (int c = 0; c < 4; c++) {
					float w = d.w[c] / 255.f;
					if (w <= 0.f || d.j[c] >= ns) continue;
					const float* S0 = &amShape->M[(size_t)d.j[c] * 12];
					const float* Sm = &am->M[(size_t)d.j[c] * 12];
					for (int e = 0; e < 12; e++) { A0[e] += S0[e] * w; Am[e] += Sm[e] * w; }
				}
				const float a00 = A0[0], a01 = A0[1], a02 = A0[2], a10 = A0[4], a11 = A0[5], a12 = A0[6], a20 = A0[8], a21 = A0[9], a22 = A0[10];
				const float det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
				if (fabsf(det) > 1e-12f) {
					const float inv = 1.f / det;
					const float i00 = (a11 * a22 - a12 * a21) * inv, i01 = (a02 * a21 - a01 * a22) * inv, i02 = (a01 * a12 - a02 * a11) * inv;
					const float i10 = (a12 * a20 - a10 * a22) * inv, i11 = (a00 * a22 - a02 * a20) * inv, i12 = (a02 * a10 - a00 * a12) * inv;
					const float i20 = (a10 * a21 - a11 * a20) * inv, i21 = (a01 * a20 - a00 * a21) * inv, i22 = (a00 * a11 - a01 * a10) * inv;
					auto xform = [&](const float* x, float* o, BOOL tr) {
						const float px = x[0] - (tr ? A0[3] : 0.f), py = x[1] - (tr ? A0[7] : 0.f), pz = x[2] - (tr ? A0[11] : 0.f);
						const float rx = i00 * px + i01 * py + i02 * pz, ry = i10 * px + i11 * py + i12 * pz, rz = i20 * px + i21 * py + i22 * pz;
						o[0] = Am[0] * rx + Am[1] * ry + Am[2] * rz + (tr ? Am[3] : 0.f);
						o[1] = Am[4] * rx + Am[5] * ry + Am[6] * rz + (tr ? Am[7] : 0.f);
						o[2] = Am[8] * rx + Am[9] * ry + Am[10] * rz + (tr ? Am[11] : 0.f);
					};
					const float* q0 = (pF0.size() >= (k + 1) * 3) ? &pF0[k * 3] : d.p;
					float pN[3]; xform(q0, pN, TRUE);
					const float dx = d.p[0] - q0[0], dy = d.p[1] - q0[1], dz = d.p[2] - q0[2];
					d.p[0] = pN[0] + Lr[0] * dx + Lr[1] * dy + Lr[2] * dz;
					d.p[1] = pN[1] + Lr[3] * dx + Lr[4] * dy + Lr[5] * dz;
					d.p[2] = pN[2] + Lr[6] * dx + Lr[7] * dy + Lr[8] * dz;
					float n1[3] = { Lr[0] * d.n[0] + Lr[1] * d.n[1] + Lr[2] * d.n[2], Lr[3] * d.n[0] + Lr[4] * d.n[1] + Lr[5] * d.n[2], Lr[6] * d.n[0] + Lr[7] * d.n[1] + Lr[8] * d.n[2] };
					if (dx * dx + dy * dy + dz * dz < 1e-10f) xform(d.n, n1, FALSE);
					float nl = sqrtf(n1[0] * n1[0] + n1[1] * n1[1] + n1[2] * n1[2]);
					if (nl > 1e-6f) { d.n[0] = n1[0] / nl; d.n[1] = n1[1] / nl; d.n[2] = n1[2] / nl; }
					if (hasBust && nc.jig.size() >= (k + 1) * 3) {
						const float* j0 = &nc.jig[k * 3];
						const float jx = Lr[0] * j0[0] + Lr[1] * j0[1] + Lr[2] * j0[2];
						const float jy = Lr[3] * j0[0] + Lr[4] * j0[1] + Lr[5] * j0[2];
						const float jz = Lr[6] * j0[0] + Lr[7] * j0[1] + Lr[8] * j0[2];
						nc.jig[k * 3] = jx; nc.jig[k * 3 + 1] = jy; nc.jig[k * 3 + 2] = jz;
					}
				}
			}
			if (0 && headFit && headOpen > 1.001f && k < (int)nb) {
				int bestS = 0, bestW = -1;
				for (int c = 0; c < 4; c++) if ((int)d.w[c] > bestW) { bestW = (int)d.w[c]; bestS = d.j[c]; }
				if (bestS == headSeg && bestW > 160) {
					const float dz = d.p[2] - hjc[2];
					const float face = 1.f - Smooth01((dz + 0.01f) / 0.05f);
					if (face > 0.02f) {
						const float kOpen = 1.f + (headOpen - 1.f) * face;
						d.p[0] = hjc[0] + (d.p[0] - hjc[0]) * kOpen;
						d.p[2] = hjc[2] + (d.p[2] - hjc[2]) * kOpen;
						if (d.p[1] > hjc[1] - 0.012f)
							d.p[1] = hjc[1] + (d.p[1] - hjc[1]) * (1.f + (kOpen - 1.f) * 0.35f);
					}
				}
			}
		}
		for (int k = 0; k < nvA; k++) {
			if ((k & 63) == 0) NoteAvaPhase(4, 5, k, nvA);
			CSkinV& d = sk.bind[k];
			for (int c = 0; c < 4; c++)
				d.j[c] = (unsigned char)(d.j[c] < ns ? segJ[d.j[c]] : segJ[0]);
		}
		// 1/2 系（HumF1〜6 / HumM1〜6）は 0 系の体を骨へ載せ直してある。手足の骨は肉の芯から数 cm 外れるので、
		// 区間の中ほどの肉の中心へ軸を寄せる。肘と膝は上下の区間の中間、手は骨のまま、足首は脛に合わせる。
		const BOOL fit12 = (!strncmp(md.name, "HumF", 4) || !strncmp(md.name, "HumM", 4)) && md.name[4] >= '1' && md.name[4] <= '9';
		if (fit12) {
			const char* lim[4][3] = {
				{ "UpperArm.L", "LowerArm.L", "Hand.L" }, { "UpperArm.R", "LowerArm.R", "Hand.R" },
				{ "UpperLeg.L", "LowerLeg.L", "Foot.L" }, { "UpperLeg.R", "LowerLeg.R", "Foot.R" }
			};
			for (int l = 0; l < 4; l++) {
				int jj[3];
				float P[3][3], cs[2][3] = {};
				BOOL ok = TRUE;
				for (int e = 0; e < 3 && ok; e++) {
					jj[e] = SegJoint(segJ, m_avaSeg, ns, lim[l][e]);
					if (jj[e] < 0 || jj[e] >= md.nJ || jj[e] >= 64) { ok = FALSE; break; }
					const M4 b = MInverse(md.invBind[jj[e]]);
					P[e][0] = b.m[12]; P[e][1] = b.m[13]; P[e][2] = b.m[14];
				}
				if (!ok) continue;
				for (int sgi = 0; sgi < 2; sgi++) {
					const float* A = P[sgi];
					const float* B = P[sgi + 1];
					const float ab[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
					const float L2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
					if (L2 < 1e-6f) continue;
					int got = 0;
					for (int k = 0; k < nbA; k++) {
						if (DomJoint(sk.bind[k]) != jj[sgi]) continue;
						const float* p = sk.bind[k].p;
						const float t = ((p[0] - A[0]) * ab[0] + (p[1] - A[1]) * ab[1] + (p[2] - A[2]) * ab[2]) / L2;
						if (t < 0.25f || t > 0.75f) continue;
						const float r[3] = { p[0] - A[0] - ab[0] * t, p[1] - A[1] - ab[1] * t, p[2] - A[2] - ab[2] * t };
						if (r[0] * r[0] + r[1] * r[1] + r[2] * r[2] > 0.12f * 0.12f) continue;
						for (int a = 0; a < 3; a++) cs[sgi][a] += r[a];
						got++;
					}
					if (got >= 12) for (int a = 0; a < 3; a++) cs[sgi][a] /= (float)got;
					else for (int a = 0; a < 3; a++) cs[sgi][a] = 0.f;
				}
				for (int a = 0; a < 3; a++) {
					nc.jOff[jj[0]][a] = cs[0][a];
					nc.jOff[jj[1]][a] = 0.5f * (cs[0][a] + cs[1][a]);
					nc.jOff[jj[2]][a] = (l < 2) ? 0.f : cs[1][a];
				}
			}
		}
		// JK とスーツ（アバターなし）は袖が前腕、スカートが腹で腰まで上がる。
		// 共通の乗せ替え・円柱への押し出し・体の法線では面が割れるので、作者の骨と位置のまま描く。ミニスカートの 9cm だけ足す。
		const BOOL sewn = !strcmp(m_avaCos[ci].name, "jk") || !strcmp(m_avaCos[ci].name, "suit");
		// 胸の骨だけの袖は、構えで腕が動くと胴に残る。骨からの距離で連続的に腕の重みを足す。一気に付け替えると面が裂ける。
		// 胸の前面と、足・腰は変えない。
		if (!sewn) {
			const char* seg[4][2] = {
				{ "UpperArm.L", "LowerArm.L" }, { "UpperArm.R", "LowerArm.R" },
				{ "LowerArm.L", "Hand.L" }, { "LowerArm.R", "Hand.R" }
			};
			const char* keepOff[10] = { "Hips", "UpperLeg.L", "UpperLeg.R", "LowerLeg.L", "LowerLeg.R", "Foot.L", "Foot.R", "Head", "Abdomen", "Spine" };
			int offJ[10], jb[4][2];
			float As[4][3], Bs[4][3];
			BOOL okS[4] = {};
			for (int i = 0; i < 10; i++) offJ[i] = SegJoint(segJ, m_avaSeg, ns, keepOff[i]);
			for (int s = 0; s < 4; s++) {
				jb[s][0] = SegJoint(segJ, m_avaSeg, ns, seg[s][0]);
				jb[s][1] = SegJoint(segJ, m_avaSeg, ns, seg[s][1]);
				okS[s] = jb[s][0] >= 0 && jb[s][0] < md.nJ && jb[s][1] >= 0 && jb[s][1] < md.nJ;
				if (!okS[s]) continue;
				const M4 a = MInverse(md.invBind[jb[s][0]]);
				const M4 b = MInverse(md.invBind[jb[s][1]]);
				As[s][0] = a.m[12]; As[s][1] = a.m[13]; As[s][2] = a.m[14];
				Bs[s][0] = b.m[12]; Bs[s][1] = b.m[13]; Bs[s][2] = b.m[14];
				if (jb[s][0] < 64) for (int a = 0; a < 3; a++) As[s][a] += nc.jOff[jb[s][0]][a];
				if (jb[s][1] < 64) for (int a = 0; a < 3; a++) Bs[s][a] += nc.jOff[jb[s][1]][a];
			}
			float hipY = 0.f, cx = 0.f, cz = 0.f;
			int nHip = 0;
			const char* uln[2] = { "UpperLeg.L", "UpperLeg.R" };
			for (int s = 0; s < 2; s++) {
				const int j = SegJoint(segJ, m_avaSeg, ns, uln[s]);
				if (j < 0 || j >= md.nJ) continue;
				const M4 b = MInverse(md.invBind[j]);
				hipY += b.m[13]; cx += b.m[12]; cz += b.m[14];
				nHip++;
			}
			if (nHip == 2) {
				hipY /= 2.f; cx /= 2.f; cz /= 2.f;
				for (int k = nbA; k < nvA; k++) {
					CSkinV& d = sk.bind[k];
					if (d.p[1] < hipY + 0.02f) continue;
					const int dom = DomJoint(d);
					BOOL skirt = FALSE;
					for (int i = 0; i < 10; i++) if (offJ[i] >= 0 && dom == offJ[i]) skirt = TRUE;
					if (skirt) continue;
					int aw = 0;
					for (int c = 0; c < 4; c++) for (int s = 0; s < 4; s++) if (okS[s] && ((int)d.j[c] == jb[s][0] || (int)d.j[c] == jb[s][1])) aw += d.w[c];
					if (aw >= 40) continue;
					float best = 1e9f;
					int pick = -1;
					float pickT = 0.f;
					for (int s = 0; s < 4; s++) {
						if (!okS[s]) continue;
						const float ab[3] = { Bs[s][0] - As[s][0], Bs[s][1] - As[s][1], Bs[s][2] - As[s][2] };
						const float L2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
						if (L2 < 1e-6f) continue;
						float t = ((d.p[0] - As[s][0]) * ab[0] + (d.p[1] - As[s][1]) * ab[1] + (d.p[2] - As[s][2]) * ab[2]) / L2;
						if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
						const float rx = d.p[0] - (As[s][0] + ab[0] * t), ry = d.p[1] - (As[s][1] + ab[1] * t), rz = d.p[2] - (As[s][2] + ab[2] * t);
						const float d2 = rx * rx + ry * ry + rz * rz;
						if (d2 < best) { best = d2; pick = s; pickT = t; }
					}
					const float ad = sqrtf(best);
					if (pick < 0) continue;
					const float dx = d.p[0] - cx, dz = d.p[2] - cz;
					const float td = sqrtf(dx * dx + dz * dz);
					// JK のスカートは腹の骨で、腰の横に下りた腕へ乗っていた。腹は上で除外済み。
					// アジア人は腕が胴に近い。胴の中心より腕に近い布だけ。胸の前面と背中の中央は残す。
					if (ad > td - 0.01f) continue;
					if (dz > 0.02f && fabsf(dx) < 0.10f && d.p[1] < hipY + 0.55f) continue;
					if (dz < -0.03f && fabsf(dx) < 0.11f && d.p[1] > hipY + 0.20f && d.p[1] < hipY + 0.55f) continue;
					float f = 1.f - Smooth01((ad - 0.05f) / 0.06f);
					if (f <= 0.04f) continue;
					const int bone = pick >= 2 ? jb[pick][0] : (pickT < 0.55f ? jb[pick][0] : jb[pick][1]);
					if (bone < 0 || bone > 255) continue;
					int js[4], ws[4];
					for (int c = 0; c < 4; c++) { js[c] = d.j[c]; ws[c] = d.w[c]; }
					int slot = -1, slotW = 256;
					for (int c = 0; c < 4; c++) if (js[c] == bone) slot = c;
					if (slot < 0) {
						for (int c = 0; c < 4; c++) if (ws[c] < slotW) { slotW = ws[c]; slot = c; }
						js[slot] = bone;
						ws[slot] = 0;
					}
					float acc[4], sum = 0.f;
					for (int c = 0; c < 4; c++) { acc[c] = (float)ws[c] * (1.f - f); sum += acc[c]; }
					acc[slot] += 255.f * f;
					sum += 255.f * f;
					if (sum < 1.f) continue;
					int left = 255;
					for (int c = 0; c < 4; c++) {
						int v = (c == 3) ? left : (int)(acc[c] / sum * 255.f + 0.5f);
						if (v < 0) v = 0;
						if (v > left) v = left;
						d.j[c] = (unsigned char)js[c];
						d.w[c] = (unsigned char)v;
						left -= v;
					}
				}
			}
		}
		// 腕を少し太くし、カプセルより内の袖を外へ。軸に潰れた頂点は法線で戻す。
		{
			auto atJ = [&](const char* name, float* o) -> BOOL {
				const int j = SegJoint(segJ, m_avaSeg, ns, name);
				if (j < 0 || j >= md.nJ) return FALSE;
				const M4 b = MInverse(md.invBind[j]);
				o[0] = b.m[12]; o[1] = b.m[13]; o[2] = b.m[14];
				if (j < 64) { o[0] += nc.jOff[j][0]; o[1] += nc.jOff[j][1]; o[2] += nc.jOff[j][2]; }
				return TRUE;
			};
			auto onAxis = [&](const float* A, const float* B, const float* p, float* t, float* ax, float* rx, float* ry, float* rz, float* rl) -> BOOL {
				const float ab[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
				const float L2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
				if (L2 < 1e-8f) return FALSE;
				const float ap[3] = { p[0] - A[0], p[1] - A[1], p[2] - A[2] };
				float tt = (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / L2;
				if (tt < -0.04f || tt > 1.08f) return FALSE;
				if (tt < 0.f) tt = 0.f; else if (tt > 1.f) tt = 1.f;
				*t = tt;
				ax[0] = A[0] + ab[0] * tt; ax[1] = A[1] + ab[1] * tt; ax[2] = A[2] + ab[2] * tt;
				*rx = p[0] - ax[0]; *ry = p[1] - ax[1]; *rz = p[2] - ax[2];
				*rl = sqrtf((*rx) * (*rx) + (*ry) * (*ry) + (*rz) * (*rz));
				return TRUE;
			};
			auto away = [&](const CSkinV& d, const float* A, const float* B, float* rx, float* ry, float* rz, float* rl) -> BOOL {
				if (*rl >= 1e-5f) return TRUE;
				*rx = d.n[0]; *ry = d.n[1]; *rz = d.n[2];
				*rl = sqrtf((*rx) * (*rx) + (*ry) * (*ry) + (*rz) * (*rz));
				if (*rl >= 1e-5f) return TRUE;
				const float ab[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
				*rx = ab[2]; *ry = 0.f; *rz = -ab[0];
				if (d.p[0] < 0.f) { *rx = -*rx; *rz = -*rz; }
				*rl = sqrtf((*rx) * (*rx) + (*ry) * (*ry) + (*rz) * (*rz));
				return *rl >= 1e-5f;
			};
			const char* pair[4][2] = {
				{ "UpperArm.L", "LowerArm.L" }, { "UpperArm.R", "LowerArm.R" },
				{ "LowerArm.L", "Hand.L" }, { "LowerArm.R", "Hand.R" }
			};
			for (int s = 0; s < 4; s++) {
				float A[3], B[3];
				if (!atJ(pair[s][0], A) || !atJ(pair[s][1], B)) continue;
				const int j0 = SegJoint(segJ, m_avaSeg, ns, pair[s][0]);
				const int j1 = SegJoint(segJ, m_avaSeg, ns, pair[s][1]);
				if (j0 < 0) continue;
				const float amt = s < 2 ? 0.006f : 0.004f, pad = s < 2 ? 0.007f : 0.006f;
				for (int k = 0; k < nbA; k++) {
					const int d = DomJoint(sk.bind[k]);
					if (d != j0 && d != j1) continue;
					float t, ax[3], rx, ry, rz, rl;
					if (!onAxis(A, B, sk.bind[k].p, &t, ax, &rx, &ry, &rz, &rl) || rl > 0.08f) continue;
					if (!away(sk.bind[k], A, B, &rx, &ry, &rz, &rl)) continue;
					const float g = amt * Smooth01(t * 4.f) * Smooth01((1.f - t) * 4.f);
					if (g <= 0.f) continue;
					float* p = sk.bind[k].p;
					p[0] += rx / rl * g; p[1] += ry / rl * g; p[2] += rz / rl * g;
				}
				// 太くした腕の形を、軸に沿った段 × まわりの角度で測る。袖は頂点ごとにその形の外へだけ出す。
				// 一つの平均の円柱へ揃えると、力こぶや肩は肌が突き抜け、細い所は袖が波打つ。脇の胴の布まで円柱へ引くと角が立つ。
				enum { PT = 12, PA = 12 };
				float prof[PT][PA];
				for (int a = 0; a < PT; a++) for (int b = 0; b < PA; b++) prof[a][b] = -1.f;
				const float ab[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
				const float abL = sqrtf(ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2]);
				if (abL < 0.05f) continue;
				const float an[3] = { ab[0] / abL, ab[1] / abL, ab[2] / abL };
				float e1[3] = { -an[2] * an[0], -an[2] * an[1], 1.f - an[2] * an[2] };
				{
					const float l = sqrtf(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
					if (l < 1e-4f) continue;
					e1[0] /= l; e1[1] /= l; e1[2] /= l;
				}
				const float e2[3] = { an[1] * e1[2] - an[2] * e1[1], an[2] * e1[0] - an[0] * e1[2], an[0] * e1[1] - an[1] * e1[0] };
				auto binF = [&](float t, float rx, float ry, float rz, float* ft, float* fa) {
					*ft = Clampf((t + 0.04f) / 1.12f, 0.f, 1.f) * (float)(PT - 1);
					const float ang = atan2f(rx * e2[0] + ry * e2[1] + rz * e2[2], rx * e1[0] + ry * e1[1] + rz * e1[2]);
					*fa = (ang / 6.2831853f + 0.5f) * (float)PA;
				};
				for (int k = 0; k < nbA; k++) {
					const int d = DomJoint(sk.bind[k]);
					if (d != j0 && d != j1) continue;
					int wl = 0;
					for (int c = 0; c < 4; c++) if ((int)sk.bind[k].j[c] == j0 || (int)sk.bind[k].j[c] == j1) wl += sk.bind[k].w[c];
					if (wl < 180) continue;
					float t, ax[3], rx, ry, rz, rl;
					if (!onAxis(A, B, sk.bind[k].p, &t, ax, &rx, &ry, &rz, &rl) || rl > 0.08f || rl < 1e-4f) continue;
					float ft, fa;
					binF(t, rx, ry, rz, &ft, &fa);
					const int bt = Clampi((int)(ft + 0.5f), 0, PT - 1), ba = ((int)floorf(fa) % PA + PA) % PA;
					if (rl > prof[bt][ba]) prof[bt][ba] = rl;
				}
				for (int pass = 0; pass < 6; pass++) for (int a = 0; a < PT; a++) for (int b = 0; b < PA; b++) {
					if (prof[a][b] > 0.f) continue;
					float best = -1.f;
					best = fmaxf(best, prof[a][(b + 1) % PA]);
					best = fmaxf(best, prof[a][(b + PA - 1) % PA]);
					if (a > 0) best = fmaxf(best, prof[a - 1][b]);
					if (a < PT - 1) best = fmaxf(best, prof[a + 1][b]);
					prof[a][b] = best;
				}
				float pr[PT][PA];
				for (int a = 0; a < PT; a++) for (int b = 0; b < PA; b++) {
					float m = prof[a][b];
					m = fmaxf(m, prof[a][(b + 1) % PA]);
					m = fmaxf(m, prof[a][(b + PA - 1) % PA]);
					pr[a][b] = m > 0.f ? Clampf(m, 0.020f, 0.075f) : 0.036f;
				}
				for (int k = nbA; k < nvA; k++) {
					// JK とスーツの袖は作者の骨に乗っている。円柱へ押し出すと腕から離れる。
					if (sewn) continue;
					if (k < (int)sk.cloth.size() && !sk.cloth[k]) continue;
					CSkinV& d = sk.bind[k];
					int aw = 0;
					for (int c = 0; c < 4; c++) if ((int)d.j[c] == j0 || (int)d.j[c] == j1) aw += d.w[c];
					const float wA = Smooth01(((float)aw - 40.f) / 140.f);
					if (wA <= 0.f) continue;
					float t, ax[3], rx, ry, rz, rl;
					if (!onAxis(A, B, d.p, &t, ax, &rx, &ry, &rz, &rl)) continue;
					if (rl > 0.16f) continue;
					if (!away(d, A, B, &rx, &ry, &rz, &rl)) continue;
					float ft, fa;
					binF(t, rx, ry, rz, &ft, &fa);
					const int t0 = Clampi((int)ft, 0, PT - 2);
					const float wt = Clampf(ft - (float)t0, 0.f, 1.f);
					const int a0 = ((int)floorf(fa - 0.5f) % PA + PA) % PA;
					const float wa = (fa - 0.5f) - floorf(fa - 0.5f);
					const int a1 = (a0 + 1) % PA;
					const float body = (pr[t0][a0] * (1.f - wa) + pr[t0][a1] * wa) * (1.f - wt) + (pr[t0 + 1][a0] * (1.f - wa) + pr[t0 + 1][a1] * wa) * wt;
					// 肩側の端（脇の下）は胴の布と混ざるので弱める。肩は下の押し出しが体の法線で外へ出す。
					const float wEnd = s < 2 ? Smooth01((t - 0.02f) / 0.14f) : 1.f;
					const float want = body + pad;
					if (rl >= want) continue;
					const float dest = rl + (want - rl) * wA * wEnd;
					const float sc = dest / rl;
					d.p[0] = ax[0] + rx * sc; d.p[1] = ax[1] + ry * sc; d.p[2] = ax[2] + rz * sc;
				}
			}
			// 体の中に沈んだ服を、同じ関節の体の外へ。脇の袖を胴の法線で押さない。
			{
				const int jHeadG = SegJoint(segJ, m_avaSeg, ns, "Head");
				const float CELL = 0.03f;
				auto cellOf = [&](const float* p, int dx, int dy, int dz) -> unsigned {
					int x = (int)floorf(p[0] / CELL) + dx + 512, y = (int)floorf(p[1] / CELL) + dy + 512, z = (int)floorf(p[2] / CELL) + dz + 512;
					return (unsigned)(x & 1023) | ((unsigned)(y & 1023) << 10) | ((unsigned)(z & 1023) << 20);
				};
				static Buf<unsigned long long> bk;
				bk.clear();
				for (int k = 0; k < nbA; k++)
					bk.push_back(((unsigned long long)cellOf(sk.bind[k].p, 0, 0, 0) << 32) | (unsigned)k);
				const int nk = bk.size();
				if (nk > 0) qsort(bk.data(), nk, sizeof(unsigned long long), CmpU64);
				// 重ね着はパックで肌から 4mm 以上・下の布から 3mm 以上離してある。ここで厚く揃えると襟がシャツと同じ面に潰れる。
				const float pad = 0.004f;
				const float reach = 0.04f;
				// JK とスーツは体の外に乗っている。法線で押すと胸と裾の面が割れる。
				for (int it = 0; it < 4 && !sewn; it++) for (int k = nbA; k < nvA; k++) {
					if (k < (int)sk.cloth.size() && !sk.cloth[k]) continue;
					CSkinV& d = sk.bind[k];
					const int cj = DomJoint(d);
					if (cj == jHeadG) continue;
					const float* p = d.p;
					float dA = 1e9f;
					int nearA = -1;
					for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
						const unsigned long long cell = cellOf(p, dx, dy, dz);
						int lo = 0, hi = nk;
						while (lo < hi) { int m = (lo + hi) >> 1; if ((bk[m] >> 32) < cell) lo = m + 1; else hi = m; }
						for (int q = lo; q < nk && (bk[q] >> 32) == cell; q++) {
							const int i = (int)(bk[q] & 0xFFFFFFFFu);
							const float* b = sk.bind[i].p;
							const float d2 = (b[0] - p[0]) * (b[0] - p[0]) + (b[1] - p[1]) * (b[1] - p[1]) + (b[2] - p[2]) * (b[2] - p[2]);
							if (d2 < dA) { dA = d2; nearA = i; }
						}
					}
					const int nearI = nearA;
					if (nearI < 0 || dA > reach * reach) continue;
					const float* b = sk.bind[nearI].p;
					float nx = sk.bind[nearI].n[0], ny = sk.bind[nearI].n[1], nz = sk.bind[nearI].n[2];
					float nl = sqrtf(nx * nx + ny * ny + nz * nz);
					if (nl < 1e-5f) continue;
					nx /= nl; ny /= nl; nz /= nl;
					const float side = (p[0] - b[0]) * nx + (p[1] - b[1]) * ny + (p[2] - b[2]) * nz;
					if (side >= pad) continue;
					const float push = pad - side;
					d.p[0] += nx * push; d.p[1] += ny * push; d.p[2] += nz * push;
				}
			}
		}
		Buf<ClothGpuN> clothN;
		Buf<ClothGpuV> clothV;
		Buf<float> clothP;
		Buf<BYTE> expose;
		clothN.arena = clothV.arena = clothP.arena = expose.arena = 1;
		Buf<SoftGpuN> softN;
		Buf<SoftGpuV> softV;
		softN.arena = softV.arena = 1;
		FlashAvaLocal(74);
		// 男性の胸板の揺れの重みは、女性の胸の下の布の張りや胸の球には使わない。
		if (m_cbCloth) PrepCloth(nc, nbA, nvA, cTri, segJ, m_avaSeg, ns, md, (nc.jig.empty() || maleAva) ? NULL : nc.jig.data(), clothN, clothV, clothP, expose, softN, softV, sewn);
		FlashAvaLocal(90);
		if (expose.size() == nbA) {
			// 服が 1cm ほどの所で覆っている肌（1）は 3.5mm 内へ下げる。揺れや布の遅れ、関節の曲げで面が少しずれても、肌が服を突き抜けない。
			for (int k = 0; k < nbA; k++) {
				if (expose[k] != 1) continue;
				CSkinV& d = sk.bind[k];
				d.p[0] -= d.n[0] * 0.0035f; d.p[1] -= d.n[1] * 0.0035f; d.p[2] -= d.n[2] * 0.0035f;
			}
			BYTE outArm[256] = {};
			{
				const int ua[2] = { SegJoint(segJ, m_avaSeg, ns, "UpperArm.L"), SegJoint(segJ, m_avaSeg, ns, "UpperArm.R") };
				for (int j = 0; j < md.nJ && j < 256; j++) {
					for (int a = j, g = 0; a >= 0 && a < md.nJ && g < 64; a = md.parent[a], g++) {
						if (a == ua[0] || a == ua[1]) { outArm[j] = 1; break; }
					}
				}
			}
			auto armV = [&](int k) { const int d = DomJoint(sk.bind[k]); return d >= 0 && d < 256 && outArm[d]; };
			BYTE outHand[256] = {};
			{
				const int hd[2] = { SegJoint(segJ, m_avaSeg, ns, "Hand.L"), SegJoint(segJ, m_avaSeg, ns, "Hand.R") };
				for (int j = 0; j < md.nJ && j < 256; j++) {
					for (int a = j, g = 0; a >= 0 && a < md.nJ && g < 64; a = md.parent[a], g++) {
						if (a == hd[0] || a == hd[1]) { outHand[j] = 1; break; }
					}
				}
			}
			float yCap = 0.f;
			int nCap = 0;
			{
				const int ua[2] = { SegJoint(segJ, m_avaSeg, ns, "UpperArm.L"), SegJoint(segJ, m_avaSeg, ns, "UpperArm.R") };
				for (int s = 0; s < 2; s++) {
					if (ua[s] < 0 || ua[s] >= md.nJ) continue;
					const M4 jb = MInverse(md.invBind[ua[s]]);
					yCap += jb.m[13];
					nCap++;
				}
				yCap = (nCap ? yCap / (float)nCap : 1.34f) - 0.03f;
			}
			auto shCap = [&](int k) { return sk.bind[k].p[1] > yCap; };
			// スカートの内側の素体（3）と、袖が覆っている腕（1）は keep で残っていても描かない。肩の殻は残す。
			{
				size_t o = 0;
				for (size_t t = 0; t + 2 < idx.size(); t += 3) {
					const int a = idx[t], b = idx[t + 1], c = idx[t + 2];
					if (a < nbA && b < nbA && c < nbA && expose[a] == 3 && expose[b] == 3 && expose[c] == 3) continue;
					if (a < nbA && b < nbA && c < nbA && armV(a) && armV(b) && armV(c)
						&& !shCap(a) && !shCap(b) && !shCap(c)
						&& expose[a] != 2 && expose[b] != 2 && expose[c] != 2) continue;
					idx[o] = idx[t]; idx[o + 1] = idx[t + 1]; idx[o + 2] = idx[t + 2];
					o += 3;
				}
				idx.resize((int)o);
			}
			// スカートの下で消してあった脚を戻す。裾が脚から離れても、ふくらはぎが欠けないように。
			auto visOk = [](int e) { return e == 1 || e == 2; };
			for (size_t t = 0; t * 3 + 2 < body.bodyTri.size(); t++) {
				if (t / 8 < cos.keep.size() && ((cos.keep[t / 8] >> (t & 7)) & 1)) continue;
				for (int ch = bOff[(int)t]; ch < bOff[(int)t + 1]; ch++) {
					const int a = bTri[ch * 3], b = bTri[ch * 3 + 1], c = bTri[ch * 3 + 2];
					if (a >= nbA || b >= nbA || c >= nbA) continue;
					if (armV(a) || armV(b) || armV(c)) continue;
					const int ea = expose[a], eb = expose[b], ec = expose[c];
					if (!visOk(ea) || !visOk(eb) || !visOk(ec) || (ea < 2 && eb < 2 && ec < 2)) continue;
					idx.push_back((WORD)a); idx.push_back((WORD)b); idx.push_back((WORD)c);
				}
			}
			// 袖の下の素体は keep のまま。布から 3cm 以上離れた腕と、肩の殻は戻す。
			for (size_t t = 0; t * 3 + 2 < body.bodyTri.size(); t++) {
				if (t / 8 < cos.keep.size() && ((cos.keep[t / 8] >> (t & 7)) & 1)) continue;
				for (int ch = bOff[(int)t]; ch < bOff[(int)t + 1]; ch++) {
					const int a = bTri[ch * 3], b = bTri[ch * 3 + 1], c = bTri[ch * 3 + 2];
					if (a >= nbA || b >= nbA || c >= nbA) continue;
					if (!armV(a) || !armV(b) || !armV(c)) continue;
					if (shCap(a) && shCap(b) && shCap(c)) {
						const int ea = expose[a], eb = expose[b], ec = expose[c];
						const int nHid = (ea == 1) + (eb == 1) + (ec == 1);
						if (nHid >= 3) continue;
						if (ea != 1 || eb != 1 || ec != 1) {
							idx.push_back((WORD)a); idx.push_back((WORD)b); idx.push_back((WORD)c);
						}
						continue;
					}
					const int ea = expose[a], eb = expose[b], ec = expose[c];
					if (!visOk(ea) || !visOk(eb) || !visOk(ec) || (ea < 2 && eb < 2 && ec < 2)) continue;
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
			size_t w = 0;
			const int nvm = (int)sk.bind.size();
			for (size_t t = 0; t + 2 < idx.size(); t += 3) {
				int a = idx[t], b = idx[t + 1], c = idx[t + 2];
				if ((unsigned)a >= (unsigned)nvm || (unsigned)b >= (unsigned)nvm || (unsigned)c >= (unsigned)nvm) continue;
				idx[w] = (WORD)a; idx[w + 1] = (WORD)b; idx[w + 2] = (WORD)c;
				w += 3;
			}
			idx.resize((int)w);
		}
		if (TacAvaBad()) { g_avaNeedReset = 1; return holdBare(); }
		if (idx.size() < 3 || sk.bind.size() > 65535) return FALSE;
		// 頭を載せてから骨の倍率で頂点が動く。目印を先に取ると、唇も瞬きも空を撫でる。
		for (const CreatureSkin& hs : m_skins) if (hs.model == model && hs.faceSet == am->gender && hs.faceN == body.nFace) {
			sk.faceSet = hs.faceSet;
			sk.faceStart = 0;
			sk.faceN = body.nFace;
			memcpy(sk.faceLin, hs.faceLin, sizeof(sk.faceLin));
			break;
		}
		if (sk.faceSet < 0 && body.nFace > 30) BindFaceSynth(sk, 0, body.nFace);
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
			&& clothP.size() == CLOTH_SEGS * CLOTH_PT * CLOTH_PF + 16 + 4 * 13 * CLOTH_PF + 4) {
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
		if (nc.softN > 0 && nc.softN < SOFT_MAX && softN.size() == nc.softN + 1 && softV.size() == nc.softVN) {
			const void* src[2] = { softN.data(), softV.data() };
			const UINT cnt[2] = { (UINT)softN.size(), (UINT)nc.softVN };
			const UINT stride[2] = { (UINT)sizeof(SoftGpuN), (UINT)sizeof(SoftGpuV) };
			for (int b = 0; b < 2; b++) {
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
				if (FAILED(m_dev->CreateBuffer(&bd, &sd, &nc.softSB[b])) || FAILED(m_dev->CreateShaderResourceView(nc.softSB[b], &sv, &nc.softSRV[b]))) {
					for (int k = 0; k < 2; k++) { TAC_REL(nc.softSRV[k]); TAC_REL(nc.softSB[k]); }
					nc.softN = nc.softVN = 0;
					break;
				}
			}
		} else nc.softN = nc.softVN = 0;
		FlashAvaLocal(100);
		{
			const int jHead = SegJoint(segJ, m_avaSeg, ns, "Head");
			const int jNeck = SegJoint(segJ, m_avaSeg, ns, "Neck");
			float neckY = -1.f;
			if (jNeck >= 0 && jNeck < md.nJ) neckY = MInverse(md.invBind[jNeck]).m[13];
			MeasureHostSkull(nc.skin, jHead, neckY);
		}
		if (m_avaBatchI + 1 < (m_avaBatchN < 1 ? 1 : m_avaBatchN)) m_avaBatchI++;
		m_avaCache.push_back(std::move(nc));
		ac = &m_avaCache.back();
	}
	if (m_avaBuildOnly) return ac != NULL;
	// 揺れの大きさはカップ番号（0=A … 7=H）。ばねの硬さと遅れは GpuSkin 側。
	SkinAndDraw(ac->skin, ac->gm, ac->jig.empty() ? NULL : ac->jig.data(), (float)ac->cup, pose, world, dp,
		(ac->clothN > 0 || ac->softN > 0) ? ac : NULL, owner);
	for (int w = 0; w < 32; w++) if (m_avaWait[w].model == model && m_avaWait[w].cos == ci && m_avaWait[w].cup == cupK) m_avaWait[w].frame = -1;
	return TRUE;
}

static void TacVel(const M4& prev, const M4& now, float dt, float out[3])
{
	out[0] = out[1] = out[2] = 0.f;
	if (dt < 1e-4f) return;
	const V3 w = v3(now.m[12] - prev.m[12], now.m[13] - prev.m[13], now.m[14] - prev.m[14]) * (1.f / dt);
	V3 m = MXformDir(MInverse(now), w);
	const float sp = Len(m);
	if (sp > 6.f) m = m * (6.f / sp);
	out[0] = m.x; out[1] = m.y; out[2] = m.z;
}

static void TacWind(float time, const M4& world, const float vel[3], float amp, float gait, float fly, float wind[4], float air[4], float head[3])
{
	// 強風・暴風は布全体を同じ向きへ。そよ風は別方向のまま渡し、点ごとにずらして波にする。上下は air.w。
	// 移動の向かい風は強い風の側へ足す。見本はその場なので歩幅から前（-Z）へ進んでいるものとする。
	// head は増幅前の向かい風。服はこれを外してマップの風だけにする。髪はそのまま。
	const TacAir w = TacAirHeld(time);
	const M4 inv = MInverse(world);
	const V3 main = MXformDir(inv, v3(w.mx, 0.f, w.mz));
	const V3 br = MXformDir(inv, v3(w.bx, 0.f, w.bz));
	float vx = vel ? vel[0] : 0.f, vy = vel ? vel[1] : 0.f, vz = vel ? vel[2] : 0.f;
	float sp = sqrtf(vx * vx + vy * vy + vz * vz);
	const BOOL flying = fly > 0.5f;
	if (gait > 0.04f) {
		const float fake = gait * (flying ? 2.3f : 1.6f);
		if (fake > sp) { vx = 0.f; vy = 0.f; vz = -fake; sp = fake; }
	}
	float hx = 0.f, hy = 0.f, hz = 0.f;
	if (sp > 0.12f) {
		const float stream = (flying ? 0.90f : 0.55f) + fminf(sp, 4.f) * (flying ? 0.14f : 0.09f);
		const float gale = sqrtf(w.mx * w.mx + w.mz * w.mz) / 2.70f;
		const float s = stream / sp * (1.f + fminf(gale, 1.f) * 0.45f);
		hx = -vx * s; hy = -vy * s; hz = -vz * s;
	}
	if (head) { head[0] = hx; head[1] = hy; head[2] = hz; }
	wind[0] = (main.x + hx) * amp;
	wind[1] = (main.y + hy) * amp;
	wind[2] = (main.z + hz) * amp;
	wind[3] = 0.f;
	air[0] = br.x * amp;
	air[1] = br.y * amp;
	air[2] = br.z * amp;
	air[3] = w.y * amp;
}

// マップの風と同じ位相。強風と暴風、上と下は、足し合わせる前の成分。
static void TacAirSplit(float time, const M4& inv, float amp, float breeze[3], float strong[3], float gale[3], float& up, float& down)
{
	const float br = 0.62f + 0.38f * sinf(time * 0.31f);
	const float st = TacGate(sinf(time * 0.17f + 0.6f), 0.55f);
	const float ga = TacGate(sinf(time * 0.083f + 2.4f), 0.90f);
	const float dn = TacGate(sinf(time * 0.13f + 1.2f), 0.72f);
	const float upg = TacGate(sinf(time * 0.19f + 3.6f), 0.78f);
	const float aB = time * 0.11f;
	const float aS = time * 0.047f + 1.9f;
	const float aG = time * 0.031f + 4.4f;
	const V3 b = MXformDir(inv, v3(cosf(aB) * (0.40f * br), 0.f, sinf(aB) * (0.40f * br)));
	const V3 s = MXformDir(inv, v3(cosf(aS) * st * 1.30f, 0.f, sinf(aS) * st * 1.30f));
	const V3 g = MXformDir(inv, v3(cosf(aG) * ga * 2.70f, 0.f, sinf(aG) * ga * 2.70f));
	breeze[0] = b.x * amp; breeze[1] = b.y * amp; breeze[2] = b.z * amp;
	strong[0] = s.x * amp; strong[1] = s.y * amp; strong[2] = s.z * amp;
	gale[0] = g.x * amp; gale[1] = g.y * amp; gale[2] = g.z * amp;
	up = upg * 0.95f * amp;
	down = dn * 1.15f * amp;
}

// ゲートが立っているあいだ方向を覚え、落ちたあとも hold フレーム残し、attack で上げ release で下ろす。
static void WindEnv(float d[3], float& amp, float& env, int& hold, const float* live, float thresh, int attack, int holdN, int release)
{
	float a = sqrtf(live[0] * live[0] + live[1] * live[1] + live[2] * live[2]);
	if (a > thresh) {
		float shown = amp * (env > 0.2f ? env : 0.2f);
		if (a > shown) {
			float inv = 1.f / a;
			d[0] = live[0] * inv; d[1] = live[1] * inv; d[2] = live[2] * inv;
			amp = a;
		}
		hold = holdN;
	} else if (hold > 0) {
		hold--;
	}
	float target = (a > thresh || hold > 0) ? 1.f : 0.f;
	int span = (target > env) ? attack : release;
	if (span < 1) span = 1;
	float step = 1.f / (float)span;
	if (env < target) env = fminf(1.f, env + step);
	else if (env > target) env = fmaxf(0.f, env - step);
	if (env <= 0.f && hold <= 0 && a <= thresh) amp = 0.f;
}

static void WindAdd(float* o, const float d[3], float amp, float env)
{
	float s = amp * env;
	o[0] += d[0] * s;
	o[1] += d[1] * s;
	o[2] += d[2] * s;
}

static void TacGrav(const M4& world, float g, float out[3])
{
	const V3 d = MXformDir(MInverse(world), v3(0.f, -1.f, 0.f));
	const float L = Len(d);
	if (L < 1e-4f) { out[0] = 0.f; out[1] = -g; out[2] = 0.f; return; }
	out[0] = d.x / L * g; out[1] = d.y / L * g; out[2] = d.z / L * g;
}

static void TacBufCopy(ID3D11DeviceContext* ctx, ID3D11Buffer* dst, UINT dstOff, ID3D11Buffer* src, UINT srcOff, UINT bytes)
{
	if (!ctx || !dst || !src || !bytes) return;
	D3D11_BOX box;
	box.left = srcOff;
	box.right = srcOff + bytes;
	box.top = 0;
	box.bottom = 1;
	box.front = 0;
	box.back = 1;
	ctx->CopySubresourceRegion(dst, 0, dstOff, 0, 0, src, 0, &box);
}

static void TacClothGate(ID3D11DeviceContext* ctx, unsigned on)
{
	if (!ctx || !g_clothGate) return;
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(ctx->Map(g_clothGate, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms)) || !ms.pData) return;
	unsigned f[4] = { on, 0, 0, 0 };
	memcpy(ms.pData, f, sizeof(f));
	ctx->Unmap(g_clothGate, 0);
	ctx->CSSetConstantBuffers(2, 1, &g_clothGate);
}

static BOOL TacClothBatchReady(ID3D11Device* dev)
{
	if (g_clStU && g_clCnS && g_clPfS && g_clPmS) return TRUE;
	if (!dev) return FALSE;
	const UINT inst = (UINT)Renderer::CLOTH_INST;
	const UINT nodes = (UINT)Renderer::CLOTH_NODES;
	D3D11_BUFFER_DESC bd = {};
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = 16;
	bd.ByteWidth = inst * nodes * 8u * 16u;
	D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
	ud.Format = DXGI_FORMAT_UNKNOWN;
	ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	ud.Buffer.NumElements = inst * nodes * 8u;
	if (FAILED(dev->CreateBuffer(&bd, NULL, &g_clStB)) || FAILED(dev->CreateUnorderedAccessView(g_clStB, &ud, &g_clStU))) {
		TAC_REL(g_clStU); TAC_REL(g_clStB); return FALSE;
	}
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.StructureByteStride = 96;
	bd.ByteWidth = inst * nodes * 96u;
	if (FAILED(dev->CreateBuffer(&bd, NULL, &g_clCnB))) { TAC_REL(g_clStU); TAC_REL(g_clStB); return FALSE; }
	D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = DXGI_FORMAT_UNKNOWN;
	sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	sd.Buffer.NumElements = inst * nodes;
	if (FAILED(dev->CreateShaderResourceView(g_clCnB, &sd, &g_clCnS))) {
		TAC_REL(g_clCnB); TAC_REL(g_clStU); TAC_REL(g_clStB); return FALSE;
	}
	bd.StructureByteStride = 4;
	bd.ByteWidth = inst * 2048u * 4u;
	if (FAILED(dev->CreateBuffer(&bd, NULL, &g_clPfB))) {
		TAC_REL(g_clCnS); TAC_REL(g_clCnB); TAC_REL(g_clStU); TAC_REL(g_clStB); return FALSE;
	}
	sd.Buffer.NumElements = inst * 2048u;
	if (FAILED(dev->CreateShaderResourceView(g_clPfB, &sd, &g_clPfS))) {
		TAC_REL(g_clPfB); TAC_REL(g_clCnS); TAC_REL(g_clCnB); TAC_REL(g_clStU); TAC_REL(g_clStB); return FALSE;
	}
	bd = {};
	bd.ByteWidth = inst * 1536u;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	if (FAILED(dev->CreateBuffer(&bd, NULL, &g_clPmB))) {
		TAC_REL(g_clPfS); TAC_REL(g_clPfB); TAC_REL(g_clCnS); TAC_REL(g_clCnB); TAC_REL(g_clStU); TAC_REL(g_clStB); return FALSE;
	}
	sd = {};
	sd.Format = DXGI_FORMAT_R32_TYPELESS;
	sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	sd.BufferEx.NumElements = bd.ByteWidth / 4u;
	sd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	if (FAILED(dev->CreateShaderResourceView(g_clPmB, &sd, &g_clPmS))) {
		TAC_REL(g_clPmB); TAC_REL(g_clPfS); TAC_REL(g_clPfB); TAC_REL(g_clCnS); TAC_REL(g_clCnB); TAC_REL(g_clStU); TAC_REL(g_clStB); return FALSE;
	}
	return TRUE;
}

static BOOL TacHairBatchReady(ID3D11Device* dev)
{
	if (g_hStU && g_hPmS) return TRUE;
	if (!dev) return FALSE;
	const UINT inst = (UINT)Renderer::HAIR_INST;
	const UINT els = (UINT)Renderer::HAIR_G * 5u;
	D3D11_BUFFER_DESC bd = {};
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = 16;
	bd.ByteWidth = inst * els * 16u;
	D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
	ud.Format = DXGI_FORMAT_UNKNOWN;
	ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	ud.Buffer.NumElements = inst * els;
	if (FAILED(dev->CreateBuffer(&bd, NULL, &g_hStB)) || FAILED(dev->CreateUnorderedAccessView(g_hStB, &ud, &g_hStU))) {
		TAC_REL(g_hStU); TAC_REL(g_hStB); return FALSE;
	}
	bd = {};
	bd.ByteWidth = inst * 1280u;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	if (FAILED(dev->CreateBuffer(&bd, NULL, &g_hPmB))) {
		TAC_REL(g_hStU); TAC_REL(g_hStB); return FALSE;
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = DXGI_FORMAT_R32_TYPELESS;
	sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	sd.BufferEx.NumElements = bd.ByteWidth / 4u;
	sd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	if (FAILED(dev->CreateShaderResourceView(g_hPmB, &sd, &g_hPmS))) {
		TAC_REL(g_hPmB); TAC_REL(g_hStU); TAC_REL(g_hStB); return FALSE;
	}
	return TRUE;
}

static void TacHairSimRaw(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11ComputeShader* sh,
	ID3D11ShaderResourceView* guide, ID3D11ShaderResourceView* vert,
	ID3D11UnorderedAccessView* st, const void* parm, const void* cloth)
{
	if (!ctx || !sh || !st || !parm || !guide || !TacHairBatchReady(dev)) return;
	unsigned char blob[1280];
	memcpy(blob, parm, 608);
	if (cloth) memcpy(blob + 608, cloth, 672);
	else memset(blob + 608, 0, 672);
	ID3D11ShaderResourceView* noneS[3] = { NULL, NULL, NULL };
	ID3D11UnorderedAccessView* noneU = NULL;
	ctx->CSSetShaderResources(0, 3, noneS);
	ctx->CSSetUnorderedAccessViews(0, 1, &noneU, NULL);
	D3D11_BOX box;
	box.left = 0;
	box.right = 1280;
	box.top = 0;
	box.bottom = 1;
	box.front = 0;
	box.back = 1;
	ctx->UpdateSubresource(g_hPmB, 0, &box, blob, 0, 0);
	ID3D11ShaderResourceView* sv[3] = { guide, vert, g_hPmS };
	ctx->CSSetShaderResources(0, 3, sv);
	UINT z = 0;
	ctx->CSSetUnorderedAccessViews(0, 1, &st, &z);
	ctx->CSSetShader(sh, NULL, 0);
	ctx->Dispatch(1, 1, 1);
}

void Renderer::FlushClothPend()
{
	if (g_pendBusy || g_pendN <= 0 || !m_ctx) return;
	g_pendBusy = 1;
	const int n = g_pendN;
	ID3D11UnorderedAccessView* noneU[2] = { NULL, NULL };
	ID3D11ShaderResourceView* noneS[4] = { NULL, NULL, NULL, NULL };
	m_ctx->CSSetUnorderedAccessViews(0, 2, noneU, NULL);
	m_ctx->CSSetShaderResources(0, 4, noneS);
	BOOL together = n >= 2 && m_csCloth[1] && TacClothBatchReady(m_dev);
	if (together) {
		for (int i = 0; i < n; i++) {
			ClothPend& j = g_pend[i];
			if (!j.cloth || !j.ac || !j.ci || !((ClothInst*)j.ci)->sb || !j.ac->clothSB[0] || !j.ac->clothSB[2]) { together = FALSE; break; }
		}
	}
	if (together) {
		static unsigned char pack[Renderer::CLOTH_INST * 1536];
		for (int i = 0; i < n; i++) memcpy(pack + (size_t)i * 1536u, g_pend[i].parm, 1536u);
		D3D11_BOX box;
		box.left = 0;
		box.right = (UINT)n * 1536u;
		box.top = 0;
		box.bottom = 1;
		box.front = 0;
		box.back = 1;
		m_ctx->UpdateSubresource(g_clPmB, 0, &box, pack, 0, 0);
	}
	if (together) {
		for (int i = 0; i < n; i++) {
			ClothPend& j = g_pend[i];
			UINT cn = (UINT)j.ac->clothN * 96u;
			if (cn > 512u * 96u) cn = 512u * 96u;
			TacBufCopy(m_ctx, g_clCnB, (UINT)i * 512u * 96u, j.ac->clothSB[0], 0, cn);
			D3D11_BUFFER_DESC pd = {};
			j.ac->clothSB[2]->GetDesc(&pd);
			UINT pb = pd.ByteWidth;
			if (pb > 2048u * 4u) pb = 2048u * 4u;
			TacBufCopy(m_ctx, g_clPfB, (UINT)i * 2048u * 4u, j.ac->clothSB[2], 0, pb);
			TacBufCopy(m_ctx, g_clStB, (UINT)i * 512u * 8u * 16u, ((ClothInst*)j.ci)->sb, 0, 512u * 8u * 16u);
		}
		TacClothGate(m_ctx, 1);
		ID3D11ShaderResourceView* sv[4] = { g_clCnS, NULL, g_clPfS, g_clPmS };
		m_ctx->CSSetShaderResources(0, 4, sv);
		ID3D11UnorderedAccessView* uv[2] = { NULL, g_clStU };
		UINT zc[2] = { 0, 0 };
		m_ctx->CSSetUnorderedAccessViews(0, 2, uv, zc);
		m_ctx->CSSetShader(m_csCloth[1], NULL, 0);
		m_ctx->Dispatch((UINT)n, 1, 1);
		m_ctx->CSSetUnorderedAccessViews(0, 2, noneU, NULL);
		for (int i = 0; i < n; i++)
			TacBufCopy(m_ctx, ((ClothInst*)g_pend[i].ci)->sb, 0, g_clStB, (UINT)i * 512u * 8u * 16u, 512u * 8u * 16u);
	} else {
		TacClothGate(m_ctx, 0);
		for (int i = 0; i < n; i++) {
			ClothPend& j = g_pend[i];
			if (!j.cloth || !j.ac || !j.ci || !((ClothInst*)j.ci)->uav || !j.clothB) continue;
			ID3D11Buffer* cbs[2] = { j.clothB, j.boneB };
			m_ctx->CSSetConstantBuffers(0, 2, cbs);
			m_ctx->CSSetShaderResources(0, 3, j.ac->clothSRV);
			ID3D11UnorderedAccessView* uv[2] = { j.vert, ((ClothInst*)j.ci)->uav };
			UINT zc[2] = { 0, 0 };
			m_ctx->CSSetUnorderedAccessViews(0, 2, uv, zc);
			m_ctx->CSSetShader(m_csCloth[1], NULL, 0);
			m_ctx->Dispatch(1, 1, 1);
		}
		m_ctx->CSSetUnorderedAccessViews(0, 2, noneU, NULL);
	}
	for (int i = 0; i < n; i++) {
		ClothPend& j = g_pend[i];
		if (j.cloth && j.ac && j.ci && ((ClothInst*)j.ci)->uav && j.vert && j.clothB && m_csCloth[2]) {
			ID3D11Buffer* cbs[2] = { j.clothB, j.boneB };
			m_ctx->CSSetConstantBuffers(0, 2, cbs);
			m_ctx->CSSetShaderResources(0, 3, j.ac->clothSRV);
			ID3D11UnorderedAccessView* uv[2] = { j.vert, ((ClothInst*)j.ci)->uav };
			UINT zc[2] = { 0, 0 };
			m_ctx->CSSetUnorderedAccessViews(0, 2, uv, zc);
			m_ctx->CSSetShader(m_csCloth[2], NULL, 0);
			m_ctx->Dispatch(((UINT)j.ac->clothVN + 63u) / 64u, 1, 1);
		}
		if (j.soft && j.ac && j.ci && ((ClothInst*)j.ci)->softUav && j.vert && j.softB && m_csSoft[0] && m_csSoft[1]) {
			ID3D11Buffer* cb1 = j.softB;
			m_ctx->CSSetConstantBuffers(0, 1, &cb1);
			ID3D11ShaderResourceView* sv[2] = { j.ac->softSRV[0], j.ac->softSRV[1] };
			m_ctx->CSSetShaderResources(0, 2, sv);
			ID3D11UnorderedAccessView* uv[2] = { j.vert, ((ClothInst*)j.ci)->softUav };
			UINT zc[2] = { 0, 0 };
			m_ctx->CSSetUnorderedAccessViews(0, 2, uv, zc);
			m_ctx->CSSetShader(m_csSoft[0], NULL, 0);
			m_ctx->Dispatch(1, 1, 1);
			m_ctx->CSSetShader(m_csSoft[1], NULL, 0);
			m_ctx->Dispatch(((UINT)j.ac->softVN + 63u) / 64u, 1, 1);
		}
	}
	m_ctx->CSSetUnorderedAccessViews(0, 2, noneU, NULL);
	m_ctx->CSSetShaderResources(0, 4, noneS);
	g_pendN = 0;
	g_pendBusy = 0;
}

void Renderer::RunCloth(AvaCache& ac, const void* owner, CreatureSkin& src, const Pose& pose, const M4& world, const DrawParams& dp)
{
	if (m_inPortrait) return;
	const BOOL canSkirt = owner && src.outUAV && ac.clothN > 0 && ac.clothN <= CLOTH_NODES && ac.clothVN > 0
		&& ac.clothSRV[0] && ac.clothSRV[1] && ac.clothSRV[2] && m_cbCloth && m_cbSkinBone && m_csCloth[0] && m_csCloth[1] && m_csCloth[2]
		&& ac.clothHip >= 0 && ac.clothHip < pose.nJ;
	const BOOL canSoft = !m_inPortrait && owner && src.outUAV && ac.softN > 0 && ac.softN <= SOFT_MAX && ac.softVN > 0
		&& ac.softSRV[0] && ac.softSRV[1] && m_cbSoft && m_csSoft[0] && m_csSoft[1];
	if (!canSkirt && !canSoft) return;
	int deferCloth = 0;
	ClothInst* ci = NULL;
	if (m_inPortrait) {
		for (ClothInst& x : m_clothInst) if (x.slot == m_portraitSlot) { ci = &x; break; }
	} else {
		for (ClothInst& x : m_clothInst) if (x.slot < 0 && x.owner == owner && x.model == ac.model && x.cos == ac.cos && x.cup == ac.cup) { ci = &x; break; }
	}
	if (!ci) {
		// 空きが無ければ、いちばん長く描いていないキャラの分を使い回す。
		if (m_clothInst.size() < CLOTH_INST) { m_clothInst.push_back(ClothInst()); ci = &m_clothInst.back(); }
		else { ci = &m_clothInst[0]; for (ClothInst& x : m_clothInst) if (x.frame < ci->frame) ci = &x; }
		ci->valid = 0; ci->limbOk = 0;
	}
	if (m_inPortrait) {
		if (ci->slot != m_portraitSlot || ci->model != ac.model || ci->cos != ac.cos || ci->cup != ac.cup) { ci->valid = 0; ci->limbOk = 0; }
		ci->slot = m_portraitSlot;
	} else if (ci->slot >= 0 || ci->owner != owner || ci->model != ac.model || ci->cos != ac.cos || ci->cup != ac.cup) {
		ci->valid = 0; ci->limbOk = 0;
		ci->slot = -1;
	}
	ci->owner = owner; ci->model = ac.model; ci->cos = ac.cos; ci->cup = ac.cup;
	const float dtRaw = m_time - ci->time;
	int reset = !ci->valid;
	M4 delta = MIdent();
	if (!reset) {
		delta = MMul(ci->world, MInverse(world));
		const float* t = &delta.m[12];
		float dx = world.m[12] - ci->world.m[12];
		float dy = world.m[13] - ci->world.m[13];
		float dz = world.m[14] - ci->world.m[14];
		if (t[0] * t[0] + t[1] * t[1] + t[2] * t[2] > 1.5f * 1.5f
			|| dx * dx + dy * dy + dz * dz > 1.5f * 1.5f
			|| dtRaw > 0.25f || dtRaw < 0.f) reset = 1;
	}
	ci->prevFly = dp.fly;
	ci->prevGait = dp.gait;
	if (!reset) { delta.m[12] = 0.f; delta.m[13] = 0.f; delta.m[14] = 0.f; }
	// カットでも風を溜める。1.5m を超える移動だけ、上の判定で置き直す。
	const BOOL step = reset || (ci->frame != m_frame && dtRaw > 1e-4f);
	const float hs = dp.bodyScale > 0.05f ? dp.bodyScale : 1.f;
	const float dt = Clampf(dtRaw > 0.f ? dtRaw : 1.f / 60.f, 1.f / 240.f, 1.f / 20.f);
	float velUse[3] = { ci->vel[0], ci->vel[1], ci->vel[2] };
	if (reset) { velUse[0] = velUse[1] = velUse[2] = 0.f; ci->vel[0] = ci->vel[1] = ci->vel[2] = 0.f; }
	else if (dtRaw > 1e-3f) { TacVel(ci->world, world, dtRaw, velUse); ci->vel[0] = velUse[0]; ci->vel[1] = velUse[1]; ci->vel[2] = velUse[2]; }
	if (canSkirt) {
	// 今・軌跡3つ・作業・目標からのずれ・古い軌跡2つ。歩きや攻撃の1フレームで裾をなびかせない。
	if (!ci->uav) {
		TAC_REL(ci->sb);
		ci->n = 0;
		ci->valid = 0;
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (UINT)CLOTH_NODES * 8u * 16u;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = 16;
		if (FAILED(m_dev->CreateBuffer(&bd, NULL, &ci->sb))) ci->sb = NULL;
		D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
		ud.Format = DXGI_FORMAT_UNKNOWN;
		ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		ud.Buffer.NumElements = (UINT)CLOTH_NODES * 8u;
		if (!ci->sb || FAILED(m_dev->CreateUnorderedAccessView(ci->sb, &ud, &ci->uav))) { TAC_REL(ci->sb); ci->uav = NULL; }
		else ci->n = CLOTH_NODES;
	}
	if (ci->uav) {
	struct CB { M4 delta, hips, hipInv; float seg[CLOTH_SEGS][4][4]; M4 pal[CLOTH_SEGS], inv[CLOTH_SEGS]; unsigned cnt[4]; float sim[4]; float mode[4]; float wind[4]; float air[4]; float limb[CLOTH_SEGS][4]; float grav[4]; } cb;
	static_assert(sizeof(CB) == 1536, "ClothParm");
	memset(&cb, 0, sizeof(cb));
	cb.delta = delta;
	cb.hips = pose.palette[ac.clothHip];
	cb.hipInv = MInverse(cb.hips);
	cb.mode[1] = ac.clothAxis[0];
	cb.mode[2] = ac.clothAxis[1];
	// 太ももが両方とも腰に対して大きく上がる（座る・しゃがむ）ときだけ、衣装の元の重みへ戻して脚に沿わせる。
	// 走りや片足上げでここが立つと、反対の足の重みで前の布が後ろへ引かれて膝が突き抜ける。
	{
		float bendMin = 1e9f;
		int nThigh = 0;
		for (int s = 0; s < 2 && s < ac.segN; s++) {
			const int ja = ac.segJ[s][0], jb = ac.segJ[s][1];
			if (ja < 0 || jb < 0 || ja >= pose.nJ || jb >= pose.nJ) continue;
			const float* G = &ac.segG[s][0][0];
			const V3 A0 = v3(G[0], G[1], G[2]), B0 = A0 + v3(G[4], G[5], G[6]) * G[3];
			const V3 bindDir = Norm(B0 - A0);
			const V3 posed = Norm(MXformPt(pose.palette[jb], B0) - MXformPt(pose.palette[ja], A0));
			float b = 0.f;
			if (bindDir.z - posed.z > 0.05f)
				b = acosf(Clampf(Dot(posed, bindDir), -1.f, 1.f));
			if (b < bendMin) bendMin = b;
			nThigh++;
		}
		if (nThigh < 2 || dp.gait > 0.05f || dp.fly > 0.5f) bendMin = 0.f;
		cb.mode[0] = Smooth01((bendMin - 0.75f) / 0.5f);
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
	memcpy(&cb.cnt[3], &ac.torsoC[2], 4);
	cb.sim[0] = dt;
	cb.sim[1] = dt * 60.f;
	cb.sim[2] = reset ? 1.f : 0.f;
	cb.sim[3] = hs;
	cb.mode[3] = m_time;
	float head[3] = {};
	TacWind(m_time, world, velUse, 0.48f, dp.gait, dp.fly, cb.wind, cb.air, head);
	// 服は歩きも飛行もマップの風だけ。移動で作った向かい風はかけない。
	// 飛行中（停止でも翼は羽ばたく）は Wind.w を 1 より大きくし、弱い乱れを足す。
	cb.wind[0] -= head[0] * 0.48f;
	cb.wind[1] -= head[1] * 0.48f;
	cb.wind[2] -= head[2] * 0.48f;
	const float moveSp = sqrtf(velUse[0] * velUse[0] + velUse[2] * velUse[2]);
	const float moveFly = fmaxf(dp.gait, Clampf(moveSp / 3.5f, 0.f, 1.2f));
	cb.wind[3] = dp.fly > 0.5f ? (1.f + moveFly) : 0.f;
	// 生のゲートは数フレームで 0 になる。種類ごとに上げ・保持・戻しを覚えてから、初めて足す。
	if (step) {
		float br[3], st[3], ga[3], up = 0.f, dn = 0.f;
		TacAirSplit(m_time, MInverse(world), 0.48f, br, st, ga, up, dn);
		const float upv[3] = { 0.f, up, 0.f };
		const float dnv[3] = { 0.f, -dn, 0.f };
		for (int i = 0; i < 3; i++) ci->breeze.d[i] += (br[i] - ci->breeze.d[i]) * (1.f / 24.f);
		WindEnv(ci->strong.d, ci->strong.amp, ci->strong.env, ci->strong.hold, st, 0.05f, 40, 56, 48);
		WindEnv(ci->gale.d, ci->gale.amp, ci->gale.env, ci->gale.hold, ga, 0.05f, 48, 72, 56);
		WindEnv(ci->up.d, ci->up.amp, ci->up.env, ci->up.hold, upv, 0.04f, 36, 48, 40);
		WindEnv(ci->down.d, ci->down.amp, ci->down.env, ci->down.hold, dnv, 0.04f, 36, 48, 40);
		float gsum[3] = {};
		WindAdd(gsum, ci->strong.d, ci->strong.amp, ci->strong.env);
		WindAdd(gsum, ci->gale.d, ci->gale.amp, ci->gale.env);
		const float stage = m_stageWind ? 2.35f : 1.f;
		cb.wind[0] = gsum[0] * stage;
		cb.wind[1] = gsum[1] * stage;
		cb.wind[2] = gsum[2] * stage;
		cb.air[0] = ci->breeze.d[0] * stage;
		cb.air[1] = ci->breeze.d[1] * stage;
		cb.air[2] = ci->breeze.d[2] * stage;
		cb.air[3] = (ci->up.d[1] * ci->up.amp * ci->up.env + ci->down.d[1] * ci->down.amp * ci->down.env) * stage;
	}
	TacGrav(world, 9.2f, cb.grav);
	cb.grav[3] = ac.clothY0;
	if (step) {
		for (int s = 0; s < nseg; s++) {
			const float* G = &ac.segG[s][0][0];
			const V3 cur = MXformPt(cb.pal[s], v3(G[0], G[1], G[2])) * hs;
			if (ci->limbOk && !reset) {
				const V3 prev = MXformPt(delta, v3(ci->limbPrev[s][0], ci->limbPrev[s][1], ci->limbPrev[s][2]));
				const V3 d = cur - prev;
				cb.limb[s][0] = d.x; cb.limb[s][1] = d.y; cb.limb[s][2] = d.z; cb.limb[s][3] = Len(d);
			}
			ci->limbPrev[s][0] = cur.x; ci->limbPrev[s][1] = cur.y; ci->limbPrev[s][2] = cur.z;
		}
		ci->limbOk = 1;
	}
	D3D11_MAPPED_SUBRESOURCE ms;
	ID3D11Buffer* clothB = TacCbAcquire(g_clothR, m_ctx, m_cbCloth, ms);
	if (clothB) {
	memcpy(ms.pData, &cb, sizeof(cb));
	m_ctx->Unmap(clothB, 0);
	// b1 は直前の CSSkin で書いた骨の行列（座ったときに脚へ沿わせる分）。
	ID3D11Buffer* cbs[2] = { clothB, TacCbCur(g_skinBoneR, m_cbSkinBone) };
	m_ctx->CSSetConstantBuffers(0, 2, cbs);
	m_ctx->CSSetShaderResources(0, 3, ac.clothSRV);
	ID3D11UnorderedAccessView* uavs[2] = { src.outUAV, ci->uav };
	UINT cnts[2] = { 0, 0 };
	m_ctx->CSSetUnorderedAccessViews(0, 2, uavs, cnts);
	if (step) {
		m_ctx->CSSetShader(m_csCloth[0], NULL, 0);
		m_ctx->Dispatch(((UINT)ac.clothN + 63u) / 64u, 1, 1);
		// 人数が揃ってから拘束だけ同時に走らせる。描く前に FlushClothPend が書き戻す。
		if (m_instOn && g_pendN < CLOTH_INST && ci->sb && ci->uav && ac.clothSB[0] && ac.clothSB[2] && src.outUAV) {
			ClothPend& j = g_pend[g_pendN++];
			memset(&j, 0, sizeof(j));
			j.ac = &ac;
			j.ci = ci;
			j.vert = src.outUAV;
			j.clothB = clothB;
			j.boneB = TacCbCur(g_skinBoneR, m_cbSkinBone);
			memcpy(j.parm, &cb, sizeof(cb));
			j.cloth = 1;
			deferCloth = 1;
		} else {
			TacClothGate(m_ctx, 0);
			m_ctx->CSSetShader(m_csCloth[1], NULL, 0);
			m_ctx->Dispatch(1, 1, 1);
		}
	}
	if (!deferCloth) {
		m_ctx->CSSetShader(m_csCloth[2], NULL, 0);
		m_ctx->Dispatch(((UINT)ac.clothVN + 63u) / 64u, 1, 1);
	}
	}
	}
	}
	if (canSoft) {
		if (!ci->softUav) {
			TAC_REL(ci->softSb);
			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = (UINT)SOFT_MAX * 5u * 16u;
			bd.Usage = D3D11_USAGE_DEFAULT;
			bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			bd.StructureByteStride = 16;
			D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
			ud.Format = DXGI_FORMAT_UNKNOWN;
			ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
			ud.Buffer.NumElements = (UINT)SOFT_MAX * 5u;
			if (SUCCEEDED(m_dev->CreateBuffer(&bd, NULL, &ci->softSb)) && SUCCEEDED(m_dev->CreateUnorderedAccessView(ci->softSb, &ud, &ci->softUav))) {
			} else { TAC_REL(ci->softUav); TAC_REL(ci->softSb); }
		}
		if (ci->softUav) {
			struct SCB { M4 delta; M4 bone[6]; float sim[4], wind[4], air[4], bust[2][4], armA[4][4], armB[4][4], limb[2][4], body[4]; unsigned cnt[4]; float grav[4]; } scb;
			static_assert(sizeof(SCB) == 736, "SoftParm");
			memset(&scb, 0, sizeof(scb));
			scb.delta = delta;
			for (int b = 0; b < 6; b++) {
				const int j = ac.softBone[b];
				scb.bone[b] = (j >= 0 && j < pose.nJ) ? pose.palette[j] : MIdent();
			}
			if (ac.clothHip >= 0 && ac.clothHip < pose.nJ) scb.bone[5] = pose.palette[ac.clothHip];
			scb.sim[0] = dt; scb.sim[1] = dt * 60.f; scb.sim[2] = reset ? 1.f : 0.f; scb.sim[3] = hs;
			const float stage = m_stageWind ? 2.35f : 1.f;
			scb.wind[0] = (ci->strong.d[0] * ci->strong.amp * ci->strong.env + ci->gale.d[0] * ci->gale.amp * ci->gale.env) * stage;
			scb.wind[1] = (ci->strong.d[1] * ci->strong.amp * ci->strong.env + ci->gale.d[1] * ci->gale.amp * ci->gale.env) * stage;
			scb.wind[2] = (ci->strong.d[2] * ci->strong.amp * ci->strong.env + ci->gale.d[2] * ci->gale.amp * ci->gale.env) * stage;
			scb.wind[3] = dp.fly > 0.5f ? 2.f : 0.f;
			scb.air[0] = ci->breeze.d[0] * stage;
			scb.air[1] = ci->breeze.d[1] * stage;
			scb.air[2] = ci->breeze.d[2] * stage;
			scb.air[3] = (ci->up.d[1] * ci->up.amp * ci->up.env + ci->down.d[1] * ci->down.amp * ci->down.env) * stage;
			TacGrav(world, 9.2f, scb.grav);
			const int jChest = ac.softBone[0];
			const M4 chest = (jChest >= 0 && jChest < pose.nJ) ? pose.palette[jChest] : MIdent();
			for (int s = 0; s < 2; s++) {
				const V3 c = MXformPt(chest, v3(ac.bustA[s][0], ac.bustA[s][1], ac.bustA[s][2])) * hs;
				scb.bust[s][0] = c.x + src.bustLag[0];
				scb.bust[s][1] = c.y + src.bustLag[1];
				scb.bust[s][2] = c.z + src.bustLag[2];
				scb.bust[s][3] = ac.bustA[s][3] * hs;
			}
			for (int c = 0; c < 4; c++) {
				const int ja = ac.armJ[c][0], jb = ac.armJ[c][1];
				if (ja < 0 || jb < 0 || ja >= pose.nJ || jb >= pose.nJ || ac.armB[c][3] < 0.f) { scb.armB[c][3] = -1.f; continue; }
				const V3 A = MXformPt(pose.palette[ja], v3(ac.armA[c][0], ac.armA[c][1], ac.armA[c][2])) * hs;
				const V3 B = MXformPt(pose.palette[jb], v3(ac.armB[c][0], ac.armB[c][1], ac.armB[c][2])) * hs;
				scb.armA[c][0] = A.x; scb.armA[c][1] = A.y; scb.armA[c][2] = A.z; scb.armA[c][3] = ac.armA[c][3] * hs;
				scb.armB[c][0] = B.x; scb.armB[c][1] = B.y; scb.armB[c][2] = B.z; scb.armB[c][3] = ac.armB[c][3] * hs;
			}
			if (step) {
				for (int side = 0; side < 2; side++) {
					const int hj = ac.armJ[side * 2 + 1][1] >= 0 ? ac.armJ[side * 2 + 1][1] : ac.softBone[side == 0 ? 2 : 4];
					if (hj < 0 || hj >= pose.nJ) continue;
					V3 hb = v3(0.f, 0.f, 0.f);
					if (ac.model >= 0 && ac.model < (int)models.size() && hj < models[ac.model].nJ) {
						const M4 ib = MInverse(models[ac.model].invBind[hj]);
						hb = v3(ib.m[12], ib.m[13], ib.m[14]);
					}
					const V3 cur = MXformPt(pose.palette[hj], hb) * hs;
					if (ci->limbOk && !reset) {
						const V3 prev = MXformPt(delta, v3(ci->handPrev[side][0], ci->handPrev[side][1], ci->handPrev[side][2]));
						const V3 d = cur - prev;
						scb.limb[side][0] = d.x; scb.limb[side][1] = d.y; scb.limb[side][2] = d.z; scb.limb[side][3] = Len(d);
					}
					ci->handPrev[side][0] = cur.x; ci->handPrev[side][1] = cur.y; ci->handPrev[side][2] = cur.z;
				}
				ci->limbOk = 1;
			}
			V3 chestB = v3(0.f, 0.f, 0.f);
			if (ac.model >= 0 && ac.model < (int)models.size() && jChest >= 0 && jChest < models[ac.model].nJ) {
				const M4 ib = MInverse(models[ac.model].invBind[jChest]);
				chestB = v3(ib.m[12], ib.m[13], ib.m[14]);
			}
			const V3 spine = MXformPt(chest, chestB) * hs;
			scb.body[0] = spine.x; scb.body[1] = spine.y; scb.body[2] = spine.z; scb.body[3] = m_time;
			scb.cnt[0] = (unsigned)ac.softN;
			scb.cnt[1] = (unsigned)ac.softVN;
			D3D11_MAPPED_SUBRESOURCE ms;
			ID3D11Buffer* softB = TacCbAcquire(g_softR, m_ctx, m_cbSoft, ms);
			if (softB) {
				memcpy(ms.pData, &scb, sizeof(scb));
				m_ctx->Unmap(softB, 0);
				if (deferCloth && g_pendN > 0) {
					g_pend[g_pendN - 1].soft = 1;
					g_pend[g_pendN - 1].softB = softB;
				} else {
					ID3D11Buffer* cb1 = softB;
					m_ctx->CSSetConstantBuffers(0, 1, &cb1);
					ID3D11ShaderResourceView* sv[2] = { ac.softSRV[0], ac.softSRV[1] };
					m_ctx->CSSetShaderResources(0, 2, sv);
					ID3D11UnorderedAccessView* uv[2] = { src.outUAV, ci->softUav };
					UINT zc[2] = { 0, 0 };
					m_ctx->CSSetUnorderedAccessViews(0, 2, uv, zc);
					if (step) {
						m_ctx->CSSetShader(m_csSoft[0], NULL, 0);
						m_ctx->Dispatch(1, 1, 1);
					}
					m_ctx->CSSetShader(m_csSoft[1], NULL, 0);
					m_ctx->Dispatch(((UINT)ac.softVN + 63u) / 64u, 1, 1);
				}
			}
		}
	}
	ci->frame = m_frame;
	if (step) {
		ci->time = m_time;
		ci->world = world;
		ci->valid = 1;
	}
}

void Renderer::ReleaseHairInst()
{
	for (HairInst& h : m_hairInst) { TAC_REL(h.srv); TAC_REL(h.uav); TAC_REL(h.sb); }
	m_hairInst.clear();
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
	TAC_REL(m_hairGuide);
	TAC_REL(m_hairGuideSB);
	TAC_REL(m_hairVert);
	TAC_REL(m_hairVertSB);
	TAC_REL(m_hairScratchSRV);
	TAC_REL(m_hairScratchUAV);
	TAC_REL(m_hairScratchSB);
	ReleaseHairInst();
	for (int s = 0; s < 16; s++) { m_hairV0[s] = 0; m_hairVN[s] = 0; m_hairI0[s] = 0; m_hairIN[s] = 0; m_hairVMin[s] = 0; m_hairVMax[s] = -1; }
	m_hairNv = 0;
	m_scalpModel = -1;
	m_capBind.clear();
	m_capDraw.ib = NULL; m_capDraw.nIdx = 0;
	for (int s = 0; s < TAC_SCALP_CACHE; s++) { m_scalps[s].model = -1; m_scalps[s].draw.Release(); m_scalps[s].bind.clear(); }
}

void Renderer::FlushHairPend()
{
	if (g_hairBusy || g_hairN <= 0 || !m_ctx) return;
	g_hairBusy = 1;
	const int n = g_hairN;
	ID3D11UnorderedAccessView* noneU[2] = { NULL, NULL };
	ID3D11ShaderResourceView* noneS[3] = { NULL, NULL, NULL };
	m_ctx->CSSetUnorderedAccessViews(0, 2, noneU, NULL);
	m_ctx->CSSetShaderResources(0, 3, noneS);
	const UINT stBytes = (UINT)HAIR_G * 5u * 16u;
	BOOL together = n >= 2 && m_csHair[0] && m_hairGuide && m_hairVert && TacHairBatchReady(m_dev);
	if (together) {
		for (int i = 0; i < n; i++) {
			HairInst* hi = (HairInst*)g_hair[i].hi;
			if (!hi || !hi->sb || !g_hair[i].simB || !g_hair[i].clothB || !g_hair[i].vert) { together = FALSE; break; }
		}
	}
	if (together) {
		for (int i = 0; i < n; i++) {
			HairInst* hi = (HairInst*)g_hair[i].hi;
			TacBufCopy(m_ctx, g_hStB, (UINT)i * stBytes, hi->sb, 0, stBytes);
		}
		static unsigned char pack[Renderer::HAIR_INST * 1280];
		for (int i = 0; i < n; i++) {
			memcpy(pack + i * 1280, g_hair[i].parm, 608);
			memcpy(pack + i * 1280 + 608, g_hair[i].cloth, 672);
		}
		m_ctx->UpdateSubresource(g_hPmB, 0, NULL, pack, 0, 0);
		TacClothGate(m_ctx, 1);
		ID3D11ShaderResourceView* sv[3] = { m_hairGuide, m_hairVert, g_hPmS };
		m_ctx->CSSetShaderResources(0, 3, sv);
		ID3D11UnorderedAccessView* uv = g_hStU;
		UINT z = 0;
		m_ctx->CSSetUnorderedAccessViews(0, 1, &uv, &z);
		m_ctx->CSSetShader(m_csHair[0], NULL, 0);
		m_ctx->Dispatch((UINT)n, 1, 1);
		m_ctx->CSSetUnorderedAccessViews(0, 2, noneU, NULL);
		m_ctx->CSSetShaderResources(0, 3, noneS);
		for (int i = 0; i < n; i++) {
			HairInst* hi = (HairInst*)g_hair[i].hi;
			TacBufCopy(m_ctx, hi->sb, 0, g_hStB, (UINT)i * stBytes, stBytes);
		}
	}
	for (int i = 0; i < n; i++) {
		HairPend& j = g_hair[i];
		HairInst* hi = (HairInst*)j.hi;
		if (!hi || !hi->uav || !j.vert || !j.simB || !j.clothB || j.vn <= 0) continue;
		if (!together)
			TacHairSimRaw(m_dev, m_ctx, m_csHair[0], m_hairGuide, m_hairVert, hi->uav, j.parm, j.cloth);
		ID3D11Buffer* cbs[2] = { j.simB, j.clothB };
		m_ctx->CSSetConstantBuffers(0, 2, cbs);
		ID3D11ShaderResourceView* sv[2] = { m_hairGuide, m_hairVert };
		m_ctx->CSSetShaderResources(0, 2, sv);
		ID3D11UnorderedAccessView* uv[2] = { hi->uav, j.vert };
		UINT zc[2] = { 0, 0 };
		m_ctx->CSSetUnorderedAccessViews(0, 2, uv, zc);
		m_ctx->CSSetShader(m_csHair[1], NULL, 0);
		m_ctx->Dispatch(((UINT)j.vn + 63u) / 64u, 1, 1);
	}
	m_ctx->CSSetUnorderedAccessViews(0, 2, noneU, NULL);
	m_ctx->CSSetShaderResources(0, 3, noneS);
	g_hairN = 0;
	g_hairBusy = 0;
}

void Renderer::RunHair(int model, int style, int headJ, const Pose& pose, const M4& world, const void* owner, const M4& place, const M4& pal, ID3D11UnorderedAccessView* vertUAV, float cape, float girth, float gait, float fly, const void* clothMem)
{
	if (m_inPortrait) return;
	ID3D11UnorderedAccessView* dst = vertUAV ? vertUAV : m_hairUAV;
	if (style < 1 || style > 15 || m_hairVN[style] <= 0 || !m_csHair[0] || !m_csHair[1] || !m_cbHairSim || !m_hairGuide || !m_hairVert || !dst) return;
	if (headJ < 0 || headJ >= pose.nJ) return;
	const BOOL portrait = m_inPortrait || !owner;
	auto makeState = [&](ID3D11Buffer** sb, ID3D11UnorderedAccessView** uav) -> BOOL {
		if (*uav) return TRUE;
		TAC_REL(*sb);
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (UINT)HAIR_G * 5u * 16u;
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = 16;
		if (FAILED(m_dev->CreateBuffer(&bd, NULL, sb))) return FALSE;
		D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
		ud.Format = DXGI_FORMAT_UNKNOWN;
		ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		ud.Buffer.NumElements = (UINT)HAIR_G * 5u;
		if (FAILED(m_dev->CreateUnorderedAccessView(*sb, &ud, uav))) { TAC_REL(*sb); return FALSE; }
		return TRUE;
	};
	HairInst* hi = NULL;
	if (portrait) {
		for (HairInst& x : m_hairInst) if (x.slot == m_portraitSlot) { hi = &x; break; }
	} else {
		for (HairInst& x : m_hairInst) if (x.slot < 0 && x.owner == owner) { hi = &x; break; }
	}
	if (!hi) {
		if (m_hairInst.size() < HAIR_INST) { m_hairInst.push_back(HairInst()); hi = &m_hairInst.back(); }
		else {
			if (g_hairN > 0) FlushHairPend();
			hi = &m_hairInst[0];
			for (HairInst& x : m_hairInst) if (x.frame < hi->frame) hi = &x;
		}
		hi->style = -1; hi->valid = 0;
	}
	if (portrait) {
		if (hi->slot != m_portraitSlot || hi->style != style) hi->valid = 0;
		hi->slot = m_portraitSlot;
	} else if (hi->slot >= 0 || hi->owner != owner || hi->style != style) {
		hi->valid = 0;
		hi->slot = -1;
	}
	hi->owner = owner;
	hi->style = style;
	if (!makeState(&hi->sb, &hi->uav)) return;
	ID3D11UnorderedAccessView* stU = hi->uav;
	const float dtRaw = m_time - hi->time;
	int reset = !hi->valid;
	const int capeNow = cape > 0.5f ? 1 : 0;
	if (hi->capeBit != capeNow) reset = 1;
	hi->capeBit = capeNow;
	M4 delta = MIdent();
	M4 rootDelta = MIdent();
	if (!reset) {
		rootDelta = MMul(hi->world, MInverse(world));
		const float* t = &rootDelta.m[12];
		float dx = world.m[12] - hi->world.m[12];
		float dy = world.m[13] - hi->world.m[13];
		float dz = world.m[14] - hi->world.m[14];
		if (t[0] * t[0] + t[1] * t[1] + t[2] * t[2] > 1.5f * 1.5f
			|| dx * dx + dy * dy + dz * dz > 1.5f * 1.5f
			|| dtRaw > 0.25f || dtRaw < 0.f) reset = 1;
	}
	hi->prevFly = fly;
	hi->prevGait = gait;
	if (!reset) {
		// 体の回転だけを残す。平行移動はモデル空間の髪を引きずらない。
		rootDelta.m[12] = 0.f; rootDelta.m[13] = 0.f; rootDelta.m[14] = 0.f;
		delta = rootDelta;
		// 攻撃で頭だけ回ると、髪はモデル空間に残って頭が抜け、禿げて見える。頭の骨に乗せてから風を足す。
		if (hi->placeOk) {
			const M4 headDelta = MMul(MInverse(hi->place), place);
			const float hx = headDelta.m[12], hy = headDelta.m[13], hz = headDelta.m[14];
			if (hx * hx + hy * hy + hz * hz > 0.75f * 0.75f) reset = 1;
			else delta = MMul(headDelta, rootDelta);
		}
	}
	if (reset) delta = MIdent();
	const BOOL step = reset || (hi->frame != m_frame && dtRaw > 1e-4f);
	const float dt = Clampf(dtRaw > 0.f ? dtRaw : 1.f / 60.f, 1.f / 240.f, 1.f / 20.f);
	float velUse[3] = { hi->vel[0], hi->vel[1], hi->vel[2] };
	if (reset) { velUse[0] = velUse[1] = velUse[2] = 0.f; hi->vel[0] = hi->vel[1] = hi->vel[2] = 0.f; }
	else if (dtRaw > 1e-3f) { TacVel(hi->world, world, dtRaw, velUse); hi->vel[0] = velUse[0]; hi->vel[1] = velUse[1]; hi->vel[2] = velUse[2]; }
	struct HCB {
		M4 delta, head, pal, palInv;
		float sim[4], wind[4], air[4], body[4], capA[8][4], capB[8][4];
		unsigned cnt[4];
		float grav[4];
	} cb;
	static_assert(sizeof(HCB) == 608, "HairParm");
	memset(&cb, 0, sizeof(cb));
	cb.delta = delta;
	cb.head = place;
	cb.pal = pal;
	cb.palInv = MInverse(pal);
	cb.sim[0] = dt;
	cb.sim[1] = dt * 60.f;
	cb.sim[2] = reset ? 1.f : 0.f;
	cb.sim[3] = m_time;
	TacWind(m_time, world, velUse, 0.52f, gait, fly, cb.wind, cb.air, NULL);
	if (m_stageWind) {
		const float stage = 2.35f;
		cb.wind[0] *= stage; cb.wind[1] *= stage; cb.wind[2] *= stage;
		cb.air[0] *= stage; cb.air[1] *= stage; cb.air[2] *= stage; cb.air[3] *= stage;
	}
	cb.wind[3] = cape > 0.5f ? 1.f : 0.f;
	TacGrav(world, 4.6f, cb.grav);
	// 体型の太さ。胴はそのまま、首と腕は肉の付き方が半分ほど。
	const float gT = Clampf(girth > 0.1f ? girth : 1.f, 0.8f, 1.4f), gL = 1.f + (gT - 1.f) * 0.6f;
	cb.grav[3] = gT;
	for (int i = 0; i < 8; i++) { cb.capA[i][3] = -1.f; cb.capB[i][3] = -1.f; }
	const Model* md = (model >= 0 && model < (int)models.size()) ? &models[model] : NULL;
	const int rig = md ? md->rig : -1;
	auto findJ = [&](const char* name) -> int {
		if (rig < 0 || rig >= (int)rigs.size()) return -1;
		const int j = rigs[rig].Find(name);
		return (j >= 0 && j < pose.nJ) ? j : -1;
	};
	auto jointOr = [&](const char* name, int fallback) -> int {
		const int j = findJ(name);
		if (j >= 0) return j;
		return (fallback >= 0 && fallback < pose.nJ) ? fallback : -1;
	};
	// パレットの平行移動はバインド位置からの差で、待機ではほぼ 0。関節そのものの位置は逆バインドから取る。
	auto at = [&](int j, float* o) -> BOOL {
		if (j < 0 || j >= pose.nJ) return FALSE;
		V3 b = v3(0.f, 0.f, 0.f);
		if (md && j < md->nJ) {
			const M4 ib = MInverse(md->invBind[j]);
			b = v3(ib.m[12], ib.m[13], ib.m[14]);
		}
		const V3 p = MXformPt(pose.palette[j], b);
		o[0] = p.x; o[1] = p.y; o[2] = p.z;
		return TRUE;
	};
	int nc = 0;
	auto cap = [&](int ja, int jb, float r0, float r1) {
		if (nc >= 8) return;
		float A[3], B[3];
		if (!at(ja, A) || !at(jb, B)) return;
		cb.capA[nc][0] = A[0]; cb.capA[nc][1] = A[1]; cb.capA[nc][2] = A[2]; cb.capA[nc][3] = r0;
		cb.capB[nc][0] = B[0]; cb.capB[nc][1] = B[1]; cb.capB[nc][2] = B[2]; cb.capB[nc][3] = r1;
		nc++;
	};
	const int jNeck = findJ("Neck");
	const int jChest = jointOr("Chest", md ? md->chestJ : -1);
	const int jTorso = jChest >= 0 ? jChest : findJ("Torso");
	const int jHip = jointOr("Hips", md ? md->hipsJ : -1);
	const int jAb = findJ("Abdomen");
	const int jHandL = jointOr("Hand.L", md ? md->handL : -1);
	const int jHandR = jointOr("Hand.R", md ? md->handR : -1);
	// 頭側を 8cm にすると、球が髪型全体を頭蓋へ押し潰して兜になる。首の太さだけにする。
	cap(jNeck, headJ, 0.048f * gL, 0.032f);
	cap(jTorso, jHip >= 0 ? jHip : jAb, 0.090f * gT, 0.125f * gT);
	cap(findJ("Shoulder.L"), findJ("UpperArm.L"), 0.060f * gL, 0.054f * gL);
	cap(findJ("Shoulder.R"), findJ("UpperArm.R"), 0.060f * gL, 0.054f * gL);
	cap(findJ("UpperArm.L"), findJ("LowerArm.L"), 0.056f * gL, 0.048f * gL);
	cap(findJ("UpperArm.R"), findJ("LowerArm.R"), 0.056f * gL, 0.048f * gL);
	cap(findJ("LowerArm.L"), jHandL, 0.040f * gL, 0.034f * gL);
	cap(findJ("LowerArm.R"), jHandR, 0.040f * gL, 0.034f * gL);
	float body[3] = { pal.m[12], pal.m[13] - 0.22f, pal.m[14] };
	float tmp[3];
	if (at(jTorso, tmp)) { body[0] = tmp[0]; body[1] = tmp[1]; body[2] = tmp[2]; }
	else if (at(headJ, tmp)) {
		body[0] = tmp[0]; body[1] = tmp[1] - 0.22f; body[2] = tmp[2];
	}
	cb.body[0] = body[0]; cb.body[1] = body[1]; cb.body[2] = body[2]; cb.body[3] = 1.f;
	cb.cnt[0] = (unsigned)m_hairV0[style];
	cb.cnt[1] = (unsigned)style;
	cb.cnt[2] = (unsigned)nc;
	cb.cnt[3] = (unsigned)m_hairVN[style];
	D3D11_MAPPED_SUBRESOURCE ms;
	ID3D11Buffer* hairSim = TacCbAcquire(g_hairSimR, m_ctx, m_cbHairSim, ms);
	if (!hairSim) return;
	memcpy(ms.pData, &cb, sizeof(cb));
	m_ctx->Unmap(hairSim, 0);
	ID3D11Buffer* clothB = TacCbCur(g_hairClothR, m_cbHairCloth);
	ID3D11Buffer* cbs[2] = { hairSim, clothB };
	m_ctx->CSSetConstantBuffers(0, 2, cbs);
	ID3D11ShaderResourceView* sv[2] = { m_hairGuide, m_hairVert };
	m_ctx->CSSetShaderResources(0, 2, sv);
	ID3D11UnorderedAccessView* uv[2] = { stU, dst };
	UINT zc[2] = { 0, 0 };
	m_ctx->CSSetUnorderedAccessViews(0, 2, uv, zc);
	if (step) {
		m_ctx->CSSetShader(m_csHair[0], NULL, 0);
		m_ctx->Dispatch(1, 1, 1);
	}
	m_ctx->CSSetShader(m_csHair[1], NULL, 0);
	m_ctx->Dispatch(((UINT)m_hairVN[style] + 63u) / 64u, 1, 1);
	hi->frame = m_frame;
	hi->place = place;
	hi->placeOk = 1;
	if (step) { hi->time = m_time; hi->world = world; hi->valid = 1; }
}

// 髪型ごとの頭を覆う殻（最大 256 点）。載せ位置は体ごとに一度だけ探す。
static float g_hairSmp[16][256][3];
static int g_hairSmpN[16];
static int g_seatModel[16];
static float g_seatD[16][3];

void Renderer::LoadHairPack()
{
	ReleaseHair();
	if (!m_dev) return;
	const BYTE* data = NULL;
	DWORD size = 0;
	if (!LockPack(IDR_TAC_HAIR, &data, &size)) return;
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
		// 頭を覆う殻だけ残す。14cm 平均は後ろ髪に寄るので、載せ位置は頭蓋に合わせて後で探す。
		int cnt[16] = {};
		for (UINT i = 0; i < nv; i++) {
			int s = (int)(rec[i * 10 + 9] + 0.5f);
			if (s < 1 || s > 15 || rec[i * 10 + 1] < top[s] - 0.22f) continue;
			cnt[s]++;
		}
		int seen[16] = {};
		for (int s = 0; s < 16; s++) g_hairSmpN[s] = 0;
		for (UINT i = 0; i < nv; i++) {
			int s = (int)(rec[i * 10 + 9] + 0.5f);
			if (s < 1 || s > 15 || rec[i * 10 + 1] < top[s] - 0.22f) continue;
			int step = cnt[s] > 256 ? (cnt[s] + 255) / 256 : 1;
			int k = seen[s]++;
			if (k % step) continue;
			if (g_hairSmpN[s] >= 256) continue;
			g_hairSmp[s][g_hairSmpN[s]][0] = rec[i * 10];
			g_hairSmp[s][g_hairSmpN[s]][1] = rec[i * 10 + 1];
			g_hairSmp[s][g_hairSmpN[s]][2] = rec[i * 10 + 2];
			g_hairSmpN[s]++;
		}
		for (int s = 0; s < 16; s++) g_seatModel[s] = -1;
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
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	static Buf<WORD> ib;
	// 全髪型が一つの索引に入っている。描くのは今の髪型だけにして、残りのカードを頂点シェーダに通さない。
	{
		int cnt[16];
		memset(cnt, 0, sizeof(cnt));
		auto sty = [&](UINT v) -> int {
			if (v >= nv) return 0;
			int s = (int)(rec[v * 10 + 9] + 0.5f);
			return (s >= 1 && s <= 15) ? s : 0;
		};
		for (UINT t = 0; t + 2 < ni; t += 3) {
			int s0 = sty(idx[t]), s1 = sty(idx[t + 1]), s2 = sty(idx[t + 2]);
			int s = (s1 == s2) ? s1 : s0;
			if (s < 0 || s > 15) s = 0;
			cnt[s] += 3;
		}
		int sum = 0;
		for (int s = 0; s < 16; s++) { m_hairI0[s] = sum; m_hairIN[s] = cnt[s]; sum += cnt[s]; }
		ib.resize(sum > 0 ? sum : 1);
		if (ib.size() < sum) { ReleaseHair(); return; }
		int cur[16];
		for (int s = 0; s < 16; s++) cur[s] = m_hairI0[s];
		for (UINT t = 0; t + 2 < ni; t += 3) {
			int s0 = sty(idx[t]), s1 = sty(idx[t + 1]), s2 = sty(idx[t + 2]);
			int s = (s1 == s2) ? s1 : s0;
			if (s < 0 || s > 15) s = 0;
			int w = cur[s];
			ib[(size_t)w] = idx[t];
			ib[(size_t)w + 1] = idx[t + 1];
			ib[(size_t)w + 2] = idx[t + 2];
			cur[s] = w + 3;
		}
		for (int s = 0; s < 16; s++) { m_hairVMin[s] = (int)nv; m_hairVMax[s] = -1; }
		for (UINT i = 0; i < nv; i++) {
			int s = sty(i);
			if ((int)i < m_hairVMin[s]) m_hairVMin[s] = (int)i;
			if ((int)i > m_hairVMax[s]) m_hairVMax[s] = (int)i;
		}
		for (int s = 0; s < 16; s++) {
			int a = m_hairI0[s], b = a + m_hairIN[s];
			for (int k = a; k < b; k++) {
				int v = (int)ib[(size_t)k];
				if (v < m_hairVMin[s]) m_hairVMin[s] = v;
				if (v > m_hairVMax[s]) m_hairVMax[s] = v;
			}
		}
		bd.ByteWidth = (((UINT)sum * 2u) + 3u) & ~3u;
		ib.resize((int)(bd.ByteWidth / 2), 0);
		m_hairDraw.nIdx = (UINT)sum;
	}
	sd.pSysMem = ib.data();
	if (FAILED(m_dev->CreateBuffer(&bd, &sd, &m_hairDraw.ib))) { ReleaseHair(); return; }
	m_hairDraw.iBase = 0;
	m_hairDraw.stride = 36;
	m_hairDraw.i32 = 0;
	m_hairDraw.tex = -2;
	DWORD tl = 0;
	const BYTE* td = NULL;
	if (LockRc(IDR_TAC_HAIRTEX, &td, &tl) && td && tl > 32) {
		IWICImagingFactory* f = TacWicFactory();
		if (f) {
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
			TAC_REL(cv); TAC_REL(fr); TAC_REL(dec); TAC_REL(st);
		}
	}
	if (!m_hairSrv) { ReleaseHair(); return; }
	{
		struct HairGpuG { float p[4]; };
		struct HairGpuV { unsigned a[4]; float w[4]; float p[4]; };
		static_assert(sizeof(HairGpuG) == 16 && sizeof(HairGpuV) == 48, "HairGpu");
		const int HA = HAIR_A, HL = HAIR_L, HG = HAIR_G;
		static Buf<HairGpuG> gd;
		static Buf<HairGpuV> hv;
		static bool s_hairGuideBuilt = false;
		static int s_hairV0[16], s_hairVN[16];
		if (!s_hairGuideBuilt || gd.size() < 16 * HG || hv.empty()) {
			hv.clear();
			gd.resize(16 * HG);
			if (gd.size() < 16 * HG) { ReleaseHair(); return; }
			for (int i = 0; i < gd.size(); i++) { gd[i].p[0] = gd[i].p[1] = gd[i].p[2] = gd[i].p[3] = 0.f; }
			for (int s = 0; s < 16; s++) { m_hairV0[s] = 0; m_hairVN[s] = 0; }
			for (int style = 1; style <= 15; style++) {
				int nSt = 0;
				float swayMax = 0.f;
				for (UINT i = 0; i < nv; i++) {
					const int s = (int)(rec[i * 10 + 9] + 0.5f);
					if (s != style) continue;
					nSt++;
					if (rec[i * 10 + 8] > swayMax) swayMax = rec[i * 10 + 8];
				}
				m_hairV0[style] = hv.size();
				if (nSt < 8 || m_hairCapN[style] < 8) continue;
				if (swayMax < 1e-4f) swayMax = 1.f;
				const float cx = m_hairCap[style][0], cy = m_hairCap[style][1], cz = m_hairCap[style][2];
				float acc[HAIR_G][3];
				float wgt[HAIR_G];
				memset(acc, 0, sizeof(acc));
				memset(wgt, 0, sizeof(wgt));
				auto binOf = [&](float x, float z, float sway, int* id, float* w) {
					float u = (atan2f(x - cx, z - cz) + 3.14159265f) * (float)HA / 6.2831853f;
					if (u < 0.f) u += (float)HA;
					if (u >= (float)HA) u -= (float)HA;
					int u0 = (int)u;
					if (u0 < 0) u0 = 0;
					if (u0 >= HA) u0 = HA - 1;
					const float fu = u - (float)u0;
					const int u1 = (u0 + 1) % HA;
					float v = Clampf(sway / swayMax, 0.f, 1.f) * (float)(HL - 1);
					int v0 = (int)v;
					if (v0 > HL - 2) v0 = HL - 2;
					if (v0 < 0) v0 = 0;
					float fv = v - (float)v0;
					if (sway >= swayMax - 1e-5f) { v0 = HL - 2; fv = 1.f; }
					id[0] = v0 * HA + u0; id[1] = v0 * HA + u1;
					id[2] = (v0 + 1) * HA + u0; id[3] = (v0 + 1) * HA + u1;
					w[0] = (1.f - fu) * (1.f - fv); w[1] = fu * (1.f - fv);
					w[2] = (1.f - fu) * fv; w[3] = fu * fv;
				};
				for (UINT i = 0; i < nv; i++) {
					if ((int)(rec[i * 10 + 9] + 0.5f) != style) continue;
					int id[4]; float w[4];
					binOf(rec[i * 10], rec[i * 10 + 2], rec[i * 10 + 8], id, w);
					for (int e = 0; e < 4; e++) if (w[e] > 0.f) {
						wgt[id[e]] += w[e];
						acc[id[e]][0] += rec[i * 10] * w[e];
						acc[id[e]][1] += rec[i * 10 + 1] * w[e];
						acc[id[e]][2] += rec[i * 10 + 2] * w[e];
					}
				}
				for (int pass = 0; pass < 8; pass++) for (int q = 0; q < HG; q++) if (wgt[q] < 1e-5f) {
					const int ia = q % HA, il = q / HA;
					const int nbh[4] = { il * HA + (ia + HA - 1) % HA, il * HA + (ia + 1) % HA, il > 0 ? q - HA : -1, il < HL - 1 ? q + HA : -1 };
					for (int e = 0; e < 4; e++) if (nbh[e] >= 0 && wgt[nbh[e]] > 1e-5f) {
						wgt[q] = wgt[nbh[e]];
						acc[q][0] = acc[nbh[e]][0]; acc[q][1] = acc[nbh[e]][1]; acc[q][2] = acc[nbh[e]][2];
						break;
					}
				}
				for (int q = 0; q < HG; q++) {
					HairGpuG& g = gd[style * HG + q];
					if (wgt[q] < 1e-5f) { g.p[0] = cx; g.p[1] = cy; g.p[2] = cz; g.p[3] = 0.f; continue; }
					g.p[0] = acc[q][0] / wgt[q]; g.p[1] = acc[q][1] / wgt[q]; g.p[2] = acc[q][2] / wgt[q];
					const float dx = g.p[0] - cx, dy = g.p[1] - cy, dz = g.p[2] - cz;
					g.p[3] = sqrtf(dx * dx + dy * dy + dz * dz);
				}
				for (UINT i = 0; i < nv; i++) {
					if ((int)(rec[i * 10 + 9] + 0.5f) != style) continue;
					int id[4]; float w[4];
					binOf(rec[i * 10], rec[i * 10 + 2], rec[i * 10 + 8], id, w);
					HairGpuV o;
					memset(&o, 0, sizeof(o));
					o.a[0] = i;
					o.a[1] = (unsigned)id[0] | ((unsigned)id[1] << 16);
					o.a[2] = (unsigned)id[2] | ((unsigned)id[3] << 16);
					o.w[0] = w[0]; o.w[1] = w[1]; o.w[2] = w[2]; o.w[3] = w[3];
					o.p[0] = rec[i * 10]; o.p[1] = rec[i * 10 + 1]; o.p[2] = rec[i * 10 + 2];
					o.p[3] = Clampf(rec[i * 10 + 8] / swayMax, 0.f, 1.f);
					hv.push_back(o);
				}
				m_hairVN[style] = hv.size() - m_hairV0[style];
			}
			for (int s = 0; s < 16; s++) { s_hairV0[s] = m_hairV0[s]; s_hairVN[s] = m_hairVN[s]; }
			s_hairGuideBuilt = true;
		} else {
			for (int s = 0; s < 16; s++) { m_hairV0[s] = s_hairV0[s]; m_hairVN[s] = s_hairVN[s]; }
		}
		auto up = [&](const void* src, UINT cnt, UINT stride, ID3D11Buffer** sb, ID3D11ShaderResourceView** srv) -> BOOL {
			if (!src || cnt == 0) return FALSE;
			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = cnt * stride;
			bd.Usage = D3D11_USAGE_IMMUTABLE;
			bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			bd.StructureByteStride = stride;
			D3D11_SUBRESOURCE_DATA sd = { src, 0, 0 };
			if (FAILED(m_dev->CreateBuffer(&bd, &sd, sb))) return FALSE;
			D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
			sv.Format = DXGI_FORMAT_UNKNOWN;
			sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			sv.Buffer.NumElements = cnt;
			if (FAILED(m_dev->CreateShaderResourceView(*sb, &sv, srv))) { TAC_REL(*sb); return FALSE; }
			return TRUE;
		};
		if (!up(gd.data(), (UINT)gd.size(), 16, &m_hairGuideSB, &m_hairGuide)) { ReleaseHair(); return; }
		if (hv.size() > 0 && !up(hv.data(), (UINT)hv.size(), 48, &m_hairVertSB, &m_hairVert)) { ReleaseHair(); return; }
	}
	m_hairNv = nv;
	LoadPulse(4, 4);
}

void Renderer::EnsureScalp(int model, int headJ)
{
	if (model == m_scalpModel && m_capDraw.ib) return;
	for (int s = 0; s < TAC_SCALP_CACHE; s++) {
		if (m_scalps[s].model == model && m_scalps[s].draw.ib) {
			m_scalpModel = model;
			m_capDraw = m_scalps[s].draw;
			m_capBind = m_scalps[s].bind;
			return;
		}
	}
	m_scalpModel = -1;
	m_capDraw.ib = NULL; m_capDraw.nIdx = 0;
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
		// 0 系も 1/2 系も同じ共通素体の頭を使うので、頭蓋の殻は全モデル同じ範囲（後頭〜頭頂〜側頭、額は出す）にする。
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
				// 渦と分け目と、突風で髪が離れる側頭・後頭を髪色で塞ぐ。額と顔（前側）は出す。
				const float dy = cy * (1.f / 3.f) - yTop, dz = cz * (1.f / 3.f) - capCz;
				if (dy < -0.22f) continue;
				if (dy < -0.055f && dz < -0.02f) continue;
				for (int c = 0; c < 3; c++) {
					int v = ix[c];
					if (map[v] < 0) {
						if (m_capBind.size() >= 60000) { map[v] = 0; continue; }
						const CSkinV& src = sk->bind[v];
						float nx = src.p[0] - capCx, ny = src.p[1] - capCy, nz = src.p[2] - capCz;
						float nl = sqrtf(nx * nx + ny * ny + nz * nz);
						if (nl > 1e-6f) { nx /= nl; ny /= nl; nz /= nl; }
						else { nx = 0.f; ny = 1.f; nz = 0.f; }
						const float push = 0.005f;
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
					int slot = -1;
					for (int s = 0; s < TAC_SCALP_CACHE; s++) if (m_scalps[s].model < 0) { slot = s; break; }
					if (slot < 0) { slot = model & (TAC_SCALP_CACHE - 1); m_scalps[slot].draw.Release(); }
					m_scalps[slot].model = model;
					m_scalps[slot].draw = m_capDraw;
					m_scalps[slot].bind = m_capBind;
				}
			}
		}
	}
}

BOOL Renderer::DrawHumanHair(int model, int style, int headJ, const Pose& pose, const M4& world, const DrawParams& dp, int avatar, const void* owner)
{
	if (m_pass == PASS_SHADOW) return TRUE;
	const BOOL capOnly = style < 1;
	if (!capOnly && (m_hairNv < 30 || !m_csSkin || !m_hairBind || !m_hairUAV || !m_hairDraw.ib || !m_cbSkinBone || !m_cbSkinParm || !m_cbHairCloth)) return FALSE;
	if (style < 0 || model < 0 || model >= (int)models.size() || headJ < 0 || headJ >= pose.nJ) return FALSE;
	if (capOnly) {
		EnsureScalp(model, headJ);
		if (!(m_capDraw.ib && m_capBind.size() >= 3)) return FALSE;
		const M4& B = pose.palette[headJ];
		struct CapOnlyMem { const void* owner; unsigned frame; int n; DWORD tint; Buf<VtxS> v; };
		static CapOnlyMem capOnlyMem[48];
		CapOnlyMem* got = NULL;
		if (owner) {
			for (int i = 0; i < 48; i++)
				if (capOnlyMem[i].owner == owner && capOnlyMem[i].frame == m_frame && capOnlyMem[i].tint == dp.tint && capOnlyMem[i].n == (int)m_capBind.size() && capOnlyMem[i].v.size() == capOnlyMem[i].n) {
					got = &capOnlyMem[i];
					break;
				}
		}
		DrawParams dc = dp;
		dc.outline = 0.f;
		dc.twoSided = 1;
		dc.sway = dp.sway;
		dc.rim = 0.02f;
		dc.noTess = 1;
		if (got) {
			DrawStaticVerts(m_capDraw, got->v.data(), (UINT)got->n, world, dc);
			return TRUE;
		}
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
		DrawStaticVerts(m_capDraw, capPosed.data(), (UINT)capPosed.size(), world, dc);
		if (owner) {
			int slot = -1;
			for (int i = 0; i < 48; i++) if (capOnlyMem[i].owner == owner || capOnlyMem[i].owner == NULL) { slot = i; break; }
			if (slot < 0) slot = (int)(m_frame % 48u);
			capOnlyMem[slot].owner = owner;
			capOnlyMem[slot].frame = m_frame;
			capOnlyMem[slot].tint = dp.tint;
			capOnlyMem[slot].n = (int)capPosed.size();
			capOnlyMem[slot].v.resize(capOnlyMem[slot].n);
			if (capOnlyMem[slot].v.size() == capOnlyMem[slot].n)
				memcpy(capOnlyMem[slot].v.data(), capPosed.data(), (size_t)capOnlyMem[slot].n * sizeof(VtxS));
		}
		return TRUE;
	}
	const Model& md = models[model];
	int nj = pose.nJ;
	if (nj > 63) nj = 63;
	if (headJ >= nj) return FALSE;
	EnsureScalp(model, headJ);
	unsigned hkey = TacPoseKey(pose) ^ ((unsigned)style * 0x9E3779B1u) ^ ((unsigned)model << 16) ^ (unsigned)headJ ^ (dp.cape ? 0xC0A7E001u : 0u);
	HairOut* ho = owner ? TakeHairOut(owner) : NULL;
	ID3D11Buffer* hairVB = m_hairVB;
	ID3D11UnorderedAccessView* hairUAV = m_hairUAV;
	BOOL hairFresh = TRUE;
	if (ho && ho->vb && ho->frame == m_frame && ho->key == hkey) {
		hairVB = ho->vb;
		hairFresh = FALSE;
	} else if (ho && ho->uav) {
		hairVB = ho->vb;
		hairUAV = ho->uav;
	}
	const CreatureSkin* hs = NULL;
	for (int k = 0; k < (int)m_skins.size(); k++)
		if (m_skins[k].model == model && m_skins[k].crownOk) { hs = &m_skins[k]; break; }
	// 素体を描いているときは、カタログの頭蓋ではなく、その素体の頭の骨に髪の根を合わせる。
	BOOL hostSkull = FALSE;
	if (avatar > 0) {
		const AvaModel* amHost = NULL;
		for (const AvaModel& x : m_avaModels) if (!strcmp(x.name, md.name)) { amHost = &x; break; }
		int ciHost = -1;
		if (amHost) {
			for (int k = 0, n = 0; k < (int)m_avaCos.size(); k++) if (m_avaCos[k].gender == amHost->gender && ++n == avatar) { ciHost = k; break; }
		}
		const AvaCache* host = NULL;
		for (const AvaCache& c : m_avaCache) if (c.model == model && c.cos == ciHost && c.skin.crownOk && c.skin.skullC[1] > 0.4f) { host = &c; break; }
		if (!host) for (const AvaCache& c : m_avaCache) if (c.model == model && c.skin.crownOk && c.skin.skullC[1] > 0.4f) { host = &c; break; }
		if (host) { hs = &host->skin; hostSkull = TRUE; }
	}
	if (hairFresh) {
	const AvaModel* am = NULL;
	for (const AvaModel& x : m_avaModels) if (!strcmp(x.name, md.name)) { am = &x; break; }
	int headSeg = -1;
	for (int s = 0; s < m_avaSegN; s++) if (!strcmp(m_avaSeg[s], "Head")) { headSeg = s; break; }
	if (!am || headSeg < 0 || am->M.size() < (size_t)(headSeg + 1) * 12) return FALSE;
	D3D11_MAPPED_SUBRESOURCE ms;
	ID3D11Buffer* skinBone = TacCbAcquire(g_skinBoneR, m_ctx, m_cbSkinBone, ms);
	if (!skinBone) return FALSE;
	if (nj) memcpy(ms.pData, pose.palette, sizeof(M4) * (size_t)nj);
	if (nj < 63) memset((M4*)ms.pData + nj, 0, sizeof(M4) * (size_t)(63 - nj));
	float* slot = ((float*)ms.pData) + 63 * 16;
	const float* seg = &am->M[(size_t)headSeg * 12];
	// 区間行列は列ベクトル（平行移動は 3,7,11）。髪のコンピュートは行ベクトルなので転置して載せる。
	slot[0] = seg[0]; slot[1] = seg[4]; slot[2] = seg[8]; slot[3] = 0.f;
	slot[4] = seg[1]; slot[5] = seg[5]; slot[6] = seg[9]; slot[7] = 0.f;
	slot[8] = seg[2]; slot[9] = seg[6]; slot[10] = seg[10]; slot[11] = 0.f;
	slot[12] = seg[3]; slot[13] = seg[7]; slot[14] = seg[11]; slot[15] = 1.f;
	// 頭頂の型は輪で、天辺を頭蓋の天辺に揃えると渦の穴から頭が出る。1.8cm 上に置き、
	// 内側に入った分はシェーダが頭蓋の外へ出す。左右前後は頭蓋の外形の中心へ揃え、顔や片耳へずれて禿げないようにする。
	if (hs && hs->crownOk && hs->crown[1] > 0.4f) {
		int ref = (style >= 1 && style <= 15 && m_hairCapN[style] >= 8) ? style : ((m_hairCapN[1] >= 8) ? 1 : -1);
		if (ref < 0) {
			for (int s = 1; s <= 15; s++) if (m_hairCapN[s] >= 8) { ref = s; break; }
		}
		if (ref >= 0) {
			const float cx = m_hairCap[ref][0], cy = m_hairCap[ref][1], cz = m_hairCap[ref][2];
			const float rx = seg[0] * cx + seg[1] * cy + seg[2] * cz + seg[3];
			const float ry = seg[4] * cx + seg[5] * cy + seg[6] * cz + seg[7];
			const float rz = seg[8] * cx + seg[9] * cy + seg[10] * cz + seg[11];
			float liftDy = (hs->crown[1] + 0.018f) - ry;
			if (liftDy < 0.008f) liftDy = 0.008f;
			else if (liftDy > 0.22f) liftDy = 0.22f;
			slot[13] += liftDy;
			float shiftX = Clampf(hs->crown[0] - rx, -0.025f, 0.025f);
			float shiftZ = Clampf((hs->skullC[2] - 0.006f) - rz, -0.025f, 0.025f);
			slot[12] += shiftX;
			slot[14] += shiftZ;
		}
	}
	M4 hairSeg;
	memcpy(hairSeg.m, slot, sizeof(hairSeg.m));
	const M4 hairPlace = MMul(hairSeg, pose.palette[headJ]);
	m_ctx->Unmap(skinBone, 0);
	float cloth[36 + 4 + SKULL_NA * SKULL_NE];
	static_assert(sizeof(cloth) == 672, "hair cloth");
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
	ID3D11Buffer* hairCloth = TacCbAcquire(g_hairClothR, m_ctx, m_cbHairCloth, ms);
	if (!hairCloth) return FALSE;
	memcpy(ms.pData, cloth, sizeof(cloth));
	m_ctx->Unmap(hairCloth, 0);
	struct Parm { float nip0[4], nip1[4], bust[4], info[4]; unsigned extra[4]; float jig[4]; };
	Parm prm;
	memset(&prm, 0, sizeof(prm));
	prm.nip0[0] = (float)style;
	prm.nip0[1] = (float)headJ;
	prm.nip0[2] = m_time;
	prm.bust[0] = -1.f;
	prm.info[1] = (float)nj;
	UINT vBase = 0, vEnd = m_hairNv;
	if (style >= 1 && style < 16 && m_hairVMax[style] >= m_hairVMin[style] && m_hairVMin[style] >= 0) {
		vBase = (UINT)m_hairVMin[style];
		vEnd = (UINT)m_hairVMax[style] + 1u;
		if (vEnd > m_hairNv) vEnd = m_hairNv;
	}
	prm.extra[0] = vBase;
	prm.extra[1] = vEnd;
	ID3D11Buffer* skinParm = TacCbAcquire(g_skinParmR, m_ctx, m_cbSkinParm, ms);
	if (!skinParm) return FALSE;
	memcpy(ms.pData, &prm, sizeof(prm));
	m_ctx->Unmap(skinParm, 0);
	ID3D11Buffer* noneVB = NULL;
	UINT zero = 0;
	m_ctx->IASetVertexBuffers(0, 1, &noneVB, &zero, &zero);
	ID3D11Buffer* cbs[3] = { skinBone, skinParm, hairCloth };
	m_ctx->CSSetConstantBuffers(0, 3, cbs);
	ID3D11ShaderResourceView* srvs[2] = { m_hairBind, m_faceDummySRV };
	m_ctx->CSSetShaderResources(0, 2, srvs);
	if (TacQueueUses(hairVB)) FlushQueuedDraws();
	UINT cnt = 0;
	m_ctx->CSSetUnorderedAccessViews(0, 1, &hairUAV, &cnt);
	const BOOL hairSim = style >= 1 && style < 16 && m_hairVN[style] > 0 && m_csHair[0] && m_csHair[1]
		&& m_cbHairSim && m_hairGuide && m_hairVert;
	// 模擬は位置と法線だけを書く。色はスキンが書く。ここを省くとロングが透明になる。
	m_ctx->CSSetShader(m_csSkin, NULL, 0);
	if (vEnd > vBase)
		m_ctx->Dispatch(((vEnd - vBase) + 63u) / 64u, 1, 1);
	if (hairSim)
		RunHair(model, style, headJ, pose, world, owner, hairPlace, pose.palette[headJ], hairUAV, (float)dp.cape, dp.girth, dp.gait, dp.fly, cloth);
	{
		ID3D11UnorderedAccessView* nou[2] = { NULL, NULL };
		m_ctx->CSSetUnorderedAccessViews(0, 2, nou, NULL);
	}
	if (ho) { ho->frame = m_frame; ho->key = hkey; hairVB = ho->vb; }
	}
	if (m_capDraw.ib && m_capBind.size() >= 3 && headJ < pose.nJ) {
		const M4& B = pose.palette[headJ];
		struct CapMem { const void* owner; unsigned frame; int n; Buf<VtxS> v; VtxS dome[25]; int domeOk; };
		static CapMem capMem[48];
		CapMem* got = NULL;
		if (owner) {
			for (int i = 0; i < 48; i++)
				if (capMem[i].owner == owner && capMem[i].frame == m_frame && (hostSkull || (capMem[i].n == (int)m_capBind.size() && capMem[i].v.size() == capMem[i].n))) {
					got = &capMem[i];
					break;
				}
		}
		static Buf<VtxS> posed;
		if (!got) {
		if (!hostSkull) {
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
			dc.sway = dp.sway;
			dc.rim = 0.02f;
			dc.mode = 7.f;
			dc.noTess = 1;
			DrawStaticVerts(m_capDraw, posed.data(), (UINT)posed.size(), world, dc);
			if (owner) {
				int slot = -1;
				for (int i = 0; i < 48; i++) if (capMem[i].owner == owner || capMem[i].owner == NULL) { slot = i; break; }
				if (slot < 0) slot = (int)(m_frame % 48u);
				capMem[slot].owner = owner;
				capMem[slot].frame = m_frame;
				capMem[slot].n = (int)posed.size();
				capMem[slot].domeOk = 0;
				capMem[slot].v.resize(capMem[slot].n);
				if (capMem[slot].v.size() == capMem[slot].n)
					memcpy(capMem[slot].v.data(), posed.data(), (size_t)capMem[slot].n * sizeof(VtxS));
				got = &capMem[slot];
			}
		}
		} else if (owner) {
			int slot = -1;
			for (int i = 0; i < 48; i++) if (capMem[i].owner == owner || capMem[i].owner == NULL) { slot = i; break; }
			if (slot < 0) slot = (int)(m_frame % 48u);
			capMem[slot].owner = owner;
			capMem[slot].frame = m_frame;
			capMem[slot].n = 0;
			capMem[slot].domeOk = 0;
			got = &capMem[slot];
		}
		} else if (!hostSkull && got->n > 0) {
			DrawParams dc = dp;
			dc.outline = 0.f;
			dc.twoSided = 1;
			dc.sway = dp.sway;
			dc.rim = 0.02f;
			dc.mode = 7.f;
			dc.noTess = 1;
			if (!hostSkull) DrawStaticVerts(m_capDraw, got->v.data(), (UINT)got->n, world, dc);
		}
		// ポニーテールの渦はカードが地肌と同じ深さで、頭頂だけ地肌が残る。頭蓋の外 8mm の笠でその穴を塞ぐ。
		if (hs && hs->crownOk && hs->skullC[1] > 0.4f) {
			const int N = 12;
			static GpuMesh domeM;
			static int domeReady = 0;
			if (!domeReady && m_dev) {
				WORD ib[36 * 3];
				int n = 0;
				for (int i = 0; i < N; i++) {
					int a = 1 + i, b = 1 + (i + 1) % N, c0 = 1 + N + i, d0 = 1 + N + (i + 1) % N;
					ib[n++] = 0; ib[n++] = (WORD)a; ib[n++] = (WORD)b;
					ib[n++] = (WORD)a; ib[n++] = (WORD)c0; ib[n++] = (WORD)b;
					ib[n++] = (WORD)b; ib[n++] = (WORD)c0; ib[n++] = (WORD)d0;
				}
				D3D11_BUFFER_DESC bd = {};
				bd.ByteWidth = sizeof(ib);
				bd.Usage = D3D11_USAGE_IMMUTABLE;
				bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
				D3D11_SUBRESOURCE_DATA sd = { ib, 0, 0 };
				if (SUCCEEDED(m_dev->CreateBuffer(&bd, &sd, &domeM.ib))) {
					domeM.nIdx = n;
					domeM.stride = sizeof(VtxS);
					domeM.tex = -1;
					domeReady = 1;
				}
			}
			if (domeReady) {
				if (got && got->domeOk) {
					DrawParams dc = dp;
					dc.outline = 0.f;
					dc.twoSided = 1;
					dc.sway = dp.sway;
					dc.rim = 0.02f;
					dc.mode = 7.f;
					dc.noTess = 1;
					DrawStaticVerts(domeM, got->dome, 1 + N * 2, world, dc);
				} else {
					float c[3] = { hs->skullC[0], hs->skullC[1], hs->skullC[2] };
					float up[3] = { hs->crown[0] - c[0], hs->crown[1] - c[1], hs->crown[2] - c[2] };
					float rl = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
					if (rl > 0.04f) {
						up[0] /= rl; up[1] /= rl; up[2] /= rl;
						float side[3] = { up[1] * 1.f - up[2] * 0.f, up[2] * 0.f - up[0] * 1.f, up[0] * 0.f - up[1] * 0.f };
						float sl = sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
						if (sl < 0.2f) { side[0] = 0.f; side[1] = up[2]; side[2] = -up[1]; sl = sqrtf(side[1] * side[1] + side[2] * side[2]); }
						side[0] /= sl; side[1] /= sl; side[2] /= sl;
						float fwd[3] = { side[1] * up[2] - side[2] * up[1], side[2] * up[0] - side[0] * up[2], side[0] * up[1] - side[1] * up[0] };
						const float R = rl + 0.008f;
						auto put = [&](float x, float y, float z, float nx, float ny, float nz) -> VtxS {
							VtxS d;
							memset(&d, 0, sizeof(d));
							d.p[0] = x * B.m[0] + y * B.m[4] + z * B.m[8] + B.m[12];
							d.p[1] = x * B.m[1] + y * B.m[5] + z * B.m[9] + B.m[13];
							d.p[2] = x * B.m[2] + y * B.m[6] + z * B.m[10] + B.m[14];
							float nnx = nx * B.m[0] + ny * B.m[4] + nz * B.m[8];
							float nny = nx * B.m[1] + ny * B.m[5] + nz * B.m[9];
							float nnz = nx * B.m[2] + ny * B.m[6] + nz * B.m[10];
							float nl = sqrtf(nnx * nnx + nny * nny + nnz * nnz);
							if (nl > 1e-6f) { nnx /= nl; nny /= nl; nnz /= nl; }
							d.n[0] = nnx; d.n[1] = nny; d.n[2] = nnz;
							d.c = m_capC;
							return d;
						};
						VtxS dv[1 + N * 2];
						float tx = c[0] + up[0] * R, ty = c[1] + up[1] * R, tz = c[2] + up[2] * R;
						dv[0] = put(tx, ty, tz, up[0], up[1], up[2]);
						static const float kCa[2] = { 0.913089f, 0.659983f };
						static const float kSa[2] = { 0.407760f, 0.751280f };
						static const float kCs[N] = { 1.f, 0.8660254f, 0.5f, 0.f, -0.5f, -0.8660254f, -1.f, -0.8660254f, -0.5f, 0.f, 0.5f, 0.8660254f };
						static const float kSn[N] = { 0.f, 0.5f, 0.8660254f, 1.f, 0.8660254f, 0.5f, 0.f, -0.5f, -0.8660254f, -1.f, -0.8660254f, -0.5f };
						for (int ring = 0; ring < 2; ring++) {
							float ca = kCa[ring], sa = kSa[ring];
							for (int i = 0; i < N; i++) {
								float cs = kCs[i], sn = kSn[i];
								float dx = up[0] * ca + (side[0] * cs + fwd[0] * sn) * sa;
								float dy = up[1] * ca + (side[1] * cs + fwd[1] * sn) * sa;
								float dz = up[2] * ca + (side[2] * cs + fwd[2] * sn) * sa;
								dv[1 + ring * N + i] = put(c[0] + dx * R, c[1] + dy * R, c[2] + dz * R, dx, dy, dz);
							}
						}
						if (got) {
							memcpy(got->dome, dv, sizeof(dv));
							got->domeOk = 1;
						}
						DrawParams dc = dp;
						dc.outline = 0.f;
						dc.twoSided = 1;
						dc.sway = dp.sway;
						dc.rim = 0.02f;
						dc.mode = 7.f;
						dc.noTess = 1;
						DrawStaticVerts(domeM, dv, 1 + N * 2, world, dc);
					}
				}
			}
		}
	}
	ID3D11Buffer* saved = m_hairDraw.vb;
	m_hairDraw.vb = hairVB ? hairVB : m_hairVB;
	DrawParams hd = dp;
	if (hd.mode < 0.5f) hd.mode = 7.f;
	hd.noTess = 1;
	UINT keepN = m_hairDraw.nIdx, keepB = m_hairDraw.iBase;
	if (style >= 1 && style < 16 && m_hairIN[style] >= 3) {
		m_hairDraw.iBase = (UINT)m_hairI0[style];
		m_hairDraw.nIdx = (UINT)m_hairIN[style];
	}
	DrawStatic(m_hairDraw, world, hd);
	m_hairDraw.nIdx = keepN;
	m_hairDraw.iBase = keepB;
	m_hairDraw.vb = saved;
	return TRUE;
}

float Renderer::AvatarClothBack(int model, int avatar, float y)
{
	struct Hit { int model, avatar; float y, z; };
	static Hit hit[24];
	static int hitN = 0;
	for (int i = 0; i < hitN; i++)
		if (hit[i].model == model && hit[i].avatar == avatar && hit[i].y == y) return hit[i].z;
	float z = -1e9f;
	if (model < 0 || model >= (int)models.size() || avatar <= 0 || m_avaSegN <= 0) z = -1e9f;
	else {
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
	z = ac->backZ[i] * (1.f - f) + ac->backZ[i + 1] * f;
	}
	if (hitN < 24) {
		hit[hitN].model = model;
		hit[hitN].avatar = avatar;
		hit[hitN].y = y;
		hit[hitN].z = z;
		hitN++;
	}
	return z;
}

void Renderer::DrawGear(int i, const M4& world, const DrawParams& dp)
{
	if (i < 0 || i >= (int)m_gear.size()) return;
	DrawParams d = dp;
	d.noTess = 1;
	if (d.mode > 0.5f && d.mode < 2.5f) d.mode = 0.f;
	DrawStatic(m_gear[i], world, d);
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
	int a0 = 0, a1 = 0, b0 = 0, b1 = 0;
	float af = 0.f, bf = 0.f;
	if (A && A->nFrames > 1) {
		float f = ta * A->fps, span = (float)(A->nFrames - 1);
		if (la) { f = fmodf(f, span); if (f < 0) f += span; }
		else f = Clampf(f, 0, span);
		a0 = (int)f; a1 = min(A->nFrames - 1, a0 + 1); af = f - a0;
	}
	if (B && blend < 1.f && B->nFrames > 1) {
		float f = tb * B->fps, span = (float)(B->nFrames - 1);
		if (lb) { f = fmodf(f, span); if (f < 0) f += span; }
		else f = Clampf(f, 0, span);
		b0 = (int)f; b1 = min(B->nFrames - 1, b0 + 1); bf = f - b0;
	}
	const int nJ = m.nJ;
	for (int j = 0; j < nJ; j++) {
		Q4 q = m.restR[j];
		V3 t = m.restT[j];
		if (A) q = (A->nFrames <= 1) ? A->q[j] : QNlerp(A->q[(size_t)a0 * nJ + j], A->q[(size_t)a1 * nJ + j], af);
		if (B && blend < 1.f) {
			Q4 qb = (B->nFrames <= 1) ? B->q[j] : QNlerp(B->q[(size_t)b0 * nJ + j], B->q[(size_t)b1 * nJ + j], bf);
			q = QNlerp(qb, q, blend);
		}
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

// 同じ時刻の風は定数バッファも中身も同じ。ClearState のあとだけ結び直す。
static int g_shaderAirOk = 0;
static float g_shaderAirTime = 0.f;
static int g_fcbLive = 0;
static unsigned char g_fcbSnap[1024];
static int g_ptSavedOk = 0;
static unsigned char g_ptSavedFcb[1024];
static M4 g_ptSavedLvp;
static void TacBindCacheDrop()
{
	g_shaderAirOk = 0;
	g_fcbLive = 0;
}

// マップシェーダが頂点と画素のたびに回していた 8 サンプル。服の TacAirHeld（32）とは別式。
static void TacUploadShaderAir(ID3D11DeviceContext* ctx, ID3D11Buffer* fallback, float time)
{
	if (g_shaderAirOk && g_shaderAirTime == time) return;
	float bx = 0.f, bz = 0.f, mx = 0.f, mz = 0.f, y = 0.f;
	for (int i = 0; i < 8; ++i) {
		const float t = time - (float)i * (4.f / 60.f);
		const float breeze = 0.62f + 0.38f * sinf(t * 0.31f);
		const float a = t * 0.11f;
		bx += cosf(a) * (0.40f * breeze);
		bz += sinf(a) * (0.40f * breeze);
		const float strong = TacGate(sinf(t * 0.17f + 0.6f), 0.55f);
		const float gale = TacGate(sinf(t * 0.083f + 2.4f), 0.90f);
		const float aS = t * 0.047f + 1.9f;
		const float aG = t * 0.031f + 4.4f;
		mx += cosf(aS) * strong * 1.30f + cosf(aG) * gale * 2.70f;
		mz += sinf(aS) * strong * 1.30f + sinf(aG) * gale * 2.70f;
		const float down = TacGate(sinf(t * 0.13f + 1.2f), 0.72f);
		const float up = TacGate(sinf(t * 0.19f + 3.6f), 0.78f);
		y += up * 0.95f - down * 1.15f;
	}
	const float inv = 0.125f;
	bx *= inv; bz *= inv; mx *= inv; mz *= inv; y *= inv;
	float gust = sqrtf(mx * mx + mz * mz) / 2.70f + sqrtf(bx * bx + bz * bz) * 0.45f + fabsf(y) * 0.28f;
	if (gust < 0.f) gust = 0.f;
	if (gust > 1.f) gust = 1.f;
	float o[8] = { bx, bz, mx, mz, y, gust, 0.f, 0.f };
	D3D11_MAPPED_SUBRESOURCE ms;
	ID3D11Buffer* b = TacCbAcquire(g_airR, ctx, fallback, ms);
	if (!b) return;
	memcpy(ms.pData, o, sizeof(o));
	ctx->Unmap(b, 0);
	ctx->VSSetConstantBuffers(4, 1, &b);
	ctx->PSSetConstantBuffers(4, 1, &b);
	ctx->HSSetConstantBuffers(4, 1, &b);
	ctx->DSSetConstantBuffers(4, 1, &b);
	g_shaderAirTime = time;
	g_shaderAirOk = 1;
}

// ============================================================ フレーム
void Renderer::UpdateFrameCB()
{
	FlushQueuedDraws();
	static_assert(sizeof(m_fcb) <= sizeof(g_fcbSnap), "frame cb");
	if (g_fcbLive && memcmp(g_fcbSnap, &m_fcb, sizeof(m_fcb)) == 0) {
		TacUploadShaderAir(m_ctx, m_cbAir, m_fcb.eye[3]);
		return;
	}
	D3D11_MAPPED_SUBRESOURCE ms;
	ID3D11Buffer* b = TacCbAcquire(g_frameR, m_ctx, m_cbFrame, ms);
	if (!b) return;
	memcpy(ms.pData, &m_fcb, sizeof(m_fcb));
	m_ctx->Unmap(b, 0);
	m_ctx->VSSetConstantBuffers(0, 1, &b);
	m_ctx->PSSetConstantBuffers(0, 1, &b);
	m_ctx->HSSetConstantBuffers(0, 1, &b);
	m_ctx->DSSetConstantBuffers(0, 1, &b);
	memcpy(g_fcbSnap, &m_fcb, sizeof(m_fcb));
	g_fcbLive = 1;
	TacUploadShaderAir(m_ctx, m_cbAir, m_fcb.eye[3]);
}

void Renderer::BeginFrame()
{
	MarkGpuBegin();
	FlushQueuedDraws();
	m_avaLoading = 0;
	m_avaFlashed = FALSE;
	if (g_avaNeedReset) {
		g_avaNeedReset = 0;
		ReleaseAvatarCache();
		TacAvaReset();
	}
	m_frame++;
	m_previewPost = FALSE;
	m_fcb.q[0] = (float)m_quality;
	// 無印の整数はこれまでの段と同じ。+ は隣の段へ寄せ、限界突破+10 だけ少し上。
	float rank = TacQRank(m_quality, m_qPlus);
	m_fcb.q[1] = TacQLerp(rank, 0.f, 0.f, 0.f, 2.6f, 5.2f, 6.8f, 8.6f, 10.2f);
	m_fcb.q[2] = TacQLerp(rank, 0.f, 0.f, 0.f, 0.f, 0.055f, 0.08f, 0.11f, 0.135f);
	m_fcb.q[3] = TacQLerp(rank, 0.f, 0.f, 0.42f, 0.55f, 1.f, 1.25f, 1.55f, 1.8f);
	m_fcb.sight[0] = m_fcb.sight[1] = m_fcb.sight[2] = m_fcb.sight[3] = 0;
	memset(m_fcb.ally, 0, sizeof(m_fcb.ally));
	m_fcb.camNear[0] = m_fcb.camNear[1] = m_fcb.camNear[2] = m_fcb.camNear[3] = 0;
	m_fcb.camFwd[0] = m_fcb.camFwd[1] = m_fcb.camFwd[2] = m_fcb.camFwd[3] = 0;
	m_fcb.wxPack[0] = (float)m_wx;
	m_fcb.wxPack[1] = (float)m_hour;
	m_fcb.wxPack[2] = TacQRank(m_quality, m_qPlus);
	m_fcb.wxPack[3] = 0;
	m_fcb.reflectInfo[0] = m_fcb.reflectInfo[1] = m_fcb.reflectInfo[2] = m_fcb.reflectInfo[3] = 0;
	m_cutDeck = -1;
	ID3D11Buffer* frame = TacCbCur(g_frameR, m_cbFrame);
	m_ctx->VSSetConstantBuffers(0, 1, &frame);
	m_ctx->PSSetConstantBuffers(0, 1, &frame);
	TacUploadShaderAir(m_ctx, m_cbAir, m_fcb.eye[3]);
	if (m_cbRipple) {
		m_ctx->VSSetConstantBuffers(3, 1, &m_cbRipple);
		m_ctx->PSSetConstantBuffers(3, 1, &m_cbRipple);
	}
	m_ctx->HSSetConstantBuffers(0, 1, &frame);
	m_ctx->DSSetConstantBuffers(0, 1, &frame);
	ID3D11Buffer* cb[2] = { m_cbObj, TacCbCur(g_boneR, m_cbBones) };
	m_ctx->VSSetConstantBuffers(1, 2, cb);
	m_ctx->PSSetConstantBuffers(1, 1, &m_cbObj);
	m_ctx->HSSetConstantBuffers(1, 1, &m_cbObj);
	m_ctx->DSSetConstantBuffers(1, 1, &m_cbObj);
	g_cbBound = m_cbObj;
	g_objValid = FALSE;
	ID3D11SamplerState* ss[3] = { m_sLin, m_sCmp, m_sClamp };
	m_ctx->PSSetSamplers(0, 3, ss);
	m_fcb.screen[0] = (float)m_w; m_fcb.screen[1] = (float)m_h; m_fcb.screen[2] = 1.f / m_w; m_fcb.screen[3] = 1.f / m_h;
	SetLiquidRipples(NULL, 0);
}

void Renderer::SetCamera(const M4& view, const M4& proj, V3 eye, float time)
{
	m_view = view; m_proj = proj; m_vp = MMul(view, proj); m_eye = eye; m_time = time;
	m_fcb.vp = m_vp;
	if (!m_inPortrait) m_fcb.invVP = MInverse(m_vp);
	m_fcb.eye[0] = eye.x; m_fcb.eye[1] = eye.y; m_fcb.eye[2] = eye.z; m_fcb.eye[3] = time;
}

void Renderer::SetLight(V3 dir, V3 focus, float radius)
{
	dir = Norm(dir);
	// マップだけ、光の高さを保ったまま水平にゆっくり回す。約3分で一周。
	// キャラの向きは決まらないので、一方だけが陰に固定されないようにする。顔枠と作成プレビューは止めたまま。
	if (radius > 5.f) {
		float yaw = fmodf(m_time * 0.035f, 6.2831853f);
		float c = cosf(yaw), s = sinf(yaw);
		float x = dir.x * c - dir.z * s;
		float z = dir.x * s + dir.z * c;
		dir.x = x;
		dir.z = z;
		dir = Norm(dir);
	}
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

void Renderer::SetSight(const float* xz, int n, float radius)
{
	if (n < 0) n = 0;
	if (n > 16) n = 16;
	m_fcb.sight[0] = radius;
	m_fcb.sight[1] = (float)m_stageId;
	m_fcb.sight[2] = (float)n;
	m_fcb.sight[3] = (radius > 0.5f && n > 0 && xz) ? 1.f : 0.f;
	memset(m_fcb.ally, 0, sizeof(m_fcb.ally));
	if (!xz || m_fcb.sight[3] < 0.5f) return;
	for (int i = 0; i < n; i++) {
		m_fcb.ally[i][0] = xz[i * 2];
		m_fcb.ally[i][1] = xz[i * 2 + 1];
		m_fcb.ally[i][2] = 1.f;
	}
}

void Renderer::SetAmbient(DWORD sky, DWORD gnd, float strength, float hard)
{
	m_fcb.skyAmb[0] = (sky & 255) / 255.f; m_fcb.skyAmb[1] = ((sky >> 8) & 255) / 255.f; m_fcb.skyAmb[2] = ((sky >> 16) & 255) / 255.f; m_fcb.skyAmb[3] = strength;
	m_fcb.gndAmb[0] = (gnd & 255) / 255.f; m_fcb.gndAmb[1] = ((gnd >> 8) & 255) / 255.f; m_fcb.gndAmb[2] = ((gnd >> 16) & 255) / 255.f; m_fcb.gndAmb[3] = hard;
}

void Renderer::BeginShadowPass()
{
	FlushQueuedDraws();
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
	m_dsWant = m_dsWrite;
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
	M4 keep = m_fcb.vp;
	m_fcb.vp = m_lightVP;
	UpdateFrameCB();
	m_fcb.vp = keep;
}

BOOL Renderer::BeginReflectPass(float planeY, V3 lookAt)
{
	FlushQueuedDraws();
	if (!m_ctx || m_inPortrait || !m_refRtv || !m_refDsv || !m_refSrv) return FALSE;
	if (m_quality <= 0 || m_quality == 4) return FALSE;
	m_reflecting = 1;
	m_pass = PASS_MAIN;
	m_refSavedVp = m_fcb.vp;
	m_refSavedInv = m_fcb.invVP;
	m_refSavedEyeY = m_fcb.eye[1];
	m_refSavedSx = m_fcb.screen[0];
	m_refSavedSy = m_fcb.screen[1];
	m_refSavedNear0 = m_fcb.camNear[0];
	m_refSavedNear1 = m_fcb.camNear[1];
	m_fcb.camNear[0] = m_fcb.camNear[1] = 0;
	m_refSavedDeck = m_cutDeck;
	m_cutDeck = -1;
	V3 reye = m_eye;
	reye.y = planeY * 2.f - m_eye.y;
	V3 rat = lookAt;
	rat.y = planeY * 2.f - lookAt.y;
	V3 dir = rat - reye;
	if (fabsf(dir.x) + fabsf(dir.z) < 0.08f) rat.z += 0.25f;
	M4 view = MLookAtLH(reye, rat, v3(0, -1.f, 0));
	m_fcb.reflectVP = MMul(view, m_proj);
	m_refSavedCull = m_vp;
	m_vp = m_fcb.reflectVP;
	m_fcb.vp = m_fcb.reflectVP;
	m_fcb.invVP = MInverse(m_fcb.reflectVP);
	m_fcb.eye[1] = reye.y;
	m_fcb.screen[0] = (float)m_refW;
	m_fcb.screen[1] = (float)m_refH;
	m_fcb.reflectInfo[0] = 0;
	m_fcb.reflectInfo[1] = planeY;
	m_fcb.reflectInfo[2] = 1.f;
	m_fcb.reflectInfo[3] = m_refBlur;
	ID3D11ShaderResourceView* refOff = NULL;
	m_ctx->PSSetShaderResources(8, 1, &refOff);
	ID3D11RenderTargetView* rt = m_refRtv;
	m_ctx->OMSetRenderTargets(1, &rt, m_refDsv);
	// 反射の色も消す。残すと前フレームの反射が透けて残る。
	float cc[4] = { 0.04f, 0.05f, 0.07f, 1.f };
	m_ctx->ClearRenderTargetView(m_refRtv, cc);
	m_ctx->ClearDepthStencilView(m_refDsv, D3D11_CLEAR_DEPTH, 1.f, 0);
	D3D11_VIEWPORT vp = { 0, 0, (float)m_refW, (float)m_refH, 0, 1 };
	m_ctx->RSSetViewports(1, &vp);
	m_ctx->RSSetState(m_rsFront);
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_dsWant = m_dsWrite;
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
	UpdateFrameCB();
	return TRUE;
}

void Renderer::EndReflectPass()
{
	FlushQueuedDraws();
	if (!m_reflecting) return;
	m_reflecting = 0;
	m_vp = m_refSavedCull;
	m_fcb.vp = m_refSavedVp;
	m_fcb.invVP = m_refSavedInv;
	m_fcb.eye[1] = m_refSavedEyeY;
	m_fcb.screen[0] = m_refSavedSx;
	m_fcb.screen[1] = m_refSavedSy;
	m_fcb.camNear[0] = m_refSavedNear0;
	m_fcb.camNear[1] = m_refSavedNear1;
	m_cutDeck = m_refSavedDeck;
	m_fcb.reflectInfo[0] = m_refSrv ? 1.f : 0.f;
	m_fcb.reflectInfo[2] = 0;
	m_ctx->RSSetState(m_rsBack);
	ID3D11RenderTargetView* none = NULL;
	m_ctx->OMSetRenderTargets(1, &none, NULL);
	UpdateFrameCB();
}

void Renderer::ReuseReflect(float planeY)
{
	if (!m_refSrv) return;
	m_fcb.reflectInfo[0] = 1.f;
	m_fcb.reflectInfo[1] = planeY;
	m_fcb.reflectInfo[2] = 0.f;
	m_fcb.reflectInfo[3] = m_refBlur;
}

void Renderer::BeginMainPass()
{
	FlushQueuedDraws();
	if (!m_inPortrait) PollStageLook();
	m_pass = m_inPortrait ? PASS_PORTRAIT : PASS_MAIN;
	if (m_inPortrait) {
		int sx = (m_portraitSlot % 6) * PORTRAIT_PX, sy = (m_portraitSlot / 6) * PORTRAIT_PX;
		D3D11_VIEWPORT vp = { (float)sx, (float)sy, (float)PORTRAIT_PX, (float)PORTRAIT_PX, 0, 1 };
		m_ctx->RSSetViewports(1, &vp);
		// 枠の色は消さないと、選択順で枠が入れ替わったとき前の髪が残る。深度は BeginPortrait で全面クリア済み。
		if (m_ctx1) {
			D3D11_RECT rc;
			rc.left = sx; rc.top = sy; rc.right = sx + PORTRAIT_PX; rc.bottom = sy + PORTRAIT_PX;
			m_ctx1->ClearView(m_ptRtv, m_ptClear, &rc, 1);
		}
	} else {
		BOOL mrt = TacQFx(m_quality) && m_zRtv && m_zSrv;
		ID3D11RenderTargetView* rt[2] = { m_msRtv, m_zRtv };
		m_ctx->OMSetRenderTargets(mrt ? 2 : 1, rt, m_dsv);
		// MSAA の色バッファは必ず消す。消さないと空で塗られないサンプル（シルエット端・薄い翼）に前フレームが残り、透けて見える。
		float cc[4] = { .4f, .55f, .75f, 1 };
		m_ctx->ClearRenderTargetView(m_msRtv, cc);
		if (mrt) { float zc[4] = { 1, 1, 1, 1 }; m_ctx->ClearRenderTargetView(m_zRtv, zc); }
		m_ctx->ClearDepthStencilView(m_dsv, D3D11_CLEAR_DEPTH, 1.f, 0);
		D3D11_VIEWPORT vp = { 0, 0, (float)m_w, (float)m_h, 0, 1 };
		m_ctx->RSSetViewports(1, &vp);
	}
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_dsWant = m_dsWrite;
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
	m_ctx->RSSetState(m_inPortrait ? m_rsNone : (m_reflecting ? m_rsFront : m_rsBack));
	m_fcb.screen[0] = m_inPortrait ? (float)PORTRAIT_PX : (float)m_w;
	m_fcb.screen[1] = m_inPortrait ? (float)PORTRAIT_PX : (float)m_h;
	float themeKeep = m_fcb.fogColor[3];
	if (m_inPortrait) m_fcb.fogColor[3] = -1.f;
	UpdateFrameCB();
	if (m_inPortrait) m_fcb.fogColor[3] = themeKeep;
	if (!m_inPortrait || m_portraitSlot < 0) {
		ID3D11ShaderResourceView* sh = (m_shSrv && m_fcb.light[3] > .5f) ? m_shSrv : NULL;
		m_ctx->PSSetShaderResources(1, 1, &sh);
		if (m_groundSrv) m_ctx->PSSetShaderResources(3, 1, &m_groundSrv);
		if (m_matSrv) m_ctx->PSSetShaderResources(4, 1, &m_matSrv);
		if (m_photoSrv) m_ctx->PSSetShaderResources(5, 1, &m_photoSrv);
		if (m_sphereSrv) m_ctx->PSSetShaderResources(6, 1, &m_sphereSrv);
		if (m_envSrv) m_ctx->PSSetShaderResources(7, 1, &m_envSrv);
		if (m_refSrv && m_fcb.reflectInfo[0] > 0.5f) m_ctx->PSSetShaderResources(8, 1, &m_refSrv);
		else {
			ID3D11ShaderResourceView* refOff = NULL;
			m_ctx->PSSetShaderResources(8, 1, &refOff);
		}
	}
}

void Renderer::ClearDepth()
{
	m_ctx->ClearDepthStencilView(m_inPortrait ? m_ptDsv : m_dsv, D3D11_CLEAR_DEPTH, 1.f, 0);
}

void Renderer::SetObj(const M4& world, const DrawParams& dp, float outlineW)
{
	TacObjCB o;
	TacFillObj(o, world, dp, outlineW);
	TacUploadObj(m_ctx, m_cbObj, o, m_tessOn);
}

void Renderer::ClearTess()
{
	if (!m_ctx) return;
	if (m_tessOn) {
		m_ctx->HSSetShader(NULL, NULL, 0);
		m_ctx->DSSetShader(NULL, NULL, 0);
		m_tessOn = FALSE;
	}
	m_ctx->GSSetShader(NULL, NULL, 0);
	m_bindKey = -1;
	m_lastPsSrv = NULL;
}

void Renderer::BindForPass(BOOL skinned, BOOL outline)
{
	BOOL tess = m_allowTess && !skinned && !outline && !m_inPortrait && !m_reflecting
		&& m_pass != PASS_SHADOW
		&& TacQRich(m_quality, m_qPlus) && m_vsT && m_hsT && m_dsT;
	int key = (tess ? 1 : 0) | (skinned ? 2 : 0) | (outline ? 4 : 0)
		| ((int)m_pass << 3) | (m_inPortrait ? 64 : 0) | (m_reflecting ? 128 : 0)
		| (m_psOverride ? 256 : 0) | (m_quality << 9);
	if (key == m_bindKey) {
		if (!tess && m_tessOn) {
			m_ctx->HSSetShader(NULL, NULL, 0);
			m_ctx->DSSetShader(NULL, NULL, 0);
			m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_tessOn = FALSE;
		}
		return;
	}
	m_bindKey = key;
	m_ctx->IASetInputLayout(skinned ? m_ilK : m_ilS);
	if (tess) {
		m_ctx->VSSetShader(m_vsT, NULL, 0);
		m_ctx->HSSetShader(m_hsT, NULL, 0);
		m_ctx->DSSetShader(m_dsT, NULL, 0);
		m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
		m_tessOn = TRUE;
	} else {
		if (m_tessOn) {
			m_ctx->HSSetShader(NULL, NULL, 0);
			m_ctx->DSSetShader(NULL, NULL, 0);
			m_tessOn = FALSE;
		}
		m_ctx->VSSetShader(skinned ? m_vsK : m_vsS, NULL, 0);
		m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}
	if (m_pass == PASS_SHADOW) {
		m_ctx->PSSetShader(NULL, NULL, 0);
		return;
	}
	m_ctx->PSSetShader(outline ? m_psO : m_psM, NULL, 0);
	m_ctx->RSSetState(outline ? m_rsFront : (m_reflecting ? m_rsFront : m_rsBack));
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
	FlushQueuedDraws();
	ClearTess();
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
	{
		ID3D11PixelShader* sky = m_psSky;
		if (m_theme >= 0 && m_theme < 10 && m_psSkyT[m_theme]) sky = m_psSkyT[m_theme];
		m_ctx->PSSetShader(sky, NULL, 0);
	}
	m_ctx->RSSetState(m_rsNone);
	m_ctx->OMSetDepthStencilState(m_dsRead, 0);
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
	m_ctx->Draw(3, 0);
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_dsWant = m_dsWrite;
}

// m_vbDyn は追記リング。満杯で DISCARD し、それまでは NO_OVERWRITE で後ろへ足す（大きな頂点バッファの改名を毎回起こさない）。
// DISCARD は以前の領域を無効にするため、描画待ちキューにそのバッファが残っていたら先に吐き出す。
static void* TacRingMap(ID3D11DeviceContext* ctx, ID3D11Buffer* b, UINT cap, UINT& pos, UINT bytes, UINT& off)
{
	if (!b || !ctx || !bytes || bytes > cap) return NULL;
	UINT at = (pos + 15u) & ~15u;
	D3D11_MAP mt = D3D11_MAP_WRITE_NO_OVERWRITE;
	if (pos > cap || at > cap || bytes > cap - at) { at = 0; mt = D3D11_MAP_WRITE_DISCARD; }
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(ctx->Map(b, 0, mt, 0, &ms)) || !ms.pData) return NULL;
	pos = at + bytes;
	off = at;
	return (BYTE*)ms.pData + at;
}

void* Renderer::DynMap(UINT bytes, UINT& off)
{
	UINT at = (m_dynPos + 15u) & ~15u;
	if ((m_dynPos > m_vbDynBytes || at > m_vbDynBytes || bytes > m_vbDynBytes - at) && TacQueueUses(m_vbDyn))
		FlushQueuedDraws();
	return TacRingMap(m_ctx, m_vbDyn, m_vbDynBytes, m_dynPos, bytes, off);
}

void Renderer::DrawStaticVerts(const GpuMesh& m, const VtxS* v, UINT nv, const M4& world, const DrawParams& dp)
{
	if (!m.ib || !v || !nv || !m_vbDyn || nv * sizeof(VtxS) > m_vbDynBytes) { DrawStatic(m, world, dp); return; }
	UINT off = 0;
	void* dst = DynMap(nv * (UINT)sizeof(VtxS), off);
	if (!dst) return;
	memcpy(dst, v, nv * sizeof(VtxS));
	m_ctx->Unmap(m_vbDyn, 0);
	m_dynOff = off;
	GpuMesh d = m;
	d.vb = m_vbDyn;
	d.stride = sizeof(VtxS);
	DrawStatic(d, world, dp);
}

void Renderer::FlushQueuedDraws()
{
	if (g_hairN > 0 && !g_drawFlush) FlushHairPend();
	if (g_pendN > 0 && !g_drawFlush) FlushClothPend();
	if (g_drawN <= 0 || !m_ctx || g_drawFlush) return;
	g_drawFlush = TRUE;
	// キュー再生中は各コマンドの深度状態を結ぶ。最後のコマンド（たとえば α の水は深度書き込み無し）が
	// 呼び出し側の m_dsWant を上書きしたまま抜けると、このあとのキャラ一括描画が深度を書かず、
	// 両面の体は内側が見えて透け、奥のキャラの翼が手前へ回る。呼び出し前の意図へ必ず戻す。
	ID3D11DepthStencilState* savedWant = m_dsWant;
	ID3D11Buffer* vb = NULL;
	ID3D11Buffer* ib = NULL;
	UINT stride = 0xFFFFFFFFu, vbOff = 0xFFFFFFFFu;
	DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN;
	ID3D11ShaderResourceView* srv = (ID3D11ShaderResourceView*)1;
	ID3D11VertexShader* vs = (ID3D11VertexShader*)1;
	ID3D11HullShader* hs = (ID3D11HullShader*)1;
	ID3D11DomainShader* dom = (ID3D11DomainShader*)1;
	ID3D11PixelShader* ps = (ID3D11PixelShader*)1;
	ID3D11InputLayout* il = (ID3D11InputLayout*)1;
	ID3D11BlendState* blend = (ID3D11BlendState*)1;
	ID3D11RasterizerState* rs = (ID3D11RasterizerState*)1;
	ID3D11DepthStencilState* depth = (ID3D11DepthStencilState*)1;
	D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	for (int i = 0; i < g_drawN; ++i) {
		const TacDrawCmd& c = g_drawQ[i];
		if (c.il != il) { m_ctx->IASetInputLayout(c.il); il = c.il; }
		if (c.topo != topo) { m_ctx->IASetPrimitiveTopology(c.topo); topo = c.topo; }
		if (c.vs != vs) { m_ctx->VSSetShader(c.vs, NULL, 0); vs = c.vs; }
		if (c.hs != hs) { m_ctx->HSSetShader(c.hs, NULL, 0); hs = c.hs; }
		if (c.dom != dom) { m_ctx->DSSetShader(c.dom, NULL, 0); dom = c.dom; }
		m_tessOn = (c.hs != NULL);
		if (c.ps != ps) { m_ctx->PSSetShader(c.ps, NULL, 0); ps = c.ps; }
		if (c.rs != rs) { m_ctx->RSSetState(c.rs); rs = c.rs; }
		if (c.blend != blend) { m_ctx->OMSetBlendState(c.blend, NULL, 0xFFFFFFFF); blend = c.blend; }
		if (c.depth != depth) { m_ctx->OMSetDepthStencilState(c.depth, 0); depth = c.depth; m_dsWant = c.depth; }
		if (c.ps && c.srv != srv) {
			ID3D11ShaderResourceView* s = c.srv;
			m_ctx->PSSetShaderResources(0, 1, &s);
			srv = c.srv;
			m_lastPsSrv = c.srv;
		}
		if (c.vb != vb || c.stride != stride || c.vbOff != vbOff) {
			UINT off = c.vbOff, st = c.stride;
			m_ctx->IASetVertexBuffers(0, 1, &c.vb, &st, &off);
			vb = c.vb;
			stride = c.stride;
			vbOff = c.vbOff;
		}
		if (c.ib != ib || c.ibFmt != ibFmt) {
			m_ctx->IASetIndexBuffer(c.ib, c.ibFmt, 0);
			ib = c.ib;
			ibFmt = c.ibFmt;
		}
		TacUploadObj(m_ctx, m_cbObj, c.obj, m_tessOn);
		m_ctx->DrawIndexed(c.nIdx, c.iBase, 0);
	}
	g_drawN = 0;
	g_drawFlush = FALSE;
	// 深度状態を呼び出し前へ戻し、GPU の結びも合わせる。これをしないと直後のキャラ描画へ漏れる。
	m_dsWant = savedWant;
	{
		ID3D11DepthStencilState* want = m_dsWant ? m_dsWant : m_dsWrite;
		if (want != depth) m_ctx->OMSetDepthStencilState(want, 0);
	}
	if (m_tessOn) {
		m_ctx->HSSetShader(NULL, NULL, 0);
		m_ctx->DSSetShader(NULL, NULL, 0);
		m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		m_ctx->VSSetShader(m_vsS, NULL, 0);
		m_tessOn = FALSE;
	}
	m_bindKey = -1;
	m_lastPsSrv = NULL;
}

void Renderer::EnqueueStatic(const GpuMesh& m, const M4& world, const DrawParams& dp, float outlineW, BOOL outline)
{
	if (g_drawN >= TAC_DRAWQ) FlushQueuedDraws();
	if (g_drawN >= TAC_DRAWQ || !m.vb || !m.ib) return;
	TacDrawCmd& c = g_drawQ[g_drawN];
	memset(&c, 0, sizeof(c));
	c.vb = m.vb;
	c.ib = m.ib;
	c.nIdx = m.nIdx;
	c.iBase = m.iBase;
	c.stride = m.stride;
	c.vbOff = (m.vb == m_vbDyn) ? m_dynOff : 0;
	c.ibFmt = m.i32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
	BOOL tess = !outline && m_allowTess && !m_inPortrait && !m_reflecting && m_pass != PASS_SHADOW
		&& TacQRich(m_quality, m_qPlus) && m_vsT && m_hsT && m_dsT;
	c.il = m_ilS;
	c.topo = tess ? D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	c.vs = tess ? m_vsT : ((m_pass == PASS_SHADOW && m_vsSh && dp.mode == 0.f) ? m_vsSh : m_vsS);
	c.hs = tess ? m_hsT : NULL;
	c.dom = tess ? m_dsT : NULL;
	c.depth = m_dsWant ? m_dsWant : m_dsWrite;
	if (m_pass == PASS_SHADOW) {
		c.ps = NULL;
		c.rs = m_rsShadow;
		c.blend = m_bsOpaque;
		c.srv = NULL;
	} else {
		c.srv = (m.tex == -2 && m_hairSrv) ? m_hairSrv : SrvFor(m.tex);
		c.ps = outline ? m_psO : (m_psOverride ? m_psOverride : m_psM);
		if (outline) c.rs = m_rsFront;
		else if (dp.twoSided) c.rs = m_rsNone;
		else c.rs = m_reflecting ? m_rsFront : m_rsBack;
		BOOL alpha = ((dp.tint >> 24) & 255) < 255;
		c.blend = alpha ? m_bsAlpha : m_bsOpaque;
	}
	TacFillObj(c.obj, world, dp, outlineW);
	c.pair = g_drawPair;
	c.seq = g_drawSeq++;
	c.pre = outline ? 0 : 1;
	c.alpha = (c.blend == m_bsAlpha) ? 1 : 0;
	g_drawN++;
}

void Renderer::DrawStaticNow(const GpuMesh& m, const M4& world, const DrawParams& dp)
{
	if (!m.vb || !m.ib) return;
	if (m_dsWant) m_ctx->OMSetDepthStencilState(m_dsWant, 0);
	m_allowTess = !(dp.outline > 0.f) && !dp.noTess;
	UINT st = m.stride, off = (m.vb == m_vbDyn) ? m_dynOff : 0;
	m_ctx->IASetVertexBuffers(0, 1, &m.vb, &st, &off);
	m_ctx->IASetIndexBuffer(m.ib, m.i32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
	if (m_pass == PASS_SHADOW) {
		if (((dp.tint >> 24) & 255) < 128 || dp.dissolve > .5f) return;
		SetObj(world, dp, 0);
		BindForPass(FALSE, FALSE);
		if (m_vsSh && dp.mode == 0.f) {
			m_ctx->VSSetShader(m_vsSh, NULL, 0);
			m_bindKey = -1;
		}
		m_ctx->DrawIndexed(m.nIdx, m.iBase, 0);
		return;
	}
	ID3D11ShaderResourceView* srv = (m.tex == -2 && m_hairSrv) ? m_hairSrv : SrvFor(m.tex);
	if (srv != m_lastPsSrv) {
		m_ctx->PSSetShaderResources(0, 1, &srv);
		m_lastPsSrv = srv;
	}
	BOOL alpha = ((dp.tint >> 24) & 255) < 255;
	m_ctx->OMSetBlendState(alpha ? m_bsAlpha : m_bsOpaque, NULL, 0xFFFFFFFF);
	if (dp.outline > 0 && !alpha) {
		BindForPass(FALSE, TRUE);
		SetObj(world, dp, dp.outline);
		m_ctx->DrawIndexed(m.nIdx, m.iBase, 0);
		m_bindKey = -1;
	}
	BindForPass(FALSE, FALSE);
	SetObj(world, dp, 0);
	if (m_psOverride) m_ctx->PSSetShader(m_psOverride, NULL, 0);
	if (dp.twoSided) m_ctx->RSSetState(m_rsNone);
	m_ctx->DrawIndexed(m.nIdx, m.iBase, 0);
	if (dp.twoSided) m_ctx->RSSetState(m_reflecting ? m_rsFront : m_rsBack);
	if (alpha) m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
}

static void AvxCopy(void* dst, const void* src, size_t n)
{
	SimdKernels::Get().Copy(dst, src, n);
}

enum { TAC_INST_N = 64, TAC_INST_G = 96 };
struct TacInstG {
	ID3D11Buffer* vb;
	ID3D11Buffer* ib;
	UINT nIdx, stride;
	int tex, i32;
	DrawParams dp;
	int n, seq, alpha;
	M4 w[TAC_INST_N];
};
static TacInstG g_ig[TAC_INST_G];
static int g_igN = 0;
static int g_igSeq = 1;

static BOOL SameInstDp(const DrawParams& a, const DrawParams& b)
{
	return a.tint == b.tint && a.outlineColor == b.outlineColor && a.twoSided == b.twoSided
		&& a.outline == b.outline && a.mode == b.mode && a.emissive == b.emissive
		&& a.hue == b.hue && a.dissolve == b.dissolve && a.flash == b.flash
		&& a.rim == b.rim && a.sway == b.sway && a.noTess == b.noTess;
}

void Renderer::ResetActorPipe()
{
	if (!m_ctx) return;
	if (m_instOn) EndInstBatch();
	FlushQueuedDraws();
	ClearTess();
	m_psOverride = NULL;
	g_objValid = FALSE;
	g_cbBound = NULL;
	m_bindKey = -1;
	m_lastPsSrv = NULL;
	m_ctx->GSSetShader(NULL, NULL, 0);
	m_ctx->IASetInputLayout(m_ilS);
	m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_ctx->VSSetShader(m_vsS, NULL, 0);
	if (m_cutActors) m_cutActorGen++;
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_dsWant = m_dsWrite;
	if (m_pass == PASS_SHADOW) {
		m_ctx->RSSetState(m_rsShadow);
		m_ctx->PSSetShader(NULL, NULL, 0);
	} else {
		m_ctx->RSSetState(m_inPortrait ? m_rsNone : (m_reflecting ? m_rsFront : m_rsBack));
		m_ctx->PSSetShader(m_psM, NULL, 0);
	}
}

void Renderer::BeginInstBatch()
{
	if (m_instOn || m_inPortrait) return;
	FlushQueuedDraws();
	if (!m_vsB || !m_ilB || !m_vbInst) return;
	m_instOn = TRUE;
	g_igN = 0;
}

void Renderer::EndInstBatch()
{
	if (!m_instOn) return;
	FlushHairPend();
	FlushClothPend();
	FlushInst();
	FlushQueuedDraws();
	m_instOn = FALSE;
}

BOOL Renderer::PushInst(const GpuMesh& m, const M4& world, const DrawParams& dp)
{
	if (!m_vsB || !m_ilB || !m_vbInst || !m.vb || !m.ib || m.i32) return FALSE;
	if (m.stride && m.stride != sizeof(VtxS)) return FALSE;
	if (m.vb == m_vbDyn || m.vb == m_hairVB) return FALSE;
	if (m_inPortrait || m_psOverride || dp.dissolve > 0.001f || dp.mode != 0.f) return FALSE;
	if (((dp.tint >> 24) & 255) < 255) return FALSE;
	if (m_pass == PASS_SHADOW && ((dp.tint >> 24) & 255) < 128) return FALSE;
	BOOL tess = m_allowTess && !dp.noTess && !m_inPortrait && !m_reflecting && m_pass != PASS_SHADOW
		&& TacQRich(m_quality, m_qPlus) && m_vsT && m_hsT && m_dsT;
	if (tess) return FALSE;
	if (m_dsWant && m_dsWant != m_dsWrite) return FALSE;
	int alpha = (((dp.tint >> 24) & 255) < 255) ? 1 : 0;
	for (int i = 0; i < g_igN; ++i) {
		TacInstG& g = g_ig[i];
		if (g.n >= TAC_INST_N || !g.vb) continue;
		if (g.vb != m.vb || g.ib != m.ib || g.nIdx != m.nIdx || g.tex != m.tex) continue;
		if (!SameInstDp(g.dp, dp)) continue;
		g.w[g.n++] = world;
		return TRUE;
	}
	if (g_igN >= TAC_INST_G) FlushInst();
	if (g_igN >= TAC_INST_G) return FALSE;
	TacInstG& g = g_ig[g_igN++];
	g.vb = m.vb;
	g.ib = m.ib;
	g.nIdx = m.nIdx;
	g.stride = m.stride ? m.stride : (UINT)sizeof(VtxS);
	g.tex = m.tex;
	g.i32 = m.i32;
	g.dp = dp;
	g.n = 1;
	g.seq = g_igSeq++;
	g.alpha = alpha;
	g.w[0] = world;
	return TRUE;
}

void Renderer::FlushInst()
{
	if (g_igN <= 0 || !m_ctx || !m_vsB || !m_ilB) { g_igN = 0; return; }
	int nGrp = g_igN;
	g_igN = 0;
	BOOL gpu = FALSE;
	ID3D11Buffer* boundVb = NULL;
	ID3D11Buffer* boundIb = NULL;
	ID3D11ShaderResourceView* boundSrv = (ID3D11ShaderResourceView*)1;
	for (int i = 0; i < nGrp; ++i) {
		TacInstG& g = g_ig[i];
		if (!g.vb || !g.ib || g.n <= 0) continue;
		if (g.n < 2) {
			GpuMesh one;
			one.vb = g.vb;
			one.ib = g.ib;
			one.nIdx = g.nIdx;
			one.stride = g.stride;
			one.tex = g.tex;
			one.i32 = g.i32;
			gpu = FALSE;
			boundVb = NULL;
			boundIb = NULL;
			boundSrv = (ID3D11ShaderResourceView*)1;
			DrawStaticNow(one, g.w[0], g.dp);
			continue;
		}
		if (!gpu) {
			if (m_tessOn) {
				m_ctx->HSSetShader(NULL, NULL, 0);
				m_ctx->DSSetShader(NULL, NULL, 0);
				m_tessOn = FALSE;
			}
			m_ctx->IASetInputLayout(m_ilB);
			m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			ID3D11VertexShader* instVs = (m_pass == PASS_SHADOW && m_vsBSh) ? m_vsBSh : m_vsB;
			m_ctx->VSSetShader(instVs, NULL, 0);
			m_bindKey = -1;
			gpu = TRUE;
		}
		D3D11_MAPPED_SUBRESOURCE ms;
		if (FAILED(m_ctx->Map(m_vbInst, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) continue;
		AvxCopy(ms.pData, g.w, (size_t)g.n * sizeof(M4));
		m_ctx->Unmap(m_vbInst, 0);
		if (boundVb != g.vb || boundIb != g.ib) {
			ID3D11Buffer* bufs[2] = { g.vb, m_vbInst };
			UINT st[2] = { g.stride, 64u };
			UINT off[2] = { 0, 0 };
			m_ctx->IASetVertexBuffers(0, 2, bufs, st, off);
			m_ctx->IASetIndexBuffer(g.ib, g.i32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
			boundVb = g.vb;
			boundIb = g.ib;
		} else {
			UINT st = 64, off = 0;
			m_ctx->IASetVertexBuffers(1, 1, &m_vbInst, &st, &off);
		}
		if (m_pass != PASS_SHADOW) {
			ID3D11ShaderResourceView* srv = (g.tex == -2 && m_hairSrv) ? m_hairSrv : SrvFor(g.tex);
			if (srv != boundSrv) {
				m_ctx->PSSetShaderResources(0, 1, &srv);
				boundSrv = srv;
				m_lastPsSrv = srv;
			}
		}
		m_ctx->OMSetDepthStencilState(m_dsWant ? m_dsWant : m_dsWrite, 0);
		auto issue = [&](BOOL outline) {
			SetObj(MIdent(), g.dp, outline ? g.dp.outline : 0.f);
			if (m_pass == PASS_SHADOW) {
				m_ctx->PSSetShader(NULL, NULL, 0);
				m_ctx->RSSetState(m_rsShadow);
				m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
			} else {
				m_ctx->PSSetShader(outline ? m_psO : (m_psOverride ? m_psOverride : m_psM), NULL, 0);
				if (outline) m_ctx->RSSetState(m_rsFront);
				else if (g.dp.twoSided) m_ctx->RSSetState(m_rsNone);
				else m_ctx->RSSetState(m_reflecting ? m_rsFront : m_rsBack);
				m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
			}
			m_ctx->DrawIndexedInstanced(g.nIdx, (UINT)g.n, 0, 0, 0);
		};
		if (m_pass != PASS_SHADOW && g.dp.outline > 0.f) issue(TRUE);
		issue(FALSE);
	}
	if (gpu) {
		ID3D11Buffer* none = NULL;
		UINT zst = 0;
		m_ctx->IASetVertexBuffers(1, 1, &none, &zst, &zst);
		m_ctx->IASetInputLayout(m_ilS);
		m_ctx->VSSetShader(m_vsS, NULL, 0);
		m_bindKey = -1;
		m_lastPsSrv = NULL;
		if (m_pass != PASS_SHADOW) {
			m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
			m_ctx->RSSetState(m_reflecting ? m_rsFront : m_rsBack);
		}
	}
}

void Renderer::DrawStatic(const GpuMesh& m, const M4& world, const DrawParams& dp)
{
	if (!m.vb || !m.ib || !m_ctx) return;
	m_allowTess = !(dp.outline > 0.f) && !dp.noTess;
	if (g_drawFlush || m.vb == m_hairVB) {
		if (!g_drawFlush) FlushQueuedDraws();
		DrawStaticNow(m, world, dp);
		return;
	}
	BOOL alpha = ((dp.tint >> 24) & 255) < 255;
	BOOL soft = alpha || dp.dissolve > 0.001f;
	if (m_instOn && !soft && PushInst(m, world, dp)) return;
	BOOL split = FALSE;
	if (m_instOn && soft && g_igN > 0) { FlushInst(); split = TRUE; }
	g_drawPair++;
	if (m_pass == PASS_SHADOW) {
		if (((dp.tint >> 24) & 255) < 128 || dp.dissolve > .5f) return;
		EnqueueStatic(m, world, dp, 0.f, FALSE);
		if (split) FlushQueuedDraws();
		return;
	}
	if (dp.outline > 0.f && !alpha)
		EnqueueStatic(m, world, dp, dp.outline, TRUE);
	EnqueueStatic(m, world, dp, 0.f, FALSE);
	if (split) FlushQueuedDraws();
}

void Renderer::DrawSkinned(const Model& m, const Pose& p, const M4& world, const DrawParams& dp)
{
	FlushQueuedDraws();
	if (m_dsWant) m_ctx->OMSetDepthStencilState(m_dsWant, 0);
	D3D11_MAPPED_SUBRESOURCE ms;
	ID3D11Buffer* bone = TacCbAcquire(g_boneR, m_ctx, m_cbBones, ms);
	if (bone) {
		int nj = p.nJ;
		if (nj < 0) nj = 0;
		if (nj > MAXJ) nj = MAXJ;
		if (nj) memcpy(ms.pData, p.palette, sizeof(M4) * (size_t)nj);
		if (nj < MAXJ) memset((M4*)ms.pData + nj, 0, sizeof(M4) * (size_t)(MAXJ - nj));
		m_ctx->Unmap(bone, 0);
		m_ctx->VSSetConstantBuffers(2, 1, &bone);
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
			if (m_pass == PASS_SHADOW && m_vsKSh) {
				m_ctx->VSSetShader(m_vsKSh, NULL, 0);
				m_bindKey = -1;
			}
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
	DrawParams d = dp;
	d.noTess = 1;
	if (d.mode > 0.5f && d.mode < 2.5f) d.mode = 0.f;
	DrawStatic(a.mesh, w, d);
}

void Renderer::DrawTerrain()
{
	DrawParams dp;
	dp.rim = 0;
	m_psOverride = (m_theme >= 0 && m_theme < TH_COUNT && m_psGround[m_theme]) ? m_psGround[m_theme] : m_psTheme;
	for (int ch = 0; ch < 16; ch++) {
		GpuMesh& chunk = m_terrainChunk[ch];
		if (!chunk.vb) continue;
		int cx = ch & 3, cz = ch >> 2;
		V3 mid = MXformPt(m_sceneShift, v3(cx * 16.f + 8.f, 6.f, cz * 16.f + 8.f));
		// 影はライトの範囲、本描画と反射はカメラ。外の区画は画素に出ない。
		if (m_pass == PASS_SHADOW) { if (!InLight(mid, 16.f)) continue; }
		else if (!InView(mid, 16.f)) continue;
		DrawStatic(chunk, m_sceneShift, dp);
	}
	int gx = -99, gz = -99;
	if (m_fadeAbove >= 0 && m_fadeX >= 0 && m_fadeZ >= 0) { gx = m_fadeX >> 4; gz = m_fadeZ >> 4; }
	BOOL hasCover = FALSE;
	GpuMesh* coverSet[4] = { m_cover1, m_cover2, m_cover3, m_cover4 };
	for (int ci = 0; ci < 4 && !hasCover; ci++)
		for (int ch = 0; ch < 16; ch++) if (coverSet[ci][ch].vb) { hasCover = TRUE; break; }
	if (hasCover) {
		if (m_reflecting) {
			m_fcb.reflectInfo[2] = 2.f;
			UpdateFrameCB();
			m_ctx->OMSetDepthStencilState(m_dsRead, 0);
			m_dsWant = m_dsRead;
		}
		for (int ci = 0; ci < 4; ci++) {
			for (int ch = 0; ch < 16; ch++) {
				GpuMesh& cover = coverSet[ci][ch];
				if (!cover.vb) continue;
				int layer = ci + 1;
				if (m_cutDeck >= 0 && layer != m_cutDeck) continue;
				int cx = ch & 3, cz = ch >> 2;
				V3 cMid = v3(cx * 16.f + 8.f, 3.f, cz * 16.f + 8.f);
				if (m_pass == PASS_SHADOW ? !InLight(cMid, 14.f) : !InView(cMid, 14.f)) continue;
				int above = !m_reflecting && m_cutDeck < 0 && gx >= 0 && abs(cx - gx) <= 1 && abs(cz - gz) <= 1 && layer > m_fadeAbove;
				if (m_pass == PASS_SHADOW) {
					if (!above) DrawStatic(cover, m_sceneShift, dp);
					continue;
				}
				DrawParams cd = dp;
				cd.twoSided = 1;
				if (above) {
					cd.tint = 0x78FFFFFF;
					m_ctx->OMSetDepthStencilState(m_dsRead, 0);
					m_dsWant = m_dsRead;
				}
				DrawStatic(cover, m_sceneShift, cd);
				if (above) { m_ctx->OMSetDepthStencilState(m_dsWrite, 0); m_dsWant = m_dsWrite; }
			}
		}
		if (m_reflecting) {
			m_fcb.reflectInfo[2] = 1.f;
			UpdateFrameCB();
			m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
			m_dsWant = m_dsWrite;
		}
	}
	m_psOverride = m_psProp;
	dp.twoSided = 1;
	dp.sway = TacAirHeld(m_time).sway;
	for (auto& b : m_props) {
		if (b.rad > 0.f && !BoundVisible(b.cx, b.cy, b.cz, b.rad)) continue;
		dp.emissive = b.mat;
		dp.hue = 0;
		dp.rim = 0.02f;
		dp.outline = 0;
		DrawStatic(b.mesh, m_sceneShift, dp);
	}
	if (m_pass != PASS_SHADOW) {
		m_psOverride = m_psLava;
		for (int i = 0; i < 16; i++) {
			BoundMesh& c = m_lavaChunk[i];
			if (!c.mesh.vb || c.rad <= 0.f) continue;
			if (!BoundVisible(c.cx, c.cy, c.cz, c.rad)) continue;
			DrawParams lp;
			lp.mode = 2.f;
			lp.twoSided = 1;
			lp.noTess = 1;
			DrawStatic(c.mesh, m_sceneShift, lp);
		}
		if (m_lava.vb) {
			DrawParams lp;
			lp.mode = 2.f;
			lp.twoSided = 1;
			lp.noTess = 1;
			DrawStatic(m_lava, m_sceneShift, lp);
		}
	}
	m_psOverride = NULL;
}

BOOL Renderer::GroundChunkVisible() const
{
	for (int ch = 0; ch < 16; ch++) {
		if (!m_terrainChunk[ch].vb) continue;
		int cx = ch & 3, cz = ch >> 2;
		V3 mid = MXformPt(m_sceneShift, v3(cx * 16.f + 8.f, 6.f, cz * 16.f + 8.f));
		if (InView(mid, 16.f)) return TRUE;
	}
	return FALSE;
}

void Renderer::SetStageId(int stage)
{
	if (stage < 0) stage = 0;
	m_stageId = stage;
	m_fcb.sight[1] = (float)m_stageId;
}

void Renderer::SetLiquidRipples(const float* rip, int n)
{
	if (!m_cbRipple || !m_ctx) return;
	float z[48 * 4];
	memset(z, 0, sizeof(z));
	if (rip && n > 0) {
		if (n > 48) n = 48;
		memcpy(z, rip, (size_t)n * 16u);
	}
	static float s_lastRip[48 * 4];
	static ID3D11Buffer* s_lastBuf = NULL;
	if (s_lastBuf == m_cbRipple && memcmp(s_lastRip, z, sizeof(z)) == 0) return;
	D3D11_MAPPED_SUBRESOURCE ms;
	if (FAILED(m_ctx->Map(m_cbRipple, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
	memcpy(ms.pData, z, sizeof(z));
	m_ctx->Unmap(m_cbRipple, 0);
	memcpy(s_lastRip, z, sizeof(z));
	s_lastBuf = m_cbRipple;
}

void Renderer::DrawWater()
{
	if (m_pass == PASS_SHADOW) return;
	BOOL has = m_water.vb != NULL;
	if (!has) {
		for (int i = 0; i < 16; i++) if (m_waterChunk[i].mesh.vb) { has = TRUE; break; }
	}
	if (!has) return;
	DrawParams dp;
	dp.mode = 1.f;
	dp.twoSided = 1;
	dp.noTess = 1;
	dp.tint = (m_waterColor & 0xFFFFFF) | 0xFA000000;
	m_ctx->OMSetDepthStencilState(m_dsRead, 0);
	m_dsWant = m_dsRead;
	m_psOverride = m_psWater;
	BOOL anyW = FALSE;
	for (int i = 0; i < 16; i++) {
		BoundMesh& c = m_waterChunk[i];
		if (!c.mesh.vb || c.rad <= 0.f) continue;
		if (!BoundVisible(c.cx, c.cy, c.cz, c.rad)) continue;
		DrawStatic(c.mesh, m_sceneShift, dp);
		anyW = TRUE;
	}
	if (!anyW && m_water.vb) DrawStatic(m_water, m_sceneShift, dp);
	m_psOverride = NULL;
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_dsWant = m_dsWrite;
}

void Renderer::DrawOverlayQuads(const VtxS* q, int n, int cursorStyle)
{
	FlushQueuedDraws();
	ClearTess();
	if (!q || n <= 0 || m_pass == PASS_SHADOW || !m_vbDyn) return;
	DrawParams dp;
	dp.mode = (float)cursorStyle;
	SetObj(MIdent(), dp, 0);
	m_ctx->IASetInputLayout(m_ilS);
	m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_ctx->VSSetShader(m_vsS, NULL, 0);
	m_ctx->PSSetShader(m_psOV, NULL, 0);
	m_ctx->RSSetState(m_rsNone);
	m_ctx->OMSetDepthStencilState(m_dsRead, 0);
	m_ctx->OMSetBlendState(m_bsAlpha, NULL, 0xFFFFFFFF);
	m_bindKey = -1;
	UINT st = sizeof(VtxS), off = 0;
	auto chunkList = [&](const VtxS* src, int nv) {
		int cap = (int)(m_vbDynBytes / sizeof(VtxS));
		if (cap > 3) cap -= cap % 3;
		if (cap < 3) return;
		for (int base = 0; base < nv; base += cap) {
			int cnt = nv - base;
			if (cnt > cap) cnt = cap;
			cnt -= cnt % 3;
			if (cnt <= 0) break;
			void* dst = DynMap((UINT)cnt * (UINT)sizeof(VtxS), off);
			if (!dst) return;
			AvxCopy(dst, src + base, (size_t)cnt * sizeof(VtxS));
			m_ctx->Unmap(m_vbDyn, 0);
			m_ctx->IASetVertexBuffers(0, 1, &m_vbDyn, &st, &off);
			m_ctx->Draw((UINT)cnt, 0);
		}
	};
	if (cursorStyle || (n % 4) != 0) chunkList(q, n);
	else {
		static const int kOrd[6] = { 0, 1, 2, 0, 2, 3 };
		int cap = (int)(m_vbDynBytes / sizeof(VtxS));
		if (cap > 6) cap -= cap % 6;
		if (cap < 6) return;
		int quads = n / 4;
		int per = cap / 6;
		for (int base = 0; base < quads; base += per) {
			int cnt = quads - base;
			if (cnt > per) cnt = per;
			VtxS* dst = (VtxS*)DynMap((UINT)cnt * 6u * (UINT)sizeof(VtxS), off);
			if (!dst) return;
			for (int k = 0; k < cnt; ++k) {
				const VtxS* src = q + (base + k) * 4;
				for (int t = 0; t < 6; ++t) dst[k * 6 + t] = src[kOrd[t]];
			}
			m_ctx->Unmap(m_vbDyn, 0);
			m_ctx->IASetVertexBuffers(0, 1, &m_vbDyn, &st, &off);
			m_ctx->Draw((UINT)cnt * 6u, 0);
		}
	}
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_dsWant = m_dsWrite;
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
}

void Renderer::DrawRibbon(const Buf<VtxP>& v)
{
	FlushQueuedDraws();
	ClearTess();
	if (v.empty() || m_pass == PASS_SHADOW) return;
	size_t bytes = v.size() * sizeof(VtxP);
	if (bytes > m_vbDynBytes) return;
	UINT st = sizeof(VtxP), off = 0;
	void* dst = DynMap((UINT)bytes, off);
	if (!dst) return;
	memcpy(dst, v.data(), bytes);
	m_ctx->Unmap(m_vbDyn, 0);
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
	m_dsWant = m_dsWrite;
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
}

static V3 XformPt(V3 p, const M4& m)
{
	return v3(
		p.x * m.m[0] + p.y * m.m[4] + p.z * m.m[8] + m.m[12],
		p.x * m.m[1] + p.y * m.m[5] + p.z * m.m[9] + m.m[13],
		p.x * m.m[2] + p.y * m.m[6] + p.z * m.m[10] + m.m[14]);
}
static V3 XformDir(V3 p, const M4& m)
{
	return v3(
		p.x * m.m[0] + p.y * m.m[4] + p.z * m.m[8],
		p.x * m.m[1] + p.y * m.m[5] + p.z * m.m[9],
		p.x * m.m[2] + p.y * m.m[6] + p.z * m.m[10]);
}

void Renderer::DrawParticles(const Particle* ps, int nPart, const M4* mapXform)
{
	FlushQueuedDraws();
	ClearTess();
	if (!ps || nPart <= 0 || m_pass == PASS_SHADOW) return;
	V3 right = Norm(v3(m_view.m[0], m_view.m[4], m_view.m[8]));
	V3 up = Norm(v3(m_view.m[1], m_view.m[5], m_view.m[9]));
	V3 fwd = Norm(v3(m_view.m[2], m_view.m[6], m_view.m[10]));
	if (m_gsP && m_vsPT && m_ilPT) {
		struct PPt { float p[3]; float vel[4]; float sz[4]; float c[4]; };
		enum { PN = 4096 };
		static PPt buf[2][PN];
		int vn[2] = { 0, 0 };
		for (int pi = 0; pi < nPart; pi++) {
			const Particle& p = ps[pi];
			if (p.life <= 0) continue;
			V3 pos = p.p, vel = p.v;
			if (mapXform && p.wxHit != 0) {
				pos = XformPt(p.p, *mapXform);
				vel = XformDir(p.v, *mapXform);
			}
			float t = 1.f - p.life / max(1e-4f, p.max);
			float sz = Lerpf(p.size, p.size1, t);
			float rad = sz * 1.8f;
			if (p.stretch > 0.f) rad += Len(vel) * p.stretch;
			if (rad < 0.2f) rad = 0.2f;
			if (!mapXform && !InView(pos, rad)) continue;
			float a = p.a * Clampf(p.life / max(1e-4f, p.max) * 3.f, 0, 1) * Clampf(t * 8.f + .1f, 0, 1);
			int bi = p.add ? 1 : 0;
			if (vn[bi] >= PN) continue;
			PPt& o = buf[bi][vn[bi]++];
			o.p[0] = pos.x; o.p[1] = pos.y; o.p[2] = pos.z;
			o.vel[0] = vel.x; o.vel[1] = vel.y; o.vel[2] = vel.z; o.vel[3] = p.stretch;
			o.sz[0] = sz; o.sz[1] = (float)p.shape; o.sz[2] = o.sz[3] = 0.f;
			o.c[0] = p.r; o.c[1] = p.g; o.c[2] = p.b; o.c[3] = a;
		}
		M4 basis = MIdent();
		basis.m[0] = right.x; basis.m[1] = right.y; basis.m[2] = right.z;
		basis.m[4] = up.x; basis.m[5] = up.y; basis.m[6] = up.z;
		basis.m[8] = fwd.x; basis.m[9] = fwd.y; basis.m[10] = fwd.z;
		DrawParams bdp;
		SetObj(basis, bdp, 0);
		BOOL mrt = TacQFx(m_quality) && m_zRtv && m_msRtv && !m_inPortrait;
		if (mrt) {
			ID3D11RenderTargetView* one = m_msRtv;
			m_ctx->OMSetRenderTargets(1, &one, m_dsv);
		}
		m_ctx->IASetInputLayout(m_ilPT);
		m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
		m_ctx->VSSetShader(m_vsPT, NULL, 0);
		m_ctx->GSSetShader(m_gsP, NULL, 0);
		ID3D11Buffer* objb = g_cbBound ? g_cbBound : m_cbObj;
		ID3D11Buffer* gcb[2] = { TacCbCur(g_frameR, m_cbFrame), objb };
		m_ctx->GSSetConstantBuffers(0, 2, gcb);
		m_ctx->PSSetShader(m_psP, NULL, 0);
		m_ctx->RSSetState(m_rsNone);
		m_ctx->OMSetDepthStencilState(m_dsRead, 0);
		m_bindKey = -1;
		for (int b = 0; b < 2; b++) {
			if (vn[b] <= 0) continue;
			UINT bytes = (UINT)vn[b] * (UINT)sizeof(PPt);
			if (bytes > m_vbDynBytes) continue;
			UINT st = sizeof(PPt), off = 0;
			void* dst = DynMap(bytes, off);
			if (!dst) continue;
			memcpy(dst, buf[b], bytes);
			m_ctx->Unmap(m_vbDyn, 0);
			m_ctx->IASetVertexBuffers(0, 1, &m_vbDyn, &st, &off);
			m_ctx->OMSetBlendState(b ? m_bsAdd : m_bsPremul, NULL, 0xFFFFFFFF);
			m_ctx->Draw((UINT)vn[b], 0);
		}
		m_ctx->GSSetShader(NULL, NULL, 0);
		ID3D11Buffer* gnone[2] = { NULL, NULL };
		m_ctx->GSSetConstantBuffers(0, 2, gnone);
		if (mrt) {
			ID3D11RenderTargetView* rt[2] = { m_msRtv, m_zRtv };
			m_ctx->OMSetRenderTargets(2, rt, m_dsv);
		}
		m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
		m_dsWant = m_dsWrite;
		m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
		return;
	}
	enum { PV = 16384 };
	static VtxP buf[2][PV];
	int vn[2] = { 0, 0 };
	for (int pi = 0; pi < nPart; pi++) {
		const Particle& p = ps[pi];
		if (p.life <= 0) continue;
		V3 pos = p.p;
		V3 vel = p.v;
		if (mapXform && p.wxHit != 0) {
			pos = XformPt(p.p, *mapXform);
			vel = XformDir(p.v, *mapXform);
		}
		float t = 1.f - p.life / max(1e-4f, p.max);
		float sz = Lerpf(p.size, p.size1, t);
		float rad = sz * 1.8f;
		if (p.stretch > 0.f) rad += Len(vel) * p.stretch;
		if (rad < 0.2f) rad = 0.2f;
		if (!mapXform && !InView(pos, rad)) continue;
		float a = p.a * Clampf(p.life / max(1e-4f, p.max) * 3.f, 0, 1) * Clampf(t * 8.f + .1f, 0, 1);
		V3 ax = right * sz, ay = up * sz;
		if (p.stretch > 0) {
			V3 vd = vel;
			float sp = Len(vd);
			if (sp > 1e-3f) {
				vd = vd * (1.f / sp);
				V3 side = Norm(Cross(vd, fwd));
				ay = vd * (sz + sp * p.stretch);
				ax = side * sz * .5f;
			}
		}
		VtxP q[4];
		V3 c[4] = { pos - ax - ay, pos - ax + ay, pos + ax + ay, pos + ax - ay };
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
	// 加算の粒が線形深度まで塗ると、後段のぼけが盤面を粒の奥行きで扱う。
	BOOL mrt = TacQFx(m_quality) && m_zRtv && m_msRtv && !m_inPortrait;
	if (mrt) {
		ID3D11RenderTargetView* one = m_msRtv;
		m_ctx->OMSetRenderTargets(1, &one, m_dsv);
	}
	for (int b = 0; b < 2; b++) {
		if (vn[b] <= 0) continue;
		size_t bytes = min((size_t)vn[b] * sizeof(VtxP), (size_t)m_vbDynBytes);
		UINT st = sizeof(VtxP), off = 0;
		void* dst = DynMap((UINT)bytes, off);
		if (!dst) {
			if (mrt) {
				ID3D11RenderTargetView* rt[2] = { m_msRtv, m_zRtv };
				m_ctx->OMSetRenderTargets(2, rt, m_dsv);
			}
			return;
		}
		memcpy(dst, buf[b], bytes);
		m_ctx->Unmap(m_vbDyn, 0);
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
	if (mrt) {
		ID3D11RenderTargetView* rt[2] = { m_msRtv, m_zRtv };
		m_ctx->OMSetRenderTargets(2, rt, m_dsv);
	}
	m_ctx->OMSetDepthStencilState(m_dsWrite, 0);
	m_dsWant = m_dsWrite;
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
		case TH_SEABED: return TL_SAND;
		default: return TL_GRASS;
		}
	case T_FOREST: return theme == TH_SNOW ? TL_SNOW : (theme == TH_SEABED ? TL_SAND : (theme == TH_DESERT ? TL_SAND : (theme == TH_VOLCANO ? TL_ASH : TL_FOREST)));
	case T_HILL: return theme == TH_SNOW ? TL_SNOW : (theme == TH_SEABED ? TL_SAND : (theme == TH_VOLCANO ? TL_ASH : (theme == TH_CANYON || theme == TH_DESERT ? TL_DIRT : TL_GRASS)));
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
	if (theme == TH_SEABED && t != T_WATER) return TL_CLIFF_ROCK;
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
	case TH_SEABED: r = .62f; g = .88f; b = 1.0f; break;
	case TH_FALLS: r = .92f; g = .98f; b = 1.0f; break;
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

static unsigned CellHash(int x, int z, int salt)
{
	unsigned h = (unsigned)x * 2246822519u ^ (unsigned)z * 3266489917u ^ (unsigned)salt * 668265263u;
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	return h;
}

static void AddPuff(Renderer::SceneryPuff* p, int& n, int kind, float x, float y, float z, float minD)
{
	if (n >= 24) return;
	float md = minD * minD;
	for (int i = 0; i < n; i++) {
		if (p[i].kind != kind) continue;
		float dx = p[i].x - x, dz = p[i].z - z;
		if (dx * dx + dz * dz < md) return;
	}
	p[n].x = x; p[n].y = y; p[n].z = z; p[n].kind = kind;
	n++;
}

static void Blade(MeshBuild& mb, V3 a, V3 b, V3 c, V3 d, DWORD bot, DWORD top)
{
	V3 nrm = Norm(Cross(b - a, d - a));
	mb.Quad(a, b, c, d, nrm, 0, 1, 1, 0, -1, bot);
	size_t nv = mb.v.size();
	if (nv >= 4) { mb.v[nv - 2].c = top; mb.v[nv - 1].c = top; }
	mb.Quad(b, a, d, c, v3(-nrm.x, -nrm.y, -nrm.z), 0, 1, 1, 0, -1, bot);
	nv = mb.v.size();
	if (nv >= 4) { mb.v[nv - 2].c = top; mb.v[nv - 1].c = top; }
}

static void Tuft(MeshBuild& mb, float x, float y, float z, float h, float w, float yaw, DWORD bot, DWORD top)
{
	float cs = cosf(yaw), sn = sinf(yaw);
	V3 r = v3(cs * w, 0, sn * w);
	V3 q = v3(-sn * w * 0.7f, 0, cs * w * 0.7f);
	V3 o = v3(x, y, z);
	V3 tip = v3(cs * h * 0.18f, h, sn * h * 0.18f);
	size_t from = mb.v.size();
	Blade(mb, o - r, o + r, o + r + tip, o - r + tip, bot, top);
	Blade(mb, o - q, o + q, o + q + tip, o - q + tip, bot, top);
	for (size_t i = from; i < mb.v.size(); i++) {
		mb.v[i].n[0] *= 0.35f;
		mb.v[i].n[2] *= 0.35f;
		mb.v[i].n[1] = 0.92f;
	}
}

static void ConeRing(MeshBuild& mb, float x, float y0, float z, float y1, float r0, float r1, DWORD col)
{
	const int seg = 7;
	for (int i = 0; i < seg; i++) {
		float a0 = i * 6.2831853f / seg, a1 = (i + 1) * 6.2831853f / seg;
		float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
		V3 p0 = v3(x + c0 * r0, y0, z + s0 * r0);
		V3 p1 = v3(x + c1 * r0, y0, z + s1 * r0);
		V3 p2 = v3(x + c1 * r1, y1, z + s1 * r1);
		V3 p3 = v3(x + c0 * r1, y1, z + s0 * r1);
		mb.Quad(p0, p1, p2, p3, Norm(Cross(p1 - p0, p3 - p0)), 0, 1, 1, 0, -1, col);
	}
}

static bool ScenicGround(int t)
{
	return t == T_PLAIN || t == T_HILL || t == T_SAND || t == T_SNOW || t == T_SWAMP
		|| t == T_RUINS || t == T_ROCK || t == T_FOREST || t == T_ROAD;
}

static bool ScenicSkipProp(int prop)
{
	return prop == PK_TENT || prop == PK_FLAG || prop == PK_BARREL || prop == PK_CRATE
		|| prop == PK_LANTERN || prop == PK_MOUNT || prop == PK_HILLTREES || prop == PK_TREES || prop == PK_KIT;
}

static void ScatterTheme(const Map& m, MeshBuild& mb, MeshBuild& lava, Renderer::SceneryPuff* puff, int& puffN)
{
	const int theme = m.theme;
	struct Cand { int x, z, sc; };
	Cand cand[24];
	int cn = 0;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		const Cell& c = m.At(x, z);
		if (theme == TH_VOLCANO && c.t == T_LAVA) {
			int nb = 0;
			static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
			for (int k = 0; k < 4; k++) {
				int nx = x + dx[k], nz = z + dz[k];
				if (Map::In(nx, nz) && m.At(nx, nz).t == T_LAVA) nb++;
			}
			int sc = nb * 10 + (int)(CellHash(x, z, 5) % 7);
			if (cn < 24) { cand[cn].x = x; cand[cn].z = z; cand[cn].sc = sc; cn++; }
			else {
				int w = 0;
				for (int i = 1; i < cn; i++) if (cand[i].sc < cand[w].sc) w = i;
				if (sc > cand[w].sc) { cand[w].x = x; cand[w].z = z; cand[w].sc = sc; }
			}
		}
		if (mb.v.size() > 52000) continue;
		if (!ScenicGround(c.t) || ScenicSkipProp(c.prop)) continue;
		if (c.t == T_ROCK && theme != TH_CANYON && theme != TH_RUINS && theme != TH_VOLCANO) continue;
		unsigned h = CellHash(x, z, theme + 3);
		float ox = 0.18f + ((h >> 1) & 1023) / 1023.f * 0.64f;
		float oz = 0.18f + ((h >> 11) & 1023) / 1023.f * 0.64f;
		if (ox > 0.4f && ox < 0.6f) ox = (ox < 0.5f) ? 0.32f : 0.68f;
		if (oz > 0.4f && oz < 0.6f) oz = (oz < 0.5f) ? 0.32f : 0.68f;
		float px = x + ox, pz = z + oz, py = m.StandY(x, z);
		float yaw = (h & 255) / 255.f * 6.2831853f;
		float u = (h & 65535) / 65535.f;
		switch (theme) {
		case TH_PLAINS:
			if ((h % 3) != 0) break;
			{
				DWORD bot = RGBA8(46, 78, 28), top = RGBA8(110, 176, 58, 72);
				if ((h % 11) == 0) { bot = RGBA8(64, 88, 30); top = (h & 16) ? RGBA8(236, 196, 64, 90) : RGBA8(228, 120, 156, 90); }
				Tuft(mb, px, py, pz, 0.22f + u * 0.26f, 0.06f + u * 0.04f, yaw, bot, top);
				if ((h % 5) == 0) Tuft(mb, px + 0.14f, py, pz - 0.1f, 0.14f + u * 0.12f, 0.045f, yaw + 0.9f, RGBA8(52, 90, 32), RGBA8(130, 186, 64, 80));
				if ((h % 23) == 0) AddPuff(puff, puffN, 4, px, py + 0.3f, pz, 12.f);
			}
			break;
		case TH_CANYON:
			if ((h % 4) != 0) break;
			Tuft(mb, px, py, pz, 0.14f + u * 0.12f, 0.055f, yaw, RGBA8(112, 84, 46), RGBA8(176, 146, 82, 100));
			if ((h % 8) == 0) {
				AddBox(mb, v3(px, py + 0.05f, pz), v3(0.24f, 0.045f, 0.14f), RGBA8(138, 102, 72), -1);
				AddBox(mb, v3(px + 0.06f, py + 0.12f, pz - 0.04f), v3(0.12f, 0.03f, 0.08f), RGBA8(156, 118, 84), -1);
				AddPuff(puff, puffN, 5, px, py + 0.4f, pz, 14.f);
			}
			break;
		case TH_FOREST:
			if ((h % 4) != 0) break;
			Tuft(mb, px, py, pz, 0.16f + u * 0.1f, 0.11f, yaw, RGBA8(22, 64, 30), RGBA8(46, 118, 46, 64));
			if ((h % 9) == 0) {
				AddBox(mb, v3(px, py + 0.05f, pz), v3(0.025f, 0.05f, 0.025f), RGBA8(214, 196, 164), -1);
				AddBox(mb, v3(px, py + 0.11f, pz), v3(0.08f, 0.028f, 0.08f), (h & 32) ? RGBA8(168, 48, 40) : RGBA8(196, 170, 120), -1);
			}
			if ((h % 7) == 0) AddPuff(puff, puffN, 6, px, py + 0.8f, pz, 12.f);
			break;
		case TH_SNOW:
			if ((h % 5) != 0) break;
			AddBox(mb, v3(px, py + 0.035f, pz), v3(0.26f, 0.04f, 0.16f), RGBA8(236, 242, 248), -1);
			if ((h % 2) == 0) Tuft(mb, px + 0.08f, py, pz, 0.1f + u * 0.06f, 0.035f, yaw, RGBA8(198, 214, 224), RGBA8(246, 250, 255, 150));
			if ((h % 7) == 0) AddPuff(puff, puffN, 7, px, py, pz, 14.f);
			break;
		case TH_DESERT:
			if ((h % 17) == 0 && c.t != T_ROCK) {
				float sc = 0.85f + u * 0.55f, cs = cosf(yaw), sn = sinf(yaw);
				DWORD g = RGBA8(62, 108, 56), g2 = RGBA8(84, 136, 66);
				AddBox(mb, v3(px, py + 0.34f * sc, pz), v3(0.065f * sc, 0.34f * sc, 0.065f * sc), g, -1);
				AddBox(mb, v3(px + cs * 0.16f * sc, py + 0.48f * sc, pz + sn * 0.16f * sc), v3(0.13f * sc, 0.038f * sc, 0.038f * sc), g2, -1);
				AddBox(mb, v3(px + cs * 0.26f * sc, py + 0.64f * sc, pz + sn * 0.26f * sc), v3(0.04f * sc, 0.15f * sc, 0.04f * sc), g, -1);
				AddBox(mb, v3(px - cs * 0.1f * sc, py + 0.34f * sc, pz - sn * 0.1f * sc), v3(0.038f * sc, 0.12f * sc, 0.038f * sc), g2, -1);
			} else if ((h % 6) == 0) {
				Tuft(mb, px, py, pz, 0.12f + u * 0.08f, 0.05f, yaw, RGBA8(122, 96, 54), RGBA8(176, 146, 84, 110));
			}
			if ((h % 21) == 0) AddPuff(puff, puffN, 8, px, py + 0.2f, pz, 14.f);
			break;
		case TH_VOLCANO:
			if ((h % 4) != 0) break;
			Tuft(mb, px, py, pz, 0.1f + u * 0.08f, 0.045f, yaw, RGBA8(42, 32, 26), RGBA8(78, 52, 36, 120));
			break;
		case TH_SWAMP:
			if ((h % 3) != 0) break;
			Tuft(mb, px, py, pz, 0.42f + u * 0.32f, 0.028f, yaw, RGBA8(48, 68, 30), RGBA8(132, 138, 58, 55));
			if ((h % 17) == 0) AddPuff(puff, puffN, 3, px, py + 0.25f, pz, 8.f);
			break;
		case TH_RUINS:
			if ((h % 5) == 0) AddBox(mb, v3(px, py + 0.04f, pz), v3(0.12f, 0.045f, 0.08f), RGBA8(124, 118, 110), -1);
			if ((h % 4) == 0) Tuft(mb, px, py, pz, 0.18f + u * 0.1f, 0.07f, yaw, RGBA8(28, 70, 32), RGBA8(72, 132, 58, 75));
			if ((h % 17) == 0) AddPuff(puff, puffN, 9, px, py + 0.5f, pz, 12.f);
			break;
		case TH_SEABED:
			if ((h % 4) == 0) {
				Tuft(mb, px, py, pz, 0.65f + u * 0.55f, 0.035f, yaw, RGBA8(16, 64, 46), RGBA8(36, 150, 86, 36));
				if ((h % 8) == 0) AddPuff(puff, puffN, 2, px, py + 0.15f, pz, 6.f);
			}
			if ((h % 11) == 0) {
				AddBox(mb, v3(px, py + 0.06f, pz), v3(0.1f, 0.06f, 0.08f), RGBA8(206, 92, 84), -1);
				AddBox(mb, v3(px + 0.08f, py + 0.1f, pz), v3(0.05f, 0.08f, 0.04f), RGBA8(72, 150, 156), -1);
			}
			break;
		case TH_FALLS: {
			int wet = 0;
			static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
			for (int k = 0; k < 4; k++) {
				int nx = x + dx[k], nz = z + dz[k];
				if (Map::In(nx, nz) && m.At(nx, nz).t == T_WATER) wet = 1;
			}
			if (wet && (h % 3) == 0) Tuft(mb, px, py, pz, 0.32f + u * 0.2f, 0.03f, yaw, RGBA8(40, 78, 36), RGBA8(150, 176, 72, 60));
			else if ((h % 4) == 0) Tuft(mb, px, py, pz, 0.08f + u * 0.05f, 0.08f, yaw, RGBA8(36, 86, 38), RGBA8(90, 150, 64, 90));
			if (wet && (h % 4) == 0) AddPuff(puff, puffN, 14, px, py + 0.35f, pz, 10.f);
			break; }
		case TH_ORBIT:
			if ((h % 5) == 0) AddBox(mb, v3(px, py + 0.08f, pz), v3(0.18f + u * 0.12f, 0.05f, 0.1f), RGBA8(90, 108, 140), -1);
			if ((h % 9) == 0) AddBox(mb, v3(px, py + 0.24f, pz), v3(0.18f, 0.18f, 0.18f), RGBA8(150, 185, 255), -1);
			if ((h % 11) == 0) AddPuff(puff, puffN, 10, px, py + 1.2f, pz, 10.f);
			break;
		case TH_STATION:
			if ((h % 5) == 0) AddBox(mb, v3(px, py + 0.2f, pz), v3(0.04f, 0.2f, 0.04f), RGBA8(140, 150, 160), -1);
			if ((h % 4) == 0) AddBox(mb, v3(px, py + 0.42f, pz), v3(0.16f, 0.03f, 0.03f), (h & 8) ? RGBA8(255, 70, 40) : RGBA8(40, 180, 255), -1);
			if ((h % 17) == 0) AddPuff(puff, puffN, 11, px, py + 0.5f, pz, 12.f);
			break;
		case TH_CITADEL:
			if ((h % 6) == 0) AddBox(mb, v3(px, py + 0.38f, pz), v3(0.13f, 0.38f, 0.13f), RGBA8(210, 198, 224), -1);
			if ((h % 5) == 0) AddBox(mb, v3(px, py + 0.78f, pz), v3(0.22f, 0.045f, 0.22f), RGBA8(110, 78, 140), -1);
			if ((h % 9) == 0) {
				AddBox(mb, v3(px, py + 0.55f, pz), v3(0.025f, 0.52f, 0.025f), RGBA8(92, 70, 48), -1);
				AddBox(mb, v3(px, py + 1.12f, pz), v3(0.08f, 0.07f, 0.08f), RGBA8(255, 196, 110), -1);
				AddPuff(puff, puffN, 12, px, py + 1.15f, pz, 8.f);
			}
			if ((h % 13) == 0) AddBox(mb, v3(px, py + 0.42f, pz), v3(0.1f, 0.16f, 0.04f), RGBA8(170, 90, 230), -1);
			if ((h % 11) == 0) AddPuff(puff, puffN, 12, px, py + 0.9f, pz, 10.f);
			break;
		case TH_LANTERN:
			if ((h % 4) == 0) AddBox(mb, v3(px, py + 0.45f, pz), v3(0.02f, 0.4f, 0.02f), RGBA8(210, 200, 170), -1);
			if ((h % 4) == 0) AddBox(mb, v3(px, py + 0.88f, pz), v3(0.07f, 0.05f, 0.07f), RGBA8(255, 210, 120), -1);
			if ((h % 3) == 0) AddPuff(puff, puffN, 13, px, py + 0.9f, pz, 10.f);
			break;
		default: break;
		}
	}
	if (theme != TH_VOLCANO) return;
	int used[24] = {};
	for (int k = 0; k < 4; k++) {
		int best = -1;
		for (int i = 0; i < cn; i++) {
			if (used[i]) continue;
			int apart = 1;
			for (int j = 0; j < cn; j++) if (used[j]) {
				int dx = cand[i].x - cand[j].x, dz = cand[i].z - cand[j].z;
				if (dx * dx + dz * dz < 36) apart = 0;
			}
			if (!apart) continue;
			if (best < 0 || cand[i].sc > cand[best].sc) best = i;
		}
		if (best < 0) break;
		used[best] = 1;
		float cx = cand[best].x + 0.5f, cz = cand[best].z + 0.5f;
		float base = m.StandY(cand[best].x, cand[best].z) - 0.02f;
		ConeRing(mb, cx, base, cz, base + 0.55f, 0.42f, 0.30f, RGBA8(48, 30, 26));
		ConeRing(mb, cx, base + 0.55f, cz, base + 1.05f, 0.30f, 0.18f, RGBA8(32, 22, 20));
		ConeRing(mb, cx, base + 1.05f, cz, base + 1.32f, 0.18f, 0.28f, RGBA8(96, 40, 20));
		float iy = base + 1.14f;
		lava.Quad(v3(cx - 0.14f, iy, cz - 0.14f), v3(cx - 0.14f, iy, cz + 0.14f),
			v3(cx + 0.14f, iy, cz + 0.14f), v3(cx + 0.14f, iy, cz - 0.14f), v3(0, 1, 0), 0, 0, 1, 1, -1, 0xFFFFFFFF);
		AddPuff(puff, puffN, 0, cx, base + 1.42f, cz, 6.f);
	}
}

static DWORD LiquidFlow(const Map& m, int x, int z, int liquid)
{
	float fx = 0.f, fz = 0.f, rush = 0.f;
	float y0 = m.StandY(x, z);
	static const int dx[4] = { 1, -1, 0, 0 };
	static const int dz[4] = { 0, 0, 1, -1 };
	for (int k = 0; k < 4; k++) {
		int nx = x + dx[k], nz = z + dz[k];
		BOOL in = Map::In(nx, nz);
		int nbT = in ? m.At(nx, nz).t : T_VOID;
		if (!in || nbT == T_VOID || nbT == T_CHASM) {
			if (liquid == T_WATER && m.theme != TH_SEABED) { fx += (float)dx[k] * 2.2f; fz += (float)dz[k] * 2.2f; rush = 1.f; }
			if (liquid == T_LAVA) { fx += (float)dx[k] * 1.4f; fz += (float)dz[k] * 1.4f; rush = max(rush, 0.9f); }
			continue;
		}
		const Cell& nb = m.At(nx, nz);
		if (nb.t == liquid) {
			float drop = y0 - m.StandY(nx, nz);
			if (drop > 0.012f) { fx += dx[k] * drop; fz += dz[k] * drop; }
		}
		if (liquid == T_WATER && m.theme != TH_SEABED && y0 - m.TopY(nx, nz) > 0.85f) {
			fx += (float)dx[k] * 1.8f;
			fz += (float)dz[k] * 1.8f;
			rush = 1.f;
		}
		if (liquid == T_LAVA && nb.t == T_LAVA) {
			float drop = y0 - m.StandY(nx, nz);
			if (drop > 0.02f) rush = max(rush, 0.55f);
		}
		if (liquid == T_LAVA && nb.t != T_LAVA && y0 - m.TopY(nx, nz) > 0.9f) {
			fx += (float)dx[k] * 1.6f;
			fz += (float)dz[k] * 1.6f;
			rush = max(rush, 0.85f);
		}
	}
	if (m.theme == TH_FALLS && liquid == T_WATER) { fx += 0.9f; rush = max(rush, 0.72f); }
	else if (m.theme == TH_VOLCANO && liquid == T_LAVA) { fz += 0.35f; rush = max(rush, 0.5f); }
	else if (m.theme == TH_SWAMP && liquid == T_WATER) { fx += 0.22f; fz += 0.1f; }
	else if (m.theme == TH_SEABED && liquid == T_WATER) { fx += 0.28f; fz -= 0.18f; }
	else { fx += 0.2f; fz += 0.08f; }
	float len = sqrtf(fx * fx + fz * fz);
	if (len < 1e-4f) len = 1.f;
	fx /= len; fz /= len;
	int r = Clampi((int)((fx * 0.5f + 0.5f) * 255.f + 0.5f), 0, 255);
	int g = Clampi((int)((fz * 0.5f + 0.5f) * 255.f + 0.5f), 0, 255);
	int b = Clampi((int)(rush * 255.f + 0.5f), 0, 255);
	return RGBA8(r, g, b, 255);
}

static int VisBent(const Map& m, int x, int z, int kind, int layer)
{
	float mid = TacVisY(m, x, z, x + 0.5f, z + 0.5f, kind, layer);
	static const float px[8] = { 0, 1, 0, 1, 0.5f, 1, 0.5f, 0 };
	static const float pz[8] = { 0, 0, 1, 1, 0, 0.5f, 1, 0.5f };
	for (int i = 0; i < 8; i++) {
		float y = TacVisY(m, x, z, x + px[i], z + pz[i], kind, layer);
		if (fabsf(y - mid) > 0.015f) return 1;
	}
	return 0;
}

static void EmitCurved(MeshBuild& geo, const Map& m, int x, int z, int kind, int layer, DWORD col)
{
	int n = VisBent(m, x, z, kind, layer) ? 4 : 1;
	float ys[5][5];
	for (int j = 0; j <= n; j++) for (int i = 0; i <= n; i++) {
		float px = (float)x + (float)i / (float)n;
		float pz = (float)z + (float)j / (float)n;
		float y = TacVisY(m, x, z, px, pz, kind, layer);
		if (kind == 1) y -= 0.02f;
		ys[j][i] = y;
	}
	for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
		float x0 = (float)x + (float)i / (float)n, x1 = (float)x + (float)(i + 1) / (float)n;
		float z0 = (float)z + (float)j / (float)n, z1 = (float)z + (float)(j + 1) / (float)n;
		V3 a = v3(x0, ys[j][i], z0);
		V3 b = v3(x0, ys[j + 1][i], z1);
		V3 c = v3(x1, ys[j + 1][i + 1], z1);
		V3 d = v3(x1, ys[j][i + 1], z0);
		V3 nrm = Norm(Cross(b - a, d - a));
		if (nrm.y < 0.f) nrm = v3(-nrm.x, -nrm.y, -nrm.z);
		geo.Quad(a, b, c, d, nrm, z0 - z, x0 - x, z1 - z, x1 - x, -1, col);
	}
}

static void EdgePoint(int x, int z, int dir, float t, float& px, float& pz)
{
	if (dir == 0) { px = x + 1.f; pz = (float)z + t; }
	else if (dir == 1) { px = (float)x; pz = (float)z + 1.f - t; }
	else if (dir == 2) { px = (float)x + 1.f - t; pz = (float)z + 1.f; }
	else { px = (float)x + t; pz = (float)z; }
}

static DWORD FallTint(DWORD flow, int fromSky)
{
	return (flow & 0x00FFFFFF) | (fromSky ? 0xA0000000 : 0xFF000000);
}

static void EmitCurtain(MeshBuild& mb, float x0, float z0, float x1, float z1, float top, float bot, float nx, float nz, float off, DWORD col)
{
	float ox = nx * off, oz = nz * off;
	V3 n = v3(nx, 0, nz);
	mb.Quad(v3(x0 + ox, top, z0 + oz), v3(x1 + ox, top, z1 + oz),
		v3(x1 + ox, bot, z1 + oz), v3(x0 + ox, bot, z0 + oz),
		n, 0, 0, 1, 1, -1, col);
}

static BOOL InteriorPool(const Map& m, int x, int z)
{
	if (x <= 0 || z <= 0 || x >= MAPN - 1 || z >= MAPN - 1) return FALSE;
	static const int dx[4] = { 1, -1, 0, 0 }, dz[4] = { 0, 0, 1, -1 };
	for (int k = 0; k < 4; k++) {
		int nx = x + dx[k], nz = z + dz[k];
		if (!Map::In(nx, nz)) return FALSE;
		int t = m.At(nx, nz).t;
		if (t == T_VOID || t == T_CHASM) return FALSE;
	}
	return TRUE;
}

static void EmitCeilingFall(MeshBuild& wb, Renderer::SceneryPuff* puff, int& puffN, const Map& m, int x, int z)
{
	float surf = m.StandY(x, z);
	float ceilY = 20.f;
	float bot = surf + 0.12f;
	DWORD col = FallTint(LiquidFlow(m, x, z, T_WATER), 1);
	unsigned h = CellHash(x, z, 91);
	float cx = x + 0.5f, cz = z + 0.5f;
	float half = 0.4f;
	if (h & 16) {
		EmitCurtain(wb, cx - half, cz, cx + half, cz, ceilY, bot, 0, 1, 0.08f, col);
		EmitCurtain(wb, cx - half, cz, cx + half, cz, ceilY, bot, 0, 1, -0.08f, col);
	} else {
		EmitCurtain(wb, cx, cz - half, cx, cz + half, ceilY, bot, 1, 0, 0.08f, col);
		EmitCurtain(wb, cx, cz - half, cx, cz + half, ceilY, bot, 1, 0, -0.08f, col);
	}
	AddPuff(puff, puffN, 1, cx, (ceilY + surf) * 0.5f, cz, 8.f);
	AddPuff(puff, puffN, 16, cx, surf + 0.08f, cz, 5.f);
}

static void EmitEdgeWall(MeshBuild& geo, const Map& m, int x, int z, int dir, int kind, int layer, float drop, float floorY, int useFloor, DWORD col)
{
	const int N = 4;
	float px[5], pz[5], y[5];
	for (int i = 0; i <= N; i++) {
		EdgePoint(x, z, dir, (float)i / (float)N, px[i], pz[i]);
		y[i] = TacVisY(m, x, z, px[i], pz[i], kind, layer);
		if (kind == 1) y[i] -= 0.02f;
	}
	V3 nrm = dir == 0 ? v3(1, 0, 0) : dir == 1 ? v3(-1, 0, 0) : dir == 2 ? v3(0, 0, 1) : v3(0, 0, -1);
	for (int i = 0; i < N; i++) {
		float y0 = y[i], y1 = y[i + 1];
		float b0 = useFloor ? floorY : y0 - drop;
		float b1 = useFloor ? floorY : y1 - drop;
		if (b0 > y0) b0 = y0;
		if (b1 > y1) b1 = y1;
		if (y0 <= b0 + 1e-3f && y1 <= b1 + 1e-3f) continue;
		geo.Quad(v3(px[i], y0, pz[i]), v3(px[i + 1], y1, pz[i + 1]),
			v3(px[i + 1], b1, pz[i + 1]), v3(px[i], b0, pz[i]), nrm, 0, 0, 1, 1, -1, col);
	}
}

struct KitMesh { int theme; Buf<VtxS> v; Buf<WORD> i; };
static KitMesh gKit[40];
static int gKitN = 0, gKitReady = 0, gKitAt[16], gKitCnt[16];

static void EnsureThemeKits()
{
	if (gKitReady) return;
	gKitReady = 1;
	gKitN = 0;
	for (int i = 0; i < 16; i++) { gKitAt[i] = 0; gKitCnt[i] = 0; }
	static_assert(sizeof(VtxS) == 36, "kit vertex");
	wchar_t dir[MAX_PATH];
	GetModuleFileNameW(NULL, dir, MAX_PATH);
	wchar_t* sl = wcsrchr(dir, L'\\');
	if (sl) *sl = 0;
	wchar_t path[MAX_PATH];
	swprintf_s(path, L"%s\\tacticsbgm\\resource\\tactics\\props\\kit.bin", dir);
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (h == INVALID_HANDLE_VALUE) return;
	DWORD sz = GetFileSize(h, NULL);
	if (sz < 8 || sz >= 16u * 1024u * 1024u) { CloseHandle(h); return; }
	HANDLE hMap = CreateFileMappingW(h, NULL, PAGE_READONLY, 0, 0, NULL);
	const BYTE* buf = hMap ? (const BYTE*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0) : NULL;
	BOOL ok = buf && memcmp(buf, "TPR1", 4) == 0;
	if (!ok) {
		if (buf) UnmapViewOfFile(buf);
		if (hMap) CloseHandle(hMap);
		CloseHandle(h);
		return;
	}
	const BYTE* p = buf + 4;
	const BYTE* end = buf + sz;
	UINT n = 0;
	memcpy(&n, p, 4); p += 4;
	if (n > 40) n = 40;
	for (UINT i = 0; i < n && p + 12 <= end; i++) {
		UINT theme = 0, nv = 0, ni = 0;
		memcpy(&theme, p, 4); p += 4;
		memcpy(&nv, p, 4); p += 4;
		memcpy(&ni, p, 4); p += 4;
		size_t vb = (size_t)nv * sizeof(VtxS);
		size_t ib = ((size_t)ni * 2u + 3u) & ~3u;
		if (theme >= 16 || !nv || ni < 3 || nv > 65000 || p + vb + ib > end) break;
		KitMesh& km = gKit[gKitN];
		km.theme = (int)theme;
		km.v.resize((int)nv);
		km.i.resize((int)ni);
		if (!km.v.data() || !km.i.data()) break;
		memcpy(km.v.data(), p, vb); p += vb;
		memcpy(km.i.data(), p, (size_t)ni * 2u); p += ib;
		gKitN++;
	}
	UnmapViewOfFile(buf);
	CloseHandle(hMap);
	CloseHandle(h);
	for (int t = 0; t < 16; t++) {
		int b = -1, c = 0;
		for (int i = 0; i < gKitN; i++) if (gKit[i].theme == t) {
			if (b < 0) b = i;
			c++;
		}
		gKitAt[t] = b < 0 ? 0 : b;
		gKitCnt[t] = c;
	}
}

static void PlaceKit(MeshBuild& mb, int theme, int var, float x, float y, float z, float yaw)
{
	if (theme < 0 || theme >= 16 || gKitCnt[theme] <= 0) return;
	if (mb.v.size() > 220000) return;
	const KitMesh& s = gKit[gKitAt[theme] + (var % gKitCnt[theme])];
	float sc = 0.92f + (var % 5) * 0.05f;
	M4 w = MMul(MMul(MScale(sc, sc, sc), MRotY(yaw)), MTrans(x, y, z));
	UINT base = (UINT)mb.v.size();
	for (int i = 0; i < (int)s.v.size(); i++) {
		VtxS vx = s.v[i];
		V3 p = MXformPt(w, v3(vx.p[0], vx.p[1], vx.p[2]));
		V3 nrm = Norm(MXformDir(w, v3(vx.n[0], vx.n[1], vx.n[2])));
		vx.p[0] = p.x; vx.p[1] = p.y; vx.p[2] = p.z;
		vx.n[0] = nrm.x; vx.n[1] = nrm.y; vx.n[2] = nrm.z;
		mb.v.push_back(vx);
	}
	for (int i = 0; i < (int)s.i.size(); i++) mb.i.push_back(base + s.i[i]);
}

static int TacChunkOf(float x, float z)
{
	int cx = (int)floorf(x * (1.f / 16.f));
	int cz = (int)floorf(z * (1.f / 16.f));
	if (cx < 0) cx = 0;
	if (cx > 3) cx = 3;
	if (cz < 0) cz = 0;
	if (cz > 3) cz = 3;
	return (cz << 2) + cx;
}

static void TacSplitChunks(const MeshBuild& src, MeshBuild dst[16])
{
	for (int i = 0; i < 16; i++) { dst[i].v.clear(); dst[i].i.clear(); }
	const VtxS* v = src.v.data();
	const UINT* idx = src.i.data();
	int nv = (int)src.v.size();
	int ni = (int)src.i.size();
	for (int t = 0; t + 2 < ni; t += 3) {
		UINT i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
		if (i0 >= (UINT)nv || i1 >= (UINT)nv || i2 >= (UINT)nv) continue;
		float x = (v[i0].p[0] + v[i1].p[0] + v[i2].p[0]) * (1.f / 3.f);
		float z = (v[i0].p[2] + v[i1].p[2] + v[i2].p[2]) * (1.f / 3.f);
		MeshBuild& d = dst[TacChunkOf(x, z)];
		UINT b = (UINT)d.v.size();
		d.v.push_back(v[i0]);
		d.v.push_back(v[i1]);
		d.v.push_back(v[i2]);
		d.i.push_back(b);
		d.i.push_back(b + 1);
		d.i.push_back(b + 2);
	}
}

static void TacMeshBounds(const MeshBuild& mb, float& cx, float& cy, float& cz, float& rad)
{
	float mn[3] = { 1e9f, 1e9f, 1e9f }, mx[3] = { -1e9f, -1e9f, -1e9f };
	for (int i = 0; i < (int)mb.v.size(); i++) {
		const float* p = mb.v[i].p;
		for (int k = 0; k < 3; k++) {
			if (p[k] < mn[k]) mn[k] = p[k];
			if (p[k] > mx[k]) mx[k] = p[k];
		}
	}
	cx = (mn[0] + mx[0]) * 0.5f;
	cy = (mn[1] + mx[1]) * 0.5f;
	cz = (mn[2] + mx[2]) * 0.5f;
	float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
	rad = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz) + 1.5f;
}

BOOL Renderer::BuildTerrain(Map& m)
{
	m_theme = Clampi(m.theme, 0, TH_COUNT - 1);
	m_terrain.Release();
	for (int i = 0; i < 16; i++) {
		m_terrainChunk[i].Release();
		m_waterChunk[i].mesh.Release();
		m_waterChunk[i].rad = 0;
		m_lavaChunk[i].mesh.Release();
		m_lavaChunk[i].rad = 0;
	}
	for (int i = 0; i < 16; i++) { m_cover1[i].Release(); m_cover2[i].Release(); m_cover3[i].Release(); m_cover4[i].Release(); }
	m_water.Release(); m_lava.Release();
	for (auto& b : m_props) b.mesh.Release();
	m_props.clear();
	static MeshBuild wb, lb, wood, sceneMb, tch[16], cov1[16], cov2[16], cov3[16], cov4[16];
	wb.v.clear(); wb.i.clear(); lb.v.clear(); lb.i.clear(); wood.v.clear(); wood.i.clear();
	for (int i = 0; i < 16; i++) { tch[i].v.clear(); tch[i].i.clear(); }
	for (int i = 0; i < 16; i++) {
		cov1[i].v.clear(); cov1[i].i.clear(); cov2[i].v.clear(); cov2[i].i.clear();
		cov3[i].v.clear(); cov3[i].i.clear(); cov4[i].v.clear(); cov4[i].i.clear();
	}
	sceneMb.v.clear(); sceneMb.i.clear();
	m_puffN = 0;
	{
		int iA = FindStatic("mountain_A"), iB = FindStatic("mountain_B"), iC = FindStatic("mountain_C"), iG = FindStatic("mountain_A_grass");
		auto radOf = [&](int si) -> float {
			if (si < 0) return 0.f;
			const float* b = statics[si].bb;
			float hx = max(fabsf(b[0]), fabsf(b[3]));
			float hz = max(fabsf(b[2]), fabsf(b[5]));
			return sqrtf(hx * hx + hz * hz);
		};
		BYTE keep[MAPN][MAPN];
		memset(keep, 0, sizeof(keep));
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			Cell& src = m.c[z][x];
			if (src.prop != PK_MOUNT) continue;
			int fw = src.gparam & 15, fd = (src.gparam >> 4) & 15;
			if (fw < 1 || fw > 3) fw = 1;
			if (fd < 1 || fd > 3) fd = 1;
			for (int dz = 0; dz < fd; dz++) for (int dx = 0; dx < fw; dx++) {
				int ix = x + dx, iz = z + dz;
				if (Map::In(ix, iz)) keep[iz][ix] = 1;
			}
			int si = (m.theme == TH_PLAINS || m.theme == TH_FOREST) && (src.var & 3) == 0 ? iG : ((src.var % 3 == 0) ? iA : (src.var % 3 == 1 ? iB : iC));
			if (m.theme == TH_VOLCANO) si = iC;
			else if (m.theme == TH_SNOW) si = iA;
			else if (m.theme == TH_DESERT) si = iB;
			float modelR = radOf(si);
			if (modelR < 1e-3f) continue;
			float span = (fw > fd ? (float)fw : (float)fd);
			float sc = (0.62f + (src.var % 4) * .03f) * (fw == 1 && fd == 1 ? 1.f : span * 0.92f);
			// 見た目の円より 1 マス内側だけを通行不可にする。
			float R = modelR * sc - 1.f;
			if (R < 0.45f) continue;
			float cx = x + 0.5f + (fw - 1) * 0.5f;
			float cz = z + 0.5f + (fd - 1) * 0.5f;
			int reach = (int)ceilf(R);
			if (reach > 11) reach = 11;
			for (int dz = -reach; dz <= reach; dz++) for (int dx = -reach; dx <= reach; dx++) {
				int ix = x + dx, iz = z + dz;
				if (!Map::In(ix, iz)) continue;
				float qx = cx < (float)ix ? (float)ix : (cx > (float)ix + 1.f ? (float)ix + 1.f : cx);
				float qz = cz < (float)iz ? (float)iz : (cz > (float)iz + 1.f ? (float)iz + 1.f : cz);
				float ddx = qx - cx, ddz = qz - cz;
				if (ddx * ddx + ddz * ddz > R * R) continue;
				keep[iz][ix] = 1;
				Cell& c = m.c[iz][ix];
				if (c.t == T_VOID || c.t == T_CHASM || c.t == T_DOOR || c.t == T_BRIDGE || c.t == T_LAVA || c.t == T_WATER) continue;
				if (TerrainInfo(c.t).costFoot >= 99) continue;
				BOOL home = FALSE;
				for (int p = 0; p < m.nP; p++) if (m.pStartX[p] == ix && m.pStartZ[p] == iz) home = TRUE;
				for (int e = 0; e < m.nE; e++) if (m.eStartX[e] == ix && m.eStartZ[e] == iz) home = TRUE;
				if (home) continue;
				BOOL anchor = (c.prop == PK_MOUNT);
				c.t = T_MOUNTAIN;
				if (!anchor) { c.prop = PK_NONE; c.gparam = 0; }
			}
		}
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			Cell& c = m.c[z][x];
			if (c.t != T_MOUNTAIN || c.prop == PK_MOUNT || keep[z][x]) continue;
			c.t = T_HILL;
		}
	}
	const float bottom = -3.2f;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		const Cell& c = m.At(x, z);
		if (c.t == T_VOID) continue;
		int tchI = ((z >> 4) << 2) + (x >> 4);
		if (tchI < 0) tchI = 0;
		if (tchI > 15) tchI = 15;
		MeshBuild& geo = tch[tchI];
		DWORD col = (c.t == T_CHASM) ? RGBAf(.35f, .32f, .33f) : 0xFFFFFFFF;
		if (c.t == T_WATER) col = 0xFEFFFFFF;
		if (c.t != T_BRIDGE) {
			EmitCurved(geo, m, x, z, 0, 0, col);
		} else {
			float y = m.TopY(x, z);
			int fl = BridgeFloor(c);
			if (fl) {
				float uy = fl * kHStep;
				geo.Quad(v3((float)x, uy, (float)z), v3((float)x, uy, (float)z + 1), v3((float)x + 1, uy, (float)z + 1), v3((float)x + 1, uy, (float)z), v3(0, 1, 0),
					0, 0, 1, 1, -1, 0xFFFFFFFF);
				float mid = (uy + y) * 0.5f, hh = (y - uy) * 0.5f;
				AddBox(wood, v3(x + .12f, mid, z + .12f), v3(.05f, hh, .05f), 0xFFFFFFFF, -1);
				AddBox(wood, v3(x + .88f, mid, z + .12f), v3(.05f, hh, .05f), 0xFFFFFFFF, -1);
				AddBox(wood, v3(x + .12f, mid, z + .88f), v3(.05f, hh, .05f), 0xFFFFFFFF, -1);
				AddBox(wood, v3(x + .88f, mid, z + .88f), v3(.05f, hh, .05f), 0xFFFFFFFF, -1);
			}
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
		if (c.t != T_BRIDGE) {
			for (int k = 0; k < 4; k++) {
				int nx = x + dx[k], nz = z + dz[k];
				int nbIn = Map::In(nx, nz);
				int nbT = nbIn ? m.At(nx, nz).t : T_VOID;
				int blends = nbIn && nbT != T_VOID && nbT != T_BRIDGE;
				if (!blends) {
					float floorY = bottom;
					if (nbIn && nbT == T_BRIDGE) floorY = m.TopY(nx, nz);
					EmitEdgeWall(geo, m, x, z, k, 0, 0, 0.f, floorY, 1, 0xFFFFFFFF);
				}
				float surf = m.StandY(x, z);
				float ny = nbIn ? m.TopY(nx, nz) : bottom;
				BOOL offMap = !nbIn || nbT == T_VOID;
				BOOL drop = nbIn && nbT != T_VOID && surf - ny > 0.9f;
				if (m.theme != TH_SEABED && c.t == T_WATER && (offMap || drop)) {
					float ex0, ez0, ex1, ez1;
					EdgePoint(x, z, k, 0.f, ex0, ez0);
					EdgePoint(x, z, k, 1.f, ex1, ez1);
					float nxn = (float)dx[k], nzn = (float)dz[k];
					float fallBot = offMap ? -9.f : ny + 0.15f;
					if (nbIn && (nbT == T_CHASM || nbT == T_VOID)) fallBot = -9.f;
					DWORD wc = FallTint(LiquidFlow(m, x, z, T_WATER), 0);
					EmitCurtain(wb, ex0, ez0, ex1, ez1, surf, fallBot, nxn, nzn, 0.08f, wc);
					EmitCurtain(wb, ex0, ez0, ex1, ez1, surf, fallBot, nxn, nzn, 0.18f, wc);
					float mxOut = (ex0 + ex1) * 0.5f + nxn * 0.14f, mzOut = (ez0 + ez1) * 0.5f + nzn * 0.14f;
					AddPuff(m_puff, m_puffN, 1, mxOut, (surf + fallBot) * 0.5f, mzOut, 5.f);
					AddPuff(m_puff, m_puffN, 16, mxOut, surf + 0.05f, mzOut, 4.f);
					if (fallBot > -8.f)
						AddPuff(m_puff, m_puffN, 16, mxOut, fallBot + 0.1f, mzOut, 4.f);
				}
				if (c.t == T_LAVA && (offMap || (nbIn && (nbT == T_CHASM || nbT == T_VOID)))) {
					float ex0, ez0, ex1, ez1;
					EdgePoint(x, z, k, 0.f, ex0, ez0);
					EdgePoint(x, z, k, 1.f, ex1, ez1);
					float nxn = (float)dx[k], nzn = (float)dz[k];
					float fallBot = offMap ? -9.f : ny + 0.12f;
					if (nbIn && (nbT == T_CHASM || nbT == T_VOID)) fallBot = -9.f;
					DWORD lc = LiquidFlow(m, x, z, T_LAVA);
					EmitCurtain(lb, ex0, ez0, ex1, ez1, surf + 0.04f, fallBot, nxn, nzn, 0.1f, lc);
					EmitCurtain(lb, ex0, ez0, ex1, ez1, surf + 0.02f, fallBot + 0.35f, nxn, nzn, 0.2f, lc);
					unsigned h = CellHash(x, z, k + 17);
					int drips = 2 + (int)(h % 3);
					for (int d = 0; d < drips; d++) {
						float t0 = (d + 0.18f) / (float)drips;
						float t1 = t0 + 0.16f;
						if (t1 > 1.f) t1 = 1.f;
						float dx0, dz0, dx1, dz1;
						EdgePoint(x, z, k, t0, dx0, dz0);
						EdgePoint(x, z, k, t1, dx1, dz1);
						float extra = 0.6f + ((h >> (d * 3)) & 7) * 0.22f;
						EmitCurtain(lb, dx0, dz0, dx1, dz1, surf, fallBot - extra, nxn, nzn, 0.16f, lc);
					}
					AddPuff(m_puff, m_puffN, 8, (ex0 + ex1) * 0.5f, (surf + fallBot) * 0.5f, (ez0 + ez1) * 0.5f, 6.f);
				}
			}
		}
		if (c.t == T_WATER)
			EmitCurved(wb, m, x, z, 1, T_WATER, LiquidFlow(m, x, z, T_WATER));
		else if (c.t == T_LAVA)
			EmitCurved(lb, m, x, z, 1, T_LAVA, LiquidFlow(m, x, z, T_LAVA));
		if (m.theme != TH_SEABED && c.t == T_WATER && InteriorPool(m, x, z) && (CellHash(x, z, 91) % 10) == 0)
			EmitCeilingFall(wb, m_puff, m_puffN, m, x, z);
		for (int L = 1; L <= 4; L++) {
			if ((int)c.nLay < L + 1) continue;
			int ch = ((z >> 4) << 2) + (x >> 4);
			MeshBuild& dk = L == 1 ? cov1[ch] : L == 2 ? cov2[ch] : L == 3 ? cov3[ch] : cov4[ch];
			EmitCurved(dk, m, x, z, 2, L, 0xFFFFFFFF);
			for (int k = 0; k < 4; k++) {
				int nx = x + dx[k], nz = z + dz[k];
				if (Map::In(nx, nz) && m.At(nx, nz).t != T_VOID) continue;
				EmitEdgeWall(dk, m, x, z, k, 2, L, 0.72f, 0.f, 0, 0xFFFFFFFF);
			}
		}
	}
	ScatterTheme(m, sceneMb, lb, m_puff, m_puffN);
	for (int i = 0; i < 16; i++) {
		if (tch[i].v.empty()) continue;
		if (!MakeMesh32(m_terrainChunk[i], tch[i].v.data(), sizeof(VtxS), (UINT)tch[i].v.size(), tch[i].i.data(), (UINT)tch[i].i.size())) return FALSE;
		m_terrainChunk[i].tex = TEX_ATLAS;
	}
	for (int i = 0; i < 16; i++) {
		if (!cov1[i].v.empty()) { MakeMesh32(m_cover1[i], cov1[i].v.data(), sizeof(VtxS), (UINT)cov1[i].v.size(), cov1[i].i.data(), (UINT)cov1[i].i.size()); m_cover1[i].tex = TEX_ATLAS; }
		if (!cov2[i].v.empty()) { MakeMesh32(m_cover2[i], cov2[i].v.data(), sizeof(VtxS), (UINT)cov2[i].v.size(), cov2[i].i.data(), (UINT)cov2[i].i.size()); m_cover2[i].tex = TEX_ATLAS; }
		if (!cov3[i].v.empty()) { MakeMesh32(m_cover3[i], cov3[i].v.data(), sizeof(VtxS), (UINT)cov3[i].v.size(), cov3[i].i.data(), (UINT)cov3[i].i.size()); m_cover3[i].tex = TEX_ATLAS; }
		if (!cov4[i].v.empty()) { MakeMesh32(m_cover4[i], cov4[i].v.data(), sizeof(VtxS), (UINT)cov4[i].v.size(), cov4[i].i.data(), (UINT)cov4[i].i.size()); m_cover4[i].tex = TEX_ATLAS; }
	}
	static MeshBuild chunkPart[16];
	auto uploadLiquid = [&](MeshBuild& src, BoundMesh* dst, int tex) {
		TacSplitChunks(src, chunkPart);
		for (int i = 0; i < 16; i++) {
			dst[i].mesh.Release();
			dst[i].rad = 0;
			if (chunkPart[i].v.empty()) continue;
			if (!MakeMesh32(dst[i].mesh, chunkPart[i].v.data(), sizeof(VtxS), (UINT)chunkPart[i].v.size(), chunkPart[i].i.data(), (UINT)chunkPart[i].i.size())) continue;
			dst[i].mesh.tex = tex;
			TacMeshBounds(chunkPart[i], dst[i].cx, dst[i].cy, dst[i].cz, dst[i].rad);
		}
	};
	uploadLiquid(wb, m_waterChunk, TEX_WHITE);
	uploadLiquid(lb, m_lavaChunk, TEX_WHITE);
	switch (m.theme) {
	case TH_SWAMP: m_waterColor = RGBA8(70, 100, 60); break;
	case TH_SNOW: m_waterColor = RGBA8(110, 160, 210); break;
	case TH_DESERT: m_waterColor = RGBA8(50, 150, 170); break;
	case TH_SEABED: m_waterColor = RGBA8(20, 70, 90); break;
	case TH_FALLS: m_waterColor = RGBA8(190, 222, 245); break;
	case TH_ORBIT: m_waterColor = RGBA8(70, 100, 170); break;
	case TH_STATION: m_waterColor = RGBA8(40, 140, 170); break;
	case TH_CITADEL: m_waterColor = RGBA8(90, 62, 130); break;
	case TH_LANTERN: m_waterColor = RGBA8(170, 186, 220); break;
	default: m_waterColor = RGBA8(55, 125, 190); break;
	}

	// 小物。木・岩・橋は写真テクスチャ、旗やランタンはモデル自身のテクスチャ。
	static MeshBuild treeMb, rockMb, proc, tentMb, flagMb, lilyMb, plantMb, lantMb, pumpMb, kitMb;
	treeMb.v.clear(); treeMb.i.clear(); rockMb.v.clear(); rockMb.i.clear(); proc.v.clear(); proc.i.clear();
	tentMb.v.clear(); tentMb.i.clear(); flagMb.v.clear(); flagMb.i.clear(); lilyMb.v.clear(); lilyMb.i.clear();
	plantMb.v.clear(); plantMb.i.clear(); lantMb.v.clear(); lantMb.i.clear(); pumpMb.v.clear(); pumpMb.i.clear();
	kitMb.v.clear(); kitMb.i.clear();
	EnsureThemeKits();
	auto flush = [&](MeshBuild& mb, int tex, float mat) {
		if (mb.v.empty()) return;
		TacSplitChunks(mb, chunkPart);
		for (int i = 0; i < 16; i++) {
			if (chunkPart[i].v.empty()) continue;
			PropBatch pb;
			pb.hue = 0;
			pb.mat = mat;
			pb.cx = pb.cy = pb.cz = 0;
			pb.rad = 0;
			if (MakeMesh32(pb.mesh, chunkPart[i].v.data(), sizeof(VtxS), (UINT)chunkPart[i].v.size(), chunkPart[i].i.data(), (UINT)chunkPart[i].i.size())) {
				pb.mesh.tex = tex;
				TacMeshBounds(chunkPart[i], pb.cx, pb.cy, pb.cz, pb.rad);
				m_props.push_back(pb);
			}
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
				int fw = c.gparam & 15, fd = (c.gparam >> 4) & 15;
				if (fw < 1 || fw > 3) fw = 1;
				if (fd < 1 || fd > 3) fd = 1;
				int si = (m.theme == TH_PLAINS || m.theme == TH_FOREST) && (c.var & 3) == 0 ? sMtG : ((c.var % 3 == 0) ? sMtA : (c.var % 3 == 1 ? sMtB : sMtC));
				if (m.theme == TH_VOLCANO) si = sMtC;
				else if (m.theme == TH_SNOW) si = sMtA;
				else if (m.theme == TH_DESERT) si = sMtB;
				float span = (fw > fd ? (float)fw : (float)fd);
				float sc = (0.62f + (c.var % 4) * .03f) * (fw == 1 && fd == 1 ? 1.f : span * 0.92f);
				place(rockMb, si, cx + (fw - 1) * 0.5f, cy - .05f, cz + (fd - 1) * 0.5f, sc, rot, tint, 0);
				if (m.theme == TH_VOLCANO && span >= 2.f)
					AddPuff(m_puff, m_puffN, 0, cx + (fw - 1) * 0.5f, cy + sc * 0.35f, cz + (fd - 1) * 0.5f, 2.2f);
				break; }
			case PK_LILY:
				place((c.var & 1) ? lilyMb : plantMb, (c.var & 1) ? sLily : sPlant, cx + jx * 3, m.StandY(x, z) - .01f, cz + jz * 3, 3.f, rot, tint, 0.9f);
				break;
			case PK_PILLAR: {
				float hgt = .9f + (c.var % 5) * .18f;
				AddBox(proc, v3(cx, cy + .06f, cz), v3(.36f, .06f, .36f), tint, -1);
				AddBox(proc, v3(cx, cy + .12f + hgt * .5f, cz), v3(.24f, hgt * .5f, .24f), tint, -1);
				if ((c.var & 3) != 0) AddBox(proc, v3(cx, cy + .12f + hgt + .06f, cz), v3(.32f, .06f, .32f), tint, -1);
				break; }
			case PK_GRAVE: place(rockMb, sGrave, cx, cy, cz, .42f, rot, tint, 0); break;
			case PK_TENT: place(tentMb, sTent, cx, cy, cz, 1.9f, rot, tint, 0.55f); break;
			case PK_FLAG: place(flagMb, sFlag, cx, cy, cz, 3.f, rot, tint, 1.f); break;
			case PK_BARREL: place(wood, sBarrel, cx, cy, cz, 2.6f, rot, tint, 0); place(wood, sBarrel, cx + .25f, cy, cz - .2f, 2.2f, rot, tint, 0); break;
			case PK_CRATE: place(wood, sCrate, cx, cy, cz, 2.6f, rot, tint, 0); break;
			case PK_LANTERN: place(lantMb, sLant, cx + .3f, cy, cz + .3f, .6f, rot, tint, 0.4f); break;
			case PK_PUMPKIN: place(pumpMb, sPump, cx, cy, cz, .45f, rot, tint, 0.2f); break;
			case PK_KIT: PlaceKit(kitMb, m.theme, c.var, cx, cy, cz, rot); break;
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
		flush(kitMb, TEX_WHITE, 3);
	}
	flush(wood, TEX_WHITE, 2);
	flush(sceneMb, TEX_WHITE, 3);
	BuildMinimap(m);
	return TRUE;
}

static void MiniRgb(const Map& m, int t, int bridgeFloor, int& r, int& g, int& b)
{
	r = 100; g = 160; b = 80;
	switch (t) {
	case T_PLAIN:
		if (m.theme == TH_SNOW) { r = 230; g = 235; b = 245; }
		else if (m.theme == TH_DESERT) { r = 220; g = 195; b = 140; }
		else if (m.theme == TH_VOLCANO) { r = 85; g = 75; b = 72; }
		else if (m.theme == TH_CANYON) { r = 150; g = 115; b = 80; }
		else if (m.theme == TH_SEABED) { r = 70; g = 140; b = 120; }
		else if (m.theme == TH_FALLS) { r = 90; g = 150; b = 80; }
		else if (m.theme == TH_ORBIT) { r = 82; g = 96; b = 132; }
		else if (m.theme == TH_STATION) { r = 90; g = 100; b = 112; }
		else if (m.theme == TH_CITADEL) { r = 158; g = 140; b = 172; }
		else if (m.theme == TH_LANTERN) { r = 190; g = 196; b = 214; }
		else { r = 105; g = 165; b = 80; }
		break;
	case T_FOREST:
		if (m.theme == TH_SNOW) { r = 120; g = 150; b = 160; }
		else if (m.theme == TH_VOLCANO) { r = 70; g = 48; b = 36; }
		else if (m.theme == TH_DESERT) { r = 70; g = 110; b = 50; }
		else { r = 28; g = 118; b = 42; }
		break;
	case T_HILL: r = 140; g = 160; b = 90; break;
	case T_MOUNTAIN: r = 120; g = 112; b = 105; break;
	case T_WATER:
		if (m.theme == TH_SNOW) { r = 210; g = 230; b = 248; }
		else if (m.theme == TH_SWAMP) { r = 48; g = 78; b = 42; }
		else if (m.theme == TH_VOLCANO) { r = 40; g = 70; b = 100; }
		else if (m.theme == TH_FALLS) { r = 150; g = 220; b = 245; }
		else if (m.theme == TH_SEABED) { r = 16; g = 80; b = 130; }
		else if (m.theme == TH_DESERT) { r = 36; g = 150; b = 170; }
		else { r = 40; g = 130; b = 220; }
		break;
	case T_CHASM: r = 20; g = 18; b = 22; break;
	case T_LAVA: r = 240; g = 100; b = 30; break;
	case T_SAND: r = 225; g = 200; b = 140; break;
	case T_SNOW: r = 235; g = 240; b = 250; break;
	case T_SWAMP: r = 80; g = 90; b = 55; break;
	case T_ROAD: r = 185; g = 170; b = 140; break;
	case T_BRIDGE:
		if (bridgeFloor) { r = 196; g = 132; b = 64; }
		else { r = 160; g = 110; b = 60; }
		break;
	case T_RUINS: r = 170; g = 165; b = 150; break;
	case T_ROCK: r = 90; g = 88; b = 86; break;
	case T_VOID: r = 12; g = 14; b = 20; break;
	default: break;
	}
}

void Renderer::BuildMinimap(const Map& m)
{
	for (int i = 0; i < LAY_N; i++) { TAC_REL(m_srvMiniL[i]); TAC_REL(m_texMiniL[i]); }
	m_miniN = 1;
	for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
		int n = CellLayN(m.At(x, z));
		if (n > m_miniN) m_miniN = n;
	}
	if (m_miniN > LAY_N) m_miniN = LAY_N;
	enum { MINI_S = MAPN * 4 };
	static Buf<DWORD> px;
	px.resize((size_t)MINI_S * MINI_S);
	for (int L = 0; L < m_miniN; L++) {
		for (int z = 0; z < MAPN; z++) for (int x = 0; x < MAPN; x++) {
			const Cell& c = m.At(x, z);
			int r = 12, g = 14, b = 22;
			float sh = 1.f;
			int tr = T_VOID;
			if (LayerOn(c, L) && LayerTr(c, L) != T_VOID) {
				tr = LayerTr(c, L);
				MiniRgb(m, tr, L == 0 && BridgeFloor(c), r, g, b);
				if (L == 1) { r = (r * 3 + 210) / 4; g = (g * 3 + 180) / 4; b = (b * 3 + 90) / 4; }
				else if (L == 2) { r = (r * 3 + 210) / 4; g = (g * 3 + 120) / 4; b = (b * 3 + 170) / 4; }
				else if (L == 3) { r = (r * 3 + 120) / 4; g = (g * 3 + 210) / 4; b = (b * 3 + 160) / 4; }
				else if (L >= 4) { r = (r * 3 + 160) / 4; g = (g * 3 + 140) / 4; b = (b * 3 + 220) / 4; }
				sh = .82f + LayerHt(c, L) * .04f;
				if (c.gimm == GM_HEAL) { r = 80; g = 230; b = 120; sh = 1.f; }
				else if (c.gimm == GM_MIGHT) { r = 255; g = 200; b = 60; sh = 1.f; }
				else if (c.gimm == GM_WARP) { r = 80; g = 210; b = 255; sh = 1.f; }
				else if (c.gimm == GM_CHEST) { r = 230; g = 180; b = 50; sh = 1.f; }
				else if (tr == T_DOOR) { r = 150; g = 100; b = 50; sh = 1.f; }
			}
			int rr = (int)(r * sh), gg = (int)(g * sh), bb = (int)(b * sh);
			for (int oy = 0; oy < 4; oy++) for (int ox = 0; ox < 4; ox++) {
				int pr = rr, pg = gg, pb = bb;
				if (ox == 0 || oy == 0) { pr = pr * 3 / 4; pg = pg * 3 / 4; pb = pb * 3 / 4; }
				if (tr == T_WATER && ((ox + oy) & 1)) { pr = min(255, pr + 28); pg = min(255, pg + 24); pb = min(255, pb + 18); }
				if (tr == T_WATER && c.gimm == GM_NONE && c.gparam > 0 && c.gparam <= 4) {
					int dk = 14 * (int)c.gparam;
					pr = max(0, pr - dk); pg = max(0, pg - dk); pb = max(0, pb - dk / 2);
				}
				px[(size_t)(z * 4 + oy) * MINI_S + (x * 4 + ox)] = BGRA(pr, pg, pb);
			}
		}
		MakeTex(m_dev, m_ctx, px.data(), MINI_S, MINI_S, FALSE, &m_texMiniL[L], &m_srvMiniL[L]);
	}
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
		int nv = (int)s.bind.size();
		int step = 1;
		if (nv > 1536) step = (nv + 1535) / 1536;
		for (int i = 0; i < nv; i += step) {
			const CSkinV& v = s.bind[(size_t)i];
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
	FlushQueuedDraws();
	if (!m_inPortrait) {
		m_inPortrait = TRUE;
		memcpy(g_ptSavedFcb, &m_fcb, sizeof(m_fcb));
		g_ptSavedLvp = m_lightVP;
		g_ptSavedOk = 1;
		SetSight(NULL, 0, 0);
		SetFog(500.f, 1000.f, 0, RGBA8(0, 0, 0));
		SetAmbient(RGBA8(206, 192, 178), RGBA8(148, 116, 94), .58f, .55f);
		SetLight(Norm(v3(.42f, .84f, -.40f)), v3(0, 0, 0), 3.5f);
		SetShadowEnabled(FALSE);
		m_ctx->OMSetRenderTargets(1, &m_ptRtv, m_ptDsv);
		m_ctx->ClearDepthStencilView(m_ptDsv, D3D11_CLEAR_DEPTH, 1.f, 0);
		ID3D11ShaderResourceView* sh = NULL;
		m_ctx->PSSetShaderResources(1, 1, &sh);
		if (m_groundSrv) m_ctx->PSSetShaderResources(3, 1, &m_groundSrv);
		if (m_matSrv) m_ctx->PSSetShaderResources(4, 1, &m_matSrv);
		if (m_photoSrv) m_ctx->PSSetShaderResources(5, 1, &m_photoSrv);
		if (m_sphereSrv) m_ctx->PSSetShaderResources(6, 1, &m_sphereSrv);
		if (m_envSrv) m_ctx->PSSetShaderResources(7, 1, &m_envSrv);
		ID3D11ShaderResourceView* refOff = NULL;
		m_ctx->PSSetShaderResources(8, 1, &refOff);
	}
	m_portraitSlot = slot;
	return TRUE;
}

void Renderer::EndPortrait()
{
	FlushQueuedDraws();
}

void Renderer::FinishPortraits()
{
	FlushQueuedDraws();
	const BOOL wasPortrait = m_inPortrait;
	m_inPortrait = FALSE;
	if (wasPortrait && g_ptSavedOk) {
		memcpy(&m_fcb, g_ptSavedFcb, sizeof(m_fcb));
		m_lightVP = g_ptSavedLvp;
		g_ptSavedOk = 0;
	}
	m_fcb.screen[0] = (float)m_w; m_fcb.screen[1] = (float)m_h;
	if (wasPortrait) UpdateFrameCB();
}

// ============================================================ HUD
BOOL Renderer::ApplyPost()
{
	ClearTess();
	if (m_inPortrait || !m_psBloom || !m_psBlur || !m_psGrade || !m_sceneTex || !m_blA || !m_blB || !m_bb) return FALSE;
	ID3D11RenderTargetView* nullRT = NULL;
	m_ctx->OMSetRenderTargets(0, &nullRT, NULL);
	ID3D11ShaderResourceView* unb[2] = { NULL, NULL };
	m_ctx->PSSetShaderResources(0, 2, unb);
	if (m_msaa > 1 && m_msTex)
		m_ctx->ResolveSubresource(m_sceneTex, 0, m_msTex, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
	else
		m_ctx->CopyResource(m_sceneTex, m_bb);

	m_ctx->VSSetShader(m_vsQ, NULL, 0);
	m_ctx->IASetInputLayout(NULL);
	m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_ctx->RSSetState(m_rsNone);
	m_ctx->OMSetDepthStencilState(m_dsOff, 0);
	m_ctx->OMSetBlendState(m_bsOpaque, NULL, 0xFFFFFFFF);
	m_ctx->PSSetSamplers(0, 1, &m_sClamp);
	int curW = -1, curH = -1;
	ID3D11PixelShader* curPs = NULL;
	auto fs = [&](ID3D11PixelShader* ps, ID3D11ShaderResourceView* t0, ID3D11ShaderResourceView* t1, ID3D11RenderTargetView* dst, int w, int h, const DrawParams& dp, float p0) {
		m_ctx->PSSetShaderResources(0, 2, unb);
		m_ctx->OMSetRenderTargets(1, &dst, NULL);
		if (w != curW || h != curH) {
			D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
			m_ctx->RSSetViewports(1, &vp);
			curW = w; curH = h;
		}
		SetObj(MIdent(), dp, p0);
		ID3D11ShaderResourceView* srv[2] = { t0, t1 };
		m_ctx->PSSetShaderResources(0, 2, srv);
		if (ps != curPs) { m_ctx->PSSetShader(ps, NULL, 0); curPs = ps; }
		m_ctx->Draw(3, 0);
	};
	ID3D11ShaderResourceView* src = m_sceneSrv;
	// 作成プレビューは Q.w を負にする。シェーダは場面そのものを返すだけなので、全面の書き写しをしない。
	float qRank = TacQRank(m_quality, m_qPlus);
	if (m_fcb.q[3] >= 0.f && m_theme >= 0 && TacQFx(m_quality) && m_dofSrv && m_zSrv && (qRank >= 2.f ? (qRank >= 3.5f ? m_psDofU : m_psDofH) : NULL)) {
		if (m_msaa > 1 && m_zTex && m_zRes)
			m_ctx->ResolveSubresource(m_zRes, 0, m_zTex, 0, DXGI_FORMAT_R16_FLOAT);
		DrawParams df;
		df.emissive = TacQLerp(qRank, 0.f, 0.f, 4.f, 6.f, 14.f, 18.f, 24.f, 28.f);
		df.hue = 0.16f;
		fs(qRank >= 3.5f ? m_psDofU : m_psDofH, m_sceneSrv, m_zSrv, m_dofRtv, m_w, m_h, df, 0);
		src = m_dofSrv;
	}
	int nblur = (int)(TacQLerp(qRank, 0.f, 2.f, 3.f, 4.f, 5.f, 6.f, 8.f, 8.f) + 0.5f);
	if (m_gpu == 3 && m_quality < 5) nblur = min(nblur, 2);
	if (nblur > 0) {
	DrawParams ex;
		ex.mode = 0.92f;
	fs(m_psBloom, m_sceneSrv, NULL, m_blARtv, m_blW, m_blH, ex, 0);
		for (int pass = 0; pass < nblur; pass++) {
		float rad = pass ? 2.2f : 1.f;
		DrawParams bh; bh.mode = 0; bh.emissive = 1.f / m_blW; bh.hue = rad;
		fs(m_psBlur, m_blASrv, NULL, m_blBRtv, m_blW, m_blH, bh, 1.f);
		DrawParams bv; bv.mode = 1.f; bv.emissive = 1.f / m_blH; bv.hue = rad;
		fs(m_psBlur, m_blBSrv, NULL, m_blARtv, m_blW, m_blH, bv, 0);
		}
	} else if (m_blARtv) {
		float zc[4] = { 0, 0, 0, 0 };
		m_ctx->ClearRenderTargetView(m_blARtv, zc);
	}
	float su, sv, svis;
	ProjectSun(m_vp, m_eye, m_fcb.light, su, sv, svis);
	DrawParams gr;
	gr.mode = 0.62f; gr.emissive = su; gr.hue = sv; gr.rim = 1.f; gr.sway = svis;
	gr.dissolve = (float)m_wx;
	gr.flash = (float)m_hour;
	ID3D11PixelShader* grade = (m_quality == 4 && m_psGradeTiny) ? m_psGradeTiny : m_psGrade;
	ID3D11ShaderResourceView* zBind = (TacQFx(m_quality) && m_zSrv) ? m_zSrv : NULL;
	m_ctx->PSSetShaderResources(2, 1, &zBind);
	fs(grade, src, m_blASrv, m_bbRtv, m_w, m_h, gr, 0);
	ID3D11ShaderResourceView* zOff = NULL;
	m_ctx->PSSetShaderResources(2, 1, &zOff);
	m_ctx->PSSetShaderResources(0, 2, unb);
	ID3D11SamplerState* ss[2] = { m_sLin, m_sCmp };
	m_ctx->PSSetSamplers(0, 2, ss);
	return TRUE;
}

BOOL Renderer::LoadSkyReady() const
{
	return m_loadSky && m_loadSkyW == m_w && m_loadSkyH == m_h && m_w > 0 && m_h > 0;
}

void Renderer::DropLoadSky()
{
	if (m_ctx) {
		ID3D11RenderTargetView* nullRT = NULL;
		m_ctx->OMSetRenderTargets(0, &nullRT, NULL);
	}
	TAC_REL(m_loadSky);
	m_loadSkyW = m_loadSkyH = 0;
}

void Renderer::RememberLoadSky()
{
	if (!m_ctx || !m_dev || !m_bb || m_w < 1 || m_h < 1) return;
	D3D11_TEXTURE2D_DESC bd = {};
	m_bb->GetDesc(&bd);
	if ((int)bd.Width != m_w || (int)bd.Height != m_h || bd.SampleDesc.Count != 1) return;
	if (!m_loadSky || m_loadSkyW != m_w || m_loadSkyH != m_h) {
		TAC_REL(m_loadSky);
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = bd.Width;
		td.Height = bd.Height;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = bd.Format;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		if (FAILED(m_dev->CreateTexture2D(&td, NULL, &m_loadSky))) { m_loadSkyW = m_loadSkyH = 0; return; }
		m_loadSkyW = m_w;
		m_loadSkyH = m_h;
	}
	ID3D11RenderTargetView* nullRT = NULL;
	m_ctx->OMSetRenderTargets(0, &nullRT, NULL);
	m_ctx->CopyResource(m_loadSky, m_bb);
	m_ctx->OMSetRenderTargets(1, &m_bbRtv, NULL);
	D3D11_VIEWPORT vp = { 0, 0, (float)m_w, (float)m_h, 0, 1 };
	m_ctx->RSSetViewports(1, &vp);
}

void Renderer::ShowLoadSky()
{
	if (!m_ctx || !m_bb || !m_bbRtv || !LoadSkyReady()) return;
	FlushQueuedDraws();
	ClearTess();
	m_inPortrait = FALSE;
	ID3D11ShaderResourceView* unb[9] = {};
	m_ctx->PSSetShaderResources(0, 9, unb);
	ID3D11RenderTargetView* nullRT = NULL;
	m_ctx->OMSetRenderTargets(0, &nullRT, NULL);
	m_ctx->CopyResource(m_bb, m_loadSky);
	m_ctx->OMSetRenderTargets(1, &m_bbRtv, NULL);
	D3D11_VIEWPORT vp = { 0, 0, (float)m_w, (float)m_h, 0, 1 };
	m_ctx->RSSetViewports(1, &vp);
	m_fcb.screen[0] = (float)m_w;
	m_fcb.screen[1] = (float)m_h;
	m_fcb.screen[2] = 1.f / (float)m_w;
	m_fcb.screen[3] = 1.f / (float)m_h;
	UpdateFrameCB();
	m_hud.clear();
	m_hudSrv = m_srv[TEX_WHITE];
	m_hudPipe = 0;
}

void Renderer::BeginHud()
{
	FlushQueuedDraws();
	m_inPortrait = FALSE;
	m_fcb.screen[0] = (float)m_w; m_fcb.screen[1] = (float)m_h;
	ClearTess();
	// 作成プレビューは空が画面の大半。負の被写界で、空の画素だけ重いサンプルを省く。マップはそのまま。
	if (m_previewPost) { m_fcb.q[3] = -1.f; UpdateFrameCB(); }
	if (!ApplyPost() && m_msaa > 1 && m_msTex && m_bb)
		m_ctx->ResolveSubresource(m_bb, 0, m_msTex, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
	m_ctx->OMSetRenderTargets(1, &m_bbRtv, NULL);
	D3D11_VIEWPORT vp = { 0, 0, (float)m_w, (float)m_h, 0, 1 };
	m_ctx->RSSetViewports(1, &vp);
	m_fcb.screen[0] = (float)m_w; m_fcb.screen[1] = (float)m_h; m_fcb.screen[2] = 1.f / m_w; m_fcb.screen[3] = 1.f / m_h;
	UpdateFrameCB();
	m_hud.clear();
	m_hudSrv = m_srv[TEX_WHITE];
	m_hudPipe = 0;
}

void Renderer::FlushHud()
{
	if (m_hud.empty()) return;
	const BOOL glass = (m_hudSrv == (ID3D11ShaderResourceView*)1);
	const int wantPipe = glass ? 2 : 1;
	if (m_hudPipe != wantPipe) {
		if (m_hudPipe == 0) {
			ClearTess();
			m_ctx->IASetInputLayout(m_ilH);
			m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			m_ctx->VSSetShader(m_vsH, NULL, 0);
			m_ctx->RSSetState(m_rsHud);
			m_ctx->OMSetDepthStencilState(m_dsOff, 0);
			m_ctx->OMSetBlendState(m_bsPremul, NULL, 0xFFFFFFFF);
			m_ctx->PSSetSamplers(0, 1, &m_sClamp);
		}
		m_ctx->PSSetShader(glass ? m_psUi : m_psH, NULL, 0);
		m_hudPipe = wantPipe;
	}
	if (!glass) m_ctx->PSSetShaderResources(0, 1, &m_hudSrv);
	const size_t maxV = 6 * 4096;
	size_t off = 0;
	while (off < (size_t)m_hud.size()) {
		size_t n = min(maxV, (size_t)m_hud.size() - off);
		UINT st = sizeof(VtxH), o = 0;
		void* dst = TacRingMap(m_ctx, m_vbHud, (UINT)(sizeof(VtxH) * 6 * 4096 * 4), m_hudPos, (UINT)(n * sizeof(VtxH)), o);
		if (!dst) break;
		memcpy(dst, m_hud.data() + off, n * sizeof(VtxH));
		m_ctx->Unmap(m_vbHud, 0);
		m_ctx->IASetVertexBuffers(0, 1, &m_vbHud, &st, &o);
		m_ctx->Draw((UINT)n, 0);
		off += n;
	}
	m_hud.clear();
}

void Renderer::EndHud()
{
	FlushHud();
	if (m_ctx) {
		ID3D11SamplerState* ss[2] = { m_sLin, m_sCmp };
		m_ctx->PSSetSamplers(0, 2, ss);
	}
	m_hudPipe = 0;
}

void Renderer::HudPush(float x, float y, float w, float h, float u0, float v0, float u1, float v1, DWORD c0, DWORD c1, ID3D11ShaderResourceView* srv)
{
	if (!srv) srv = m_srv[TEX_WHITE];
	if (srv != m_hudSrv) { FlushHud(); m_hudSrv = srv; }
	if (m_hud.size() + 6 > 24576) FlushHud();
	VtxH a = { x, y, u0, v0, c0, { 0, 0, 0, 0 }, 0 };
	VtxH b = { x + w, y, u1, v0, c0, { 0, 0, 0, 0 }, 0 };
	VtxH c = { x + w, y + h, u1, v1, c1, { 0, 0, 0, 0 }, 0 };
	VtxH d = { x, y + h, u0, v1, c1, { 0, 0, 0, 0 }, 0 };
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
	float rad = (w < h ? w : h) * 0.16f;
	if (rad > 14.f) rad = 14.f;
	if (rad < 5.f) rad = 5.f;
	float a = alpha * ((fill >> 24) & 255) / 255.f;
	DWORD fc = (fill & 0x00FFFFFF) | 0xFF000000;
	ID3D11ShaderResourceView* gsrv = (ID3D11ShaderResourceView*)1;
	if (m_hudSrv != gsrv) { FlushHud(); m_hudSrv = gsrv; }
	if (m_hud.size() + 6 > 24576) FlushHud();
	VtxH v0 = { x, y, 0, 0, fc, { w, h, rad, a }, border };
	VtxH v1 = { x + w, y, 1, 0, fc, { w, h, rad, a }, border };
	VtxH v2 = { x + w, y + h, 1, 1, fc, { w, h, rad, a }, border };
	VtxH v3 = { x, y + h, 0, 1, fc, { w, h, rad, a }, border };
	m_hud.push_back(v0); m_hud.push_back(v1); m_hud.push_back(v2);
	m_hud.push_back(v0); m_hud.push_back(v2); m_hud.push_back(v3);
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
	VtxH p[4] = {};
	for (int i = 0; i < 4; i++) {
		p[i].x = xy[i * 2]; p[i].y = xy[i * 2 + 1];
		p[i].u = uv[i * 2]; p[i].v = uv[i * 2 + 1];
		p[i].c = c;
	}
	m_hud.push_back(p[0]); m_hud.push_back(p[1]); m_hud.push_back(p[2]);
	m_hud.push_back(p[0]); m_hud.push_back(p[2]); m_hud.push_back(p[3]);
}

static void TacTextUnlink(int* head, int* next, UINT hash, int slot)
{
	if (!hash || slot < 0) return;
	int b = (int)(hash & 1023u);
	int prev = -1;
	for (int i = head[b]; i >= 0; ) {
		int nxt = next[i];
		if (i == slot) {
			if (prev < 0) head[b] = nxt;
			else next[prev] = nxt;
			next[slot] = -1;
			return;
		}
		prev = i;
		i = nxt;
	}
}

TextSprite* Renderer::GetText(const wchar_t* s, float px, int bold, int shadow, int box, float boxW, float boxH)
{
	if (!s || !s[0]) return NULL;
	static_assert(TAC_TEXTH == 1024, "text buckets");
	wchar_t key[TAC_TEXTK];
	_snwprintf_s(key, _TRUNCATE, L"%d|%d|%d|%d|%d|%s", (int)(px * 4), bold, shadow, box ? (int)boxW : 0, box ? (int)boxH : 0, s);
	UINT hv = 2166136261u;
	for (const wchar_t* p = key; *p; p++) { hv ^= (UINT)*p; hv *= 16777619u; }
	if (!hv) hv = 1;
	for (int ti = m_textHead[hv & 1023u]; ti >= 0; ti = m_textNext[ti]) {
		if (m_textHash[ti] != hv || !m_text[ti].live) continue;
		if (!wcscmp(m_text[ti].key, key)) { m_text[ti].sp.lastFrame = m_frame; return &m_text[ti].sp; }
	}
	int slot = -1, oldest = -1;
	DWORD oldestF = 0;
	for (int ti = 0; ti < TAC_TEXTN; ti++) {
		if (!m_text[ti].live) { if (slot < 0) slot = ti; continue; }
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
	if (m_text[slot].live) {
		TacTextUnlink(m_textHead, m_textNext, m_textHash[slot], slot);
		TAC_REL(m_text[slot].sp.srv); TAC_REL(m_text[slot].sp.tex);
	}
	m_text[slot].live = 1;
	wcsncpy_s(m_text[slot].key, key, _TRUNCATE);
	m_text[slot].sp = ts;
	m_textHash[slot] = hv;
	m_textNext[slot] = m_textHead[hv & 1023u];
	m_textHead[hv & 1023u] = slot;
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


void Renderer::MarkGpuBegin()
{
	if (!m_ctx || !m_qDisjoint || !m_qTs0 || !m_qTs1) return;
	if (m_gpuArmed == 2) {
		D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
		UINT64 a = 0, b = 0;
		if (m_ctx->GetData(m_qDisjoint, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;
		if (!dj.Disjoint && dj.Frequency
			&& m_ctx->GetData(m_qTs0, &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK
			&& m_ctx->GetData(m_qTs1, &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && b >= a)
			m_gpuMs = (float)((double)(b - a) * 1000.0 / (double)dj.Frequency);
		m_gpuArmed = 0;
	}
	if (m_gpuArmed != 0) return;
	m_ctx->Begin(m_qDisjoint);
	m_ctx->End(m_qTs0);
	m_gpuArmed = 1;
}

void Renderer::MarkGpuEnd()
{
	if (!m_ctx || m_gpuArmed != 1 || !m_qDisjoint || !m_qTs1) return;
	m_ctx->End(m_qTs1);
	m_ctx->End(m_qDisjoint);
	m_gpuArmed = 2;
}

void Renderer::Present()
{
	Present(1, 0);
}

void Renderer::Present(int, int)
{
	MarkGpuEnd();
	FlushQueuedDraws();
	if (!m_swap) return;
	m_presentBusy = FALSE;
	const BOOL vis = m_hwnd && ::IsWindowVisible(m_hwnd);
	HRESULT hr = m_swap->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
	if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
		m_presentBusy = TRUE;
		if (vis) m_presented = TRUE;
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
		if (m_frame - m_text[ti].sp.lastFrame > 300) {
			TacTextUnlink(m_textHead, m_textNext, m_textHash[ti], ti);
			TAC_REL(m_text[ti].sp.srv); TAC_REL(m_text[ti].sp.tex);
			m_text[ti].live = 0;
			m_textHash[ti] = 0;
		}
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

static BOOL TacInClip(const float* m, V3 p, float r)
{
	float rad = (r < .6f ? .6f : r) * 1.15f + .5f;
	float x = p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12];
	float y = p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13];
	float z = p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14];
	float w = p.x * m[3] + p.y * m[7] + p.z * m[11] + m[15];
	float dx = rad * sqrtf(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
	float dy = rad * sqrtf(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
	float dz = rad * sqrtf(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
	float dw = rad * sqrtf(m[3] * m[3] + m[7] * m[7] + m[11] * m[11]);
	float wMax = w + dw;
	if (wMax < .02f) return FALSE;
	if (x < -wMax - dx || x > wMax + dx) return FALSE;
	if (y < -wMax - dy || y > wMax + dy) return FALSE;
	if (z < -dz || z > wMax + dz) return FALSE;
	return TRUE;
}

BOOL Renderer::InView(V3 p, float r) const
{
	return TacInClip(m_vp.m, p, r);
}

BOOL Renderer::InLight(V3 p, float r) const
{
	return TacInClip(m_lightVP.m, p, r);
}

BOOL Renderer::BoundVisible(float cx, float cy, float cz, float rad) const
{
	if (rad < 1.f) rad = 1.f;
	V3 mid = MXformPt(m_sceneShift, v3(cx, cy, cz));
	if (m_pass == PASS_SHADOW) return InLight(mid, rad);
	return InView(mid, rad);
}

void Renderer::ScreenRay(float sx, float sy, V3& org, V3& dir) const
{
	M4 inv = m_inPortrait ? MInverse(m_vp) : m_fcb.invVP;
	float nx = sx / m_w * 2.f - 1.f, ny = 1.f - sy / m_h * 2.f;
	V3 a = MXformPt(inv, v3(nx, ny, 0.f));
	V3 b = MXformPt(inv, v3(nx, ny, 1.f));
	org = a;
	dir = Norm(b - a);
}

} // namespace otac
