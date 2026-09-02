#include "stdafx.h"
#include "CSasamiScoreHist.h"

void ScScoreHistInit(ScScoreHist* h)
{
	if (!h) return;
	memset(h, 0, sizeof(*h));
	h->head = -1;
}

void ScScoreHistPush(ScScoreHist* h, const ScEvent* ev, int evCount)
{
	if (!h || !ev) return;
	if (evCount < 0) evCount = 0;
	if (evCount > SC_EV_MAX) evCount = SC_EV_MAX;
	/* Drop redo branch */
	h->size = h->head + 1;
	if (h->size >= SC_HIST_MAX) {
		/* shift left */
		memmove(h->snap[0], h->snap[1], sizeof(h->snap[0]) * (SC_HIST_MAX - 1));
		memmove(h->count, h->count + 1, sizeof(int) * (SC_HIST_MAX - 1));
		h->size = SC_HIST_MAX - 1;
		h->head = SC_HIST_MAX - 2;
	}
	h->head++;
	h->size = h->head + 1;
	memcpy(h->snap[h->head], ev, sizeof(ScEvent) * (size_t)evCount);
	h->count[h->head] = evCount;
	h->redo = 0;
}

int ScScoreHistUndo(ScScoreHist* h, ScEvent* ev, int* evCount, int evMax)
{
	if (!h || !ev || !evCount || h->head < 0) return 0;
	if (h->head == 0) {
		/* restore first snapshot then stay; need prior empty */
		int n = h->count[0];
		if (n > evMax) n = evMax;
		memcpy(ev, h->snap[0], sizeof(ScEvent) * (size_t)n);
		*evCount = n;
		h->redo++;
		return 1;
	}
	h->head--;
	h->redo++;
	int n = h->count[h->head];
	if (n > evMax) n = evMax;
	memcpy(ev, h->snap[h->head], sizeof(ScEvent) * (size_t)n);
	*evCount = n;
	return 1;
}

int ScScoreHistRedo(ScScoreHist* h, ScEvent* ev, int* evCount, int evMax)
{
	if (!h || !ev || !evCount || h->redo <= 0) return 0;
	if (h->head + 1 >= h->size) return 0;
	h->head++;
	h->redo--;
	int n = h->count[h->head];
	if (n > evMax) n = evMax;
	memcpy(ev, h->snap[h->head], sizeof(ScEvent) * (size_t)n);
	*evCount = n;
	return 1;
}
