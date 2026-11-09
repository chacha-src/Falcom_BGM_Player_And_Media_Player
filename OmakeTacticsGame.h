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

// メッセージの出方。種類ごとに固定で、順には回さない。
enum {
	MSG_BLUR = 0, // ぼかしから砕けて散る。クリティカル、撃破、突進
	MSG_DEPTH,    // 奥から迫って引く。勝利、進化、見出し、増援
	MSG_ORBIT,    // 周回して集まる。混乱、睡眠、スタン、ワープ
	MSG_SLAB,     // 三枚が合わさって割れる。技名、扉、押し、段差
	MSG_FACE,     // こちらを向くまで回る。手番、告知、バフ
	MSG_RISE,     // 地面から空へ。回復、経験、お金、入手
	MSG_BURST,    // 一点から弾ける。ダメージ、毒、溶岩
	MSG_DROP      // 残像を引いて落ちる。回避、失敗、敗北、落下
};

struct Popup {
	V3 p;
	wchar_t s[48];
	DWORD col;
	float t, life, size, delay;
	int style;     // 0=数値 1=文字 2=クリティカル
	int pat;       // MSG_*
};

struct Banner {
	wchar_t s[96];
	wchar_t sub[128];
	float t, life;
	DWORD col;
	int style;     // 0=帯 1=大見出し 2=小さめ通知
	int pat;       // MSG_*
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
	void GoHomeFromBattle();
	SaveData& Save() { return m_save; }
	int PaceHz() const;
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
	void ReleaseInput();
	void OnBecameActive();
	BOOL WantsTextInput() const { return m_textFocus != 0; }
	int TextFocus() const { return m_textFocus; }
	void ClearTextFocus();
	void SanitizeTextFocus();
	BOOL TextLive(wchar_t*& buf, int& maxLen);
	void SetTextCaret(int caret);
	int TextCaret() const { return m_textCaret; }
	void SetImeComp(const wchar_t* s);
	void TextBoxRect(RECT& rc) const;
	void SetSellFromText(const wchar_t* s);
	void SellToText(wchar_t* s, int n) const;
	void NotifyFindChanged();
	BOOL FoeFindLive() const { return m_screen == SC_BASE && m_codex == 4; }
	const wchar_t* MonFindText() const { return m_monFind; }
	void ArmFoeFind();
	void CommitMonFind(const wchar_t* s);
	void RequestImeReload() { m_textReload = 1; }
	BOOL TakeImeReload() { BOOL r = m_textReload; m_textReload = 0; return r; }
	float TextCaretPx();
	float TextFontPx() const { return m_textPx > 1.f ? m_textPx : 16.f; }
	float TextPad() const { return m_textPad; }

private:
	// 基盤
	BOOL LoadEverything();
	void NoteLoad(int permille);
	void BeginLoadSpan(int a, int b);
	int MeasureLoadUnits(int& showUnits);
	void OpenLoadSlice(int units);
	void CloseLoadSlice();
	BOOL PaintLoadScreen();
	void DrawLoadHud(int permille = -1);
	static void LoadPulseCb(int cur, int n, void* user);
	BOOL Recover();
	void PulseQuality(float work, float span);
	void ApplyQualityNow(int q);
	void ApplyQualityFine(int idx);
	void PickQuality(int delta);
	int QualityNow() const;
	// 保存の拍は利用者の選択のまま。自動の保険（一段ずつ下げ、余力で一段上げ）は実行時だけ上書きする。
	int SyncNow() const;
	void RenderFrame();
	void SetupCamera(V3 target, float yaw, float pitch, float dist, float fov);
	void RenderPortraits();
	BOOL RenderActorPortrait(int slot, Actor& a, DWORD bg0, DWORD bg1, int fitBody = 0, BOOL live = FALSE);
	void InvalidatePortraits();
	void DrawPortrait(int slot, float x, float y, float s, DWORD frame);
	int RosterPortrait(int roster) const;
	void PrepareMenuPortraits();
	void ShadowAndMain(int scene);
	void DrawScene(int scene);

