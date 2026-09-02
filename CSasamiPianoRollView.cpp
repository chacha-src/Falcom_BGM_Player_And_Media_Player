#include "stdafx.h"
#include "CSasamiPianoRollView.h"
#include "CCustomControl.h"

void ScPianoRollInit(ScPianoRollView* v)
{
	if (!v) return;
	memset(v, 0, sizeof(*v));
	v->noteTop = 96;
	v->rowH = 8;
	v->pxBeat = SC_PX_BEAT_DEFAULT;
	v->scrollY = 0;
}

static int NoteY(const ScPianoRollView* v, const CRect& rc, int note)
{
	const int topNote = v->noteTop - (v->scrollY / max(1, v->rowH));
	return rc.top + (topNote - note) * v->rowH;
}

static int RollMidi(const ScEvent& e, const ScStaffUi* u, int curPart)
{
	if (e.kind == SC_EV_FM_NOTE)
		return ScStaffEvSoundingMidi(e, u && u->isFmScore ? 1 : 0, curPart);
	return (int)e.a;
}

static void ScPianoRollPaintInner(CDC& dc, const CRect& rc, ScPianoRollView* v,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart)
{
	v->rc = rc;
	v->pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	dc.FillSolidRect(rc, RGB(32, 34, 40));
	const int keyW = 28;
	CRect keys(rc.left, rc.top, rc.left + keyW, rc.bottom);
	CRect grid(keys.right, rc.top, rc.right, rc.bottom);
	const int gridLeft = ScStaffGridLeftPx(grid.left, u, ev, evCount);
	dc.FillSolidRect(keys, RGB(48, 50, 58));
	const int tpm = ScStaffTicksPerMeasure(u);
	const int pxBeat = v->pxBeat;
	const int rows = rc.Height() / max(1, v->rowH) + 2;
	const int topNote = v->noteTop - (v->scrollY / max(1, v->rowH));
	dc.SetBkMode(TRANSPARENT);
	for (int r = 0; r < rows; r++) {
		const int note = topNote - r;
		if (note < 0 || note > 127) continue;
		const int y = rc.top + r * v->rowH;
		const int black = ((1 << (note % 12)) & 0x54A) != 0; /* C# D# F# G# A# */
		dc.FillSolidRect(grid.left, y, grid.Width(), v->rowH,
			black ? RGB(28, 30, 36) : RGB(36, 38, 46));
		dc.FillSolidRect(keys.left, y, keys.Width(), v->rowH,
			black ? RGB(20, 20, 24) : RGB(220, 220, 230));
		dc.FillSolidRect(grid.left, y + v->rowH - 1, grid.Width(), 1, RGB(50, 52, 60));
	}
	const int scrollX = u->scrollX;
	for (int t = 0; ; t += SC_PPQN / 4) {
		const int x = gridLeft + (t * pxBeat) / SC_PPQN - scrollX;
		if (x > grid.right) break;
		if (x < grid.left) continue;
		const int isBar = (tpm > 0 && (t % tpm) == 0);
		const int isBeat = (t % SC_PPQN) == 0;
		dc.FillSolidRect(x, grid.top, 1, grid.Height(),
			isBar ? RGB(120, 140, 180) : (isBeat ? RGB(70, 80, 100) : RGB(45, 48, 55)));
	}
	if (ev) {
		for (int i = 0; i < evCount; i++) {
			const ScEvent& e = ev[i];
			if (e.ch != (uint8_t)curPart) continue;
			if (e.kind != SC_EV_NOTE && e.kind != SC_EV_FM_NOTE) continue;
			const int midiNote = RollMidi(e, u, curPart);
			const int x0 = gridLeft + ((int)e.tick * pxBeat) / SC_PPQN - scrollX;
			const int x1 = gridLeft + (((int)e.tick + (int)e.dur) * pxBeat) / SC_PPQN - scrollX;
			const int y = NoteY(v, rc, midiNote);
			if (y + v->rowH < rc.top || y > rc.bottom) continue;
			if (x1 < grid.left || x0 > grid.right) continue;
			CRect nr(max(x0, grid.left), y, max(x0 + 2, min(x1, grid.right)), y + v->rowH - 1);
			int sel = 0;
			if (u->nSel > 0) {
				for (int s = 0; s < u->nSel && s < SC_SEL_MAX; s++)
					if (u->selList[s] == i) { sel = 1; break; }
			} else if (u->selEv == i) sel = 1;
			dc.FillSolidRect(nr, sel ? RGB(255, 200, 80) : RGB(80, 170, 255));
		}
	}
	const int mx = gridLeft + ((int)u->markerTick * pxBeat) / SC_PPQN - scrollX;
	if (mx >= grid.left && mx <= grid.right)
		dc.FillSolidRect(mx, grid.top, 2, grid.Height(), RGB(220, 60, 60));
	if (u->previewActive) {
		const int px = gridLeft + ((int)u->playheadTick * pxBeat) / SC_PPQN - scrollX;
		if (px >= grid.left && px <= grid.right)
			dc.FillSolidRect(px, grid.top, 2, grid.Height(), RGB(80, 220, 120));
	}
}

