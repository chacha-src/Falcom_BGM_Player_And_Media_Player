#pragma once
#include "afxwin.h"
#include "CCustomControl.h"
#include "resource.h"
#include <d3d11.h>
#include <dxgi.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;
struct ID3D11VertexShader;
struct ID3D11HullShader;
struct ID3D11DomainShader;
struct ID3D11PixelShader;
struct ID3D11ComputeShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11SamplerState;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11BlendState;

class CS3rView : public CCustomStatic
{
	DECLARE_DYNAMIC(CS3rView)
public:
	CS3rView();
	virtual ~CS3rView();
	void RequestRedraw() {}
	BOOL InitDx();
	void ReleaseDx();
	void ClearTerrMesh();
	void ClearStaticMeshes();
	BOOL UploadTerrMesh(const void* verts, UINT nVerts);
	BOOL UploadDefaultVB(ID3D11Buffer** dst, UINT* nOut, const void* verts, UINT nVerts);
	BOOL UploadDefaultIB(ID3D11Buffer** dst, UINT* nOut, const UINT* idx, UINT nIdx);
	BOOL ResizeDx(int w, int h);
	int m_dxFailStage; // InitDx 失敗段階（デバッグ用）
	HRESULT m_dxFailHr;
	BOOL EnsureSceneTargets(int w, int h);
	void PresentFrame();
	BOOL m_ready;
	int m_vw, m_vh;

	ID3D11Device* m_dev;
	ID3D11DeviceContext* m_imm;
	IDXGISwapChain* m_swap;
	ID3D11RenderTargetView* m_bbRtv;
	ID3D11Texture2D* m_dsTex;
	ID3D11DepthStencilView* m_dsv;
	ID3D11ShaderResourceView* m_dsSrv;
	ID3D11Texture2D* m_sceneTex;
	ID3D11RenderTargetView* m_sceneRtv;
	ID3D11ShaderResourceView* m_sceneSrv;
	ID3D11Texture2D* m_postTex;
	ID3D11RenderTargetView* m_postRtv;
	ID3D11ShaderResourceView* m_postSrv;
	ID3D11Texture2D* m_shadowTex;
	ID3D11DepthStencilView* m_shadowDsv;
	ID3D11ShaderResourceView* m_shadowSrv;
	enum { S3R_SHADOW_SIZE = 1024 };

	ID3D11VertexShader* m_vsTess;
	ID3D11HullShader* m_hsTess;
	ID3D11DomainShader* m_dsTess;
	ID3D11PixelShader* m_psBand;
	ID3D11VertexShader* m_vsSolid;
	ID3D11VertexShader* m_vsInst; // 機体・障害の GPU インスタンス
	ID3D11PixelShader* m_psSolid;
	ID3D11PixelShader* m_psTerr; // 地形専用（傾斜・河川・テーマ着色）
	ID3D11PixelShader* m_psCraft; // 機体専用（キャラテクスチャ）
	ID3D11VertexShader* m_vsHud;
	ID3D11PixelShader* m_psHud;
	ID3D11PixelShader* m_psHudLine;
	ID3D11VertexShader* m_vsPost;
	ID3D11PixelShader* m_psSsr;
	ID3D11PixelShader* m_psDof;
	ID3D11PixelShader* m_psFinal;
	ID3D11ComputeShader* m_csNoise;
	ID3D11InputLayout* m_ilPatch;
	ID3D11InputLayout* m_ilSolid;
	ID3D11InputLayout* m_ilInst;
	ID3D11InputLayout* m_ilHud;

