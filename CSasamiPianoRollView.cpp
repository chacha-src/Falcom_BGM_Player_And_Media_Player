#include "stdafx.h"
#include "CSasamiPianoRollView.h"
#include "CCustomControl.h"

void ScPianoRollInit(ScPianoRollView* v)
{
	if (!v) return;
	memset(v, 0, sizeof(*v));
	v->noteTop = 127;
	v->rowH = SC_ROLL_ROW_H_MIN;
	v->pxBeat = SC_PX_BEAT_DEFAULT;
	v->scrollY = (127 - SC_ROLL_NOTE_HI) * SC_ROLL_ROW_H_MIN;
	v->zoomH = 0;
	v->hoverNote = -1;
}

void ScPianoRollFit(ScPianoRollView* v, int keysH)
{
	if (!v) return;
	if (keysH < 8) keysH = 8;
	const int nFit = SC_ROLL_NOTE_HI - SC_ROLL_NOTE_LO + 1;
	int h = v->zoomH;
	if (h <= 0) {
		h = keysH / nFit;
		if (h < SC_ROLL_ROW_H_MIN) h = SC_ROLL_ROW_H_MIN;
	}
	if (h < SC_ROLL_ROW_H_MIN) h = SC_ROLL_ROW_H_MIN;
	if (h > 48) h = 48;
	v->rowH = h;
	v->noteTop = 127;
	const int maxY = ScPianoRollMaxScrollY(v, keysH);
	if (v->scrollY > maxY) v->scrollY = maxY;
	if (v->scrollY < 0) v->scrollY = 0;
}

void ScPianoRollNoteName(int note, wchar_t* buf, int cch)
{
	if (!buf || cch < 4) return;
	if (note < 0) note = 0;
	if (note > 127) note = 127;
	static const wchar_t* kN[12] = {
		L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"
	};
	_snwprintf_s(buf, cch, _TRUNCATE, L"%s%d", kN[note % 12], note / 12 - 1);
}

void ScPianoRollMarkRect(const CRect& rc, CRect* outMark)
{
	if (!outMark) return;
	outMark->SetRect(rc.left + SC_ROLL_KEY_W, rc.top, rc.right, rc.top + SC_ROLL_MARK_H);
}

int ScPianoRollPtInKeys(const CRect& rc, CPoint pt)
{
	return (pt.x >= rc.left && pt.x < rc.left + SC_ROLL_KEY_W
		&& pt.y >= rc.top + SC_ROLL_MARK_H && pt.y < rc.bottom) ? 1 : 0;
}

int ScPianoRollPtInMarkLane(const CRect& rc, CPoint pt)
{
	return (pt.x >= rc.left + SC_ROLL_KEY_W && pt.x < rc.right
		&& pt.y >= rc.top && pt.y < rc.top + SC_ROLL_MARK_H) ? 1 : 0;
}

static int KeysTop(const CRect& rc)
{
	return rc.top + SC_ROLL_MARK_H;
}

static int RollGridLeft(const CRect& rc)
{
	return rc.left + SC_ROLL_KEY_W;
}

static int NoteY(const ScPianoRollView* v, const CRect& rc, int note)
{
	const int topNote = v->noteTop - (v->scrollY / max(1, v->rowH));
	return KeysTop(rc) + (topNote - note) * v->rowH;
}

static int RollMidi(const ScEvent& e, const ScStaffUi* u, int curPart)
{
	if (e.kind == SC_EV_FM_NOTE)
		return ScStaffEvSoundingMidi(e, u && u->isFmScore ? 1 : 0, curPart);
	return (int)e.a;
}

static int IsRollMark(const ScEvent& e, int isFm)
{
	if (ScStaffIsStaffMarkKind(e.kind, isFm)) return 1;
	if (e.kind == SC_EV_TEMPO || e.kind == SC_EV_FM_TEMPO) return 1;
	return 0;
}

