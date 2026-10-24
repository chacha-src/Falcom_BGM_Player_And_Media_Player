#pragma once
// おまけタクティクス: DirectX11 描画（アセット・GPU スキニング・地形・粒子・HUD）

#include <d3d11.h>
#include <dxgi1_2.h>
#include <vector>
#include <map>
#include <string>
#include "OmakeTacticsData.h"

namespace otac {

struct VtxS { float p[3], n[3], uv[2]; DWORD c; };
struct VtxK { float p[3], n[3], uv[2]; BYTE j[4]; BYTE w[4]; };
struct VtxH { float x, y, u, v; DWORD c; };
struct VtxP { float p[3]; float u, v, shape; float c[4]; };

inline DWORD RGBA8(int r, int g, int b, int a = 255)
{
	return (DWORD)(Clampi(r, 0, 255) | (Clampi(g, 0, 255) << 8) | (Clampi(b, 0, 255) << 16) | (Clampi(a, 0, 255) << 24));
}
inline DWORD RGBAf(float r, float g, float b, float a = 1.f)
{
	return RGBA8((int)(r * 255.f + .5f), (int)(g * 255.f + .5f), (int)(b * 255.f + .5f), (int)(a * 255.f + .5f));
}

struct GpuMesh {
	ID3D11Buffer* vb;
	ID3D11Buffer* ib;
	UINT nIdx;
	UINT stride;
	int tex;       // テクスチャ番号（-1=白）
	int glow;
	int i32;       // 32bit インデックス
	GpuMesh() : vb(NULL), ib(NULL), nIdx(0), stride(0), tex(-1), glow(0), i32(0) {}
	void Release();
};

enum { MAXJ = 64 };

struct Clip {
	char name[32];
	int nFrames;
	float fps;
	std::vector<Q4> q;    // nFrames*nJ
	std::vector<V3> hips; // nFrames
	float Duration() const { return nFrames > 1 ? (nFrames - 1) / fps : 0.f; }
};

struct Rig {
	int nJ;
	char names[MAXJ][24];
	int parent[MAXJ];
	int hips;
	std::vector<Clip> clips;
	int Find(const char* n) const;
	int FindClip(const char* n) const;
};

struct Model {
	char name[32];
	int rig;         // -1=手続きモデル
	int tex;
	int nJ;
	int parent[MAXJ];
	V3 restT[MAXJ];
	Q4 restR[MAXJ];
	V3 restS[MAXJ];
	M4 top;
	M4 invBind[MAXJ];
	std::vector<GpuMesh> parts;
	float headMin[3], headMax[3];  // 頭部（頭ボーン基準）
	int headJ, chestJ, handR, handL, hipsJ;
};

struct Attach {
	char name[32];
	char joint[24];
	int tex;
	GpuMesh mesh;
};

struct StaticSrc {
	char name[32];
	int tex;
	float bb[6];
	std::vector<VtxS> v;
	std::vector<WORD> i;
};

struct Pose {
	int nJ;
	M4 local[MAXJ];
	M4 global[MAXJ];
	M4 palette[MAXJ];
};

// 骨単位の追加変換（女性体型・飛行の翼など）
struct PoseMods {
	V3 scale[MAXJ];      // 子骨にも伝播
	V3 own[MAXJ];        // その骨のスキン頂点のみ（子へ伝播しない）
	int any;
	void Reset(int n) { any = 0; for (int i = 0; i < n && i < MAXJ; i++) scale[i] = own[i] = v3(1, 1, 1); }
};

struct DrawParams {
	DWORD tint;          // RGBA
	float outline;       // 0=なし
	float mode;
	float emissive;
	float hue;
	float dissolve;
	float flash;
	float rim;
	float sway;
	DWORD outlineColor;
	int twoSided;
	int hairJoint;
	DWORD hairTint;
	float clothHue;      // 人間の服の染め色だけを CPU スキニング時に回す
	int face;            // 人間の顔の型（0=既定）
	DrawParams() : tint(0xFFFFFFFF), outline(0), mode(0), emissive(0), hue(0), dissolve(0), flash(0), rim(.35f), sway(0), outlineColor(0xFF201814), twoSided(0), hairJoint(-1), hairTint(0xFFFFFFFF), clothHue(0), face(0) {}
};

enum Pass { PASS_SHADOW, PASS_MAIN, PASS_PORTRAIT };

struct TextSprite {
	ID3D11Texture2D* tex;
	ID3D11ShaderResourceView* srv;
	int w, h;           // テクスチャ寸法
	int cw, ch;         // 使用領域
	DWORD lastFrame;
};

struct Particle {
	V3 p, v;
	float life, max;
	float size, size1;
	float r, g, b, a;
	int shape;
	int add;
	float grav;
	float stretch;      // 速度方向に伸ばす
	float drag;
};

class Renderer {
public:
	Renderer();
	~Renderer();
	BOOL Init(HWND hwnd, int quality);
	void Release();
	BOOL Resize(int w, int h);
	BOOL Ready() const { return m_dev != NULL && m_swap != NULL; }
	BOOL AssetsReady() const { return m_assetsOk; }
	int W() const { return m_w; }
	int H() const { return m_h; }
	ID3D11Device* Dev() { return m_dev; }
	void SetQuality(int q);

