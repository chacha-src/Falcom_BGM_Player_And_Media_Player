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
struct ID3D11VertexShader;
struct ID3D11HullShader;
struct ID3D11DomainShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11Buffer;
struct ID3D11SamplerState;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11BlendState;

class CS3mView : public CCustomStatic
{
	DECLARE_DYNAMIC(CS3mView)
public:
	CS3mView();
	virtual ~CS3mView();
	void RequestRedraw() {}
	BOOL InitDx();
	void ReleaseDx();
	BOOL ResizeDx(int w, int h);
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
	enum { S3M_SHADOW_SIZE = 1024 };
	// 0=壁鏡 1=床鏡 2.. =窓・アイテム等（NDC軽量なので多めに持つ）
	enum { S3M_MIRROR_N = 20, S3M_MIRROR_SIZE = 384, S3M_MIRROR_FX0 = 2, S3M_MIRROR_FX_N = 18 };
	ID3D11Texture2D* m_mirrorTex[S3M_MIRROR_N];
	ID3D11RenderTargetView* m_mirrorRtv[S3M_MIRROR_N];
	ID3D11ShaderResourceView* m_mirrorSrv[S3M_MIRROR_N];
	ID3D11Texture2D* m_mirrorDs;
	ID3D11DepthStencilView* m_mirrorDsv;

	ID3D11VertexShader* m_vsTess;
	ID3D11HullShader* m_hsTess;
	ID3D11DomainShader* m_dsTess;
	ID3D11PixelShader* m_psWall;
	ID3D11VertexShader* m_vsSolid;
	ID3D11PixelShader* m_psSolid;
	ID3D11VertexShader* m_vsHud;
	ID3D11PixelShader* m_psHud;
	ID3D11PixelShader* m_psHudLine;
	ID3D11VertexShader* m_vsPost;
	ID3D11PixelShader* m_psSsr;
	ID3D11PixelShader* m_psDof;
	ID3D11PixelShader* m_psFinal;
	ID3D11InputLayout* m_ilPatch;
	ID3D11InputLayout* m_ilSolid;
	ID3D11InputLayout* m_ilHud;

	ID3D11Buffer* m_cbFrame;
	ID3D11Buffer* m_vbDyn;
	ID3D11Buffer* m_vbHud;
	UINT m_vbDynBytes;
	UINT m_vbHudBytes;
	// RenderScene 毎フレ new 回避（大マップで歩くほど遅くならないよう再利用）
	BYTE* m_cpuDynScratch;
	UINT m_cpuDynScratchBytes;
	BYTE* m_cpuHudScratch;
	UINT m_cpuHudScratchBytes;

