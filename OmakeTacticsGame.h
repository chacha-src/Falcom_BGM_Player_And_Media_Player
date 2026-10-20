#pragma once
// おまけタクティクス: ゲーム本体（画面・UI・戦闘進行）

#include "OmakeTacticsModels.h"
#include "OmakeTacticsText.h"

namespace otac {

enum Screen { SC_LOADING, SC_FAIL, SC_BASE, SC_CREATE, SC_PARTY, SC_EVOLVE, SC_BATTLE, SC_RESULT };

struct Env {
	DWORD skyTop, skyBot, fog, ambSky, ambGnd, ground;
	V3 light;
	float amb, hard;
};
void ThemeEnv(int theme, Env& e);

struct PadState {
	float lx, ly, rx, ry, lt, rt;
	int buttons;   // bit0=A bit1=B bit2=X bit3=Y bit4=LB bit5=RB bit6=Back bit7=Start
	int hat;
	int connected;
};

struct UiBtn { int id; float x, y, w, h; int enabled; int sel; };

struct Popup {
	V3 p;
	wchar_t s[48];
	DWORD col;
	float t, life, size, delay;
	int style;     // 0=数値 1=文字 2=クリティカル
};

struct Banner {
	wchar_t s[96];
	wchar_t sub[128];
	float t, life;
	DWORD col;
	int style;     // 0=帯 1=大見出し 2=小さめ通知
};

// 戦闘アクションの再生
struct SeqStep {
	int attacker, defender;
	int skill;
	int counter;
	int nHits;
	Strike hits[MAX_UNITS];
};

enum Phase {
	PH_NONE, PH_INTRO, PH_NEXT, PH_P_MOVE, PH_MOVING, PH_P_CMD, PH_P_SKILLS, PH_P_TARGET, PH_P_SKILLTARGET, PH_P_SHOVE,
	PH_ACTION, PH_E_SHOW, PH_E_MOVE, PH_END, PH_LEVELUP, PH_OVER, PH_MENU
};

struct LevelUpInfo {
	int unit;
	int oldLv, newLv;
	int gains[S_COUNT];
	int learned[4];
	int nLearned;
};

class Game {
public:
	Game();
	~Game();
	BOOL Init(HWND view, int w, int h);
	void Shutdown();
	void Resize(int w, int h);
	void RefreshSurface();
	void Tick(float dt);
	BOOL WantsClose() const { return m_wantClose; }
	void ClearClose() { m_wantClose = FALSE; }
	SaveData& Save() { return m_save; }
	void Persist();

	void OnMouseMove(int x, int y);
	void OnMouseDown(int btn, int x, int y);
	void OnMouseUp(int btn, int x, int y);
	void OnWheel(int delta);
	void OnKey(int vk, BOOL down);
	void OnChar(wchar_t ch);
	void OnPad(const PadState& p, float dt);
	BOOL WantsTextInput() const { return m_textFocus != 0; }

private:
	// 基盤
	BOOL LoadEverything();
	BOOL Recover();
	void RenderFrame();
	void SetupCamera(V3 target, float yaw, float pitch, float dist, float fov);
	void RenderPortraits();
	void RenderActorPortrait(int slot, Actor& a, DWORD bg0, DWORD bg1);
	void InvalidatePortraits();
	void DrawPortrait(int slot, float x, float y, float s, DWORD frame);
	void ShadowAndMain(int scene);
	void DrawScene(int scene);

	// UI
	float U(float v) const { return v * m_ui; }
	BOOL Btn(int id, float x, float y, float w, float h, const wchar_t* label, BOOL enabled = TRUE, BOOL selected = FALSE, float px = 0, DWORD tint = 0);
	int Sel(int id, float x, float y, float w, float h, const wchar_t* label, const wchar_t* value);
	BOOL Hit(float x, float y, float w, float h) const;
	BOOL ClickIn(float x, float y, float w, float h);
	void Panel(float x, float y, float w, float h, float alpha = 1.f);
	void Bar(float x, float y, float w, float h, float frac, DWORD c0, DWORD c1, float ghost = -1);
	void NavMove(int dx, int dy);
	void NavConfirm();
	void NavBack();
	void DrawBanners(float dt);
	void DrawPopups(float dt);
	void AddBanner(const wchar_t* s, const wchar_t* sub, DWORD col, int style, float life);
	void AddPopup(V3 p, const wchar_t* s, DWORD col, int style, float delay = 0);
	void DrawHelpOverlay();
	void DrawStatLines(const CharData& c, float x, float y, float w, const int* gains, int hpCur, int mpCur);
	const wchar_t* ClassName(int cls) const;
	void UnitTitle(const Unit& u, wchar_t* out, int n) const;