	ID3D11Buffer* m_cbFrame;
	ID3D11Buffer* m_vbDyn;
	ID3D11Buffer* m_vbTerr; // 地形はコース生成時に一度だけ。毎フレーム組まない
	ID3D11Buffer* m_vbBand;
	ID3D11Buffer* m_vbWater;
	ID3D11Buffer* m_vbScenery; // ゲートなど少量の静的 TRIANGLELIST
	ID3D11Buffer* m_vbObs;
	ID3D11Buffer* m_ibObs;
	ID3D11Buffer* m_vbObsInst;
	ID3D11Buffer* m_vbCraft;
	ID3D11Buffer* m_ibCraft;
	ID3D11Buffer* m_vbCraftInst; // DYNAMIC、毎フレーム 12 機分だけ
	ID3D11Buffer* m_vbHud;
	UINT m_vbDynBytes;
	UINT m_vbHudBytes;
	UINT m_vbTerrN;
	UINT m_vbBandN;
	UINT m_vbWaterN;
	UINT m_vbSceneryN;
	UINT m_obsNvGpu, m_obsNiGpu, m_obsInstN;
	UINT m_craftNvGpu, m_craftNiGpu;
	BYTE* m_cpuDynScratch;
	UINT m_cpuDynScratchBytes;
	BYTE* m_cpuHudScratch;
	UINT m_cpuHudScratchBytes;

	enum { S3R_THEME_N = 8 };
	ID3D11Texture2D* m_texTheme[S3R_THEME_N];
	ID3D11ShaderResourceView* m_srvTheme[S3R_THEME_N];
	ID3D11Texture2D* m_texThemeD[S3R_THEME_N];
	ID3D11ShaderResourceView* m_srvThemeD[S3R_THEME_N];
	ID3D11Texture2D* m_texCraft;
	ID3D11ShaderResourceView* m_srvCraft;
	ID3D11Texture2D* m_texCraftD;
	ID3D11ShaderResourceView* m_srvCraftD;
	ID3D11Texture2D* m_texBand;
	ID3D11ShaderResourceView* m_srvBand;
	ID3D11Texture2D* m_texWater;
	ID3D11ShaderResourceView* m_srvWater;
	ID3D11Texture2D* m_texObs;
	ID3D11ShaderResourceView* m_srvObs;
	ID3D11Texture2D* m_texEnv;
	ID3D11ShaderResourceView* m_srvEnv;
	ID3D11Texture2D* m_texEnv2;
	ID3D11ShaderResourceView* m_srvEnv2;
	ID3D11Texture2D* m_texSky;
	ID3D11ShaderResourceView* m_srvSky;
	ID3D11Texture2D* m_texSky2;
	ID3D11ShaderResourceView* m_srvSky2;
	ID3D11Texture2D* m_texItem;
	ID3D11ShaderResourceView* m_srvItem;
	ID3D11Texture2D* m_texWood;
	ID3D11ShaderResourceView* m_srvWood;
	ID3D11Texture2D* m_texNoise;
	ID3D11ShaderResourceView* m_srvNoise;
	ID3D11UnorderedAccessView* m_uavNoise;
	ID3D11Texture2D* m_texClear;
	ID3D11ShaderResourceView* m_srvClear;
	int m_clearTexW, m_clearTexH;
	ID3D11Texture2D* m_texHud;
	ID3D11ShaderResourceView* m_srvHud;
	int m_hudTexW, m_hudTexH;
	ID3D11Texture2D* m_texStand;
	ID3D11ShaderResourceView* m_srvStand;
	int m_standTexW, m_standTexH;
	ID3D11Texture2D* m_texBubble;
	ID3D11ShaderResourceView* m_srvBubble;
	int m_bubbleTexW, m_bubbleTexH;
	enum { S3R_BUBBLE_MAX = 12 };
	float m_bubbleU0[S3R_BUBBLE_MAX], m_bubbleV0[S3R_BUBBLE_MAX];
	float m_bubbleU1[S3R_BUBBLE_MAX], m_bubbleV1[S3R_BUBBLE_MAX];
	int m_bubbleN;