static void RollMarkStyle(const ScEvent& e, wchar_t* label, int cch, COLORREF* bg, COLORREF* fg, COLORREF* edge)
{
	if (e.kind == SC_EV_TEMPO || e.kind == SC_EV_FM_TEMPO) {
		_snwprintf_s(label, cch, _TRUNCATE, L"t%d", (int)e.a);
		*bg = RGB(255, 220, 160); *fg = RGB(120, 50, 10); *edge = RGB(200, 120, 40);
	} else if (e.kind == SC_EV_FM_LOOP_START) {
		_snwprintf_s(label, cch, _TRUNCATE, L"|:%u", (unsigned)(e.a ? e.a : 2));
		*bg = RGB(215, 232, 255); *fg = RGB(20, 60, 130); *edge = RGB(60, 110, 180);
	} else if (e.kind == SC_EV_FM_LOOP_END) {
		wcsncpy_s(label, cch, L":|", _TRUNCATE);
		*bg = RGB(215, 232, 255); *fg = RGB(20, 60, 130); *edge = RGB(60, 110, 180);
	} else if (e.kind == SC_EV_PEDAL_ON) {
		wcsncpy_s(label, cch, L"Ped.", _TRUNCATE);
		*bg = RGB(235, 220, 245); *fg = RGB(80, 30, 110); *edge = RGB(140, 80, 170);
	} else if (e.kind == SC_EV_PEDAL_OFF) {
		wcsncpy_s(label, cch, L"*", _TRUNCATE);
		*bg = RGB(235, 220, 245); *fg = RGB(80, 30, 110); *edge = RGB(140, 80, 170);
	} else if (e.kind == SC_EV_OTTAVA) {
		_snwprintf_s(label, cch, _TRUNCATE, L"%s", ScStaffOttavaLabel((int)(int8_t)e.a));
		*bg = RGB(255, 250, 210); *fg = RGB(100, 70, 10); *edge = RGB(180, 140, 40);
	} else if (e.kind == SC_EV_OTTAVA_END) {
		wcsncpy_s(label, cch, L"loco", _TRUNCATE);
		*bg = RGB(255, 250, 210); *fg = RGB(100, 70, 10); *edge = RGB(180, 140, 40);
	} else if (e.kind == SC_EV_SOFT_VIB) {
		if (e.c == 0) wcsncpy_s(label, cch, L"fxx", _TRUNCATE);
		else if (e.a == 1) wcsncpy_s(label, cch, L"trem", _TRUNCATE);
		else if (e.a == 2) wcsncpy_s(label, cch, L"pan~", _TRUNCATE);
		else if (e.a == 3) wcsncpy_s(label, cch, L"sqvib", _TRUNCATE);
		else wcsncpy_s(label, cch, L"vib", _TRUNCATE);
		*bg = RGB(220, 245, 240); *fg = RGB(20, 90, 80); *edge = RGB(60, 150, 140);
	} else if (e.kind == SC_EV_SOFT_PORTA) {
		_snwprintf_s(label, cch, _TRUNCATE, L"porta%+d", (int)e.a - 64);
		*bg = RGB(220, 245, 240); *fg = RGB(20, 90, 80); *edge = RGB(60, 150, 140);
	} else if (e.kind == SC_EV_JUMP_MARK) {
		wcsncpy_s(label, cch, L"Q", _TRUNCATE);
		*bg = RGB(255, 242, 200); *fg = RGB(130, 70, 10); *edge = RGB(200, 140, 50);
	} else if (e.kind == SC_EV_FM_JUMP) {
		wcsncpy_s(label, cch, L"J", _TRUNCATE);
		*bg = RGB(255, 220, 220); *fg = RGB(140, 30, 30); *edge = RGB(200, 70, 70);
	} else {
		wcsncpy_s(label, cch, L"m", _TRUNCATE);
		*bg = RGB(180, 180, 100); *fg = RGB(40, 40, 20); *edge = RGB(120, 120, 60);
	}
}

void ScPianoRollGridRect(const ScPianoRollView* v, const CRect& rc, const ScStaffUi* u,
	const ScEvent* ev, int evCount, CRect* outKeys, CRect* outGrid, int* outGridLeft)
{
	(void)v; (void)u; (void)ev; (void)evCount;
	CRect keys(rc.left, KeysTop(rc), rc.left + SC_ROLL_KEY_W, rc.bottom);
	CRect grid(keys.right, keys.top, rc.right, rc.bottom);
	if (outKeys) *outKeys = keys;
	if (outGrid) *outGrid = grid;
	if (outGridLeft) *outGridLeft = grid.left;
}