	enum { S3M_WALL_VAR = 16, S3M_THEME_N = 4 };
	ID3D11Texture2D* m_texBrick[S3M_THEME_N];
	ID3D11ShaderResourceView* m_srvBrick[S3M_THEME_N];
	ID3D11Texture2D* m_texFloor[S3M_THEME_N];
	ID3D11ShaderResourceView* m_srvFloor[S3M_THEME_N];
	ID3D11Texture2D* m_texMirrorWall;
	ID3D11ShaderResourceView* m_srvMirrorWall;
	ID3D11Texture2D* m_texMirrorFloor;
	ID3D11ShaderResourceView* m_srvMirrorFloor;
	ID3D11Texture2D* m_texEnv;
	ID3D11ShaderResourceView* m_srvEnv;
	ID3D11Texture2D* m_texClear;
	ID3D11ShaderResourceView* m_srvClear;
	int m_clearTexW, m_clearTexH;
	enum { S3M_MAP_SIZE = 4096 };
	ID3D11Texture2D* m_texMap;
	ID3D11ShaderResourceView* m_srvMap;
	ID3D11Texture2D* m_texTip;
	ID3D11ShaderResourceView* m_srvTip;
	int m_tipW, m_tipH;
	ID3D11Texture2D* m_texBadge;
	ID3D11ShaderResourceView* m_srvBadge;
	int m_badgeW, m_badgeH;
	ID3D11Texture2D* m_texCallout;
	ID3D11ShaderResourceView* m_srvCallout;
	int m_calloutW, m_calloutH;
	enum { S3M_CALLOUT_MAX = 20 };
	float m_calloutU0[S3M_CALLOUT_MAX], m_calloutV0[S3M_CALLOUT_MAX];
	float m_calloutU1[S3M_CALLOUT_MAX], m_calloutV1[S3M_CALLOUT_MAX];
	int m_calloutN;

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
	BOOL BakeClearTexture(const wchar_t* text, float alpha);
	void ReleaseClearTexture();
	BOOL BakeTipTexture(const wchar_t* text);
	void ReleaseTipTexture();
	BOOL BakeBadgeTexture(const wchar_t* text);
	void ReleaseBadgeTexture();
	BOOL BakeFloorBarTexture(int nFloors, int viewFloor, int playerFloor, const wchar_t* const* labels);
	void ReleaseFloorBarTexture();
	BOOL BakeCalloutAtlas(const wchar_t* const* labels, int nLabels);
	void ReleaseCalloutTexture();
	ID3D11Texture2D* m_texFloorBar;
	ID3D11ShaderResourceView* m_srvFloorBar;
	int m_floorBarW, m_floorBarH;
	float m_floorBarTabX0[4];
	float m_floorBarTabX1[4];
	float m_floorBarRecX0, m_floorBarRecX1;

protected:
	afx_msg void OnPaint();
	afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
	afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
	afx_msg void OnSize(UINT, int, int);
	afx_msg void OnDestroy();
	afx_msg void OnContextMenu(CWnd*, CPoint);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	int HitMoveDir(CPoint pt) const;
	CPoint m_dragOrigin;
	int m_dragging;
	int m_dragTurnAcc;
	DECLARE_MESSAGE_MAP()
};