	ID3D11SamplerState* m_sampLin;
	ID3D11SamplerState* m_sampPoint;
	ID3D11SamplerState* m_sampCmp;
	ID3D11RasterizerState* m_rsSolid;
	ID3D11RasterizerState* m_rsShadow;
	ID3D11RasterizerState* m_rsNoCull;
	ID3D11DepthStencilState* m_dssWrite;
	ID3D11DepthStencilState* m_dssRead;
	ID3D11DepthStencilState* m_dssOff;
	ID3D11BlendState* m_bsOpaque;
	ID3D11BlendState* m_bsAlpha;
	ID3D11BlendState* m_bsAdd;

	BOOL CreateShaders();
	BOOL CreateProcTextures();
	BOOL BakeNoiseCS();
	BOOL BakeClearTexture(const wchar_t* text, float alpha);
	void ReleaseClearTexture();
	BOOL BakeHudTexture(const wchar_t* text);
	void ReleaseHudTexture();
	struct S3rStandRow {
		wchar_t name[16];
		int rank;
		int isPlayer;
		float cr, cg, cb;
		int lapShowN;
		int lapNo[4];
		float lapSec[4];
		int retired;
	};
	BOOL BakeStandingsTexture(const S3rStandRow* rows, int nRows);
	void ReleaseStandingsTexture();
	struct S3rBubbleRow {
		wchar_t text[40];
		int isPlayer;
	};
	BOOL BakeBubbleTexture(const S3rBubbleRow* rows, int nRows);
	void ReleaseBubbleTexture();

protected:
	afx_msg void OnPaint();
	afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT, int, int);
	afx_msg void OnDestroy();
	afx_msg void OnContextMenu(CWnd*, CPoint);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMButtonUp(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	DECLARE_MESSAGE_MAP()
};

class CSoft3DRaceDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CSoft3DRaceDlg)
public:
	CSoft3DRaceDlg(CWnd* p = NULL);
	virtual ~CSoft3DRaceDlg();
	enum { IDD = IDD_SOFT3DRACE };
	enum {
		S3R_MAX_CRAFT = 12,
		S3R_MAX_OBS = 640,
		S3R_MAX_ITEMS = 64,
		S3R_SPLINE_MAX = 96,
		S3R_PATH_SAMPLES = 1024,
		S3R_CRAFT_VMAX = 4800,
		S3R_CRAFT_IMAX = 12000,
		S3R_OBS_VMAX = 6400,
		S3R_OBS_IMAX = 19200,
		S3R_HM_N = 161,
		S3R_CARVE_MAX = 128,
		ITEM_TEMPO = 1,
		ITEM_PITCH_UP = 2,
		ITEM_PITCH_DN = 4,
		ITEM_NEXT = 8,
		ITEM_EQ = 16,
		ITEM_WINDOW = 32,
		ITEM_TEMPO_DN = 64,
		ITEM_PREV = 128,
		ITEM_VOL_UP = 256,
		ITEM_VOL_DN = 512,
		ITEM_REVERB = 1024,
		ITEM_XFADE = 2048,
		ITEM_EQ_FLAT = 4096,
		ITEM_RANDOM = 8192,
		ITEM_ALL = 16383,
		KIND_TEMPO = 1,
		KIND_TEMPO_DN = 2,
		KIND_PITCH_UP = 3,
		KIND_PITCH_DN = 4,
		KIND_NEXT = 5,
		KIND_PREV = 6,
		KIND_VOL_UP = 7,
		KIND_VOL_DN = 8,
		KIND_EQ = 9,
		KIND_EQ_FLAT = 10,
		KIND_REVERB = 11,
		KIND_XFADE = 12,
		KIND_RANDOM = 13,
		PHASE_IDLE = 0,
		PHASE_COUNTDOWN = 1,
		PHASE_RACE = 2,
		PHASE_FINISH = 3,
		PHASE_PODIUM = 4,
		PHASE_DEMO = 5, // 生成〜スタート前：全体俯瞰＋全機AIデモ
		AI_SUPER_EASY = 0, // 超簡単（旧・普通相当のゆるさ）
		AI_EASY = 1,
		AI_NORMAL = 2,
		AI_HARD = 3,
		AI_FEROCIOUS = 4, // 強烈
		AI_COUNT = 5,
		LEN_AUTO = 0,
		LEN_SHORT = 1,
		LEN_NORMAL = 2,
		LEN_LONG = 3,
		THEME_AUTO = 0,
		THEME_FOREST = 1,
		THEME_RUINS = 2,
		THEME_OIL = 3,
		THEME_NIGHT = 4,
		THEME_UNDER = 5,
		THEME_GRASS = 6,
		THEME_MESA = 7,
		THEME_CLOUD = 8,
		THEME_COUNT = 8
	};

