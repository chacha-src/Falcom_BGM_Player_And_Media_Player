#pragma once
// MIMPI / TMIDI WRD スクリプト。PC-98 テキスト 80x25 + グラフィック 640x400。
#include "MagImage.h"
#include <vector>

struct WrdCmd {
	int tick48;     // 4分=48。@WAIT 系の絶対時刻
	int kind;       // WRD_xxx
	int a, b, c, d, e, f, g;
	char text[256]; // SJIS
	wchar_t wtext[128];
	wchar_t path[MAX_PATH];
};

enum {
	WRD_NOP = 0,
	WRD_TEXT,
	WRD_WAIT,       // already baked into tick48
	WRD_LOCATE,
	WRD_COLOR,
	WRD_TON,
	WRD_ESC,
	WRD_TCLS,
	WRD_SCROLL,
	WRD_GINIT,
	WRD_GCLS,
	WRD_GSCREEN,
	WRD_GON,
	WRD_GLINE,
	WRD_GCIRCLE,
	WRD_PAL,
	WRD_PALREV,
	WRD_FADE,
	WRD_GMODE,
	WRD_GMOVE,
	WRD_MAG,
	WRD_WMODE,
	WRD_END
};

struct WrdEngine {
	std::vector<WrdCmd> cmds;
	int loaded;
	int cur;
	int lastTick;
	int tsNum, tsDen;
	int wmode;          // n in @WMODE(n)。省略時は 11（1行=8分。1小節=96）
	int wmodeChar;      // mode=1 なら |_\\ 文字単位
	int offsetMeas;
	int col40;
	int textOn;
	int gfxOn;
	int curX, curY;     // 0-based
	int saveX, saveY;   // ESC s / ESC [s
	int saveColor;      // DECSC は属性も保存。カラオケ ESC s … ESC [u 用
	int color;
	int activePage, dispPage;
	int gplane;
	wchar_t dir[MAX_PATH];
	wchar_t wrdPath[MAX_PATH];
	char cells[25][82];
	unsigned char attr[25][82];
	unsigned pal[20][16];
	unsigned gfx[640 * 400];
	MagImage mag;
	int magOk;

	/* SMF 同期 */
	int smfDiv;
	int smfOk;
	struct TempoPt { __int64 sample; unsigned tick; int usecQn; };
	std::vector<TempoPt> tempo;
	int sr;
	int usedAbsWait;    // @WAIT がある（小節同期。ノートには付けない）
	int usedBs;         // 行末 \ カラオケ
	int karaSynced;
	std::vector<int> karaNotes;   // tick48 メロディ NoteOn
	struct KaraMark { int tick48; char sjis[4]; };
	std::vector<KaraMark> karaMarks; // RCP F6 歌詞マーカー
};

void WrdEngineInit(WrdEngine* e);
void WrdEngineFree(WrdEngine* e);
int WrdEngineLoad(WrdEngine* e, const wchar_t* wrdPath);
int WrdEngineLoadSmfClock(WrdEngine* e, const wchar_t* midPath, int sampleRate);
int WrdEngineLoadRcpKaraoke(WrdEngine* e, const wchar_t* rcpPath);
void WrdEngineSyncKaraoke(WrdEngine* e);
int WrdEngineTickFromSample(const WrdEngine* e, __int64 playb);
void WrdEngineSeek(WrdEngine* e, int tick48);
void WrdEngineResetScreen(WrdEngine* e);
void WrdEnginePaint(WrdEngine* e, HDC hdc, const RECT* rc, int capH);