class CSoft3DMazeDlg : public CCustomBlurDialogBase
{
	DECLARE_DYNAMIC(CSoft3DMazeDlg)
public:
	CSoft3DMazeDlg(CWnd* p = NULL);
	virtual ~CSoft3DMazeDlg();
	enum { IDD = IDD_SOFT3DMAZE };
	enum {
		S3M_MAX = 3000,
		S3M_MIN = 10,
		S3M_MAX_FLOORS = 4,
		CELL_WALL = 1,
		CELL_FLOOR = 0,
		CELL_START = 2,
		CELL_GOAL = 3,
		CELL_TEMPO = 4,
		CELL_PITCH_UP = 5,
		CELL_PITCH_DN = 6,
		CELL_NEXT = 7,
		CELL_EQ = 8,
		CELL_WINDOW = 9,
		CELL_STAIRS_DOWN = 10,
		CELL_STAIRS_UP = 11,
		CELL_TEMPO_DN = 12,
		CELL_PREV = 13,
		CELL_VOL_UP = 14,
		CELL_VOL_DN = 15,
		CELL_REVERB = 16,
		CELL_XFADE = 17,
		CELL_EQ_FLAT = 18,
		CELL_RANDOM = 19,
		CELL_SLIME = 20,   // 粘液：移動遅延（半透明・消えず）
		CELL_SPIKE = 21,   // 棘：進入マスから1マス戻る（旧SPIN枠を流用）
		CELL_ICE = 22,     // 氷：進入方向へ1マス滑る
		CELL_DARK = 23,    // 闇：一時的に視界が狭まる
		CELL_MIRROR_WALL = 24,  // 鏡壁（通行不可・反射）
		CELL_MIRROR_FLOOR = 25, // 鏡床（通行可・反射）
		CELL_PORTAL = 26,       // 同階ワープ（対のポータルへ）
		CELL_KEY = 27,          // 鍵（拾得で所持）
		CELL_DOOR = 28,         // 扉（鍵が必要）
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
		CLEAR_IDLE = 0,
		CLEAR_TEXT_IN = 1,
		CLEAR_TEXT_HOLD = 2,
		CLEAR_TEXT_OUT = 3,
		CLEAR_FADE_OUT = 4,
		CLEAR_FADE_IN = 5,
		FLOORFX_IDLE = 0,
		FLOORFX_IN = 1,
		FLOORFX_HOLD = 2,
		FLOORFX_OUT = 3,
		PORTALFX_IDLE = 0,
		PORTALFX_OUT = 1,
		PORTALFX_IN = 2,
		DIFF_VERY_EASY = 0,
		DIFF_EASY = 1,
		DIFF_NORMAL = 2,
		DIFF_HARD = 3,
		DIFF_VERY_HARD = 4,
		DIFF_COUNT = 5
	};
	friend struct S3mCompactBfs;
protected:
	virtual void DoDataExchange(CDataExchange*);
	virtual BOOL PreTranslateMessage(MSG*);
	virtual void PostNcDestroy();
	DECLARE_MESSAGE_MAP()
	void LayoutHelpBtn();
	void LayoutAll();
	void PersistUi();
	void PersistWindowRect();
	void ApplySavedWindowRect();
	void PersistRun();
	BOOL LoadRun();
	CFont m_uiFont;
	void FreeGrid();
	BOOL AllocGrid(int n, int nFloors);
	void BindFloor(int f);
	BYTE& Cell(int x, int z) { return m_grids[m_floor][(size_t)z * (size_t)m_n + (size_t)x]; }
	BYTE CellAt(int x, int z) const { return m_grids[m_floor][(size_t)z * (size_t)m_n + (size_t)x]; }
	BYTE& Visit(int x, int z) { return m_visits[m_floor][(size_t)z * (size_t)m_n + (size_t)x]; }
	BYTE VisitAt(int x, int z) const { return m_visits[m_floor][(size_t)z * (size_t)m_n + (size_t)x]; }
	BYTE& CellF(int f, int x, int z) { return m_grids[f][(size_t)z * (size_t)m_n + (size_t)x]; }
	BYTE CellAtF(int f, int x, int z) const { return m_grids[f][(size_t)z * (size_t)m_n + (size_t)x]; }
	BYTE& VisitF(int f, int x, int z) { return m_visits[f][(size_t)z * (size_t)m_n + (size_t)x]; }
	BYTE VisitAtF(int f, int x, int z) const { return m_visits[f][(size_t)z * (size_t)m_n + (size_t)x]; }
	int ReadSizeFromUi();
	void SetSizeToUi(int n);
	int ReadBasementsFromUi();
	void SetBasementsToUi(int b);
	int ReadDifficultyFromUi();
	void SetDifficultyToUi(int d);
	void GenerateMaze();
	void GenerateMazeWithSeed(DWORD seed, int forceSize = -1);
	void GenerateOneFloor(int f);
	void PlaceStairsAndGoal();
	void AddAlternateRoutes();
	void RefreshGoalCache();
	int TargetGoalRoutes() const;
	void RecountExploreStats();
	void NoteVisitCell(int x, int z);
	BOOL MazeWalkable(int f, int x, int z) const;
	BOOL FindStairPartner(int f, int x, int z, int& outF, int& outX, int& outZ) const;
	BOOL StairsLayoutModern() const;
	int PickGoalByDifficulty(int sx, int sz, int& outX, int& outZ, int& outF);
	void RenderScene();
	void TickMove(float dt);
	void MarkVisited();
	void TryPickup();
	void ApplyItem(int kind);
	void TryTrapEnter();
	void RestoreAudioBaseline();
	void CaptureAudioBaseline();
	void UpdateStatus();
	void ShowHelpSheet();
	void ResetClearFx();
	void BeginClearSequence();
	void TickClear(float dt);
	void ResetFloorFx();
	void BeginFloorChange(int newFloor, int landX, int landZ);
	void TickFloorFx(float dt);
	void ResetPortalFx();
	void BeginPortalWarp(int toX, int toZ);
	void TickPortalFx(float dt);
	BOOL EnsureGoalReachable();
	void ClearNavPath();
	BOOL ComputeNavHint();
	void UpdateNavProgress();
	BOOL HasNavPath() const { return m_navCount > 1; }
	void RefreshFloorTex();
	CString FloorLabel(int f) const;
	void OverviewFloorDelta(int d);
	BOOL IsBlocked(float x, float z) const;
	BOOL IsBlockedF(int f, float x, float z) const;
	float AxisSpan(int i) const;
	float AxisOrigin(int i) const;
	BOOL DoorThinX(int f, int x, int z) const;
	void DoorWorldRect(int f, int x, int z, float pad, float& x0, float& z0, float& x1, float& z1) const;
	float GridToWorldX(float gx) const;
	float GridToWorldZ(float gz) const;
	int WorldToGridAxis(float w) const;
	void CamBasisYaw(float yaw, float& fwdX, float& fwdZ, float& rightX, float& rightZ) const;
	void GetRenderEye(float& ex, float& ez) const;
	float GetRenderEyeY() const;
	int ThemeOfFloor(int f) const;
	void WorldToCam(float wx, float wz, float& lx, float& lz) const;
	void WorldToMap(float wx, float wz, float& mx, float& my) const;
	BOOL TryTurn(int dir);
	BOOL TryStep(int mx, int mz);
	void RefreshClearTex();
public:
	BOOL IsOverviewActive() const { return m_mapToggle != 0; }
	void ToggleMapOverlay();
	void FocusOverviewOnPlayer();
	void OverviewFloorSet(int f);
	BOOL HitOverviewFloorUi(CPoint clientPt, int& outAction) const; // outAction: 0..n-1 floor, 100=recenter
	void LayoutOverviewFloorUi(int viewW, int viewH);
	void BeginOverviewUiPress(int action, CPoint pt) { m_ovPressAction = action; m_ovPressPt = pt; }
	BOOL FinishOverviewUiPress(CPoint pt);
	BOOL HasOverviewUiPress() const { return m_ovPressAction >= 0; }
	BOOL HandleAccelMessage(MSG* pMsg);
	BOOL BeginMapPan(CPoint clientPt);
	BOOL UpdateMapPan(CPoint clientPt);
	BOOL EndMapPan();
	BOOL IsMapPanning() const { return m_mapPanDrag != 0; }
	void ClampMapPan(int viewW, int viewH, float side);
	void TickFrame(); // timerp から呼ばれる 1フレーム更新＋描画
	BOOL InputTurn(int dir) { return IsOverviewActive() || m_floorFx == FLOORFX_IN || m_portalFx != PORTALFX_IDLE ? FALSE : TryTurn(dir); }
	BOOL InputStep(int mx, int mz) { return IsOverviewActive() || m_floorFx == FLOORFX_IN || m_portalFx != PORTALFX_IDLE ? FALSE : TryStep(mx, mz); }
	void InputOverviewFloorDelta(int d) { OverviewFloorDelta(d); }
	void InputFovZoom(int dir);
	void InputMapZoom(int dir);
	BOOL HitTestMinimap(CPoint clientPt) const;
	float EffectiveFovDeg() const;
	float MapZoomScale() const;
	float OverviewZoomScale() const;
	int OverviewZoomMaxPct() const;
	float OverviewBaseSide(int viewW, int viewH) const;
	virtual BOOL OnInitDialog();
	virtual void OnOK() {}
	virtual void OnCancel() { DestroyWindow(); }
	afx_msg void OnGen();
	afx_msg void OnNavi();
	afx_msg void OnCloseBtn();
	afx_msg void OnHelp();
	afx_msg void OnSizeChanged();
	afx_msg void OnSizeEditChange();
	afx_msg void OnBaseChanged();
	afx_msg void OnDiffChanged();
	afx_msg void OnTimer(UINT_PTR);
	afx_msg void OnSize(UINT, int, int);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnDestroy();
	afx_msg void OnContextMenu(CWnd*, CPoint);
	void ShowContextMenu(CPoint screenPt);