	// アセット
	BOOL LoadAssets();
	void LoadCreaturePack();
	void LoadGearPack();
	std::vector<Rig> rigs;
	std::vector<Model> models;
	std::vector<Attach> attaches;
	std::vector<StaticSrc> statics;
	std::vector<GpuMesh> m_creatures;
	std::vector<GpuMesh> m_gear;
	std::vector<BYTE> m_gearBow;
	struct CSkinV { float p[3], n[3]; DWORD c; unsigned char j[4], w[4]; };
	struct CreatureSkin {
		int model; std::vector<CSkinV> bind; std::vector<BYTE> cloth;
		int faceSet, faceStart, faceN; float faceLin[9];   // 顔の型を載せる頭の頂点範囲
		float extX, extY, extZ, extR;   // モデル空間の中心と半径。肖像の枠に全身を収める
		CreatureSkin() : model(-1), faceSet(-1), faceStart(0), faceN(0), extX(0), extY(0), extZ(0), extR(0) {}
	};
	std::vector<CreatureSkin> m_skins;
	// 顔の型（性別ごと）。頭の頂点ごとの位置差・法線・色
	struct FaceSet { int nFace, nv; std::vector<short> d; std::vector<signed char> n; std::vector<DWORD> c; FaceSet() : nFace(0), nv(0) {} };
	FaceSet m_faces[2];
	void LoadFacePack();
	int FaceCount(int gender) const { return (gender >= 0 && gender < 2 && m_faces[gender].nFace > 0) ? m_faces[gender].nFace : 1; }
	// アバター（人間の体と衣装を丸ごと差し替える）。頂点は共通の体の空間で持ち、j は区間番号。
	// モデルごとの区間行列で各人間の骨へ移し、初回の描画で頂点を作って保持する。
	struct AvaMesh { std::vector<CSkinV> v; std::vector<BYTE> cloth; std::vector<float> dmax, dmin; };
	struct AvaBody { int nFace; AvaMesh m; std::vector<WORD> bodyTri, extraTri; AvaBody() : nFace(0) {} };
	struct AvaCos { char name[32]; int gender; AvaMesh m; std::vector<WORD> tri; std::vector<BYTE> keep; };
	struct AvaModel { char name[32]; int gender; std::vector<float> M; std::vector<std::string> joint; };
	struct AvaCache { int model, cos; CreatureSkin skin; std::vector<float> bx, bn; GpuMesh gm; };
	std::vector<std::string> m_avaSeg;
	AvaBody m_avaBody[2];
	std::vector<AvaCos> m_avaCos;
	std::vector<AvaModel> m_avaModels;
	std::vector<AvaCache> m_avaCache;
	void LoadAvatarPack();
	void ReleaseAvatarCache();
	int AvatarCount(int gender) const;
	// avatar は性別ごとに 1 から。作れないときは FALSE（呼び手が元の体を描く）。
	BOOL DrawAvatar(int model, int avatar, int bust, const Pose& pose, const M4& world, const DrawParams& dp);
	void SkinAndDraw(const CreatureSkin& src, GpuMesh& base, const float* bx, const float* bn, float bt, const Pose& pose, const M4& world, const DrawParams& dp);
	int FindModel(const char* n) const;
	int FindAttach(const char* n) const;
	int FindStatic(const char* n) const;
	int CreatureCount() const { return (int)m_creatures.size(); }
	BOOL CreatureExtent(int model, float& cx, float& cy, float& cz, float& rad) const;
	int CreatureModel(int i) const { return (i >= 0 && i < (int)m_skins.size()) ? m_skins[i].model : -1; }
	void DrawSkinnedCreature(int i, const Pose& pose, const M4& world, const DrawParams& dp);
	int GearCount() const { return (int)m_gear.size(); }
	BOOL GearIsBow(int i) const { return i >= 0 && i < (int)m_gearBow.size() && m_gearBow[i]; }	void DrawCreature(int i, const M4& world, const DrawParams& dp);
	void DrawGear(int i, const M4& world, const DrawParams& dp);
	int AddProcModel(const Model& m, const std::vector<VtxK>& v, const std::vector<WORD>& idx);
	BOOL MakeMesh(GpuMesh& out, const void* v, UINT stride, UINT nv, const WORD* idx, UINT ni, BOOL dynamic = FALSE);
	BOOL MakeMesh32(GpuMesh& out, const void* v, UINT stride, UINT nv, const UINT* idx, UINT ni);