protected:
	virtual void DoDataExchange(CDataExchange*);
	virtual BOOL PreTranslateMessage(MSG*);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()
	void LayoutHelpBtn();
	void LayoutAll();
	void PumpQueued(BOOL input);
	void PersistUi();
	void PersistWindowRect();
	void ApplySavedWindowRect();
	CFont m_uiFont;

	int ReadAiFromUi();
	void SetAiToUi(int v);
	int ReadOppFromUi();
	void SetOppToUi(int v);
	int ReadLenFromUi();
	void SetLenToUi(int v);
	int ReadLapsFromUi();
	void SetLapsToUi(int v);
	int ReadThemeFromUi();
	void SetThemeToUi(int v);
	int ReadInvertFromUi();
	void SetInvertToUi(int v);

	void GenerateCourse();
	void GenerateCourseWithSeed(DWORD seed);
	void BuildCraftMeshes();
	void BuildObstacleMesh(int theme);
	void PlaceObstaclesAndItems();
	void ResetRaceState();
	void BeginDemoPreview();
	void StartRace();
	void TickPhysics(float dt);
	void TickAi(float dt);
	void TickItems(float dt);
	void TickCountdown(float dt);
	void TickPodium(float dt);
	void TickDemo(float dt);
	BOOL AllAliveFinished() const;
	void EnterPodium();
	float AiPaceIndep(float sk) const;
	void AiCapPair(float sk, float raceCap, float pace, float plNow, float indep, int demo, int finishRush, float& softFloor, float& hardCap) const;
	void UpdateRanks();
	void ApplyItem(int kind);
	void TryPickupCraft(int ci);
	void CaptureAudioBaseline();
	void RestoreAudioBaseline();
	void UpdateStatus();
	void ShowHelpSheet();
	void ShowContextMenu(CPoint screenPt);
	void BakeStaticMeshes();
	void RenderScene();
	void EnsureHudBake();
	void EnsureStandingsBake();

	void SplinePoint(float t, float& x, float& y, float& z) const;
	void SplineTangent(float t, float& x, float& y, float& z) const;
	void SplineFrame(float t, float& px, float& py, float& pz, float& tx, float& ty, float& tz, float& nx, float& ny, float& nz, float& bx, float& by, float& bz) const;
	float ClosestSplineT(float x, float y, float z, float hintT) const;
	// 速度に応じて pathT の前進を制限（円周コースでの誤ラップ防止）
	float AdvancePathT(float x, float y, float z, float prevT, float spd, float dt) const;
	float DistToBand(float x, float y, float z, float t, float& outCx, float& outCy, float& outCz) const;
	// 帯ローカル座標（lat=binormal, vert=normal）。見た目のリボン判定用
	void BandLocal(float x, float y, float z, float t, float& lat, float& vert, float& cx, float& cy, float& cz) const;
	float BandHalfWidth() const;
	float SpeedScale() const;
	float RaceSpeedCap(int boosted=0) const; // 物理上限（表示は SpeedToKmh で×20）
	float SpeedToKmh(float vx, float vy, float vz) const;
	float BandSpeedFactor(float lat, float vert) const; // 帯横軸中央=最大、上下オフセットで減速
	float PathArcBetween(float t0, float t1) const;
	float GroundY(float x, float z) const;
	float EffectiveLaps() const;
	float CourseScale() const;
	int EffectiveTheme() const;
	int ItemMask() const;