	// UI
	float U(float v) const { return v * m_ui; }
	BOOL Btn(int id, float x, float y, float w, float h, const wchar_t* label, BOOL enabled = TRUE, BOOL selected = FALSE, float px = 0, DWORD tint = 0);
	float FitTextPx(const wchar_t* s, float maxW, float px, int bold = 0);
	void UiOk();
	int Sel(int id, float x, float y, float w, float h, const wchar_t* label, const wchar_t* value, int on = 1);
	BOOL Hit(float x, float y, float w, float h) const;
	BOOL ClickIn(float x, float y, float w, float h);
	void DrawPerfHud();
	void Panel(float x, float y, float w, float h, float alpha = 1.f);
	void Bar(float x, float y, float w, float h, float frac, DWORD c0, DWORD c1, float ghost = -1);
	void NavMove(int dx, int dy);
	void NavConfirm();
	void NavBack();
	BOOL RightClosesUi() const;
	void DrawBanners(float dt);
	void DrawPopups(float dt);
	void AddBanner(const wchar_t* s, const wchar_t* sub, DWORD col, int style, float life, int pat);
	void AddPopup(V3 p, const wchar_t* s, DWORD col, int style, int pat, float delay = 0, float life = 0);
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
	void WarmHomeAvatars();
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
	void RunSlotFile(int job);
	void ImportConfirmed();
	void DrawShop();
	float DrawSoundOpts(float x, float y, float w, float k, int* tip = 0, float* tipY = 0, float* tipX = 0, float* tipW = 0, float* tipH = 0);
	void ApplyAudio();
	void DrawCodex();
	void DrawMonCodex();
	int FoeList(int* out, int cap) const;
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
	void DrawEvoCut();
	void DoEvolve();
	void DrawJobGate();
	void DrawJobRedo();
	int NextJobPending(int from) const;
	void BeginJobRedo();
	void ApplyJobPick(int cls);
	void FinishJobSkip();

