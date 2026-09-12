#pragma once
#include "CSasamiStaffCore.h"
#include "SasamiComposerDoc.h"

enum {
	SC_ROLL_KEY_W = 56,
	SC_ROLL_RESIZE_PX = 6,
	SC_ROLL_MARK_H = 22,
	SC_ROLL_NOTE_LO = 21, /* A0 */
	SC_ROLL_NOTE_HI = 108, /* C8 */
	SC_ROLL_ROW_H_MIN = 14
};

struct ScPianoRollView {
	CRect rc;
	int scrollY; /* pitch scroll in pixels; 0 = MIDI 127 at top */
	int noteTop; /* highest MIDI note at scrollY=0 (127) */
	int rowH;
	int pxBeat;
	int zoomH; /* 0 = auto-fit 88 keys to the pane */
	int hoverNote; /* -1 none */
};

void ScPianoRollInit(ScPianoRollView* v);
void ScPianoRollFit(ScPianoRollView* v, int keysH);
void ScPianoRollPaint(CDC& dc, const CRect& rc, ScPianoRollView* v,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart);
void ScPianoRollGridRect(const ScPianoRollView* v, const CRect& rc, const ScStaffUi* u,
	const ScEvent* ev, int evCount, CRect* outKeys, CRect* outGrid, int* outGridLeft);
void ScPianoRollMarkRect(const CRect& rc, CRect* outMark);
int ScPianoRollHitNote(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CPoint pt);
int ScPianoRollHitResize(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CPoint pt);
int ScPianoRollHitMark(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CPoint pt);
int ScPianoRollHitInRect(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CRect marquee,
	int* outIdx, int outMax);
void ScPianoRollPaintSelectionMarquee(CDC& dc, CRect r);
uint32_t ScPianoRollXToTick(const ScPianoRollView* v, const CRect& rc, const ScStaffUi* u, int x);
int ScPianoRollYToNote(const ScPianoRollView* v, const CRect& rc, int y);
int ScPianoRollPtInKeys(const CRect& rc, CPoint pt);
int ScPianoRollPtInMarkLane(const CRect& rc, CPoint pt);
void ScPianoRollNoteName(int note, wchar_t* buf, int cch);
int ScPianoRollContentWidthPx(const ScStaffUi* u, const ScEvent* ev, int evCount);
int ScPianoRollContentHeightPx(const ScPianoRollView* v);
int ScPianoRollMaxScrollY(const ScPianoRollView* v, int keysH);
int ScPianoRollTimePageW(const CRect& rc);
void ScPianoRollPaintBars(CDC& dc, const CRect& outer, const ScPianoRollView* v,
	const ScStaffUi* u, const ScEvent* ev, int evCount);
/* 0=none 1=vert 2=horz 3=hZoom- 4=hZoom+ 5=vZoom- 6=vZoom+ */
int ScPianoRollHitBar(const CRect& outer, const ScPianoRollView* v, const ScStaffUi* u,
	const ScEvent* ev, int evCount, CPoint pt, int* outPos);
int ScPianoRollMapVertDrag(const CRect& outer, const ScPianoRollView* v, int keysH,
	int ptY, int anchorY, int scroll0);
int ScPianoRollMapHorzDrag(const CRect& outer, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int pageW, int ptX, int anchorX, int scroll0);