public:
	BOOL HandleAccelMessage(MSG* pMsg);
	void TickFrame();
	void InputAccel(BOOL on) { m_accelHeld = on ? 1 : 0; }
	void InputBrake(BOOL on) { m_brakeHeld = on ? 1 : 0; }
	void InputLookback(BOOL on) { m_lookback = on ? 1 : 0; }
	void InputSteerDelta(float dyaw, float dpitch); // マウス: 自機の向き
	void InputCameraDelta(float dyaw, float dpitch); // パッド右スティック等: 追従カメラオフセット
	void InputZoom(int dir);
	virtual BOOL OnInitDialog();
	virtual void OnOK() {}
	virtual void OnCancel() { DestroyWindow(); }
	afx_msg void OnStart();
	afx_msg void OnGen();
	afx_msg void OnCloseBtn();
	afx_msg void OnHelp();
	afx_msg void OnAiChanged();
	afx_msg void OnOppChanged();
	afx_msg void OnLenChanged();
	afx_msg void OnLapsChanged();
	afx_msg void OnThemeChanged();
	afx_msg void OnInvertChanged();
	afx_msg void OnTimer(UINT_PTR);
	afx_msg void OnSize(UINT, int, int);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnDestroy();
	afx_msg void OnContextMenu(CWnd*, CPoint);

	CCustomStandardButton m_help, m_start, m_gen, m_close;
	CCustomStatic m_aiL, m_oppL, m_lenL, m_lapsL, m_themeL, m_invertL, m_hint, m_status;
	CCustomComboBox m_ai, m_opp, m_len, m_laps, m_theme, m_invert;
	CS3rView m_view;
	CToolTipCtrl m_tooltip;

	struct S3rKnot { float x, y, z; };
	struct S3rCraft {
		float x, y, z;
		float yaw, pitch;
		float vx, vy, vz;
		float fuel, hp;
		float pathT;
		float lapProgress;
		int lap;
		int rank;
		int alive;
		int isPlayer;
		int colorIdx;
		float boostT, slowT, agilityT, fogT, dofT, flashT;
		float smokeT;
		float bestLap;
		float raceTime;
		float finishTime;
		int finished;
		int retired; // 1=高速シミュでも到達できずリタイア
		float aiSteerBias;
		float aiSkill;
		// 帯上の最終位置（コースアウト復帰地点）
		float chkX, chkY, chkZ;
		float chkYaw, chkPitch;
		float chkPathT;
		float offBandT;
		float courseOutCool;
		int offBand; // 1=帯外フリー走行中
		float aiCutT; // ショートカット合流目標 pathT（未使用時 -1）
		float aiCutTimer;
		float aiCutCool; // >0 の間は通常ライン走行（カット禁止・中央固定）
		wchar_t name[16];
		float lapTimes[12];
		int lapTimesN;
	};
	void RetireCraft(S3rCraft& c);
	void RespawnCraftToCheckpoint(S3rCraft& c, float fuelAmt, float cool);
	// ショートカット失敗／帯外危険時：帯中央へ戻しライン固定へ
	void AbortAiToLine(S3rCraft& c, float lineLockSec);
	void AlignCraftToPath(S3rCraft& c, float lookAhead=0.02f);
	struct S3rObs {
		float x, y, z;
		float yaw, pitch;
		float sx, sy, sz;
		int kind;
		float damage;
		float pathT;
		int hazard; // 0=景色のみ（衝突ダメージなし）
	};
	struct S3rItem {
		float x, y, z;
		int kind;
		int taken;
		float spin;
		float pathT;
	};
	struct S3rMesh {
		float* v; // xyz nxnynz uv rgba interleaved as float packs via S3RVertex in cpp
		UINT* idx;
		int nv, ni;
	};

	S3rKnot m_knots[S3R_SPLINE_MAX];
	int m_knotN;
	float m_pathLen;
	float m_pathSampleXYZ[S3R_PATH_SAMPLES][3];
	float m_pathSampleT[S3R_PATH_SAMPLES];
	float m_pathCumLen[S3R_PATH_SAMPLES];

	S3rCraft m_crafts[S3R_MAX_CRAFT];
	int m_craftN;
	S3rObs m_obs[S3R_MAX_OBS];
	int m_obsN;
	S3rItem m_items[S3R_MAX_ITEMS];
	int m_itemN;

	// Procedural craft base mesh (instance via color)
	float m_craftVert[S3R_CRAFT_VMAX * 12];
	UINT m_craftIdx[S3R_CRAFT_IMAX];
	int m_craftNv, m_craftNi;
	float m_obsVert[S3R_OBS_VMAX * 12];
	UINT m_obsIdx[S3R_OBS_IMAX];
	int m_obsNv, m_obsNi;

	int m_phase;
	float m_countT;
	int m_countShown;
	float m_podiumT;
	float m_finishSimT; // FINISH中の裏シミュレーション累計秒
	int m_aiRaceLv; // レース開始時のAI難易度（0..4）
	int m_podiumOrder[3];
	float m_confetti[96][6]; // x y z vx vy life
	int m_themeActive;
	int m_layoutKind; // 0楕円 1八の字 2スタジアム 3箱型凸 4腎臓凹 5ピーナッツ 6三角凸
	int m_lapsTarget;
	float m_bandHalf;
	float m_camYawOff, m_camPitchOff;
	float m_camZoom;
	float m_camSx, m_camSy, m_camSz; // 酔い対策：カメラ位置スムーズ
	float m_camAx, m_camAy, m_camAz; // 注視点スムーズ
	int m_camSmoothInit;
	int m_lookback;
	int m_accelHeld, m_brakeHeld;
	int m_mouseLook;
	CPoint m_lastMouse;
	DWORD m_lastTick;
	int m_inTick;
	DWORD m_rng;
	DWORD m_genSeed;
	DWORD m_spaceToggleTick;
	int m_baseTempoPos;
	int m_basePitchPos;
	float m_anim;
	float m_raceClock;
	float m_playerSpdEma; // 自機の実効速度（低難易度AIの基準）
	int m_playerAccel;
	int m_wrongWay;
	float m_overlayHold; // LAP/COURSE OUT 表示の優先保持
	float m_sfxHitCool;
	CStringW m_hudBakeText;
	CStringW m_clearBakeText;
	float m_clearBakeA;
	int m_hudDirty;
	int m_clearDirty;
	int m_standDirty;
	float m_reverbFogBoost;
	float m_eqDofBoost;
	float m_podiumBaseX, m_podiumBaseY, m_podiumBaseZ;
	float m_demoCamT; // 俯瞰オービット角
	float m_demoCamElev; // 俯瞰の高さバイアス
	float m_demoMidX, m_demoMidY, m_demoMidZ, m_demoRad;
	float m_hm[S3R_HM_N * S3R_HM_N];
	float m_hmRaw[S3R_HM_N * S3R_HM_N];
	float m_hmPathDist[S3R_HM_N * S3R_HM_N];
	float m_hmX0, m_hmZ0, m_hmStep;
	int m_hmReady;
	float m_waterY;
	float m_carveX0[S3R_CARVE_MAX], m_carveY0[S3R_CARVE_MAX], m_carveZ0[S3R_CARVE_MAX];
	float m_carveX1[S3R_CARVE_MAX], m_carveY1[S3R_CARVE_MAX], m_carveZ1[S3R_CARVE_MAX];
	int m_carveCeil[S3R_CARVE_MAX];
	int m_carveN;
};

void OpenSoft3DRaceModeless(CWnd*);
void CloseSoft3DRaceIfOpen();
BOOL IsSoft3DRaceActive();
BOOL IsSoft3DRaceOpen();
BOOL Soft3DRacePreTranslate(MSG* pMsg);
void Soft3DRaceOnTimerp();
