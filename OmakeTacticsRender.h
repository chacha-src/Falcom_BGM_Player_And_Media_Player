#pragma once
// おまけタクティクス: DirectX11 描画（アセット・GPU スキニング・地形・粒子・HUD）

#include <d3d11.h>
#include <dxgi1_2.h>
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
// 物を持つ手の指の型。人体の Grip クリップのコマ（値 − 1）と同じ並び（pack_mon.py の UAL_GRIPS）。
// GRIP_NONE は持たない手で、クリップに捕った指の動きのまま。
enum { GRIP_NONE, GRIP_FIST, GRIP_STAFF, GRIP_WAND, GRIP_BOW, GRIP_TRIGGER, GRIP_BOOK, GRIP_SHIELD, GRIP_PINCH };
// 髪を出す頭蓋の表。シェーダの HairCloth と同じ並び。
enum { SKULL_NA = 16, SKULL_NE = 8 };

struct Clip {
	char name[32];
	int nFrames;
	float fps;
	Buf<Q4> q;    // nFrames*nJ
	Buf<V3> hips; // nFrames
	float Duration() const { return nFrames > 1 ? (nFrames - 1) / fps : 0.f; }
};

struct Rig {
	int nJ;
	char names[MAXJ][24];
	int parent[MAXJ];
	int hips;
	Fix<Clip, 40> clips;
	int Find(const char* n) const;
	int FindClip(const char* n) const;
	Rig() : nJ(0), hips(0) { memset(names, 0, sizeof(names)); memset(parent, 0, sizeof(parent)); }
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
	Buf<GpuMesh> parts;
	float headMin[3], headMax[3];  // 頭部（頭ボーン基準）
	int headJ, chestJ, handR, handL, hipsJ;
	// 拳メッシュの中心と、肘から拳へ抜ける柄の向き。手の骨の空間。骨の原点は拳からずれている。
	float palmR[3], palmL[3];
	float axisR[3], axisL[3];
	int palmOk;   // bit0=右 bit1=左
	volatile long live;   // 1 は読み終わった体。予約しただけの枠は 0
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
	Buf<VtxS> v;
	Buf<WORD> i;
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
	int hideMeshHair;    // 1 ならメッシュに焼き込まれた髪を描かない（選んだ髪型を被せる）
	DWORD hairTint;
	float clothHue;      // 人間の服の染め色だけを CPU スキニング時に回す
	int face;            // 人間の顔の型（0=既定）
	float bodyScale;     // 身長。CS がスキン後の位置へ掛ける
	DWORD clothTint;     // 0 以外なら服をこの色へ。元の明暗と色相差は残す
	int skinTone;        // 0無し 1白人 2アジア人 3黒人 4ラテン系 5中東系 6南アジア系 7ポリネシア系。服と髪は変えない
	DrawParams() : tint(0xFFFFFFFF), outline(0), mode(0), emissive(0), hue(0), dissolve(0), flash(0), rim(.35f), sway(0), outlineColor(0xFF201814), twoSided(0), hairJoint(-1), hideMeshHair(0), hairTint(0xFFFFFFFF), clothHue(0), face(0), bodyScale(1.f), clothTint(0), skinTone(0) {}
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
	typedef void (*TacLoadPulseFn)(int cur, int total, void* user);
	void SetLoadPulse(TacLoadPulseFn fn, void* user) { m_loadFn = fn; m_loadUser = user; }
	void LoadPulse(int cur, int total) { if (m_loadFn && total > 0) m_loadFn(cur, total, m_loadUser); }
	BOOL LoadAssets();
	void LoadCreaturePack();
	void LoadGearPack();
	Fix<Rig, 192> rigs;
	Fix<Model, 192> models;
	Fix<Attach, 64> attaches;
	Fix<StaticSrc, 48> statics;
	Fix<GpuMesh, 160> m_creatures;
	// カタログは索引だけ。起動時に読む人型はパーティのもの。残りは作成・戦闘・図鑑でその時に読む。
	enum { MON_CATALOG = 1024 };
	int m_monN;
	int m_monOff[MON_CATALOG];
	int m_monModel[MON_CATALOG];
	unsigned char m_monBody[MON_CATALOG];
	int m_bodySlot[MON_CATALOG];   // 人型パック番号 → 予約した枠。それ以外は -1
	int m_body0M, m_body0R, m_body0C, m_bodyN;
	int m_keepModels, m_keepRigs, m_keepCreatures;
	volatile long m_gearShown, m_avaReady;
	void EnsureBody(const char* name);
	const BYTE* m_monBytes;
	DWORD m_monBytesN;
	void LoadBattleCreatures(const int* pack, int n);
	Fix<GpuMesh, 256> m_gear;
	Fix<BYTE, 256> m_gearGrip;   // GRIP_*。名前から読込時に決める
	struct CSkinV { float p[3], n[3]; DWORD c; unsigned char j[4], w[4]; };
	struct CreatureSkin {
		int model; Buf<CSkinV> bind; Buf<BYTE> cloth;
		int faceSet, faceStart, faceN; float faceLin[9];   // 顔の型を載せる頭の頂点範囲
		int faceSynth;                                   // 1 = デジット頭。顔の型は目印から作る
		float faceEye[2][3], faceNose[3], faceChin[3], faceHalf, faceCx;
		Buf<WORD> headTri;   // このメッシュ自身の頭と髪。アバターは素体の頭を隠してこれを載せる
		Buf<BYTE> meshHair;  // 1 = 焼き込みの髪。髪型を選んだときはこの頂点を含む面を描かない
		ID3D11Buffer* baldIB = NULL;
		UINT baldN = 0;
		float extX, extY, extZ, extR;   // バインド頂点の中心と半径
		ID3D11Buffer* bindSB = NULL;
		ID3D11ShaderResourceView* bindSRV = NULL;
		ID3D11Buffer* outVB = NULL;
		ID3D11UnorderedAccessView* outUAV = NULL;
		ID3D11Buffer* faceSB = NULL;
		ID3D11ShaderResourceView* faceSRV = NULL;
		int faceGot = -1;
		int gpuNv = 0;
		float bustLag[3] = {};
		float bustVel[3] = {};
		float bustPrev[3] = {};
		int bustPrevOk = 0;
		unsigned bustFrame = 0;
		float crown[3]; // 頭ボーン頂点の天辺。読み込み時に一度測り、描画中はバインドを読み直さない
		int crownOk;
		// 頭蓋の外形。中心 skullC から方位 16 × 仰角 8 の向きごとの最大半径。髪のカードをこの外へ出す。
		float skullC[3];
		float skullR[SKULL_NA * SKULL_NE];
		CreatureSkin() : model(-1), faceSet(-1), faceStart(0), faceN(0), faceSynth(0), faceHalf(0), faceCx(0), extX(0), extY(0), extZ(0), extR(0), crownOk(0) {
			crown[0] = crown[1] = crown[2] = 0;
			skullC[0] = skullC[1] = skullC[2] = 0;
			memset(skullR, 0, sizeof(skullR));
		}
	};
	Fix<CreatureSkin, 160> m_skins;
	// 顔の型（性別ごと）。頭の頂点ごとの位置差・法線・色
	struct FaceSet { int nFace, nv; Buf<short> d; Buf<signed char> n; Buf<DWORD> c; FaceSet() : nFace(0), nv(0) {} };
	FaceSet m_faces[2];
	void LoadFacePack();
	void BindFaceSynth(CreatureSkin& s, int first, int count);
	void UpdateFaceSynth(CreatureSkin& src, int face);
	int FaceCount(int gender) const { return (gender >= 0 && gender < 2 && m_faces[gender].nFace > 0) ? m_faces[gender].nFace : 1; }
	// アバター（人間の体と衣装を丸ごと差し替える）。頂点は共通の体の空間で持ち、j は区間番号。
	// モデルごとの区間行列で各人間の骨へ移し、初回の描画で頂点を作って保持する。
	struct AvaMesh { Buf<CSkinV> v; Buf<BYTE> cloth; Buf<float> dmax, dmin; };
	struct AvaBody { int nFace; AvaMesh m; Buf<WORD> bodyTri, extraTri; AvaBody() : nFace(0) {} };
	struct AvaCos { char name[32]; int gender; AvaMesh m; Buf<WORD> tri; Buf<BYTE> keep; const BYTE* pending; UINT pendingN; };
	struct AvaModel { char name[32]; int gender; Buf<float> M; char joint[64][24]; int nJoint; AvaModel() : gender(0), nJoint(0) { name[0] = 0; memset(joint, 0, sizeof(joint)); } };
	// 衣装の布を押し出す脚の形。太もも・すね・足の左右 6 本。骨に沿って CLOTH_PT 段・まわり CLOTH_PF 分割の半径。
	// スカートは腰を囲む格子（周 CLOTH_SECT × 縦およそ 5cm）。点は CLOTH_NODES まで（シェーダの NMAX）。
	// 胸と袖は別の SOFT_MAX 点。髪の案内は髪型ごとに HAIR_G 点。
	enum { CLOTH_SEGS = 6, CLOTH_PT = 10, CLOTH_PF = 16, CLOTH_INST = 48, CLOTH_SECT = 32, CLOTH_NODES = 512, SOFT_MAX = 96, HAIR_A = 24, HAIR_L = 8, HAIR_G = 192, HAIR_INST = 48 };
	// cup は胸の大きさ（0=A〜7=H）。形は作るときに焼き、jig は焼いた差分（胸の揺れの重み）。
	struct AvaCache {
		int model, cos, cup; CreatureSkin skin; Buf<float> jig; GpuMesh gm;
		// 衣装の背中の外面。髪をこの Z より後ろへ出す。
		float backY0, backDy, backZ[32];
		int backN;
		// 布（omake_tactics_cloth.hlsl）。[0] は格子の点 ClothN、[1] は動かす頂点 ClothV、[2] は脚の形の半径。clothN=0 は布を動かさない。
		// clothAxis はスカートの中心軸（バインドの x, z）。脚が布に入ったら、この軸から外へ押し出す。
		ID3D11Buffer* clothSB[3]; ID3D11ShaderResourceView* clothSRV[3]; int clothN, clothVN, clothHip; float clothAxis[2];
		// 胸と袖。softN=0 はタイト（体に沿ったまま）。softBone は 0 胸、1 上腕左、2 前腕左、3 上腕右、4 前腕右。
		ID3D11Buffer* softSB[2]; ID3D11ShaderResourceView* softSRV[2]; int softN, softVN;
		int softBone[6];
		int armJ[4][2];
		float bustA[2][4];
		float armA[4][4];
		float armB[4][4];
		// 脚の形の骨。segJ は [根元の骨, 先の骨]、segG は [0]=根元（xyz と長さ） [1]=骨の向き [2]=前 [3]=横（バインド）。
		// 0・1 は太もも左右（座ったかどうかもこれで見る）。4・5 は足。
		int segN; int segJ[CLOTH_SEGS][2]; float segG[CLOTH_SEGS][4][4];
		AvaCache() : model(0), cos(0), cup(0), backY0(0), backDy(0), backN(0), clothN(0), clothVN(0), clothHip(-1), softN(0), softVN(0), segN(0) {
			for (int i = 0; i < 32; i++) backZ[i] = 0;
			for (int b = 0; b < 3; b++) { clothSB[b] = NULL; clothSRV[b] = NULL; }
			softSB[0] = softSB[1] = NULL; softSRV[0] = softSRV[1] = NULL;
			clothAxis[0] = clothAxis[1] = 0.f;
			for (int i = 0; i < 6; i++) softBone[i] = -1;
			for (int i = 0; i < 4; i++) armJ[i][0] = armJ[i][1] = -1;
			memset(bustA, 0, sizeof(bustA));
			memset(armA, 0, sizeof(armA));
			memset(armB, 0, sizeof(armB));
			for (int i = 0; i < 4; i++) armB[i][3] = -1.f;
			memset(segJ, 0, sizeof(segJ)); memset(segG, 0, sizeof(segG));
		}
	};
	char m_avaSeg[64][24];
	int m_avaSegN;
	AvaBody m_avaBody[2];
	Fix<AvaCos, 256> m_avaCos;
	Fix<AvaModel, 64> m_avaModels;
	Fix<AvaCache, 128> m_avaCache;
	void LoadAvatarPack();
	void ReleaseAvatarCache();
	int AvatarCount(int gender) const;
	// avatar は性別ごとに 1 から。作れないときは FALSE（呼び手が元の体を描く）。
	// cup は 0=A〜BUST_N-1=H。owner は布の揺れを持つキャラ（NULL なら体に沿わせたまま）。
	BOOL DrawAvatar(int model, int avatar, int cup, const Pose& pose, const M4& world, const DrawParams& dp, const void* owner = NULL);
	// 着ている衣装の、その高さでの背中の外面（モデル空間の +Z）。無ければ -1e9。
	float AvatarClothBack(int model, int avatar, float y);
	BOOL DrawHumanHair(int model, int style, int headJ, const Pose& pose, const M4& world, const DrawParams& dp, int avatar, const void* owner = NULL);
	void EnsureScalp(int model, int headJ);
	void LoadHairPack();
	void ReleaseHair();
	// jig は頂点ごとの胸の揺れの重み（焼いた差分）。amt は揺れの倍率。cloth を渡すと、その衣装の布を owner の状態で動かす。
	void SkinAndDraw(CreatureSkin& src, GpuMesh& base, const float* jig, float amt, const Pose& pose, const M4& world, const DrawParams& dp,
		AvaCache* cloth = NULL, const void* owner = NULL);
	int FindModel(const char* n) const;
	int FindAttach(const char* n) const;
	int FindStatic(const char* n) const;
	int CreatureCount() const { return (int)m_creatures.size(); }
	BOOL CreatureExtent(int model, float& cx, float& cy, float& cz, float& rad) const;
	// 今のポーズでスキンしたメッシュの中心と半径（モデル空間）。肖像の枠はこれをワールドへ変換して使う。
	BOOL CreaturePosedExtent(int model, const Pose& pose, float& cx, float& cy, float& cz, float& rad) const;
	int CreatureModel(int i) const {
		if (m_monN > 0) return (i >= 0 && i < m_monN) ? m_monModel[i] : -1;
		return (i >= 0 && i < (int)m_skins.size()) ? m_skins[i].model : -1;
	}
	void DrawSkinnedCreature(int i, const Pose& pose, const M4& world, const DrawParams& dp);
	int GearCount() const { return (int)m_gearShown; }
	BOOL GearIsBow(int i) const { return i >= 0 && i < (int)m_gearGrip.size() && m_gearGrip[i] == GRIP_BOW; }
	int GearGrip(int i) const { return i >= 0 && i < (int)m_gearGrip.size() ? m_gearGrip[i] : GRIP_NONE; }
	void DrawCreature(int i, const M4& world, const DrawParams& dp);
	void DrawGear(int i, const M4& world, const DrawParams& dp);
	int AddProcModel(const Model& m, const Buf<VtxK>& v, const Buf<WORD>& idx);
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
	void DrawRibbon(const Buf<VtxP>& v);

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
	void HudGlass(float x, float y, float w, float h, DWORD fill, DWORD border, float alpha = 1.f);
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
	BOOL ApplyPost();
	BOOL CreateProcTextures();
	BOOL LoadPngTex(const BYTE* data, UINT len, int slot);
	TextSprite* GetText(const wchar_t* s, float px, int bold, int shadow, int box, float boxW, float boxH);
	void FlushHud();
	void UpdateFrameCB();
	void SetObj(const M4& world, const DrawParams& dp, float outlineW);
	void BindForPass(BOOL skinned, BOOL outline);
	void ClearTess();
	void ReleaseSkinGpu(CreatureSkin& src);
	BOOL EnsureSkinGpu(CreatureSkin& src, const float* jig);
	void UpdateFaceGpu(CreatureSkin& src, int face);
	BOOL GpuSkinAndDraw(CreatureSkin& src, const float* jig, float amt, const Pose& pose, const M4& world, const DrawParams& dp, GpuMesh& base,
		AvaCache* cloth, const void* owner);
	// 布の状態はキャラごと。同じ衣装の人が並んでも、影と本描画で二度描いても、1 フレームに 1 歩だけ進める。
	struct ClothInst {
		const void* owner; int model, cos, cup, n;
		ID3D11Buffer* sb; ID3D11UnorderedAccessView* uav;
		ID3D11Buffer* softSb; ID3D11UnorderedAccessView* softUav;
		DWORD frame; float time; M4 world; int valid;
		float limbPrev[CLOTH_SEGS][3];
		float handPrev[2][3];
		int limbOk;
		ClothInst() : owner(NULL), model(-1), cos(-1), cup(-1), n(0), sb(NULL), uav(NULL), softSb(NULL), softUav(NULL), frame(0), time(0), valid(0), limbOk(0) {
			world = MIdent();
			memset(limbPrev, 0, sizeof(limbPrev));
			memset(handPrev, 0, sizeof(handPrev));
		}
	};
	Fix<ClothInst, CLOTH_INST> m_clothInst;
	void RunCloth(AvaCache& ac, const void* owner, CreatureSkin& src, const Pose& pose, const M4& world, const DrawParams& dp);
	void ReleaseClothInst();
	void HudPush(float x, float y, float w, float h, float u0, float v0, float u1, float v1, DWORD c0, DWORD c1, ID3D11ShaderResourceView* srv);