	// 拠点
	void EnterBase();
	void BuildShowcase();
	void UpdateBase(float dt);
	void DrawBaseUi();
	void DrawCodex();
	void DrawShowcase3D();
	int PartyCount() const;
	int RosterCount() const;

	// キャラクタ作成
	void EnterCreate(int editIdx);
	void DrawCreateUi();
	void DrawCreate3D();
	void RefreshPreview();
	int CreateClassList(int* out) const;

	// 編成
	void EnterParty();
	void DrawPartyUi();
	void TogglePartyMember(int idx);
	void DeleteRoster(int idx);

	// 進化
	void EnterEvolve();
	void DrawEvolveUi();
	void DoEvolve();

	// 戦闘
	void StartBattle(int stage);
	void EndBattle(int victory);
	void UpdateBattle(float dt);
	void DrawBattle3D();
	void DrawBattleUi();
	void BattleClick(int x, int y);
	void BattleCancel();
	void BattleConfirmCursor();
	void MoveCursor(int dx, int dz);
	void UpdateHover();
	BOOL PickCell(float sx, float sy, int& cx, int& cz) const;
	V3 CellCenter(int x, int z) const;
	V3 UnitGroundPos(const Unit& u) const;
	void BeginTurn();
	void PlayerSelectMove(int x, int z);
	void StartMove(int ui, int tx, int tz, int nextPhase);
	void UpdateMove(float dt);
	void OpenCommand();
	BOOL HasAttackTarget(int ui) const;
	BOOL CanTarget(int ui, int target, int skill) const;
	void BeginAttack(int ui, int target, int skill, int cx, int cz);
	void UpdateSeq(float dt);
	void SeqImpact(const SeqStep& st);
	void FinishSeq();
	void AwardExp();
	void FinishUnitTurn(int acted);
	BOOL CheckBattleOver();
	void ComputeOverlays();
	void BuildOverlayQuads();
	void UpdateCamera(float dt);
	void UpdateUnitsVisual(float dt);
	void FaceTowards(int ui, int tx, int tz);
	void DrawTurnOrder();
	void DrawUnitPanel(const Unit& u, float x, float y, float w, BOOL big);
	void DrawForecast();
	void DrawMinimap(float x, float y, float s);
	void DrawCommandMenu();
	void DrawLevelUp();
	void DrawCutIn3D();
	void DrawCutInUi();
	void ActorForUnit(int ui);
	V3 ActorPos(int ui) const;
	float ActorFace(int ui) const;
	BOOL UnitInArena(int ui) const;

	// 演出
	void Burst(V3 p, int n, float spd, float size, float life, float r, float g, float b, int shape, int add, float grav, float stretch = 0, float upBias = 0, float size1 = -1);
	void ElementFx(int element, int kind, V3 from, V3 to, BOOL big);
	void UpdateParticles(float dt);
	void Shake(float s) { m_shake = max(m_shake, s); }

	// 結果
	void DrawResultUi();

	Renderer m_r;
	Library m_lib;
	SaveData m_save;
	HWND m_hwnd;
	int m_w, m_h;
	float m_ui;
	float m_time;
	Screen m_screen;
	BOOL m_wantClose;
	BOOL m_loaded;
	int m_loadFrames;
	float m_renderDt, m_lastRenderTime;
	int m_recoverTries;
	Rng m_rng;

	// 入力
	int m_mx, m_my;
	BOOL m_mouseSeen;
	BOOL m_lDown, m_rDown, m_mDown;
	int m_downX, m_downY;
	BOOL m_click, m_rclick;
	int m_clickX, m_clickY;
	BOOL m_dragRot, m_dragPan, m_downUi;
	int m_dragLastX, m_dragLastY;
	float m_dragDist;
	Fix<UiBtn, 96> m_btns, m_btnsPrev;
	int m_focusId, m_activateId;
	BOOL m_navMode;
	int m_selDelta;     // フォーカス中セレクタの左右入力
	int m_textFocus;
	BOOL m_keyShift;
	PadState m_padPrev;
	float m_padRepeat;
	int m_padDirPrev;
	BOOL m_fastFwd;
	BOOL m_showHelp;
	int m_codex;             // 0=閉 1=スキル 2=成長
	int m_codexPage;
	int m_skillPage;
	BOOL m_ult;
	int m_confirmDelete;

