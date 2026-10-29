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
	PH_ACTION, PH_E_SHOW, PH_E_MOVE, PH_END, PH_LEVELUP, PH_OVER, PH_MENU, PH_P_ITEM, PH_P_SUMMON
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
	void SyncBgm();
	BOOL WantsClose() const { return m_wantClose; }
	void ClearClose() { m_wantClose = FALSE; }
	SaveData& Save() { return m_save; }
	void Persist();

	void OnMouseMove(int x, int y);
	void OnMouseDown(int btn, int x, int y);
	void OnMouseUp(int btn, int x, int y);
	void OnWheel(int delta);
	void OnKey(int vk, BOOL down);
	void ZoomCam(float mul);
	void ResetCamDist();
	void ApplyZoomHold(float dt = 0.f);
	void OnChar(wchar_t ch);
	void OnPad(const PadState& p, float dt);
	BOOL WantsTextInput() const { return m_textFocus != 0; }

private:
	// 基盤
	BOOL LoadEverything();
	void NoteLoad(int permille);
	void BeginLoadSpan(int a, int b);
	BOOL PaintLoadScreen();
	void DrawLoadHud();
	static void LoadPulseCb(int cur, int n, void* user);
	BOOL Recover();
	void RenderFrame();
	void SetupCamera(V3 target, float yaw, float pitch, float dist, float fov);
	void RenderPortraits();
	void RenderActorPortrait(int slot, Actor& a, DWORD bg0, DWORD bg1, int fitBody = 0);
	void InvalidatePortraits();
	void DrawPortrait(int slot, float x, float y, float s, DWORD frame);
	int RosterPortrait(int roster) const;
	void PrepareMenuPortraits();
	void ShadowAndMain(int scene);
	void DrawScene(int scene);

	// UI
	float U(float v) const { return v * m_ui; }
	BOOL Btn(int id, float x, float y, float w, float h, const wchar_t* label, BOOL enabled = TRUE, BOOL selected = FALSE, float px = 0, DWORD tint = 0);
	void UiOk();
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
	void AddPopup(V3 p, const wchar_t* s, DWORD col, int style, float delay = 0, float life = 0);
	void AddLog(const wchar_t* s, DWORD col);
	void DrawBattleLog(float x, float y, float w, float h);
	void DrawHelpOverlay();
	float DrawStatLines(const CharData& c, float x, float y, float w, const int* gains, int hpCur, int mpCur);
	void DrawStatTip();
	void DrawSkillTip();
	const wchar_t* ClassName(int cls) const;
	void UnitTitle(const Unit& u, wchar_t* out, int n) const;

	// 拠点
	void EnterBase();
	void ProbeSuspend();
	void QueueSuspend(int kind);
	void ResumeBattle(int kind);
	void DropSuspend(int kind);
	void WaitSuspend();
	void ShutdownSnap();
	int BattlePortrait(int unit) const;
	void AssignBattlePortraits();
	void BuildShowcase();
	void RollMapSeed(int stage, BOOL force);
	void UpdateBase(float dt);
	void DrawBaseUi();
	void OpenSlotPanel();
	void DrawSlotUi();
	void RefreshSlotPeek();
	void CommitSlotName();
	void FlushCurrentSlot();
	void AdoptActiveSlot(BOOL leaveBattle);
	void SwitchToSlot(int to);
	void DeletePickedSlot();
	void CopyPickedSlot();
	void DrawShop();
	float DrawSoundOpts(float x, float y, float w, float k);
	void ApplyAudio();
	void DrawCodex();
	void DrawMonCodex();
	void DrawCodexTabs();
	void DrawMonStage();
	void EnsureMonPreview();
	void DrawShowcase3D();
	int PartyCount() const;
	int PartyCap() const;
	int RosterCap() const;
	int RosterCount() const;

	// キャラクタ作成
	void EnterCreate(int editIdx);
	void DrawCreateUi();
	void DrawCreate3D();
	void RefreshPreview();
	void PlayShown(Actor& a, int& mot, int style, int idleClip, BOOL fly, BOOL giveWings, BOOL restart);
	int DrawMotRow(int id0, float cx, float y, float avail, int mot, BOOL alt);
	int CreateClassList(int* out) const;

	// 編成
	void EnterParty();
	void DrawPartyUi();
	void DrawEquipLayer();
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
	void ApplyTerrace(int tx, int tz);
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
	void SeqImpact(const SeqStep& st, int volley);
	void FinishSeq();
	void AwardExp();
	void FinishUnitTurn(int acted);
	void ConfuseSelf(int ui);
	BOOL CheckBattleOver();
	void ComputeOverlays();
	void BuildOverlayQuads();
	void UpdateCamera(float dt);
	void UpdateUnitsVisual(float dt);
	void FaceTowards(int ui, int tx, int tz);
	void AimFormFace();
	void PickSkillCursor(int skill);
	void DrawTurnOrder();
	void DrawUnitPanel(const Unit& u, float x, float y, float w, BOOL big);
	void DrawForecast();
	void DrawMinimap(float x, float y, float s);
	void DrawCommandMenu();
	void DrawFormGuide(int skill, int tx, int tz, float avoidX, float avoidY, float avoidW, float avoidH);
	void UseConsumable(int good, int target);
	void DrawLevelUp();
	void DrawBattleStatus();
	void DrawShopFit();
	void DrawShopAsk();
	void DrawShopArm();
	void DrawPromoteOffer();
	void AdvanceLevelUp();
	void DrawCutIn3D();
	void BindSummonCut();
	void SummonImpactFx();
	void SummonLeave(float dt);
	void DrawCutInUi();
	void LayoutSpecialCut();
	void PoseFormAllies();
	void CutDressFx();
	float CutIntroLen() const;
	void ActorForUnit(int ui);
	void LoadBattleCast();
	// 読み込み済みの輸入メッシュを、まだ結んでいない敵アクターへ付け直す。
	void BindImportedActors();
	V3 ActorPos(int ui) const;
	float ActorFace(int ui) const;
	BOOL UnitInArena(int ui) const;
	BOOL CutFind(int ui, int* side, int* idx) const;
	V3 CutStand(int side, int idx) const;
	void GatherCut();

	// 演出
	void Burst(V3 p, int n, float spd, float size, float life, float r, float g, float b, int shape, int add, float grav, float stretch = 0, float upBias = 0, float size1 = -1);
	void ElementFx(int element, int kind, V3 from, V3 to, BOOL big);
	void StrikeFx(int skill, int el, int kind, int atkStyle, V3 from, V3 to, BOOL big);
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
	int m_loadPm;
	int m_loadShown;
	unsigned m_loadPaint;
	int m_loadA, m_loadB;
	BOOL m_loadLive;
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
	int m_padTrig;
	BOOL m_fastFwd;
	BOOL m_showHelp;
	int m_codex;             // 0=閉 1=スキル 2=成長 3=錬金 4=敵
	int m_codexPage;
	int m_monSel;
	float m_monYaw;
	int m_monShown;
	int m_monMot;            // 0待機 1歩き 2走り 3構え 4攻撃 5別技
	BOOL m_monDrag;
	Actor m_monAct;
	int m_codexJob;          // 0=全職。以降は職、末尾は陣形
	int m_codexSort;         // 0職 1名前 2MP 3習得
	int m_skillPage;
	int m_skillTab;          // 0攻撃 1回復 2クラフト 3陣形
	int m_skillShape;        // 攻撃: 0全部 1単体 2範囲 3広域
	int m_skillEl;           // 攻撃: 0全部。1以降は属性
	int m_skillPreview;      // スキル一覧で指している技。範囲の下描き
	int m_skMemoTab[MAX_ROSTER];
	int m_skMemoPage[MAX_ROSTER];
	int m_stashFull;         // 戦闘中に所持が12になった。不要売却のレイヤー
	BOOL m_ult;
	int m_confirmDelete;

	// カメラ
	V3 m_camTarget, m_camGoal;
	// 開戦前のマップ確認。16x16 マスの区画を蛇行で 0.5 秒ずつ見て、自軍の側で終わる。
	enum { SURVEY_BLK = 16, SURVEY_MAX = (MAPN / SURVEY_BLK) * (MAPN / SURVEY_BLK) };
	V3 m_surveyPt[SURVEY_MAX];
	int m_surveyN;
	float m_camYaw, m_camYawGoal, m_camPitch, m_camDist, m_camDistGoal;
	BOOL m_zoomHold;
	float m_zoomDist;
	int m_wheelSign;
	float m_wheelTime;
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
	int m_ptSlotUnit[Renderer::PORTRAIT_N];
	int m_ptForUnit[MAX_UNITS];
	Actor m_ptActor;
	int m_sortieAsk;
	int m_midStage;
	int m_autoStage;
	int m_shop;
	int m_btnSfx; // 次の Btn の決定音。-1 は場面別、-2 は鳴らさない
	int m_uiHover, m_uiHoverCand, m_uiHoverHit, m_uiHoverScreen, m_uiHoverShop;
	int UiHoverId() const;
	int m_shopLine;
	int m_shopTier;          // 武具: 0全部。1..10 はその職段の必要段
	int m_shopBand;          // 装飾: 0全部 1千未満 2千〜十万未満 3十万以上
	int m_shopPage;
	int m_shopSellPage;
	int m_shopKind;
	int m_shopSuper;   // 技珠店: 0=通常戦技 1=奥義
	int m_shopBack;
	int m_shopFit;
	int m_shopFitId;
	int m_shopFitPage;
	int m_shopHold;
	int m_shopWho;
	int m_partyBack;
	int m_baseOpt;
	int m_slotUi;                         // 0=閉 1=一覧 2=削除1 3=削除2 4=コピー先 5=コピー確認
	int m_slotPick;
	int m_slotDest;
	int m_slotClear[TAC_SLOT_N];
	wchar_t m_slotEdit[TAC_SLOT_NAME];
	int m_createFromParty;
	int m_evFromParty;
	int m_evJump;
	int m_statEq;
	int m_statEqPage;
	int m_itemAim;
	int m_itemPage;
	int m_statTip;     // ホバー中の能力。-1 で無し
	int m_skillTip;    // 編成のスキル説明。-1 無し、0 習得、1 戦技・奥義、2 パッシブ
	int m_skillTipId;
	int m_statOpen;    // 戦闘コマンドのステータス表示
	int m_statWho;     // 表示中のユニット。負なら行動中

	// 拠点の背景
	Map m_showMap;
	BOOL m_showBuilt;
	int m_showStage;
	int m_showTheme;
	int m_themeSel;
	DWORD m_mapSeed;
	int m_mapSeedStage;
	Actor m_showAct[MAX_PARTY];
	int m_showN;
	V3 m_showPos[MAX_PARTY];
	float m_showYaw;

	// 作成
	CharData m_edit;
	int m_editIdx;     // -1=新規
	Actor m_preview;
	float m_previewYaw;
	float m_previewPitch;
	float m_previewZoom;
	V3 m_previewPan;
	int m_lineage;
	BOOL m_previewDrag;
	BOOL m_previewPanDrag;
	int m_previewMot;  // 0待機 1歩き 2走り 3構え 4攻撃 5別技

	// 編成
	int m_partySel;
	int m_eqPick;      // 0=閉 1=武器 2=防具 3,4=装飾 5,6,9=戦技 10=奥義 7,8=パッシブ
	int m_eqPage;

	// 進化
	int m_evMain, m_evTarget;
	int m_evPartner[2];
	int m_evPick;      // 0=本体 1=進化先 2=素材
	int m_evMainPage, m_evMainPages;
	int m_evPartPage, m_evPartPages;
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
	float m_cmdFace0;    // 陣形で向く前の向き。取消で戻す
	int m_cmdFaced;
	int m_target;
	BYTE m_ovMove[MAPN][MAPN];
	BYTE m_ovAtk[MAPN][MAPN];
	BYTE m_ovThreat[MAPN][MAPN];
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
	Strike m_strk[96];
	int m_strkN;
	float m_stepL, m_stepTi;
	struct Proj { int active; float t, launch, dur; int el, kind, arrow, self, style, skill; } m_proj;
	struct TacLog { wchar_t s[80]; DWORD col; };
	TacLog m_log[24];
	int m_logN;
	float m_orderBottom;
	int m_ordSnap[8];
	int m_ordFrom[8];
	int m_ordSnapN;
	int m_ordLeave;
	float m_ordAnimT;
	int m_rosterPage;
	int m_cutStage;      // 0=覆う 1=技名 2=攻防 3=覆う 4=マップへ
	float m_cutStageT;
	int m_cutA, m_cutD;
	int m_cutAlly[6], m_cutFoe[6];
	int m_cutAllyN, m_cutFoeN;
	int m_cutKind;       // 0=通常 1=戦技 2=陣形 3=召喚
	int m_sumOn, m_sumFx, m_sumHit;
	float m_sumU;
	V3 m_sumPos, m_sumFrom, m_sumTo;
	Actor m_sumAct;
	V3 m_cutAllyPos[6];
	V3 m_cutFoePos[6];
	V3 m_cutPosA, m_cutPosD;
	float m_flashWhite;
	Fix<Popup, 64> m_deferPopups;
	int m_expGain[MAX_UNITS];
	LevelUpInfo m_lvq[MAX_PARTY * 4];
	int m_lvqN, m_lvqCur;
	int m_promoAsk;
	float m_lvT;
	int m_battleStage;
	int m_importBind;
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
