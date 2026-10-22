#pragma once
// おまけタクティクス: ルール・マップ生成・経路・戦闘計算・AI・セーブ（描画非依存）

#include <windows.h>
#include <math.h>
#include <string.h>
#include "OmakeTacticsText.h"

namespace otac {

// 実行中に伸びない固定配列（new/delete を避ける）
template<typename T, int CAP>
struct Fix {
	T v[CAP];
	int n;
	Fix() : n(0) {}
	void clear() { n = 0; }
	int size() const { return n; }
	bool empty() const { return n == 0; }
	T* begin() { return v; }
	T* end() { return v + n; }
	const T* begin() const { return v; }
	const T* end() const { return v + n; }
	T* data() { return v; }
	T& operator[](int i) { return v[i]; }
	const T& operator[](int i) const { return v[i]; }
	T& back() { return v[n - 1]; }
	const T& back() const { return v[n - 1]; }
	void push_back(const T& a) { if (n < CAP) v[n++] = a; }
	void pop_back() { if (n > 0) n--; }
	void eraseAt(int i) {
		if (i < 0 || i >= n) return;
		for (int k = i; k < n - 1; k++) v[k] = v[k + 1];
		n--;
	}
};

// ---------------- 数学（行ベクトル・左手系） ----------------
struct V3 { float x, y, z; };
inline V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline V3 operator*(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float Len(V3 a) { return sqrtf(Dot(a, a)); }
inline V3 Norm(V3 a) { float l = Len(a); return l > 1e-8f ? a * (1.f / l) : v3(0, 1, 0); }
inline V3 Lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }
inline float Clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline int Clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
inline float Lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float Smooth01(float t) { t = Clampf(t, 0, 1); return t * t * (3 - 2 * t); }
float WrapAngle(float a);

struct Q4 { float x, y, z, w; };
Q4 QNlerp(const Q4& a, const Q4& b, float t);
Q4 QAxis(V3 axis, float ang);
Q4 QMul(const Q4& a, const Q4& b);

struct M4 { float m[16]; };
M4 MIdent();
M4 MMul(const M4& a, const M4& b);              // a の後に b（行ベクトル）
M4 MTRS(V3 t, const Q4& r, V3 s);
M4 MTrans(float x, float y, float z);
M4 MScale(float x, float y, float z);
M4 MRotY(float a);
M4 MRotX(float a);
M4 MRotZ(float a);
M4 MLookAtLH(V3 eye, V3 at, V3 up);
M4 MPerspLH(float fovY, float aspect, float zn, float zf);
M4 MOrthoLH(float w, float h, float zn, float zf);
M4 MInverse(const M4& a);
V3 MXformPt(const M4& m, V3 p);
V3 MXformDir(const M4& m, V3 d);
M4 MTranspose(const M4& a);

// ---------------- 乱数 ----------------
struct Rng {
	DWORD s;
	explicit Rng(DWORD seed = 1) : s(seed ? seed : 0x9E3779B9u) {}
	DWORD Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	int Range(int lo, int hi) { return hi <= lo ? lo : lo + (int)(Next() % (DWORD)(hi - lo + 1)); }
	float F() { return (Next() & 0xFFFFFF) / 16777216.f; }
	float F(float a, float b) { return a + (b - a) * F(); }
	bool Chance(int pct) { return (int)(Next() % 100) < pct; }
};

// ---------------- 地形 ----------------
enum Terrain {
	T_PLAIN, T_FOREST, T_HILL, T_MOUNTAIN, T_WATER, T_CHASM, T_LAVA,
	T_SAND, T_SNOW, T_SWAMP, T_ROAD, T_BRIDGE, T_RUINS, T_ROCK, T_DOOR, T_VOID, T_COUNT
};
struct TerrainDef {
	int nameTx;
	int costFoot;   // 99=通行不可
	int flyOk;      // 飛行可
	int defPct;
	int avoPct;
};
const TerrainDef& TerrainInfo(int t);

enum Theme { TH_PLAINS, TH_CANYON, TH_FOREST, TH_SNOW, TH_DESERT, TH_VOLCANO, TH_SWAMP, TH_RUINS, TH_COUNT };
int ThemeNameTx(int th);

enum { MAPN = 64, MAXH = 12 };
const float kHStep = 0.4f;

enum PropKind {
	PK_NONE, PK_TREE, PK_TREES, PK_PINE, PK_DEAD, PK_ROCK, PK_MOUNT, PK_LILY, PK_PILLAR,
	PK_GRAVE, PK_TENT, PK_BARREL, PK_CRATE, PK_LANTERN, PK_PUMPKIN, PK_FLAG, PK_HILLTREES
};

enum Gimmick { GM_NONE = 0, GM_HEAL = 1, GM_MIGHT = 2, GM_WARP = 3, GM_CHEST = 4 };

struct Cell {
	BYTE t;      // Terrain
	BYTE h;      // 高さ段
	BYTE prop;   // PropKind
	BYTE var;    // 小物のバリエーション／回転
	BYTE gimm;   // Gimmick
	BYTE gparam; // ワープ組番号
};

enum { MAX_GROUPS = 200, GRP_CAP = 15 };

struct MapGroup {
	short ax, az;
	BYTE n;
	BYTE boss;
	short x[GRP_CAP];
	short z[GRP_CAP];
};

struct Map {
	int theme;
	int stage;
	DWORD seed;
	Cell c[MAPN][MAPN];   // [z][x]
	int pStartX[10], pStartZ[10]; // MAX_PARTY と一致。static_assert で固定
	int eStartX[10], eStartZ[10]; // カメラ用。先頭は最寄りの敵拠点
	int nP, nE;
	MapGroup grp[MAX_GROUPS];
	int nGrp;
	Cell& At(int x, int z) { return c[z][x]; }
	const Cell& At(int x, int z) const { return c[z][x]; }
	static bool In(int x, int z) { return x >= 0 && z >= 0 && x < MAPN && z < MAPN; }
	float TopY(int x, int z) const;      // セル上面のワールド高さ
	float StandY(int x, int z) const;    // ユニットの足元
};

void GenerateMap(Map& m, int stage, DWORD seed);

// ---------------- 能力値・クラス・スキル ----------------
enum Stat {
	S_HP, S_MP, S_STR, S_DEF, S_MAG, S_RES, S_AGI, S_DEX, S_LUK, S_MOV,
	S_JMP, S_HIT, S_AVO, S_CRT, S_GRD, S_RCV, S_MPR, S_CPL, S_RNG, S_PEN,
	S_COUNT
};
static_assert(S_COUNT == 20, "能力は 20 種");
int StatNameTx(int s);

enum MoveType { MV_FOOT, MV_FLY };
enum Group { G_MELEE, G_SWIFT, G_MAGIC, G_HOLY, G_COUNT };
enum WeaponLook {
	WL_SWORD_SHIELD, WL_AXE2, WL_CROSSBOW2, WL_KNIVES, WL_STAFF, WL_WAND_BOOK, WL_SWORD_BADGE,
	WL_SWORD_BOOK, WL_AXES, WL_SWORD2, WL_CROSS_KNIFE, WL_SWORD_SPIKE
};
enum AttackStyle { AS_SLASH, AS_CHOP2, AS_STAB, AS_SHOOT, AS_SHOOT1, AS_CAST, AS_SPIN, AS_PUNCH, AS_BITE };

enum ClassId {
	C_SOLDIER, C_FIGHTER, C_ARCHER, C_THIEF, C_MAGE, C_CLERIC,
	C_PALADIN, C_RUNEKNIGHT, C_BERSERKER, C_SNIPER, C_ASSASSIN, C_WIZARD, C_BISHOP, C_SKYKNIGHT,
	C_HERO, C_ARCHMAGE, C_SAINT, C_PHANTOM, C_DRAGONLORD,
	C_ALCHEMIST, C_ARCHALCHEMIST,
	C_PLAYER_COUNT
};
enum { TAC_LV_MAX = 999, TAC_STAGE_MAX = 9999, TAC_RANK_LINES = 6, TAC_RANK_N = 7 };

enum { LEARN_N = 10 };
struct LearnDef { int lv; int skill; };
struct ClassDef {
	int nameTx;
	int descTx;
	int tier;          // 1..3
	int magic;         // 0=戦士系 1=魔法系
	int group;
	int moveType;
	int jump;
	int rMin, rMax;    // 通常攻撃射程
	int magAttack;     // 通常攻撃が魔法ダメージ
	int weaponPow;
	int weaponLook;
	int attackStyle;
	int base[S_COUNT];
	int grow[S_COUNT]; // 成長率 %（HP/MP は ×10 で 1 レベルあたり期待値）
	LearnDef learn[LEARN_N];
};
const ClassDef& ClassInfo(int c);
int PromoteLevel(int tier);
int ExpToNext(int lv);
int NextPromoteClass(int cls);
bool IsUltimateSkill(int skill);
enum { IT_ORE = 0, IT_HERB, IT_CRYSTAL, IT_SHARD, IT_ELIXIR, IT_MIGHT, IT_DELAY, IT_COUNT = 7, IT_RAISE = 7 };
enum { IX_FULL = 0, IX_ATK, IX_DEF, IX_AGI, IX_SLOW, IX_VENOM, IX_CURE, IX_ETHER, IX_N };

enum SkillKind { SK_PHYS, SK_MAGIC, SK_HEAL, SK_BUFF, SK_PASSIVE };
enum Element { EL_NONE, EL_FIRE, EL_ICE, EL_THUNDER, EL_HOLY, EL_DARK, EL_WIND, EL_EARTH, EL_POISON };
enum SkillId {
	SKL_POWER, SKL_DOUBLE, SKL_WHIRL, SKL_PIERCE, SKL_STITCH, SKL_VITAL, SKL_BASH, SKL_DRAGONFANG,
	SKL_HOLYBLADE, SKL_ARROWRAIN, SKL_ASSASSINATE, SKL_BRAVE,
	SKL_FIRE, SKL_BLIZZARD, SKL_THUNDER, SKL_METEOR, SKL_RUNEBLADE, SKL_HOLY, SKL_DARKNESS,
	SKL_HEAL, SKL_HIGHHEAL, SKL_HEALALL, SKL_PROTECT, SKL_HASTE, SKL_BLESS,
	SKL_P_EVADE, SKL_P_MIGHT, SKL_P_SPELL, SKL_P_IRON, SKL_P_SWIFT, SKL_P_REGEN, SKL_P_CRIT,
	SKL_E_VENOM, SKL_E_BREATH, SKL_E_CRUSH, SKL_E_DRAIN, SKL_E_SPIT, SKL_E_QUAKE,
	SKL_REFRESH,
	SKL_CLEAVE, SKL_SNIPE, SKL_VENOMEDGE, SKL_SANCTUARY, SKL_SMITE, SKL_GRAVITY, SKL_WARCRY,
	SKL_RENEW, SKL_MIRROR, SKL_GODSLASH, SKL_RUIN, SKL_APOC, SKL_TRANSMUTE,
	SKL_ART0,
	SKL_ARTN = SKL_ART0 + 104,
	SKL_RAISE,
	SKL_SHOWER, SKL_SHINE, SKL_CRESCENT, SKL_GRANDSLASH, SKL_STORM, SKL_FROSTFIELD, SKL_THUNDERFIELD, SKL_NOVA,
	SKL_COUNT
};
struct SkillDef {
	int nameTx, descTx;
	int kind;
	int mp;
	int power;       // 物理: 倍率%、魔法/回復: 威力
	int rMin, rMax;  // 0=自分中心
	int area;        // 範囲半径（0=単体）
	int element;
	int hits;
	int critBonus;
	int effect;      // 追加効果
	int enemyOnly;
};
enum SkillEffect { EF_NONE, EF_SLOW, EF_DEFDOWN, EF_POISON, EF_DRAIN, EF_IGNDEF, EF_BUFF_DEF, EF_BUFF_AGI, EF_BUFF_ATK, EF_REACT, EF_RAISE };
const SkillDef& SkillInfo(int s);

// ---------------- キャラクタ（セーブ対象） ----------------
enum { MAX_ROSTER = 100, MAX_PARTY = 10, MAX_ENEMY = 640, NAME_LEN = 16 };
static_assert(MAX_PARTY == 10, "Map::pStart は 10 枠");
enum Body { B_KNIGHT, B_BARBARIAN, B_MAGE, B_ROGUE, B_ROGUE_HOOD, B_COUNT };
enum { HAIR_STYLES = 6, HAIR_COLORS = 10, OUTFIT_COLORS = 10 };

#pragma pack(push, 4)
struct CharData {
	wchar_t name[NAME_LEN];
	int used;
	int gender;      // 0=男 1=女
	int cls;
	int lv;
	int exp;
	int stat[S_COUNT];     // 最大値（HP/MP は最大）
	unsigned __int64 skills;
	int body, hair, hairColor, outfit, hat, cape;
	int kills, battles;
	DWORD uid;
	int reserved[8];
};
#pragma pack(pop)

void CharRecalcInit(CharData& c);                  // 新規作成時の初期能力
void CharRecalcProgress(CharData& c);              // 現在Lvの期待値で能力を組み直す
int  CharLevelUp(CharData& c, Rng& r, int gains[S_COUNT]);   // 戻り: 新規習得スキル id or -1
int  CharLearnAt(const CharData& c, int lv);       // その Lv で覚えるスキル
void CharGrantReached(CharData& c);                // 現在Lvまでに習得表へ載った技を付与
bool CharHasSkill(const CharData& c, int s);
void CharGiveSkill(CharData& c, int s);

// 進化
struct EvoOption { int target; int needGroup; };   // needGroup=-1 で不問
int EvoOptions(int cls, EvoOption out[4]);
bool EvoMainEligible(const CharData& c);
bool EvoPartnerEligible(const CharData& main, const CharData& p);
int EvoPartnersNeeded(const CharData& main);
bool EvoGroupSatisfied(int needGroup, const CharData* partners, int n);
void EvoApply(CharData& main, int target, const CharData* partners, int n);

// ---------------- 敵 ----------------
enum MonsterId {
	M_SLIME, M_SKEL_MINION, M_SKEL_WARRIOR, M_SKEL_ARCHER, M_BAT, M_LICH, M_POISON_SLIME,
	M_GOLEM, M_WYVERN, M_DARK_KNIGHT, M_DRAGON, M_COUNT
};
enum Look { LK_KAYKIT, LK_SLIME, LK_BAT, LK_WYVERN, LK_GOLEM, LK_MESH };
enum Gait { GAIT_BIPED, GAIT_TROT, GAIT_BOUNCE, GAIT_FLY, GAIT_SCUTTLE, GAIT_SLITHER, GAIT_WADDLE, GAIT_STOMP, GAIT_FISH };
struct MonsterDef {
	int nameTx;
	int look;          // Look
	int model;         // KayKit モデル index（LK_KAYKIT）
	int weaponLook;    // 敵用装備 0..
	int moveType, jump;
	int rMin, rMax, magAttack, weaponPow, attackStyle;
	int base[S_COUNT];
	int grow[S_COUNT];
	int skills[3];
	int minStage;
	int weight;
	float scale;
	DWORD tint;        // 0x00RRGGBB 乗算
	int maxStage;      // 0=上限なし。これを超えたステージには出さない
	int nameB;         // 0未満なら nameTx だけ。0以上なら「形容 + 種族」
};
const MonsterDef& MonsterInfo(int m);
const wchar_t* MonsterLabel(int m);
int MonsterGait(int monster);

// ---------------- 戦闘中ユニット ----------------
enum Team { TEAM_PLAYER, TEAM_ENEMY };
struct Unit {
	int alive;
	int team;
	int rosterIdx;       // 味方: ロースター番号
	int monster;         // 敵: MonsterId
	int boss;
	CharData ch;         // 戦闘中の能力（敵も同形式）
	int hp, mp;
	int x, z;
	int ct;
	float face;          // 向き（ラジアン）
	int buffDef, buffAgi, buffAtk, poison, defDown;   // 残りターン
	int mulTurns;        // マス効果: この間ステータス倍
	int alert;           // 1=行動順に入る（味方は常に 1。敵拠点は接近で起床）
	int grp;             // 敵拠点番号（味方は -1）
	int expGained;
	int levelsGained;
	int skillLearned[4];
	int nLearned;
	int killed;          // 戦闘不能
	int cp;              // 0..200。ステージをまたいで保持
	int craftEq[2];      // 0=空。戦技 id（CRAFT_BASE+）
	int accEq[2];        // 0=空。装飾 id
	// 表示用
	float px, py, pz;
	float hpShow;
	float dissolve;
	float flash;
	float hover;
};

int UnitStat(const Unit& u, int s);     // バフ込み
int UnitMove(const Unit& u);
int UnitJump(const Unit& u);
int UnitMoveType(const Unit& u);
int UnitRangeMin(const Unit& u);
int UnitRangeMax(const Unit& u);
int UnitMagAttack(const Unit& u);
int UnitWeaponPow(const Unit& u);
int UnitAttackStyle(const Unit& u);
int UnitNameTx(const Unit& u);          // 敵の種族名など（味方は -1）
const wchar_t* UnitName(const Unit& u);
bool UnitHasSkill(const Unit& u, int s);

void MakeEnemy(Unit& u, int monster, int lv, int boss, Rng& r);

// ---------------- 経路 ----------------
struct Reach {
	short cost[MAPN][MAPN];   // -1=到達不可
	short from[MAPN][MAPN];   // 前セル (z*MAPN+x)
	BYTE  canStop[MAPN][MAPN];
};
struct Battle;
void ComputeReach(const Battle& b, int ui, Reach& out);
int  ReachPath(const Reach& r, int sx, int sz, int tx, int tz, int* outXZ, int maxN);   // 戻り: 点数（開始含む）
bool CanStep(const Map& m, int fromX, int fromZ, int toX, int toZ, int moveType, int jump);
bool LineOfSight(const Map& m, int ax, int az, int bx, int bz);
bool InAttackRange(const Battle& b, int ax, int az, int rMin, int rMax, int tx, int tz, bool needLos);
int  Dist(int ax, int az, int bx, int bz);

// ---------------- 戦闘計算 ----------------
struct Strike {
	int attacker, defender;
	int skill;         // -1=通常攻撃
	int dmg;           // 回復時は負
	int hit;           // 1=命中
	int crit;
	int kill;
	int counter;       // 反撃
	int drain;
};
struct Forecast {
	int dmg, hit, crit;     // 1 回あたり
	int hits;               // 攻撃回数（追撃込み）
	int cdmg, chit, ccrit, chits;   // 反撃
	int canCounter;
	int face;               // 0=正面 1=側面 2=背面
	int tri;                // 武器相性 -1/0/+1
	int bowFly;             // 弓対飛行
	int delay;              // 命中時に削る CT
	int follow;             // 武器射程にいる味方の人数。-1=なし（実戦は各自 1/2）
	int fdmg;
};
void CalcForecast(const Battle& b, int a, int d, int skill, Forecast& f);
int  ResolveAttack(Battle& b, int a, int d, int skill, Rng& r, Strike* out, int maxOut, int cx, int cz);
int  AppendAssists(Battle& b, Rng& r, Strike* out, int n, int maxOut);
int  SkillTargets(const Battle& b, int caster, int skill, int cx, int cz, int* out, int maxOut);
bool SkillUsable(const Battle& b, int ui, int skill);
int  SkillCastRangeMin(const Unit& u, int skill);
int  SkillCastRangeMax(const Unit& u, int skill);
int  ExpForAction(const Unit& a, const Unit& d, int killed, int stage);

// ---------------- 戦闘全体 ----------------
enum { MAX_UNITS = MAX_PARTY + MAX_ENEMY };
struct Battle {
	Map map;
	Unit u[MAX_UNITS];
	int nUnits;
	int stage;
	int turnCount;
	int occ[MAPN][MAPN];      // ユニット番号+1
	void RebuildOcc();
	int UnitAt(int x, int z) const { return Map::In(x, z) ? occ[z][x] - 1 : -1; }
	int AliveCount(int team) const;
	int NextActor();          // CT を進めて次の行動者を返す
	// acting の手番終了後（CT を cost 消費）の並び。insertAt はそのユニットが次に入る位置
	int PredictOrder(int* out, int n, int acting = -1, int cost = 1000, int* insertAt = NULL) const;
};

void SetupBattle(Battle& b, int stage, DWORD seed, const CharData* party, const int* rosterIdx, int nParty);
void FinishAction(Battle& b, int ui, int moved, int acted);
int  TurnStartEffects(Battle& b, int ui, int* healOut, int* poisonOut);
void WakeGroup(Battle& b, int grp);
void WakeNearPlayers(Battle& b, int rad);
int  ApplyTile(Battle& b, int ui);   // 停止マスのギミック。Gimmick を返す。0=なし
bool CanOpenDoor(const Unit& u);
int  AdjDoor(const Battle& b, int ui, int* ox, int* oz);
void OpenDoor(Battle& b, int x, int z);
bool CanTerrace(const Battle& b, int ui, int tx, int tz);
void DoTerrace(Battle& b, int ui, int tx, int tz);
bool CanShove(const Battle& b, int ui, int ti, int* outX, int* outZ);
void DoShove(Battle& b, int ui, int ti);

// ---------------- AI ----------------
struct AiPlan {
	int moveX, moveZ;
	int target;       // -1=攻撃しない
	int skill;        // -1=通常
	int castX, castZ;
};
void AiDecide(const Battle& b, int ui, AiPlan& p, Rng& r);

// ---------------- セーブ ----------------
#pragma pack(push, 4)
struct SaveData {
	DWORD magic;
	int version;
	CharData roster[MAX_ROSTER];
	int party[MAX_PARTY];      // ロースター番号（-1=空）
	int stageCleared;          // 最大クリア
	int stageSel;
	int winX, winY, winW, winH;
	int camSpeed;
	int quality;               // 0=軽量 1=標準 2=高
	int showGrid;
	int battleAnim;            // 0=フル 1=短縮
	DWORD uidNext;
	int winSizeVer;            // 3 未満なら保存サイズを使わず、作業領域の7割で開く
	int itemN[8];              // 旧 reserved の先頭。鉱石・薬草・結晶・欠片・回復薬・剛力薬・遅延弾
	int bgmVol;                // 0..10。optInit が 0 の旧セーブは読込時に既定
	int seVol;
	int optInit;               // 1=音量を初期化済み
	int gold;
	int gearN;
	int gearId[12];            // 所持武具。0 は空。番号は Gear 表の 1 始まり
	int reserved[4];
	int partyCap;              // 5..10。店の編成拡張券
	int rosterCap;             // 20,40,60,80,100。店の名簿の頁
};
// version 1 は名簿 20・編成 5。version 2 は能力 10 種。version 3 が能力 20 種。読込は全部受け、書き出しは 3。
#pragma pack(pop)
void SaveDefaults(SaveData& s);
bool SaveLoad(SaveData& s);
bool SaveWrite(const SaveData& s);

enum { GEAR_STASH = 12 };
struct GearDef {
	int line;     // 0剣 1斧 2弓 3短 4杖 5聖
	int kind;     // 0=武器 1=防具
	int price;
	int str, mag, def, res;
	int stage;
	int mesh0, mesh1;
	int joint0, joint1;
	const char* att0;
	const char* att1;
};
int GearCount();
const GearDef* GearOf(int id);
const wchar_t* GearName(int id);
const wchar_t* GearRole(const GearDef* g);
const wchar_t* GoodDesc(int i);
int ClassLine(int cls);
int ItemExtraGet(const SaveData& s, int i);
void ItemExtraAdd(SaveData& s, int i, int d);
int ConsumableCount(const SaveData& s);
bool StashAdd(SaveData& s, int id);
void StashRemoveAt(SaveData& s, int index);
bool GearBuy(SaveData& s, int index);
bool GearSellAt(SaveData& s, int index);
void GearEquip(CharData& c, SaveData& s, int stashIndex);
int GearTryDrop(SaveData& s, int stage, int monster, Rng& r);
int MonsterDropLine(int monster);
bool DropConsumable(SaveData& s, int monster, Rng& r);
int DropTrophy(int monster, int boss, Rng& r, wchar_t* name, int nameN, int* goldOut);
int TrophyList(int* slots, int max);
bool TrophySell(SaveData& s, int slot);
void TrophyNameOf(int slot, wchar_t* name, int nameN, int* count, int* price);
struct GoodDef { int price; int item; int extra; int nameTx; int self; };
int GoodCount();
const GoodDef* GoodAt(int i);
int GoodHave(const SaveData& s, int i);
bool GoodBuy(SaveData& s, int i);
bool GoodTake(SaveData& s, int i);

enum { CRAFT_BASE = 100000, CRAFT_NORM = 15, CRAFT_SUPER_N = 6, CRAFT_PER_LINE = 21, CRAFT_LINES = 6, CRAFT_N = 126, CP_MAX = 200 };
bool IsCraftId(int skill);
bool IsCraftSuper(int skill);
int CraftCpCost(int index);
const wchar_t* CraftName(int skill);
const wchar_t* CraftShapeName(int area);
const wchar_t* CraftDesc(int skill);
int CraftSuperUnlocked(int cls);
int CraftSuperUnlockedBest(const SaveData& s);
int CraftLineOf(int skill);
int StoneCount();
void StoneAdd(int d);
bool BeadHave(int index);
int BeadCount(int index);
int CraftStoneCost(int index);
int CraftSuperNeed(int index);
bool BeadBuy(const SaveData& s, int index);
bool BeadSell(int index);
int AccCount();
const wchar_t* AccName(int id);
int AccPrice(int index);
int AccStat(int id, int stat);
bool AccBuy(SaveData& s, int index);
void EquipNextAcc(CharData& c, int slot);
void EquipNextBead(CharData& c, int slot);
int EquippedAccId(const CharData& c, int slot);
int EquippedBeadId(const CharData& c, int slot);
int AccSpare(const CharData& c, int slot, int index);
int BeadSpare(const CharData& c, int slot, int index);
bool EquipAccId(CharData& c, int slot, int id);
bool EquipBeadId(CharData& c, int slot, int bead);
const wchar_t* EquippedAccName(const CharData& c, int slot);
const wchar_t* EquippedBeadName(const CharData& c, int slot);
void CraftBindUnit(Unit& u);
void CraftSaveUnit(const Unit& u);
void CraftLoad();
void CraftSave();
void CraftPrune(const SaveData& s);
bool DropCraftLoot(int monster, int boss, Rng& r, wchar_t* name, int nameN);
int BattleHeightSteps(const Battle& b, int ax, int az, int dx, int dz);

void RandomName(wchar_t* out, int gender, Rng& r);

} // namespace otac