void ScPianoRollPaint(CDC& dc, const CRect& rc, ScPianoRollView* v,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart)
{
	if (!v || !u || rc.Width() < 8 || rc.Height() < 8) return;
	/* Win11 acrylic: paint offscreen then opaque blit (FM monitor / score body pattern).
	   Direct FillSolidRect on the window DC stays α=0 and shows the glass. */
	CDC mem;
	if (!mem.CreateCompatibleDC(&dc)) {
		ScPianoRollPaintInner(dc, rc, v, ev, evCount, u, curPart);
		return;
	}
	CBitmap bmp;
	if (!bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height())) {
		ScPianoRollPaintInner(dc, rc, v, ev, evCount, u, curPart);
		return;
	}
	CBitmap* old = mem.SelectObject(&bmp);
	if (!old) {
		ScPianoRollPaintInner(dc, rc, v, ev, evCount, u, curPart);
		return;
	}
	CRect local(0, 0, rc.Width(), rc.Height());
	ScPianoRollPaintInner(mem, local, v, ev, evCount, u, curPart);
	v->rc = rc;
	CCC_BlitStretchOpaque(dc.GetSafeHdc(), rc.left, rc.top, rc.Width(), rc.Height(),
		mem.GetSafeHdc(), 0, 0, rc.Width(), rc.Height());
	mem.SelectObject(old);
}

int ScPianoRollHitNote(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CPoint pt)
{
	if (!v || !ev || !u) return -1;
	const int keyW = 28;
	if (pt.x < rc.left + keyW) return -1;
	const int pxBeat = v->pxBeat > 0 ? v->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = ScStaffGridLeftPx(rc.left + keyW, u, ev, evCount);
	for (int i = evCount - 1; i >= 0; i--) {
		const ScEvent& e = ev[i];
		if (e.ch != (uint8_t)curPart) continue;
		if (e.kind != SC_EV_NOTE && e.kind != SC_EV_FM_NOTE) continue;
		const int midiNote = RollMidi(e, u, curPart);
		const int x0 = gridLeft + ((int)e.tick * pxBeat) / SC_PPQN - u->scrollX;
		const int x1 = gridLeft + (((int)e.tick + (int)e.dur) * pxBeat) / SC_PPQN - u->scrollX;
		const int y = NoteY(v, rc, midiNote);
		if (pt.x >= x0 && pt.x <= x1 && pt.y >= y && pt.y < y + v->rowH)
			return i;
	}
	return -1;
}

uint32_t ScPianoRollXToTick(const ScPianoRollView* v, const CRect& rc, const ScStaffUi* u, int x)
{
	if (!v || !u) return 0;
	const int keyW = 28;
	const int pxBeat = v->pxBeat > 0 ? v->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = ScStaffGridLeftPx(rc.left + keyW, u, NULL, 0);
	int rel = x - gridLeft + u->scrollX;
	if (rel < 0) rel = 0;
	return (uint32_t)((rel * SC_PPQN) / max(1, pxBeat));
}

int ScPianoRollYToNote(const ScPianoRollView* v, const CRect& rc, int y)
{
	if (!v) return 60;
	const int topNote = v->noteTop - (v->scrollY / max(1, v->rowH));
	int note = topNote - (y - rc.top) / max(1, v->rowH);
	if (note < 0) note = 0;
	if (note > 127) note = 127;
	return note;
}