	// 戦闘
	void StartBattle(int stage);
	void EndBattle(int victory);
	void UpdateBattle(float dt);
	void DrawBattle3D();
	BOOL ViewShowsGround() const;
	void DrawBattleUi();
	void PlaceBattleHud();
	void BattleClick(int x, int y);
	void BattleCancel();
	void BattleConfirmCursor();
	void ApplyTerrace(int tx, int tz);
	void MoveCursor(int dx, int dz);
	void CycleLayer(int dir);
	void SyncCursorLayer();
	void UpdateHover();
	BOOL PickCell(float sx, float sy, int& cx, int& cz, int* layer = 0) const;
	V3 CellCenter(int x, int z) const;
	V3 UnitGroundPos(const Unit& u) const;
	void BeginTurn();
	void PlayerSelectMove(int x, int z);
	void StartMove(int ui, int tx, int tz, int nextPhase);
	void UpdateMove(float dt);
	void OpenCommand();
	BOOL HasAttackTarget(int ui) const;
	BOOL ChestInRange(int ui, int x, int z) const;
	BOOL OpenChestAt(int ui, int x, int z);
	BOOL CanTarget(int ui, int target, int skill) const;
	BOOL CursorInAttackRange() const;
	void BeginAttack(int ui, int target, int skill, int cx, int cz);
	void UpdateSeq(float dt);
	void SeqImpact(const SeqStep& st, int volley);
	void FinishSeq();
	void GrantFoeLoot(int ui, int coop);
	void AwardExp();
	void FinishUnitTurn(int acted);
	void RaiseStampede();
	void ConfuseSelf(int ui);
	BOOL CheckBattleOver();
	void DemoBegin();
	void DemoLeave();
	void TickDemo(float dt);
	void DemoDrive(float dt);
	BOOL DemoEnding() const;
	void DrawDemoCurtain();
	void ComputeOverlays();
	void BuildOverlayQuads();
public:
	void SetAudioMs(float ms) { m_msAud = ms; }
	int AppendOverlayQuad(VtxS* dst, int n, int cap, int x, int z, DWORD c, float lift, float yAbs, int layer, int segs) const;
	int BuildRangeBand(int z0, int z1, const BYTE* block, VtxS* dst, int cap) const;
private:
	void ClearRipples();
	void UpdateCamera(float dt);
	void UpdateUnitsVisual(float dt);
	void FaceTowards(int ui, int tx, int tz);
	void AimFormFace();
	void PickSkillCursor(int skill);
	void DrawTurnOrder();
	void DrawUnitPanel(const Unit& u, float x, float y, float w, BOOL big, float* rx = 0, float* ry = 0, float* rw = 0, float* rh = 0);
	void DrawForecast(float* ox = 0, float* oy = 0, float* ow = 0, float* oh = 0);
	void PreviewBattleExp(long long& opt, long long& pes, int& showLv, int& nTg, BOOL& combat) const;
	void DrawMinimap(float x, float y, float s, int layer = 0);
	void DrawShowMinimap(float x, float y, float s);
	void DrawCommandMenu();
	void DrawFormGuide(int skill, int tx, int tz, float avoidX, float avoidY, float avoidW, float avoidH);
	void UseConsumable(int good, int target);
	void DrawLevelUp();
	void DrawBattleStatus(BOOL edit);
	void DrawShopFit();
	void DrawShopAsk();
	void DrawShopArm();
	void DrawShopRosterLayer(int mode, int line, int tier, const GearDef* gear, int accId, float ax, float ay, float aw, float ah);
	void DrawPromoteOffer();
	void AdvanceLevelUp();
	void DrawCutIn3D();
	void BindSummonCut();
	void SummonImpactFx();
	void SummonApply(int phase, float u);
	void SummonArrive(float dt);
	void SummonStrike(float dt);
	void SummonLeave(float dt);
	void SummonMedium(int phase, float u);
	void DrawSummonGate();
	float SummonStrikeNeed() const;
	float SummonDepartNeed() const;
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
	M4 CutSceneXform() const;
	void GatherCut();

	// 演出
	void Burst(V3 p, int n, float spd, float size, float life, float r, float g, float b, int shape, int add, float grav, float stretch = 0, float upBias = 0, float size1 = -1);
	void ElementFx(int element, int kind, V3 from, V3 to, BOOL big);
	void StrikeFx(int skill, int el, int kind, int atkStyle, V3 from, V3 to, BOOL big);
	void UpdateParticles(float dt);
	void UpdateWeatherEvents(float dt);
	void LightningStrike(V3 to, float mag);
	void WeatherTread(const Unit& u, int x, int z);
	void Shake(float s) { m_shake = max(m_shake, s); }

	// 結果
	void DrawResultUi();

