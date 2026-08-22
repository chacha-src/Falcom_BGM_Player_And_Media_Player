#pragma once
// Soft3D レース／迷路用 PCM 合成効果音（曲のストリーミングバッファとは別の DS セカンダリ）

enum {
	S3SFX_STEP = 0,
	S3SFX_ITEM = 1,
	S3SFX_KEY = 2,
	S3SFX_DOOR = 3,
	S3SFX_STAIR = 4,
	S3SFX_PORTAL = 5,
	S3SFX_COUNT = 6,
	S3SFX_GO = 7,
	S3SFX_LAP = 8,
	S3SFX_FINISH = 9,
	S3SFX_PODIUM = 10,
	S3SFX_COURSEOUT = 11,
	S3SFX_BOOST = 12,
	S3SFX_SLOW = 13,
	S3SFX_HIT = 14,
	S3SFX_WRONG = 15,
	S3SFX_GAMEOVER = 16,
	S3SFX_BUMP = 17,
	S3SFX_LOCKED = 18,
	S3SFX_SPIKE = 19,
	S3SFX_SLIME = 20,
	S3SFX_ICE = 21,
	S3SFX_DARK = 22,
	S3SFX_GOAL = 23,
	S3SFX_N = 24
};

void Soft3DSfxEnsure(HWND hwnd);
void Soft3DSfxShutdown();
void Soft3DSfxSetListener(float x, float y, float z, float yaw);
void Soft3DSfxEngine(int slot, int alive, float x, float y, float z,
	float speed, float throttle, int colorIdx, int isPlayer);
void Soft3DSfxOneShot(int kind, float x, float y, float z);
void Soft3DSfxOneShotP(int kind, float x, float y, float z, int param);
void Soft3DSfxUi(int kind, int param=0);
void Soft3DSfxPump();
