#pragma once
#include "CSasamiStaffCore.h"
#include "SasamiComposerDoc.h"

struct ScPianoRollView {
	CRect rc;
	int scrollY; /* pitch scroll: 0 = top = note 108 */
	int noteTop; /* highest MIDI note shown */
	int rowH;
	int pxBeat;
};

void ScPianoRollInit(ScPianoRollView* v);
void ScPianoRollPaint(CDC& dc, const CRect& rc, ScPianoRollView* v,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart);
int ScPianoRollHitNote(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CPoint pt);
uint32_t ScPianoRollXToTick(const ScPianoRollView* v, const CRect& rc, const ScStaffUi* u, int x);
int ScPianoRollYToNote(const ScPianoRollView* v, const CRect& rc, int y);
