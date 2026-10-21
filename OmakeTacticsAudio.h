#pragma once
// おまけタクティクスの効果音（リソース内 Ogg）と、exe 横の tactics.ogg BGM。
// プレイリストには載せない。

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

void TacAudioStart(void* hwnd);
void TacAudioStop();
void TacAudioPump();
void TacSfx(int id);
void TacAudioSetVol(int bgm, int se);
