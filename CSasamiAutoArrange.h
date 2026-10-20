#pragma once
#include "SasamiComposerDoc.h"
#include "CSasamiStaffCore.h"
#include "CSasamiScoreHist.h"

/* 既存の音符は消さない。イントロ／間奏は後ろへずらして空いた小節に足す。 */

enum ScAutoStyle : int {
	SC_AS_16BEAT = 0,
	SC_AS_8BEAT,
	SC_AS_REGGAE,
	SC_AS_TECHNO,
	SC_AS_HOUSE,
	SC_AS_TRANCE,
	SC_AS_DNB,
	SC_AS_HIPHOP,
	SC_AS_FUNK,
	SC_AS_DISCO,
	SC_AS_ROCK,
	SC_AS_METAL,
	SC_AS_BALLAD,
	SC_AS_WALTZ,
	SC_AS_BOSSA,
	SC_AS_SAMBA,
	SC_AS_SHUFFLE,
	SC_AS_MARCH,
	SC_AS_POLKA,
	SC_AS_CITYPOP,
	SC_AS_GAME,
	SC_AS_ORCH,
	SC_AS_AMBIENT,
	SC_AS_BREAKS,
	SC_AS_LATIN,
	SC_AS_ENKA,
	SC_AS_FUSION,
	SC_AS_ACOUSTIC,
	SC_AS_PIANO,
	SC_AS_FANFARE,
	SC_AS_SLOWROCK,
	SC_AS_COUNT
};

enum ScAutoJob : int {
	SC_AJ_DRUM = 1,
	SC_AJ_BASS = 2,
	SC_AJ_CHORD = 4,
	SC_AJ_MEL_VOL = 8,
	SC_AJ_MEL_PITCH = 16,
	SC_AJ_INTRO = 32,
	SC_AJ_BREAK = 64,
	SC_AJ_ENDING = 128
};

enum ScAutoErr : int {
	SC_AE_OK = 0,
	SC_AE_NONOTES = 1,
	SC_AE_EMPTY = 2,
	SC_AE_OVERFLOW = 3
};

struct ScAutoArrangeOpt {
	int style;
	int jobs;
	int introBars;
	int breakBars;
	int endBars;
	int pitchMode; /* 0 off, 1 harmony, 2 +scoop, 3 +octave */
	int isFm;
	int curPart;
	int defMeterN;
	int defMeterD;
	int defKey;
	ScStaffUi* ui;
};

struct ScAutoArrangeResult {
	int err;
	int added;
	int chDrum, chBass, chChord, chHarm;
	wchar_t msg[256];
};

struct ScAutoArrangeHost {
	ScEvent* ev;
	int* evCount;
	int evMax;
	ScStaffUi* ui;
	ScScoreHist* hist;
	int isFm;
	int curPart;
	int defMeterN;
	int defMeterD;
	int defKey;
	void* ctx;
	void (*refresh)(void* ctx);
	int (*preview)(void* ctx);
	void (*stop)(void* ctx);
};

int ScAutoArrangeCountNotes(const ScEvent* ev, int n);
const wchar_t* ScAutoStyleName(int style);
int ScAutoArrangeApply(ScEvent* ev, int* evCount, int evMax,
	const ScAutoArrangeOpt* opt, ScAutoArrangeResult* out);
/* モーダル。試し聴きは host->preview、決定でヒストリへ積む。 */
int ScAutoArrangeDlgRun(const ScAutoArrangeHost* host);
/* 音符の無いパートのソロは外す。アンドゥ後に空ソロが残らないようにする。 */
void ScAutoArrangeReleaseEmptySolo(ScStaffUi* ui, const ScEvent* ev, int n);