	// ポーズ
	void EvalPose(const Model& m, int clipA, float tA, int loopA, int clipB, float tB, int loopB, float blend, const PoseMods* mods, Pose& out) const;
	void EvalPoseLocal(const Model& m, const Q4* rot, const V3* trans, const PoseMods* mods, Pose& out) const;
	void FinishPose(const Model& m, Pose& p, const PoseMods* mods = NULL) const;

	// フレーム
	void BeginFrame();
	void SetCamera(const M4& view, const M4& proj, V3 eye, float time);
	void SetLight(V3 dirToLight, V3 focus, float radius);
	void SetFog(float start, float end, float heightFog, DWORD color);
	void SetAmbient(DWORD sky, DWORD ground, float strength, float hard);
	void BeginShadowPass();
	void BeginMainPass();
	void DrawSky(DWORD top, DWORD bottom, int speedLines, DWORD lineColor);
	void ClearDepth();
	void CommitFrame() { UpdateFrameCB(); }
	void SetShadowEnabled(BOOL on) { m_fcb.light[3] = (on && m_shSrv) ? 1.f : 0.f; }
	Pass CurPass() const { return m_pass; }

	void DrawStatic(const GpuMesh& m, const M4& world, const DrawParams& dp);
	// m の索引で、頂点だけ v に差し替えて描く（毎フレーム CPU で動かす髪など）。
	void DrawStaticVerts(const GpuMesh& m, const VtxS* v, UINT nv, const M4& world, const DrawParams& dp);
	void DrawSkinned(const Model& m, const Pose& p, const M4& world, const DrawParams& dp);
	void DrawAttach(const Attach& a, const Pose& p, int joint, const M4& world, const DrawParams& dp, const M4* extra = NULL);
	void DrawTerrain();
	void DrawWater();
	void DrawOverlayQuads(const VtxS* q, int n, int cursorStyle);
	void DrawParticles(const Particle* ps, int n);
	void DrawRibbon(const std::vector<VtxP>& v);

