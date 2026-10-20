#include "stdafx.h"
#include "CSasamiScoreHist.h"

void ScScoreHistInit(ScScoreHist* h)
{
	if (!h) return;
	h->undo.clear();
	h->redo.clear();
}

static void ScHistSnap(std::vector<ScEvent>& out, const ScEvent* ev, int evCount)
{
	if (evCount < 0) evCount = 0;
	if (evCount > SC_EV_MAX) evCount = SC_EV_MAX;
	out.assign(ev, ev + evCount);
}

static int ScHistRestore(const std::vector<ScEvent>& s, ScEvent* ev, int* evCount, int evMax)
{
	int n = (int)s.size();
	if (n > evMax) n = evMax;
	if (n > 0) memcpy(ev, s.data(), sizeof(ScEvent) * (size_t)n);
	*evCount = n;
	return 1;
}

static int ScHistSame(const std::vector<ScEvent>& s, const ScEvent* ev, int evCount)
{
	return (int)s.size() == evCount
		&& (evCount <= 0 || memcmp(s.data(), ev, sizeof(ScEvent) * (size_t)evCount) == 0);
}

void ScScoreHistPush(ScScoreHist* h, const ScEvent* ev, int evCount)
{
	if (!h || !ev) return;
	if (evCount < 0) evCount = 0;
	if (!h->undo.empty() && ScHistSame(h->undo.back(), ev, evCount))
		return;
	if ((int)h->undo.size() >= SC_HIST_MAX)
		h->undo.erase(h->undo.begin());
	h->undo.emplace_back();
	ScHistSnap(h->undo.back(), ev, evCount);
	h->redo.clear();
}

int ScScoreHistUndo(ScScoreHist* h, ScEvent* ev, int* evCount, int evMax)
{
	if (!h || !ev || !evCount) return 0;
	/* 何も変えなかった操作の Push は空振りになるので飛ばす */
	while (!h->undo.empty() && ScHistSame(h->undo.back(), ev, *evCount))
		h->undo.pop_back();
	if (h->undo.empty()) return 0;
	h->redo.emplace_back();
	ScHistSnap(h->redo.back(), ev, *evCount);
	ScHistRestore(h->undo.back(), ev, evCount, evMax);
	h->undo.pop_back();
	return 1;
}

int ScScoreHistRedo(ScScoreHist* h, ScEvent* ev, int* evCount, int evMax)
{
	if (!h || !ev || !evCount) return 0;
	while (!h->redo.empty() && ScHistSame(h->redo.back(), ev, *evCount))
		h->redo.pop_back();
	if (h->redo.empty()) return 0;
	h->undo.emplace_back();
	ScHistSnap(h->undo.back(), ev, *evCount);
	ScHistRestore(h->redo.back(), ev, evCount, evMax);
	h->redo.pop_back();
	return 1;
}

int ScScoreHistCanUndo(const ScScoreHist* h)
{
	return (h && !h->undo.empty()) ? 1 : 0;
}

int ScScoreHistCanRedo(const ScScoreHist* h)
{
	return (h && !h->redo.empty()) ? 1 : 0;
}
