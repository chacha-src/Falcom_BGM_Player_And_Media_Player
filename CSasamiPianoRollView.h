#pragma once
#include "CSasamiStaffCore.h"
#include "SasamiComposerDoc.h"

enum {
	SC_ROLL_KEY_W = 28,
	SC_ROLL_RESIZE_PX = 6
};

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
void ScPianoRollGridRect(const ScPianoRollView* v, const CRect& rc, const ScStaffUi* u,
	const ScEvent* ev, int evCount, CRect* outKeys, CRect* outGrid, int* outGridLeft);
int ScPianoRollHitNote(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CPoint pt);
int ScPianoRollHitResize(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CPoint pt);
int ScPianoRollHitInRect(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CRect marquee,
	int* outIdx, int outMax);
void ScPianoRollPaintSelectionMarquee(CDC& dc, CRect r);
uint32_t ScPianoRollXToTick(const ScPianoRollView* v, const CRect& rc, const ScStaffUi* u, int x);
int ScPianoRollYToNote(const ScPianoRollView* v, const CRect& rc, int y);