	HWND m_hwnd;
	HANDLE m_wait;
	BOOL m_lost;
	BOOL m_presented;
	DWORD m_waitStuck;   // 退役待ちが始まった時刻。0 は待ちなし
	DWORD m_lastRebind;  // 未合成のまま付け直した時刻
	int m_w, m_h;
	int m_quality;
	BOOL m_allowTess;
	int m_msaa;
	HRESULT m_lastHr;
	int m_failStage;
	BOOL m_assetsOk;
	TacLoadPulseFn m_loadFn;
	void* m_loadUser;
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
	ID3D11Texture2D* m_sceneTex;
	ID3D11RenderTargetView* m_sceneRtv;
	ID3D11ShaderResourceView* m_sceneSrv;
	ID3D11Texture2D* m_blA;
	ID3D11RenderTargetView* m_blARtv;
	ID3D11ShaderResourceView* m_blASrv;
	ID3D11Texture2D* m_blB;
	ID3D11RenderTargetView* m_blBRtv;
	ID3D11ShaderResourceView* m_blBSrv;
	ID3D11Texture2D* m_zTex;
	ID3D11RenderTargetView* m_zRtv;
	ID3D11Texture2D* m_zRes;
	ID3D11ShaderResourceView* m_zSrv;
	ID3D11Texture2D* m_dofTex;
	ID3D11RenderTargetView* m_dofRtv;
	ID3D11ShaderResourceView* m_dofSrv;
	int m_blW, m_blH;
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
	ID3D11PixelShader* m_psBloom;
	ID3D11PixelShader* m_psBlur;
	ID3D11PixelShader* m_psGrade;
	ID3D11PixelShader* m_psGradeTiny;
	ID3D11PixelShader* m_psDofH;
	ID3D11PixelShader* m_psDofU;
	ID3D11VertexShader* m_vsT;
	ID3D11HullShader* m_hsT;
	ID3D11DomainShader* m_dsT;
	ID3D11PixelShader* m_psUi;
	ID3D11PixelShader* m_psGround[8];
	ID3D11PixelShader* m_psProp;
	ID3D11PixelShader* m_psWater;
	ID3D11PixelShader* m_psLava;
	ID3D11PixelShader* m_psOverride;
	ID3D11Texture2D* m_groundTex;
	ID3D11ShaderResourceView* m_groundSrv;
	ID3D11Texture2D* m_matTex;
	ID3D11ShaderResourceView* m_matSrv;
	ID3D11ComputeShader* m_csSkin;
	ID3D11ComputeShader* m_csCloth[3];   // 格子を進める・辺を保って押し出す・頂点へ書く
	ID3D11ComputeShader* m_csSoft[2];    // 胸と袖を進める・頂点へ書く
	ID3D11Buffer* m_cbCloth;
	ID3D11Buffer* m_cbSoft;
	ID3D11Buffer* m_cbSkinBone;
	ID3D11Buffer* m_cbSkinParm;
	ID3D11Buffer* m_cbHairCloth;
	ID3D11Buffer* m_faceDummy;
	ID3D11ShaderResourceView* m_faceDummySRV;
	ID3D11Buffer* m_hairSB;
	ID3D11ShaderResourceView* m_hairBind;
	ID3D11Buffer* m_hairVB;
	ID3D11UnorderedAccessView* m_hairUAV;
	ID3D11Texture2D* m_hairTex;
	ID3D11ShaderResourceView* m_hairSrv;
	GpuMesh m_hairDraw;
	UINT m_hairNv;
	float m_hairCap[16][3];
	int m_hairCapN[16];
	ID3D11ComputeShader* m_csHair[2];
	ID3D11Buffer* m_cbHairSim;
	ID3D11Buffer* m_hairGuideSB;
	ID3D11ShaderResourceView* m_hairGuide;
	ID3D11Buffer* m_hairVertSB;
	ID3D11ShaderResourceView* m_hairVert;
	int m_hairV0[16], m_hairVN[16];
	struct HairInst {
		const void* owner; int style;
		ID3D11Buffer* sb; ID3D11UnorderedAccessView* uav; ID3D11ShaderResourceView* srv;
		DWORD frame; float time; M4 world; int valid;
		HairInst() : owner(NULL), style(-1), sb(NULL), uav(NULL), srv(NULL), frame(0), time(0), valid(0) { world = MIdent(); }
	};
	Fix<HairInst, HAIR_INST> m_hairInst;
	ID3D11Buffer* m_hairScratchSB;
	ID3D11UnorderedAccessView* m_hairScratchUAV;
	ID3D11ShaderResourceView* m_hairScratchSRV;
	void RunHair(int model, int style, int headJ, const Pose& pose, const M4& world, const void* owner, const M4& place, const M4& pal);
	void ReleaseHairInst();
	int m_scalpModel; // m_capDraw を作った体
	DWORD m_capC;     // 笠の頂点色。髪テクスチャの平均の明るさを根元の影で 8 割にした灰色
	Buf<VtxS> m_capBind;
	GpuMesh m_capDraw;
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
	struct PropBatch { GpuMesh mesh; float hue; float mat; };
	Fix<PropBatch, 16> m_props;
	DWORD m_waterColor;
	int m_theme;

	// フレーム定数
	struct FrameCB {
		M4 vp, lvp;
		float eye[4], light[4], fog[4], fogColor[4], screen[4], skyAmb[4], gndAmb[4], q[4];
	} m_fcb;
	M4 m_lightVP;

	// HUD
	Fix<VtxH, 24576> m_hud;
	ID3D11ShaderResourceView* m_hudSrv;
	enum { TAC_TEXTN = 512, TAC_TEXTK = 192 };
	struct TextSlot { wchar_t key[TAC_TEXTK]; TextSprite sp; int live; };
	TextSlot m_text[TAC_TEXTN];
	BOOL m_inPortrait;
	int m_portraitSlot;
};

} // namespace otac
