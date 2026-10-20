#pragma once
#include "SasamiComposerDoc.h"
#include <vector>

/* Push は編集前に呼ぶ。スナップは実イベント数だけ持つ（固定 65536 件確保しない）。 */
enum { SC_HIST_MAX = 200 };

struct ScScoreHist {
	std::vector<std::vector<ScEvent>> undo;
	std::vector<std::vector<ScEvent>> redo;
};

void ScScoreHistInit(ScScoreHist* h);
void ScScoreHistPush(ScScoreHist* h, const ScEvent* ev, int evCount);
int ScScoreHistUndo(ScScoreHist* h, ScEvent* ev, int* evCount, int evMax);
int ScScoreHistRedo(ScScoreHist* h, ScEvent* ev, int* evCount, int evMax);
int ScScoreHistCanUndo(const ScScoreHist* h);
int ScScoreHistCanRedo(const ScScoreHist* h);