	Renderer m_r;
	Library m_lib;
	SaveData m_save;
	int m_qApply, m_qHold, m_qMissN, m_qOkN, m_qHz, m_qSlots, m_qFine;
	int m_syncArm, m_syncArmN, m_syncRun;
	BOOL m_qDrew;
	float m_qCoolT, m_syncLock;
	float m_fps;
	float m_fpsSpan[160];
	float m_workSpan[160];
	float m_workAvg;
	float m_fpsWin;
	float m_msUpd, m_msSh, m_msRef, m_msMain, m_msPort, m_msHud, m_msPr, m_msAud;
	int m_fpsN, m_fpsI;
	LARGE_INTEGER m_fpsAt;
	HWND m_hwnd;
	int m_w, m_h;
	float m_ui;
	float m_time;
	Screen m_screen;
	BOOL m_wantClose;
	BOOL m_goHome;
	BOOL m_loaded;
	int m_loadFrames;
	int m_loadPm;
	int m_loadShown;
	unsigned m_loadPaint;
	int m_loadA, m_loadB;
	BOOL m_loadLive;
	int m_loadUnits;
	int m_avaLoadUnits;
	int m_loadHave;
	int m_sliceBase;
	int m_sliceUnits;
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
	BOOL m_dragRot, m_dragPan, m_downUi, m_rOnUi;
	int m_dragLastX, m_dragLastY;
	float m_dragDist;
	Fix<UiBtn, 96> m_btns, m_btnsPrev;
	int m_focusId, m_activateId;
	BOOL m_navMode;
	int m_selDelta;     // フォーカス中セレクタの左右入力
	int m_textFocus;
	int m_textCaret;
	BOOL m_textReload;
	wchar_t m_imeComp[48];
	float m_textBox[4];
	float m_textPx;
	float m_textPad;
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
	int m_monBand;
	int m_monSeenOnly;
	wchar_t m_monFind[24];
	float m_monYaw;
	int m_monShown;
	int m_monMot;            // 0待機 1歩き 2走り 3構え 4攻撃 5別技
	BOOL m_monDrag;
	Actor m_monAct;
	int m_codexJob;          // 0=その系統の全部。正は職ID。-1は陣形
	int m_codexFam;          // 0すべて 1初期 2上位 3..10は追加職の系統
	int m_codexJobPage;
	int m_codexShape;        // 0全部 1単体 2範囲 3広域 4直線 5扇
	int m_codexEl;           // 0全部。以降は属性
	int m_codexSort;         // 0職 1名前 2MP 3習得
	int m_growPath[14];      // ジョブツリーで上から選んだ職
	int m_growN;
	float m_growScroll;
	int m_growSeen;
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
	// 開戦前。遠い敵拠点から自軍まで、引いたカメラで一息に寄せる。
	enum { SURVEY_MAX = 4 };
	static constexpr float kIntroSweep = 2.6f;
	V3 m_surveyPt[SURVEY_MAX];
	int m_surveyN;
	float m_camYaw, m_camYawGoal, m_camPitch, m_camDist, m_camDistGoal;
	BOOL m_zoomHold;
	float m_zoomDist;
	int m_wheelSign;
	float m_wheelTime;
	V3 m_camPan;
	float m_shake;
	float m_sceneAcc;
	float m_wxAcc;
	float m_wxSplashAcc;
	float m_wxMeshWait;
	int m_wxMesh;
	// 天候イベント（雷・雹・地震）。固定配列のまま、戦闘中だけ動く。
	float m_wxEventT;      // 次の天候イベントまでの残り
	float m_boltT;         // 落雷から雷鳴（揺れ）までの遅延
	float m_boltMag;       // 雷鳴の強さ
	float m_quakeT;        // 地震の残り時間
	float m_quakeMag;      // 地震の強さ
	float m_quakeAcc;      // 地震の砂塵スポーン蓄積
	float m_hailBurstT;    // にわか雹の残り時間
	float m_hailAcc;       // にわか雹のスポーン蓄積
	float m_wxFlash;       // 稲妻フラッシュの強さ 0..1
	float m_wxFlashCol[3]; // フラッシュの色（寒色の白）
	unsigned char m_cover[MAPN * MAPN];
	V3 m_eye;

	// 共通演出
	Fix<Particle, 4096> m_parts;
	// 水面に残る波紋。x, 発生時刻, z, 強さ。シェーダの Rip[48] と同じ数。
	enum { TAC_RIPN = 48 };
	float m_rip[TAC_RIPN][4];
	int m_ripHead;
	float m_ripSeen[MAX_UNITS][2];
	float m_ripSeenT[MAX_UNITS];
	Fix<Popup, 64> m_popups;
	Fix<Banner, 12> m_banners;

