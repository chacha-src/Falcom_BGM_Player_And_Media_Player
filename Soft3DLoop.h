#pragma once

// 迷路／レースのシミュ＋DX11 を UI/音スレッドから切り離す。
// immediate context は CS で直列化（生成・リサイズ・破棄と Tick の競合防止）。

enum {
	S3PB_TEMPO = 1,   // lParam = 表示%
	S3PB_PITCH = 2,   // lParam = slider 0..400
	S3PB_VOL = 3,     // lParam = delta %
	S3PB_NEXT = 4,
	S3PB_PREV = 5,
	S3PB_PLAY = 6,
	S3PB_REVERB = 7,  // lParam = delta
	S3PB_EQ_BUMP = 8, // lParam = band | ((delta+128)<<8)
	S3PB_EQ_FLAT = 9, // lParam = step
	S3PB_XFADE = 10,
	S3PB_RANDOM = 11
};

#ifndef WM_OGG_S3_PLAYBACK
#define WM_OGG_S3_PLAYBACK (WM_APP + 105)
#endif

#ifndef WM_S3_STATUS_TEXT
#define WM_S3_STATUS_TEXT (WM_APP + 81)
#endif
#ifndef WM_S3_DX_REINIT
#define WM_S3_DX_REINIT (WM_APP + 82)
#endif

void Soft3DLoopEnsure();
void Soft3DLoopRequestStop();
void Soft3DLoopStopJoin();
void Soft3DDxLock();
void Soft3DDxUnlock();
void Soft3DPostPlayback(int op, int arg);
void Soft3DPostPlayIfStopped();
void Soft3DPostStatusText(HWND hwnd, volatile LONG* posted, LPCTSTR text);
void Soft3DPostDxReinit(HWND hwnd);
void Soft3DDxReinitAck();
void Soft3DDeferPresent(int on);
int Soft3DPresentShouldSkip();
int Soft3DLoopStopping();

struct Soft3DDxGuard {
	Soft3DDxGuard() { Soft3DDxLock(); }
	~Soft3DDxGuard() { Soft3DDxUnlock(); }
};

struct Soft3DDeferPresentGuard {
	Soft3DDeferPresentGuard() { Soft3DDeferPresent(1); }
	~Soft3DDeferPresentGuard() { Soft3DDeferPresent(0); }
};