static void ScPianoRollPaintInner(CDC& dc, const CRect& rc, ScPianoRollView* v,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart)
{
	v->rc = rc;
	v->pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	ScPianoRollFit(v, max(1, rc.Height() - SC_ROLL_MARK_H));
	dc.FillSolidRect(rc, RGB(32, 34, 40));
	CRect keys, grid;
	int gridLeft = 0;
	ScPianoRollGridRect(v, rc, u, ev, evCount, &keys, &grid, &gridLeft);
	CRect markLane;
	ScPianoRollMarkRect(rc, &markLane);
	dc.FillSolidRect(CRect(rc.left, rc.top, keys.right, keys.top), RGB(42, 44, 54));
	dc.FillSolidRect(markLane, RGB(38, 40, 50));
	dc.FillSolidRect(keys, RGB(48, 50, 58));
	const int tpm = ScStaffTicksPerMeasure(u);
	const int pxBeat = v->pxBeat;
	const int vis = max(1, keys.Height() / max(1, v->rowH) + 1);
	const int topNote = v->noteTop - (v->scrollY / max(1, v->rowH));
	dc.SetBkMode(TRANSPARENT);
	CFont keyFont;
	keyFont.CreateFont(max(11, min(16, v->rowH - 2)), 0, 0, 0, FW_NORMAL, 0, 0, 0,
		DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
	CFont* oldF = dc.SelectObject(&keyFont);
	for (int r = 0; r < vis; r++) {
		const int note = topNote - r;
		if (note < 0 || note > 127) continue;
		const int y = KeysTop(rc) + r * v->rowH;
		if (y >= rc.bottom) break;
		const int black = ((1 << (note % 12)) & 0x54A) != 0;
		const int hover = (v->hoverNote == note);
		dc.FillSolidRect(grid.left, y, grid.Width(), v->rowH,
			black ? RGB(28, 30, 36) : RGB(36, 38, 46));
		COLORREF keyBg = black ? RGB(18, 18, 22) : RGB(232, 232, 240);
		if (hover) keyBg = black ? RGB(70, 90, 40) : RGB(255, 245, 160);
		dc.FillSolidRect(keys.left, y, keys.Width(), v->rowH, keyBg);
		if (black)
			dc.FillSolidRect(keys.left + keys.Width() / 3, y, keys.Width() * 2 / 3, v->rowH, RGB(12, 12, 16));
		const int octaveLine = (note % 12) == 0;
		dc.FillSolidRect(grid.left, y + v->rowH - 1, grid.Width(), 1,
			octaveLine ? RGB(80, 88, 110) : RGB(50, 52, 60));
		dc.FillSolidRect(keys.left, y + v->rowH - 1, keys.Width(), 1, RGB(60, 60, 70));
		if (!black && (note % 12) == 0 && v->rowH >= 13) {
			wchar_t nm[16];
			ScPianoRollNoteName(note, nm, 16);
			dc.SetTextColor(hover ? RGB(40, 40, 20) : RGB(40, 44, 60));
			dc.TextOut(keys.left + 6, y + max(0, (v->rowH - 14) / 2), nm);
		}
	}
	dc.SelectObject(oldF);
	const int scrollX = u->scrollX;
	for (int t = 0; ; t += SC_PPQN / 4) {
		const int x = gridLeft + (t * pxBeat) / SC_PPQN - scrollX;
		if (x > grid.right) break;
		if (x < grid.left) continue;
		const int isBar = (tpm > 0 && (t % tpm) == 0);
		const int isBeat = (t % SC_PPQN) == 0;
		dc.FillSolidRect(x, grid.top, 1, grid.Height(),
			isBar ? RGB(120, 140, 180) : (isBeat ? RGB(70, 80, 100) : RGB(45, 48, 55)));
		if (isBar)
			dc.FillSolidRect(x, markLane.top, 1, markLane.Height(), RGB(100, 120, 160));
	}
	dc.SetTextColor(RGB(150, 155, 170));
	dc.TextOut(rc.left + 6, rc.top + 3, L"FX");
	CFont chipFont;
	chipFont.CreateFont(12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
	oldF = dc.SelectObject(&chipFont);
	if (ev) {
		for (int i = 0; i < evCount; i++) {
			const ScEvent& e = ev[i];
			if ((int)e.ch != curPart) continue;
			if (!IsRollMark(e, u->isFmScore)) continue;
			const int x = gridLeft + ((int)e.tick * pxBeat) / SC_PPQN - scrollX
				+ ScStaffMarkStackIndex(ev, evCount, i) * 12;
			if (x < grid.left - 40 || x > grid.right) continue;
			wchar_t label[32];
			COLORREF bg, fg, edge;
			RollMarkStyle(e, label, 32, &bg, &fg, &edge);
			CSize sz = dc.GetTextExtent(label);
			CRect chip(x, markLane.top + 2, x + sz.cx + 10, markLane.bottom - 2);
			const int selected = ScStaffSelHas(u, i);
			dc.FillSolidRect(chip, selected ? RGB(255, 230, 180) : bg);
			dc.Draw3dRect(chip, selected ? RGB(200, 120, 40) : edge, selected ? RGB(200, 120, 40) : edge);
			dc.SetTextColor(fg);
			dc.DrawText(label, chip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
	}
	dc.SelectObject(oldF);
	if (ev) {
		for (int i = 0; i < evCount; i++) {
			const ScEvent& e = ev[i];
			if (e.ch != (uint8_t)curPart) continue;
			if (e.kind != SC_EV_NOTE && e.kind != SC_EV_FM_NOTE) continue;
			const int midiNote = RollMidi(e, u, curPart);
			const int x0 = gridLeft + ((int)e.tick * pxBeat) / SC_PPQN - scrollX;
			const int x1 = gridLeft + (((int)e.tick + (int)e.dur) * pxBeat) / SC_PPQN - scrollX;
			const int y = NoteY(v, rc, midiNote);
			if (y + v->rowH < keys.top || y > rc.bottom) continue;
			if (x1 < grid.left || x0 > grid.right) continue;
			CRect nr(max(x0, grid.left), y, max(x0 + 2, min(x1, grid.right)), y + v->rowH - 1);
			int sel = 0;
			if (u->nSel > 0) {
				for (int s = 0; s < u->nSel && s < SC_SEL_MAX; s++)
					if (u->selList[s] == i) { sel = 1; break; }
			} else if (u->selEv == i) sel = 1;
			int vel = (e.kind == SC_EV_NOTE) ? (int)e.b : 100;
			if (vel < 1) vel = 100;
			if (vel > 127) vel = 127;
			const int r0 = sel ? 255 : 80;
			const int g0 = sel ? 200 : 170;
			const int b0 = sel ? 80 : 255;
			const int r = (r0 * vel) / 127;
			const int g = (g0 * vel) / 127;
			const int b = (b0 * vel) / 127;
			dc.FillSolidRect(nr, RGB(r, g, b));
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
	if (mx >= grid.left && mx <= grid.right) {
		dc.FillSolidRect(mx, markLane.top, 2, markLane.Height() + grid.Height(), RGB(220, 60, 60));
	}
	if (u->previewActive) {
		const int px = gridLeft + ((int)u->playheadTick * pxBeat) / SC_PPQN - scrollX;
		if (px >= grid.left && px <= grid.right)
			dc.FillSolidRect(px, markLane.top, 2, markLane.Height() + grid.Height(), RGB(80, 220, 120));
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
	if (pt.x < RollGridLeft(rc) || pt.y < KeysTop(rc)) return -1;
	const int pxBeat = v->pxBeat > 0 ? v->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = RollGridLeft(rc);
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
	if (pt.x < RollGridLeft(rc) || pt.y < KeysTop(rc)) return -1;
	const int pxBeat = v->pxBeat > 0 ? v->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = RollGridLeft(rc);
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

int ScPianoRollHitMark(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CPoint pt)
{
	if (!v || !ev || !u) return -1;
	if (!ScPianoRollPtInMarkLane(rc, pt)) return -1;
	const int pxBeat = v->pxBeat > 0 ? v->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = RollGridLeft(rc);
	int best = -1;
	int bestX = -99999;
	for (int i = 0; i < evCount; i++) {
		const ScEvent& e = ev[i];
		if ((int)e.ch != curPart) continue;
		if (!IsRollMark(e, u->isFmScore)) continue;
		const int x = gridLeft + ((int)e.tick * pxBeat) / SC_PPQN - u->scrollX
			+ ScStaffMarkStackIndex(ev, evCount, i) * 12;
		CRect chip(x, rc.top, x + 48, rc.top + SC_ROLL_MARK_H);
		if (chip.PtInRect(pt) && x >= bestX) {
			best = i;
			bestX = x;
		}
	}
	return best;
}

int ScPianoRollHitInRect(const ScPianoRollView* v, const CRect& rc,
	const ScEvent* ev, int evCount, const ScStaffUi* u, int curPart, CRect marquee,
	int* outIdx, int outMax)
{
	if (!v || !ev || !u || !outIdx || outMax <= 0) return 0;
	marquee.NormalizeRect();
	const int pxBeat = v->pxBeat > 0 ? v->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = RollGridLeft(rc);
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
	const int gridLeft = RollGridLeft(rc);
	int rel = x - gridLeft + u->scrollX;
	if (rel < 0) rel = 0;
	return (uint32_t)((rel * SC_PPQN) / max(1, pxBeat));
}

int ScPianoRollYToNote(const ScPianoRollView* v, const CRect& rc, int y)
{
	if (!v) return 60;
	const int topNote = v->noteTop - (v->scrollY / max(1, v->rowH));
	int note = topNote - (y - KeysTop(rc)) / max(1, v->rowH);
	if (note < 0) note = 0;
	if (note > 127) note = 127;
	return note;
}

int ScPianoRollContentWidthPx(const ScStaffUi* u, const ScEvent* ev, int evCount)
{
	if (!u) return SC_PPQN * 8;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	int ticks = u->contentTicks > 0 ? u->contentTicks : (SC_PPQN * 16);
	if (ev && evCount > 0) {
		for (int i = 0; i < evCount; i++) {
			const int end = (int)ev[i].tick + (int)ev[i].dur;
			if (end > ticks) ticks = end;
		}
	}
	if (ticks < SC_PPQN * 8) ticks = SC_PPQN * 8;
	return (ticks * pxBeat) / SC_PPQN + 48;
}

int ScPianoRollContentHeightPx(const ScPianoRollView* v)
{
	const int h = v && v->rowH > 0 ? v->rowH : SC_ROLL_ROW_H_MIN;
	return 128 * h;
}

int ScPianoRollMaxScrollY(const ScPianoRollView* v, int keysH)
{
	const int contentH = ScPianoRollContentHeightPx(v);
	return max(0, contentH - max(1, keysH));
}

int ScPianoRollTimePageW(const CRect& rc)
{
	return max(1, rc.Width() - SC_ROLL_KEY_W);
}

static void RollThumbMetrics(int content, int page, int trackLen, int minThumb,
	int* maxScroll, int* thumb, int* travel)
{
	*maxScroll = max(0, content - max(1, page));
	*thumb = max(minThumb, (page * trackLen) / max(page + *maxScroll, 1));
	if (*thumb > trackLen - 4) *thumb = max(minThumb, trackLen - 4);
	*travel = max(1, trackLen - *thumb - 4);
}

static CRect RollVertTrackRc(const CRect& outer)
{
	const int gw = ScStaffScrollGutterW();
	const int kZ = SC_SB_ZOOM_BTN;
	return CRect(outer.right - gw, outer.top, outer.right - kZ, outer.bottom - ScStaffScrollGutterH());
}

static CRect RollHorzTrackRc(const CRect& outer)
{
	const int gw = ScStaffScrollGutterW();
	const int gh = ScStaffScrollGutterH();
	return CRect(outer.left, outer.bottom - gh, outer.right - gw, outer.bottom - SC_SB_ZOOM_BTN);
}

static void RollPaintZoomBtn(CDC& dc, const CRect& rc, const wchar_t* lab)
{
	dc.FillSolidRect(rc, RGB(248, 248, 252));
	dc.Draw3dRect(rc, RGB(140, 142, 155), RGB(140, 142, 155));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(40, 40, 55));
	dc.DrawText(lab, (LPRECT)&rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void ScPianoRollPaintBars(CDC& dc, const CRect& outer, const ScPianoRollView* v,
	const ScStaffUi* u, const ScEvent* ev, int evCount)
{
	if (!v || !u || outer.Width() < 24 || outer.Height() < 24) return;
	const int gw = ScStaffScrollGutterW();
	const int gh = ScStaffScrollGutterH();
	const int kZ = SC_SB_ZOOM_BTN;
	const int pageW = ScPianoRollTimePageW(CRect(outer.left, outer.top, outer.right - gw, outer.bottom - gh));
	const int keysH = max(1, outer.Height() - gh - SC_ROLL_MARK_H);
	const int contentW = ScPianoRollContentWidthPx(u, ev, evCount);
	const int contentH = ScPianoRollContentHeightPx(v);
	CRect vr = RollVertTrackRc(outer);
	if (vr.Height() > 8) {
		dc.FillSolidRect(vr, RGB(200, 202, 210));
		dc.Draw3dRect(vr, RGB(140, 142, 155), RGB(140, 142, 155));
		int maxY = 0, thumbH = 28, travel = 1;
		RollThumbMetrics(contentH, keysH, vr.Height(), 28, &maxY, &thumbH, &travel);
		int y0 = vr.top + 2;
		if (maxY > 0)
			y0 = vr.top + 2 + (int)(((__int64)v->scrollY * travel) / maxY);
		CRect th(vr.left + 2, y0, vr.right - 2, y0 + thumbH);
		dc.FillSolidRect(th, RGB(70, 95, 160));
		dc.Draw3dRect(th, RGB(120, 145, 200), RGB(40, 50, 80));
	}
	CRect hr = RollHorzTrackRc(outer);
	if (hr.Width() > 8) {
		dc.FillSolidRect(hr, RGB(200, 202, 210));
		dc.Draw3dRect(hr, RGB(140, 142, 155), RGB(140, 142, 155));
		int maxX = 0, thumbW = 36, travel = 1;
		RollThumbMetrics(contentW, pageW, hr.Width(), 36, &maxX, &thumbW, &travel);
		int x0 = hr.left + 2;
		if (maxX > 0)
			x0 = hr.left + 2 + (int)(((__int64)u->scrollX * travel) / maxX);
		CRect th(x0, hr.top + 2, x0 + thumbW, hr.bottom - 2);
		dc.FillSolidRect(th, RGB(70, 95, 160));
		dc.Draw3dRect(th, RGB(120, 145, 200), RGB(40, 50, 80));
	}
	CRect hZoom(outer.right - gw - kZ * 2, outer.bottom - gh, outer.right - gw, outer.bottom);
	dc.Draw3dRect(hZoom, RGB(140, 142, 155), RGB(140, 142, 155));
	RollPaintZoomBtn(dc, CRect(hZoom.left, hZoom.top, hZoom.left + kZ, hZoom.bottom), L"-");
	RollPaintZoomBtn(dc, CRect(hZoom.left + kZ, hZoom.top, hZoom.right, hZoom.bottom), L"+");
	CRect vZoom(outer.right - kZ, outer.bottom - gh - kZ * 2, outer.right, outer.bottom - gh);
	dc.Draw3dRect(vZoom, RGB(140, 142, 155), RGB(140, 142, 155));
	RollPaintZoomBtn(dc, CRect(vZoom.left, vZoom.top, vZoom.right, vZoom.top + kZ), L"-");
	RollPaintZoomBtn(dc, CRect(vZoom.left, vZoom.top + kZ, vZoom.right, vZoom.bottom), L"+");
	CRect corner(outer.right - kZ, outer.bottom - kZ, outer.right, outer.bottom);
	dc.FillSolidRect(corner, RGB(40, 42, 50));
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3 - i; j++)
			dc.FillSolidRect(corner.right - 4 - i * 4, corner.bottom - 4 - j * 4, 2, 2, RGB(140, 145, 160));
}

static int RollMapPos(int pt, int track0, int thumb, int travel, int maxScroll)
{
	int p = pt;
	if (p < track0 + 2 + thumb / 2) p = track0 + 2 + thumb / 2;
	if (p > track0 + 2 + thumb / 2 + travel) p = track0 + 2 + thumb / 2 + travel;
	const int rel = p - (track0 + 2) - thumb / 2;
	if (maxScroll <= 0) return 0;
	return (int)(((__int64)rel * maxScroll) / max(1, travel));
}

int ScPianoRollHitBar(const CRect& outer, const ScPianoRollView* v, const ScStaffUi* u,
	const ScEvent* ev, int evCount, CPoint pt, int* outPos)
{
	if (outPos) *outPos = 0;
	if (!v || !u || !outer.PtInRect(pt)) return 0;
	const int gw = ScStaffScrollGutterW();
	const int gh = ScStaffScrollGutterH();
	const int kZ = SC_SB_ZOOM_BTN;
	CRect hZm(outer.right - gw - kZ * 2, outer.bottom - gh, outer.right - gw - kZ, outer.bottom);
	CRect hZp(outer.right - gw - kZ, outer.bottom - gh, outer.right - gw, outer.bottom);
	CRect vZm(outer.right - kZ, outer.bottom - gh - kZ * 2, outer.right, outer.bottom - gh - kZ);
	CRect vZp(outer.right - kZ, outer.bottom - gh - kZ, outer.right, outer.bottom - gh);
	if (hZm.PtInRect(pt)) return 3;
	if (hZp.PtInRect(pt)) return 4;
	if (vZm.PtInRect(pt)) return 5;
	if (vZp.PtInRect(pt)) return 6;
	const int pageW = ScPianoRollTimePageW(CRect(outer.left, outer.top, outer.right - gw, outer.bottom - gh));
	const int keysH = max(1, outer.Height() - gh - SC_ROLL_MARK_H);
	CRect vr = RollVertTrackRc(outer);
	if (vr.PtInRect(pt) && vr.Height() > 8) {
		int maxY = 0, thumbH = 28, travel = 1;
		RollThumbMetrics(ScPianoRollContentHeightPx(v), keysH, vr.Height(), 28, &maxY, &thumbH, &travel);
		if (outPos) *outPos = RollMapPos(pt.y, vr.top, thumbH, travel, maxY);
		return 1;
	}
	CRect hr = RollHorzTrackRc(outer);
	if (hr.PtInRect(pt) && hr.Width() > 8) {
		int maxX = 0, thumbW = 36, travel = 1;
		RollThumbMetrics(ScPianoRollContentWidthPx(u, ev, evCount), pageW, hr.Width(), 36, &maxX, &thumbW, &travel);
		if (outPos) *outPos = RollMapPos(pt.x, hr.left, thumbW, travel, maxX);
		return 2;
	}
	return 0;
}

int ScPianoRollMapVertDrag(const CRect& outer, const ScPianoRollView* v, int keysH,
	int ptY, int anchorY, int scroll0)
{
	if (!v) return scroll0;
	CRect vr = RollVertTrackRc(outer);
	if (vr.Height() <= 8) return scroll0;
	int maxY = 0, thumbH = 28, travel = 1;
	RollThumbMetrics(ScPianoRollContentHeightPx(v), max(1, keysH), vr.Height(), 28, &maxY, &thumbH, &travel);
	if (maxY <= 0) return 0;
	int pos = scroll0 + (int)(((__int64)(ptY - anchorY) * maxY) / travel);
	if (pos < 0) pos = 0;
	if (pos > maxY) pos = maxY;
	return pos;
}

int ScPianoRollMapHorzDrag(const CRect& outer, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int pageW, int ptX, int anchorX, int scroll0)
{
	if (!u) return scroll0;
	CRect hr = RollHorzTrackRc(outer);
	if (hr.Width() <= 8) return scroll0;
	int maxX = 0, thumbW = 36, travel = 1;
	RollThumbMetrics(ScPianoRollContentWidthPx(u, ev, evCount), max(1, pageW), hr.Width(), 36, &maxX, &thumbW, &travel);
	if (maxX <= 0) return 0;
	int pos = scroll0 + (int)(((__int64)(ptX - anchorX) * maxX) / travel);
	if (pos < 0) pos = 0;
	if (pos > maxX) pos = maxX;
	return pos;
}
