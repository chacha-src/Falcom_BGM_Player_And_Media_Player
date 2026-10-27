#pragma once
// おまけタクティクスの効果音（短い MML を FM / SSG で合成）と、exe 横 tacticsbgm フォルダの BGM。
// 場面用の曲が無ければ tactics.ogg を使う。プレイリストには載せない。

enum TacSfxId {
	TS_CURSOR = 0,
	TS_CONFIRM,
	TS_CANCEL,
	TS_ERROR,
	TS_OPEN,
	TS_TURN_P,
	TS_TURN_E,
	TS_STEP,
	TS_SWING,
	TS_BOW,
	TS_CAST,
	TS_HIT,
	TS_CRIT,
	TS_MISS,
	TS_HEAL,
	TS_BUFF,
	TS_DEATH,
	TS_WIN,
	TS_LOSE,
	TS_LEVEL,
	TS_OK_BASE,   // 拠点の決定
	TS_OK_SHOP,   // 購入
	TS_OK_BATTLE, // 戦闘コマンド
	TS_OK_MAKE,   // 作成・編成
	TS_OK_EQUIP,  // 装備の装着
	TS_OK_SELL,   // 売却
	TS_OK_CRAFT,  // 調合
	TS_OK_MAP,    // 地形の切り替え
	TS_OK_MENU,   // 設定
	TS_CHEST,     // 宝箱
	TS_DOOR,      // 扉
	TS_HURT,      // 味方が被弾
	TS_CAST_FIRE,
	TS_CAST_ICE,
	TS_CAST_THUNDER,
	TS_CAST_HOLY,
	TS_CAST_DARK,
	TS_CAST_WIND,
	TS_CAST_EARTH,
	TS_CAST_POISON,
	TS_CAST_HEAL,
	TS_CAST_BUFF,
	TS_CAST_RAISE,
	TS_SW_CHOP,
	TS_SW_STAB,
	TS_SW_PUNCH,
	TS_SW_SPIN,
	TS_SW_BITE,
	TS_CRAFT_SW,
	TS_HIT_FIRE,
	TS_HIT_ICE,
	TS_HIT_THUNDER,
	TS_HIT_HOLY,
	TS_HIT_DARK,
	TS_HIT_WIND,
	TS_HIT_EARTH,
	TS_HIT_POISON,
	TS_HIT_RANGE,
	TS_HIT_STAB,
	TS_HIT_PUNCH,
	TS_CRAFT_HIT,
	TS_CRAFT_ULT,
	TS_MISS_MAGIC,
	TS_MISS_RANGE,
	TS_RAISE,
	TS_CLEANSE,
	TS_POISON,
	TS_SLEEP,
	TS_STUN,
	TS_BLOCK,
	TS_WARP,
	TS_LEARN,
	TS_PROMOTE,
	TS_GET,
	TS_START,
	TS_GUTS,
	TS_CONFUSE,
	TS_SLOW,
	TS_DEFDOWN,
	TS_FALL,      // 味方が倒れた
	TS_N
};

// tacticsbgm 内の曲。地形曲は tacticsgame.ogg、それ以外で無いファイルは tactics.ogg に戻す。
enum TacBgmId {
	TAC_BGM_HUB = 0, // tactics.ogg（拠点・開始前）
	TAC_BGM_GAME,    // tacticsgame.ogg（地形曲が無いときの戦闘）
	TAC_BGM_WIN,     // tacticsgamewin.ogg（勝利目前）
	TAC_BGM_SHOP,    // tacticsshop.ogg
	TAC_BGM_MAKE,    // tacticsmake.ogg（作成・編成・進化）
	TAC_BGM_PLAINS,  // tacticsgameplains.ogg
	TAC_BGM_CANYON,  // tacticsgamecanyon.ogg
	TAC_BGM_FOREST,  // tacticsgameforest.ogg
	TAC_BGM_SNOW,    // tacticsgamesnow.ogg
	TAC_BGM_DESERT,  // tacticsgamedesert.ogg
	TAC_BGM_VOLCANO, // tacticsgamevolcano.ogg
	TAC_BGM_SWAMP,   // tacticsgameswamp.ogg
	TAC_BGM_RUINS,   // tacticsgameruins.ogg
	TAC_BGM_DANGER,  // tacticsgamedanger.ogg（味方HPが低いあいだ）
	TAC_BGM_CLEAR,   // tacticsgameclear.ogg（勝利）
	TAC_BGM_N
};

void TacAudioStart(void* hwnd);
void TacAudioStop();
void TacAudioPump();
// 1 なら供給は TIME_CRITICAL の専用スレッド。UI タイマーは呼ばない。
int TacAudioOnThread();
void TacSfx(int id);
// 魔法の詠唱（属性ごと）。
void TacSfxCast(int element);
// 攻撃・詠唱のモーション音。skill<0 は通常攻撃。
void TacSfxMotion(int skill, int kind, int element, int style);
// 命中音。ally なら味方被弾。
void TacSfxHit(int skill, int kind, int element, int style, int ally);
void TacSfxMiss(int kind, int style);
void TacSfxStatus(int effect, int blocked);
void TacAudioSetVol(int bgm, int se);
void TacBgmScene(int scene);
int TacBgmHas(int scene);
void TacBgmTempo(float goal);
void TacBgmFadeOut();
