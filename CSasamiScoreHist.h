#pragma once
#include "SasamiComposerDoc.h"

enum { SC_HIST_MAX = 10 };

struct ScScoreHist {
	ScEvent snap[SC_HIST_MAX][SC_EV_MAX];
	int count[SC_HIST_MAX];
	int head;  /* current snapshot index (-1 empty) */
	int size;  /* valid snapshots */
	int redo;  /* redo steps available */
};

void ScScoreHistInit(ScScoreHist* h);
void ScScoreHistPush(ScScoreHist* h, const ScEvent* ev, int evCount);
int ScScoreHistUndo(ScScoreHist* h, ScEvent* ev, int* evCount, int evMax);
int ScScoreHistRedo(ScScoreHist* h, ScEvent* ev, int* evCount, int evMax);