	// カメラ
	V3 m_camTarget, m_camGoal;
	float m_camYaw, m_camYawGoal, m_camPitch, m_camDist, m_camDistGoal;
	V3 m_camPan;
	float m_shake;
	V3 m_eye;

	// 共通演出
	Fix<Particle, 4096> m_parts;
	Fix<Popup, 64> m_popups;
	Fix<Banner, 12> m_banners;

	// ポートレート
	int m_portraitKey[Renderer::PORTRAIT_N];
	int m_portraitWant[Renderer::PORTRAIT_N];
	Actor m_ptActor;

	// 拠点の背景
	Map m_showMap;
	BOOL m_showBuilt;
	int m_showStage;
	Actor m_showAct[MAX_PARTY];
	int m_showN;
	V3 m_showPos[MAX_PARTY];
	float m_showYaw;

	// 作成
	CharData m_edit;
	int m_editIdx;     // -1=新規
	Actor m_preview;
	float m_previewYaw;
	int m_lineage;
	BOOL m_previewDrag;

	// 編成
	int m_partySel;

	// 進化
	int m_evMain, m_evTarget;
	int m_evPartner[2];
	int m_evPick;      // 0=本体 1=進化先 2=素材
	float m_evFlash;

	// 戦闘
	Battle m_b;
	Reach m_reach;
	Actor m_act[MAX_UNITS];
	Phase m_phase, m_phaseBeforeMenu;
	float m_phaseT;
	int m_cur;           // 行動中ユニット
	BOOL m_turnOpen;     // m_cur の行動が未確定（CT 未消費）
	int m_curX0, m_curZ0;   // 行動開始位置（取消用）
	float m_curFace0;
	int m_moved;
	int m_cx, m_cz;      // カーソル
	BOOL m_cursorValid;
	int m_hoverUnit;
	int m_cmdSkill;      // 選択中スキル（-1=通常）
	int m_target;
	BYTE m_ovMove[MAPN][MAPN];
	BYTE m_ovAtk[MAPN][MAPN];
	Fix<VtxS, 12288> m_ovQuads;
	BOOL m_ovDirty;
	int m_path[MAPN * 4];
	int m_pathN;
	int m_moveUnit;
	int m_movePts[MAPN * 4];
	int m_moveN;
	float m_moveT;
	int m_moveNext;
	AiPlan m_ai;
	int m_lastTeam;
	// アクション再生
	SeqStep m_seq[24];
	int m_seqN, m_seqCur;
	float m_seqT;
	int m_seqImpact;
	BOOL m_seqCut;
	int m_seqCaster, m_seqPrimary, m_seqSkill;
	float m_dispHp[MAX_UNITS];
	int m_preHp[MAX_UNITS];
	float m_dieT[MAX_UNITS];
	float m_faceShow[MAX_UNITS];
	float m_lunge[MAX_UNITS];
	Strike m_strk[32];
	int m_strkN;
	float m_stepL, m_stepTi;
	struct Proj { int active; float t, launch, dur; int el, kind, arrow, self; } m_proj;
	int m_cutStage;      // 0=覆う 1=技名 2=攻防 3=覆う 4=マップへ
	float m_cutStageT;
	int m_cutA, m_cutD;
	V3 m_cutPosA, m_cutPosD;
	float m_flashWhite;
	Fix<Popup, 64> m_deferPopups;
	int m_expGain[MAX_UNITS];
	LevelUpInfo m_lvq[MAX_PARTY * 4];
	int m_lvqN, m_lvqCur;
	float m_lvT;
	int m_battleStage;
	int m_victory;
	float m_overT;
	int m_resultLv0[MAX_PARTY], m_resultExp[MAX_PARTY];
	int m_resultRoster[MAX_PARTY];
	int m_resultN;
	int m_bossDown;
	int m_menuConfirm;
	float m_enemyWait;
};

} // namespace otac
