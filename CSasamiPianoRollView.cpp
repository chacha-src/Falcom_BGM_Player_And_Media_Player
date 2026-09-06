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

void ScPianoRollGridRect(const ScPianoRollView* v, const CRect& rc, const ScStaffUi* u,
	const ScEvent* ev, int evCount, CRect* outKeys, CRect* outGrid, int* outGridLeft)
{
	if (!v || !u) return;
	CRect keys(rc.left, rc.top, rc.left + SC_ROLL_KEY_W, rc.bottom);
	CRect grid(keys.right, rc.top, rc.right, rc.bottom);
	const int gridLeft = ScStaffGridLeftPx(grid.left, u, ev, evCount);
	if (outKeys) *outKeys = keys;
	if (outGrid) *outGrid = grid;
	if (outGridLeft) *outGridLeft = gridLeft;
}

static void ScPianoRollPaintInner(CDC& dc, const CRect& rc, ScPianoRollView* v,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart)
{
	v->rc = rc;
	v->pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	dc.FillSolidRect(rc, RGB(32, 34, 40));
	CRect keys, grid;
	int gridLeft = 0;
	ScPianoRollGridRect(v, rc, u, ev, evCount, &keys, &grid, &gridLeft);
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
		const int black = ((1 << (note % 12)) & 0x54A) != 0;
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
	/* Mark lane (top 10px): loop/ped/ottava ticks */
	if (ev) {
		for (int i = 0; i < evCount; i++) {
			const ScEvent& e = ev[i];
			const int isMark = ScStaffIsStaffMarkKind(e.kind, u->isFmScore)
				|| e.kind == SC_EV_TEMPO || e.kind == SC_EV_FM_TEMPO
				|| e.kind == SC_EV_FM_LOOP_START || e.kind == SC_EV_FM_LOOP_END
				|| e.kind == SC_EV_PEDAL_ON || e.kind == SC_EV_PEDAL_OFF
				|| e.kind == SC_EV_OTTAVA || e.kind == SC_EV_OTTAVA_END;
			if (!isMark) continue;
			const int x = gridLeft + ((int)e.tick * pxBeat) / SC_PPQN - scrollX;
			if (x < grid.left || x > grid.right) continue;
			COLORREF c = RGB(180, 180, 100);
			if (e.kind == SC_EV_TEMPO || e.kind == SC_EV_FM_TEMPO) c = RGB(255, 160, 60);
			else if (e.kind == SC_EV_FM_LOOP_START || e.kind == SC_EV_FM_LOOP_END) c = RGB(120, 200, 255);
			else if (e.kind == SC_EV_PEDAL_ON || e.kind == SC_EV_PEDAL_OFF) c = RGB(200, 120, 255);
			else c = RGB(255, 220, 120);
			dc.FillSolidRect(x, grid.top, 2, 10, c);
		}
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
			if (sel && nr.Width() > SC_ROLL_RESIZE_PX)
				dc.FillSolidRect(nr.right - 2, nr.top, 2, nr.Height(), RGB(255, 255, 255));
			int fxP = 0, fxPan = 0, fxVol = 0, fxExp = 0;
			if (ScStaffNoteHasFx(ev, evCount, i, u->isFmScore, &fxP, &fxPan, &fxVol, &fxExp)) {
				int bx = nr.left + 1;
				if (fxP) { dc.FillSolidRect(bx, nr.top, 3, 3, RGB(255, 80, 80)); bx += 4; }
				if (fxPan) { dc.FillSolidRect(bx, nr.top, 3, 3, RGB(80, 255, 80)); bx += 4; }
				if (fxVol) { dc.FillSolidRect(bx, nr.top, 3, 3, RGB(80, 80, 255)); bx += 4; }
				if (fxExp) { dc.FillSolidRect(bx, nr.top, 3, 3, RGB(255, 200, 0)); }
			}
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

void ScPianoRollPaintSelectionMarquee(CDC& dc, CRect r)
{
	r.NormalizeRect();
	CBrush br(HS_BDIAGONAL, RGB(200, 200, 80));
	CBrush* oldBr = dc.SelectObject(&br);
	dc.SetBkMode(TRANSPARENT);
	dc.Rectangle(&r);
	dc.SelectObject(oldBr);
	CPen pen(PS_DOT, 1, RGB(255, 220, 80));
	CPen* oldPen = dc.SelectObject(&pen);
	dc.SelectStockObject(NULL_BRUSH);
	dc.Rectangle(&r);
	dc.SelectObject(oldPen);
}

int ScPianoRollHitNote(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CPoint pt)
{
	if (!v || !ev || !u) return -1;
	if (pt.x < rc.left + SC_ROLL_KEY_W) return -1;
	const int pxBeat = v->pxBeat > 0 ? v->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = ScStaffGridLeftPx(rc.left + SC_ROLL_KEY_W, u, ev, evCount);
	for (int i = evCount - 1; i >= 0; i--) {
		const ScEvent& e = ev[i];
		if (e.ch != (uint8_t)curPart) continue;
		if (e.kind != SC_EV_NOTE && e.kind != SC_EV_FM_NOTE) continue;
		const int midiNote = RollMidi(e, u, curPart);
		const int x0 = gridLeft + ((int)e.tick * pxBeat) / SC_PPQN - u->scrollX;
		const int x1 = gridLeft + (((int)e.tick + (int)e.dur) * pxBeat) / SC_PPQN - u->scrollX;
		const int y = NoteY(v, rc, midiNote);
		if (pt.y < y || pt.y >= y + v->rowH) continue;
		if (pt.x >= x0 && pt.x < x1 - SC_ROLL_RESIZE_PX)
			return i;
	}
	return -1;
}

int ScPianoRollHitResize(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CPoint pt)
{
	if (!v || !ev || !u) return -1;
	if (pt.x < rc.left + SC_ROLL_KEY_W) return -1;
	const int pxBeat = v->pxBeat > 0 ? v->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = ScStaffGridLeftPx(rc.left + SC_ROLL_KEY_W, u, ev, evCount);
	for (int i = evCount - 1; i >= 0; i--) {
		const ScEvent& e = ev[i];
		if (e.ch != (uint8_t)curPart) continue;
		if (e.kind != SC_EV_NOTE && e.kind != SC_EV_FM_NOTE) continue;
		const int midiNote = RollMidi(e, u, curPart);
		const int x0 = gridLeft + ((int)e.tick * pxBeat) / SC_PPQN - u->scrollX;
		const int x1 = gridLeft + (((int)e.tick + (int)e.dur) * pxBeat) / SC_PPQN - u->scrollX;
		const int y = NoteY(v, rc, midiNote);
		if (pt.y < y || pt.y >= y + v->rowH) continue;
		const int hx0 = max(x0, x1 - SC_ROLL_RESIZE_PX);
		if (pt.x >= hx0 && pt.x <= x1 + 2)
			return i;
	}
	return -1;
}

int ScPianoRollHitInRect(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CRect marquee,
	int* outIdx, int outMax)
{
	if (!v || !ev || !u || !outIdx || outMax <= 0) return 0;
	marquee.NormalizeRect();
	const int pxBeat = v->pxBeat > 0 ? v->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = ScStaffGridLeftPx(rc.left + SC_ROLL_KEY_W, u, ev, evCount);
	int n = 0;
	for (int i = 0; i < evCount && n < outMax; i++) {
		const ScEvent& e = ev[i];
		if (e.ch != (uint8_t)curPart) continue;
		if (e.kind != SC_EV_NOTE && e.kind != SC_EV_FM_NOTE) continue;
		const int midiNote = RollMidi(e, u, curPart);
		const int x0 = gridLeft + ((int)e.tick * pxBeat) / SC_PPQN - u->scrollX;
		const int x1 = gridLeft + (((int)e.tick + (int)e.dur) * pxBeat) / SC_PPQN - u->scrollX;
		const int y = NoteY(v, rc, midiNote);
		CRect nr(x0, y, x1, y + v->rowH);
		CRect inter;
		if (inter.IntersectRect(&nr, &marquee))
			outIdx[n++] = i;
	}
	return n;
}

uint32_t ScPianoRollXToTick(const ScPianoRollView* v, const CRect& rc, const ScStaffUi* u, int x)
{
	if (!v || !u) return 0;
	const int pxBeat = v->pxBeat > 0 ? v->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = ScStaffGridLeftPx(rc.left + SC_ROLL_KEY_W, u, NULL, 0);
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