	// 地形
	BOOL BuildTerrain(const Map& m);
	void BuildMinimap(const Map& m);
	ID3D11ShaderResourceView* MinimapSrv() { return m_srvMini; }

	// ポートレート
	enum { PORTRAIT_N = 24, PORTRAIT_PX = 256 };
	BOOL BeginPortrait(int slot);
	void EndPortrait();
	ID3D11ShaderResourceView* PortraitSrv() { return m_srvPortrait; }

	// HUD
	void BeginHud();
	void HudRect(float x, float y, float w, float h, DWORD c);
	void HudRectGrad(float x, float y, float w, float h, DWORD top, DWORD bottom);
	void HudFrame(float x, float y, float w, float h, float t, DWORD c);
	void HudPanel(float x, float y, float w, float h, DWORD fill, DWORD border, float alpha = 1.f);
	void HudImage(ID3D11ShaderResourceView* srv, float x, float y, float w, float h, float u0, float v0, float u1, float v1, DWORD c);
	void HudQuad(ID3D11ShaderResourceView* srv, const float* xy, const float* uv, DWORD c);
	// align: 0=左 1=中央 2=右。戻り値: 描画幅
	float HudText(const wchar_t* s, float x, float y, float px, DWORD c, int align = 0, int bold = 0, int shadow = 1, float alpha = 1.f);
	float TextWidth(const wchar_t* s, float px, int bold = 0);
	void HudTextBox(const wchar_t* s, float x, float y, float w, float h, float px, DWORD c, int bold = 0);
	void EndHud();
	void Present();
	void EndFrameTextGc();
	BOOL FrameReady();             // 待機オブジェクト（ブロックしない）
	BOOL RefreshSurface();         // 表示後にスワップチェインを付け直す
	BOOL DeviceLost() const { return m_lost; }

	BOOL ProjectToScreen(V3 p, float& sx, float& sy) const;
	void ScreenRay(float sx, float sy, V3& org, V3& dir) const;

	HRESULT LastError() const { return m_lastHr; }
	int FailStage() const { return m_failStage; }

	DWORD m_frame;
	M4 m_view, m_proj, m_vp;
	V3 m_eye;
	float m_time;

private:
	BOOL CreateShaders();
	BOOL CreateStates();
	BOOL CreateTargets();
	void ReleaseTargets();
	BOOL CreateProcTextures();
	BOOL LoadPngTex(const BYTE* data, UINT len, int slot);
	TextSprite* GetText(const wchar_t* s, float px, int bold, int shadow, int box, float boxW, float boxH);
	void FlushHud();
	void UpdateFrameCB();
	void SetObj(const M4& world, const DrawParams& dp, float outlineW);
	void BindForPass(BOOL skinned, BOOL outline);
	void HudPush(float x, float y, float w, float h, float u0, float v0, float u1, float v1, DWORD c0, DWORD c1, ID3D11ShaderResourceView* srv);

	HWND m_hwnd;
	HANDLE m_wait;
	BOOL m_lost;
	BOOL m_presented;
	DWORD m_waitStuck;   // 退役待ちが始まった時刻。0 は待ちなし
	DWORD m_lastRebind;  // 未合成のまま付け直した時刻
	int m_w, m_h;
	int m_quality;
	int m_msaa;
	HRESULT m_lastHr;
	int m_failStage;
	BOOL m_assetsOk;
	Pass m_pass;

	ID3D11Device* m_dev;
	ID3D11DeviceContext* m_ctx;
	IDXGISwapChain1* m_swap;
	ID3D11RenderTargetView* m_bbRtv;
	ID3D11Texture2D* m_bb;
	ID3D11Texture2D* m_msTex;
	ID3D11RenderTargetView* m_msRtv;
	ID3D11Texture2D* m_dsTex;
	ID3D11DepthStencilView* m_dsv;
	ID3D11Texture2D* m_shTex;
	ID3D11DepthStencilView* m_shDsv;
	ID3D11ShaderResourceView* m_shSrv;
	int m_shSize;
	ID3D11Texture2D* m_ptTex;
	ID3D11RenderTargetView* m_ptRtv;
	ID3D11ShaderResourceView* m_srvPortrait;
	ID3D11Texture2D* m_ptDs;
	ID3D11DepthStencilView* m_ptDsv;

