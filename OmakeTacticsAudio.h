#pragma once
// おまけタクティクスの効果音（リソース内 Ogg）と、exe 横 tacticsbgm フォルダの BGM。
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
	TS_N
};

// tacticsbgm 内の曲。ファイルが無ければ TacBgmScene が tactics.ogg を使う。
enum TacBgmId {
	TAC_BGM_HUB = 0, // tactics.ogg（拠点・開始前）
	TAC_BGM_GAME,    // tacticsgame.ogg
	TAC_BGM_WIN,     // tacticsgamewin.ogg（勝利目前）
	TAC_BGM_SHOP,    // tacticsshop.ogg
	TAC_BGM_MAKE,    // tacticsmake.ogg（作成・編成・進化）
	TAC_BGM_N
};

void TacAudioStart(void* hwnd);
void TacAudioStop();
void TacAudioPump();
void TacSfx(int id);
void TacAudioSetVol(int bgm, int se);
void TacBgmScene(int scene);