	// ポートレート
	int m_portraitKey[Renderer::PORTRAIT_N];
	int m_portraitWant[Renderer::PORTRAIT_N];
	int m_ptSlotUnit[Renderer::PORTRAIT_N];
	int m_ptForUnit[MAX_UNITS];
	Actor m_ptActor;
	Actor m_ptCache[Renderer::PORTRAIT_N];
	int m_ptCacheWant[Renderer::PORTRAIT_N];
	int m_ptCacheLook[Renderer::PORTRAIT_N];
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
	int m_sellAsk, m_sellKind, m_sellId, m_sellN, m_sellMax, m_sellFresh, m_sellGo;
	int m_partyBack;
	int m_baseOpt;
	int m_slotUi;                         // 0=閉 1=一覧 2=削除1 3=削除2 4=コピー先 5=コピー確認 6=読込確認 7=書出元 8=読込先
	int m_slotPick;
	int m_slotDest;
	int m_slotClear[TAC_SLOT_N];
	wchar_t m_slotEdit[TAC_SLOT_NAME];
	int m_slotJob;
	int m_slotBusy;
	wchar_t m_impPath[MAX_PATH];
	int m_createFromParty;
	int m_evFromParty;
	int m_evJump;
	int m_statEq;
	int m_statEqPage;
	int m_itemAim;
	int m_itemPage;
	int m_aimPage;
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
	int m_mapLook;   // 1=ホームの地図を自分で見渡す
	int m_homeUi;    // 0=ホームのUIを全部隠す
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
	BOOL m_previewWing;   // 見本だけ。0で翼を描かない
	BOOL m_previewGround; // 見本だけ。1で翼のない地上の動き

	// 編成
	int m_partySel;
	int m_eqPick;      // 0=閉 1=武器 2=防具 3,4=装飾 5,6,9=戦技 10=奥義 7,8=パッシブ
	int m_eqPage;
	int m_partySkPage;
	int m_partySkPages;
	float m_partySkX, m_partySkY, m_partySkW, m_partySkH;

	// 進化
	int m_evMain, m_evTarget;
	int m_evPartner[2];
	int m_evPick;      // 0=本体 1=進化先 2=素材
	int m_evMainPage, m_evMainPages;
	int m_evPartPage, m_evPartPages;
	float m_evFlash;
	int m_evoCut;
	float m_evoT;
	int m_evoRoster, m_evoFrom, m_evoTo;
	int m_jobRedo;     // 0なし 1案内 2進化画面で振り直し 3スキップ確認
	int m_jobWho, m_jobStep, m_jobN, m_jobSkipAll, m_jobSel;
	int m_jobPath[12];

	// 戦闘
	Battle m_b;
	Reach m_reach;
	Actor m_act[MAX_UNITS];
	Phase m_phase, m_phaseBeforeMenu;
	float m_phaseT;
	int m_cur;           // 行動中ユニット
	BOOL m_turnOpen;     // m_cur の行動が未確定（CT 未消費）
	int m_curX0, m_curZ0;   // 行動開始位置（取消用）
	int m_curDeck0;
	float m_curFace0;
	int m_moved;
	int m_cx, m_cz;      // カーソル
	int m_cLayer;        // 見ている層。0=下 1=中 2=上
	int m_layerLock;     // ホイールで層を固定しているあいだ 1
	float m_miniX[3], m_miniY[3], m_miniS[3];
	int m_miniHitN;
	BOOL m_cursorValid;
	int m_hoverUnit;
	int m_cmdSkill;      // 選択中スキル（-1=通常）
	float m_cmdFace0;    // 陣形で向く前の向き。取消で戻す
	int m_cmdFaced;
	int m_target;
	BYTE m_ovMove[MAPN][MAPN];
	BYTE m_ovAtk[MAPN][MAPN];
	BYTE m_ovAtkLay[MAPN][MAPN];
	BYTE m_ovThreat[MAPN][MAPN];
	Fix<VtxS, 65536> m_ovQuads;
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
	// カットシーン中は着弾まで、攻撃前の状態異常を見せる。着弾で実体へ合わせる。
	struct VisAil {
		int poison, slow, defDown, sleep, confuse, stun, freeze, burn, bleed, blind, silence;
	};
	VisAil m_visAil[MAX_UNITS];
	float m_ailBlend[MAX_UNITS];
	int m_ailBlendEl[MAX_UNITS];
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
	struct HudLay {
		float col;
		float orderX, orderW, orderMaxB;
		float panelTop, panelH, logTop, logH;
		float stageR, stageB;
		float partyT, partyR, partyB;
		float margin;
	} m_hud;
	int m_ordSnap[8];
	int m_ordFrom[8];
	int m_ordSnapN;
	int m_ordLeave;
	float m_ordAnimT;
	int m_rosterPage;
	int m_cutStage;      // 0=覆う 1=技名 2=攻防 3=覆う 4=マップへ
	float m_cutStageT;
	int m_cutA, m_cutD;
	enum { CUT_ALLY_MAX = MAX_PARTY, CUT_FOE_MAX = 96 };
	int m_cutAlly[CUT_ALLY_MAX], m_cutFoe[CUT_FOE_MAX];
	int m_cutAllyN, m_cutFoeN;
	int m_cutKind;       // 0=通常 1=戦技 2=陣形 3=召喚
	int m_cutPat;        // 0斬 1重 2旋 3突 4射 5火 6氷 7雷 8闇 9風 10回復 11範囲回復 12支援 13敵
	int m_sumOn, m_sumFx, m_sumHit, m_sumHide;
	float m_sumU, m_sumYaw, m_sumFade;
	V3 m_sumPos, m_sumFrom, m_sumTo;
	Actor m_sumAct;
	V3 m_cutAllyPos[CUT_ALLY_MAX];
	V3 m_cutFoePos[CUT_FOE_MAX];
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
	// 放置デモ。0=なし 1=入り幕 2=再生 3=出幕。マウスは動かさず、盤とメニューの枠だけ進める。
	int m_demo;
	float m_demoT;
	float m_idleT;
	int m_demoAct;
	int m_demoTick;
	float m_demoStep;
	int m_demoStage;
	int m_demoPh;
	SaveData m_demoSave;
};