	ID3D11VertexShader* m_vsS;
	ID3D11VertexShader* m_vsK;
	ID3D11PixelShader* m_psM;
	ID3D11PixelShader* m_psO;
	ID3D11PixelShader* m_psOV;
	ID3D11VertexShader* m_vsH;
	ID3D11PixelShader* m_psH;
	ID3D11VertexShader* m_vsP;
	ID3D11PixelShader* m_psP;
	ID3D11VertexShader* m_vsQ;
	ID3D11PixelShader* m_psSky;
	ID3D11InputLayout* m_ilS;
	ID3D11InputLayout* m_ilK;
	ID3D11InputLayout* m_ilH;
	ID3D11InputLayout* m_ilP;
	ID3D11Buffer* m_cbFrame;
	ID3D11Buffer* m_cbObj;
	ID3D11Buffer* m_cbBones;
	ID3D11Buffer* m_vbHud;
	ID3D11Buffer* m_vbDyn;
	UINT m_vbDynBytes;
	ID3D11SamplerState* m_sLin;
	ID3D11SamplerState* m_sCmp;
	ID3D11SamplerState* m_sClamp;
	ID3D11RasterizerState* m_rsBack;
	ID3D11RasterizerState* m_rsFront;
	ID3D11RasterizerState* m_rsNone;
	ID3D11RasterizerState* m_rsShadow;
	ID3D11RasterizerState* m_rsHud;
	ID3D11DepthStencilState* m_dsWrite;
	ID3D11DepthStencilState* m_dsRead;
	ID3D11DepthStencilState* m_dsOff;
	ID3D11BlendState* m_bsOpaque;
	ID3D11BlendState* m_bsAlpha;
	ID3D11BlendState* m_bsPremul;
	ID3D11BlendState* m_bsAdd;

	enum { TEX_MAX = 16, TEX_WHITE = 15, TEX_ATLAS = 14, TEX_PAL = 13 };
	ID3D11Texture2D* m_tex[TEX_MAX];
	ID3D11ShaderResourceView* m_srv[TEX_MAX];
	ID3D11ShaderResourceView* SrvFor(int tex) { return (tex >= 0 && tex < TEX_MAX && m_srv[tex]) ? m_srv[tex] : m_srv[TEX_WHITE]; }
public:
	int TexPalette() const { return TEX_PAL; }
	int TexAtlas() const { return TEX_ATLAS; }
	int TexWhite() const { return TEX_WHITE; }
	static void PaletteUV(int idx, float& u, float& v);  // 16x16 パレット
	static DWORD PaletteColor(int idx);
private:
	ID3D11Texture2D* m_texMini;
	ID3D11ShaderResourceView* m_srvMini;

	// 地形
	GpuMesh m_terrain;
	GpuMesh m_water;
	GpuMesh m_lava;
	struct PropBatch { GpuMesh mesh; float hue; };
	std::vector<PropBatch> m_props;
	DWORD m_waterColor;

	// フレーム定数
	struct FrameCB {
		M4 vp, lvp;
		float eye[4], light[4], fog[4], fogColor[4], screen[4], skyAmb[4], gndAmb[4];
	} m_fcb;
	M4 m_lightVP;

	// HUD
	Fix<VtxH, 24576> m_hud;
	ID3D11ShaderResourceView* m_hudSrv;
	std::map<std::wstring, TextSprite> m_text;
	BOOL m_inPortrait;
	int m_portraitSlot;
};

} // namespace otac