	CCustomStandardButton m_help, m_gen, m_navi, m_close;
	CCustomStatic m_sizeL, m_baseL, m_diffL, m_hint, m_status;
	CCustomComboBox m_size, m_base, m_diff;
	CS3mView m_view;
	CToolTipCtrl m_tooltip;

	BYTE* m_grids[S3M_MAX_FLOORS];
	BYTE* m_visits[S3M_MAX_FLOORS];
	BYTE* m_grid;
	BYTE* m_visit;
	int m_n;
	int m_nFloors;
	int m_floor;
	int m_mapViewFloor;
	int m_goalX, m_goalZ, m_goalFloor; // キャッシュ（毎フレ全走査しない）
	DWORD m_lastMapBakeTick;
	int m_passCells;      // 探索率用（奇数通路セル数）
	int m_visitPassCells; // 訪問済み奇数通路
	float m_px, m_pz, m_yaw;
	float m_yawTarget;
	float m_pxTarget, m_pzTarget;
	int m_turning;
	int m_turnHeld;
	int m_moving;
	int m_moveHeld;
	float m_bob;
	float m_anim;
	int m_won;
	int m_clearPhase;
	float m_clearT;
	float m_clearTextA;
	float m_clearScreenA;
	float m_clearTextAPrev;
	int m_floorFx;
	float m_floorFxT;
	float m_floorTextA;
	float m_floorScreenA;
	float m_floorTextAPrev;
	CStringW m_toastBakeText; // 共有 Clear テクスチャの最終焼き文字（α変化では再焼きしない）
	int m_stairFrom;
	int m_stairTo;
	int m_stairSwapDone;
	float m_stairShiftX;
	float m_stairShiftZ;
	float m_stairCamY;
	float m_stairStartX, m_stairStartZ;
	float m_stairLandX, m_stairLandZ;
	float m_miniFade;
	int m_miniFadeFrom;
	int m_miniFadeTo;
	int m_itemsLeft;
	int m_keysHeld;
	float m_doorMsgT; // 中央トースト残り（階層ラベルと同じ BakeClearTexture 経路）
	int m_toastKind; // 0=なし 1=鍵なし扉 2=鍵取得 3=扉開放
	int m_trapCellX, m_trapCellZ;
	float m_slowT;
	int m_lastStepMx, m_lastStepMz;
	int m_iceSlideLeft;
	float m_stepFromX, m_stepFromZ;
	float m_darkT;
	int m_portalFx;
	float m_portalFxT;
	float m_portalToX, m_portalToZ;
	int m_portalIgnoreX, m_portalIgnoreZ;
	float m_portalFlashA;
	enum { S3M_NAV_MAX = 52 }; // 現在地＋最大50歩
	struct S3mNavPt { short x, z, f; };
	S3mNavPt m_navPath[S3M_NAV_MAX];
	int m_navCount;
	int m_navWaiting; // 探索中オーバーレイ
	int m_baseTempoPos;
	int m_basePitchPos;
	DWORD m_lastTick;
	DWORD m_rng;
	DWORD m_genSeed;
	DWORD m_lastAutosave;
	int m_runDirty;
	int m_mapBakeDirty;
	int m_mapToggle;
	int m_overviewFloorHeld;
	float m_mapPanX, m_mapPanY;
	int m_mapPanDrag;
	CPoint m_mapPanLast;
	int m_overviewZoomPct;
	DWORD m_spaceToggleTick;
	int m_tipIsOverview;
	CStringW m_mapBadgeText;
	CStringW m_playTipText;
	CStringW m_overviewTipText;
	void EnsureTipTexture(BOOL overviewTip);
	float m_ovBarX, m_ovBarY, m_ovBarW, m_ovBarH;
	float m_ovTabX0[4], m_ovTabX1[4];
	float m_ovRecX0, m_ovRecX1;
	int m_ovTabN;
	int m_ovPressAction; // -1 none, 0..floors-1, 100 recenter
	CPoint m_ovPressPt;
	int m_floorBarDirty;
};

void OpenSoft3DMazeModeless(CWnd*);
void CloseSoft3DMazeIfOpen();
BOOL IsSoft3DMazeActive();
BOOL IsSoft3DMazeOpen();
BOOL Soft3DMazePreTranslate(MSG* pMsg);
void Soft3DMazeOnTimerp();