inline void AppendStatBit(wchar_t* dst, int cap, int stat, int v)
{
	if (!dst || cap < 8 || !v || stat < 0 || stat >= S_COUNT) return;
	wchar_t one[40];
	swprintf_s(one, L" %s%+d", TT(StatNameTx(stat)), v);
	if ((int)wcslen(dst) + (int)wcslen(one) + 1 < cap) wcscat_s(dst, cap, one);
}
inline void GearTipText(const GearDef* g, wchar_t* out, int cap)
{
	if (!out || cap < 2) return;
	out[0] = 0;
	if (!g) return;
	AppendStatBit(out, cap, S_STR, g->str);
	AppendStatBit(out, cap, S_MAG, g->mag);
	AppendStatBit(out, cap, S_DEF, g->def);
	AppendStatBit(out, cap, S_RES, g->res);
	const wchar_t* role = GearRole(g);
	if (role && role[0]) {
		if (out[0] && (int)wcslen(out) + 2 < cap) wcscat_s(out, cap, L"\n");
		if ((int)wcslen(out) + (int)wcslen(role) + 1 < cap) wcscat_s(out, cap, role);
	}
}
inline void AccTipText(int id, wchar_t* out, int cap)
{
	if (!out || cap < 2) return;
	out[0] = 0;
	if (id <= 0) { wcsncpy_s(out, cap, TT(TX_PT_NONE), _TRUNCATE); return; }
	wcsncpy_s(out, cap, AccName(id), _TRUNCATE);
	for (int st = 0; st < S_COUNT; st++) AppendStatBit(out, cap, st, AccStat(id, st));
}
// ホバーした枠の隣に、文章が収まる大きさで説明を出す。
inline void DrawHoverNote(Renderer& r, float ax, float ay, float aw, float ah, float ui, int sw, int sh, const wchar_t* text)
{
	if (!text || !text[0] || sw < 32 || sh < 32) return;
	float px = 13.f * ui;
	if (px < 11.f) px = 11.f;
	float pad = 10.f * ui;
	float tw = 380.f * ui;
	if (tw > (float)sw * 0.46f) tw = (float)sw * 0.46f;
	if (tw > (float)sw - 16.f) tw = (float)sw - 16.f;
	if (tw < 160.f) tw = min(160.f, (float)sw - 16.f);
	float inner = tw - pad * 2.f;
	if (inner < 48.f) inner = 48.f;
	int lines = 1;
	float acc = 0.f;
	for (const wchar_t* p = text; *p; p++) {
		if (*p == L'\n') { lines++; acc = 0.f; continue; }
		acc += px;
		if (acc > inner) { lines++; acc = px; }
	}
	if (lines > 16) lines = 16;
	float th = pad * 2.f + lines * (px * 1.4f);
	float capH = (float)sh * 0.55f;
	if (th > capH) th = capH;
	if (th < 40.f * ui) th = 40.f * ui;
	float gap = 8.f * ui;
	float tx = ax - tw - gap;
	float ty = ay;
	if (tx < 8.f) {
		float right = ax + aw + gap;
		if (right + tw <= (float)sw - 8.f) tx = right;
		else {
			tx = ax;
			if (tx + tw > (float)sw - 8.f) tx = (float)sw - tw - 8.f;
			if (tx < 8.f) tx = 8.f;
			float above = ay - th - gap;
			if (above >= 8.f) ty = above;
			else {
				float below = ay + ah + gap;
				if (below + th <= (float)sh - 8.f) ty = below;
			}
		}
	}
	if (ty + th > (float)sh - 8.f) ty = (float)sh - th - 8.f;
	if (ty < 8.f) ty = 8.f;
	r.HudPanel(tx, ty, tw, th, RGBA8(16, 18, 32, 238), RGBA8(210, 180, 110));
	r.HudTextBox(text, tx + pad, ty + pad * 0.65f, inner, th - pad * 1.3f, px, RGBA8(232, 234, 242));
}
inline int SplitJobPath(Renderer& r, const CharData& c, float maxW, float px, int maxLines, wchar_t lines[][120])
{
	if (maxLines < 1 || maxW < 8.f) return 0;
	if (maxLines > 8) maxLines = 8;
	wchar_t hist[900];
	JobPathText(c, hist, 900);
	if (!hist[0]) return 0;
	wchar_t full[980];
	swprintf_s(full, L"%s  %s", TT(TX_JOB_HIST), hist);
	int n = 0;
	const wchar_t* p = full;
	wchar_t cur[120];
	cur[0] = 0;
	while (*p && n < maxLines) {
		const wchar_t* arrow = wcsstr(p, L" → ");
		size_t ntok = arrow ? (size_t)(arrow - p) : wcslen(p);
		wchar_t tok[100];
		if (ntok > 96) ntok = 96;
		memcpy(tok, p, ntok * sizeof(wchar_t));
		tok[ntok] = 0;
		wchar_t trial[120];
		if (!cur[0]) wcscpy_s(trial, tok);
		else swprintf_s(trial, L"%s → %s", cur, tok);
		if (cur[0] && r.TextWidth(trial, px, 0) > maxW) {
			wcscpy_s(lines[n++], cur);
			if (n >= maxLines) {
				size_t L = wcslen(lines[n - 1]);
				if (L + 1 < 118) wcscat_s(lines[n - 1], L"…");
				cur[0] = 0;
				break;
			}
			wcscpy_s(cur, tok);
		} else wcscpy_s(cur, trial);
		if (!arrow) break;
		p = arrow + 3;
	}
	if (cur[0] && n < maxLines) wcscpy_s(lines[n++], cur);
	else if (cur[0] && n > 0) {
		size_t L = wcslen(lines[n - 1]);
		if (L + 1 < 118) wcscat_s(lines[n - 1], L"…");
	}
	return n;
}
inline float DrawJobPath(Renderer& r, const CharData& c, float x, float y, float w, float px, DWORD col, int maxLines)
{
	wchar_t lines[8][120];
	int n = SplitJobPath(r, c, w, px, maxLines, lines);
	float lh = px + 4.f;
	for (int i = 0; i < n; i++)
		r.HudText(lines[i], x, y + lh * ((float)i + 0.5f), px, col, 0, 0);
	return n * lh;
}

} // namespace otac
