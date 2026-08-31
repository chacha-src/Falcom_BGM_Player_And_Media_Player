#include "stdafx.h"
#include "ogg.h"
#include "CSasamiStaffCore.h"
#include "SasamiToneNames.h"
#include "VstMidiEngine.h"
#include "PlayList.h"
#include "CPromptDlg.h"
#include "CPromptEngine.h"
#include "CMediaPlayerDlg.h"
#include "oggDlg.h"
#include <math.h>

extern CPlayList* pl;
extern COggDlg* og;
extern CMediaPlayerDlg* mp;
extern int plcnt;
extern int gameon;
extern void MpPushPlayHistory(LPCTSTR path, LPCTSTR displayName);

int ScStaffLineGap(const ScStaffUi* u)
{
	int s = u && u->staffScale > 0 ? u->staffScale : 100;
	int g = (SC_STAFF_LINE_GAP0 * s) / 100;
	if (g < 4) g = 4;
	if (g > 16) g = 16;
	return g;
}

int ScStaffH(const ScStaffUi* u)
{
	return ScStaffLineGap(u) * 4 + SC_STAFF_PAD0;
}

int ScStaffRowStaffTop(int rowTop)
{
	return rowTop + SC_PART_GAUGE_H + SC_STAFF_LEDGER_PAD;
}

int ScStaffRowH(const ScStaffUi* u, int track)
{
	if (!u || track < 0 || track >= u->trackCount) return ScStaffH(u);
	if (!u->visible[track]) return 18;
	const int one = ScStaffH(u);
	const int pad = SC_STAFF_LEDGER_PAD * 2;
	/* SSW-like: Tone + Exc/RPN info gauges above staff */
	if (u->clef[track] == 2) return one * 2 + SC_GRAND_STAFF_GAP + SC_PART_GAUGE_H + pad;
	return one + SC_PART_GAUGE_H + pad;
}

int ScStaffStripTotalH(const ScStaffUi* u)
{
	int n = u && u->stripCount > 0 ? u->stripCount : 1;
	if (n > SC_STRIP_LANES_MAX) n = SC_STRIP_LANES_MAX;
	return n * SC_STRIP_LANE_H + 4;
}

void ScStaffUiInit(ScStaffUi* u, int trackCount, int isFm)
{
	if (!u) return;
	memset(u, 0, sizeof(*u));
	u->trackCount = trackCount;
	if (u->trackCount > 32) u->trackCount = 32;
	u->tool = SC_TOOL_PENCIL;
	u->pxBeat = SC_PX_BEAT_DEFAULT;
	u->staffScale = 100;
	u->baseDur = SC_PPQN;
	u->placeDur = SC_PPQN;
	u->snapFit = 1;
	u->selEv = -1;
	u->dragEv = -1;
	u->stripCount = 1;
	u->stripKind[0] = isFm ? SC_STRIP_VOL : SC_STRIP_EXPR;
	u->stripKind[1] = SC_STRIP_PITCH;
	u->stripDraw = SC_STRIP_DRAW_PENCIL;
	u->stripLineAnchorCol = -1;
	u->markerTick = 0;
	u->loopATick = -1;
	u->loopBTick = -1;
	u->playheadTick = 0;
	u->previewActive = 0;
	u->transportMode = 0;
	u->markerSeekArmed = 0;
	u->followViewW = 0;
	u->dragMode = 0;
	u->meterNumer = 4;
	u->meterDenom = 4;
	u->isFmScore = isFm ? 1 : 0;
	for (int i = 0; i < u->trackCount; i++) {
		/* MIDI: show all 32 from the start. FM: all fixed parts. */
		u->visible[i] = 1;
		u->clef[i] = 2; /* default grand; click clef chip to cycle */
		if (isFm) {
			if (i < 3)
				_snwprintf_s(u->names[i], _TRUNCATE, L"FM%d", i + 1);
			else if (i < 6)
				_snwprintf_s(u->names[i], _TRUNCATE, L"SSG%d", i - 2);
			else if (i == 6) {
				_snwprintf_s(u->names[i], _TRUNCATE, L"RHY");
				u->clef[i] = 3; /* YM2608 rhythm pads */
			} else if (i < SC_FM_CH)
				_snwprintf_s(u->names[i], _TRUNCATE, L"FM%d", i - 2);
			else
				_snwprintf_s(u->names[i], _TRUNCATE, L"Misao %d", i - SC_FM_CH + 1);
		} else {
			_snwprintf_s(u->names[i], _TRUNCATE, L"MIDI %d", i + 1);
		}
	}
	for (int L = 0; L < SC_STRIP_LANES_MAX; L++)
		for (int i = 0; i < 256; i++)
			u->strip[L][i] = (u->stripKind[L] == SC_STRIP_PITCH) ? 64 : 100;
	u->contentTicks = SC_PPQN * SC_MEASURE_BEATS * SC_MEASURES_DEFAULT;
	u->contentTracks = u->trackCount;
}

int ScStaffTicksPerMeasure(const ScStaffUi* u)
{
	int numer = (u && u->meterNumer > 0) ? u->meterNumer : 4;
	int denom = (u && u->meterDenom > 0) ? u->meterDenom : 4;
	if (denom < 1) denom = 4;
	int beatTicks = (SC_PPQN * 4) / denom;
	if (beatTicks < 1) beatTicks = 1;
	int t = numer * beatTicks;
	if (t < SC_PPQN) t = SC_PPQN;
	return t;
}

void ScStaffSetMeter(ScStaffUi* u, int numer, int denom)
{
	if (!u) return;
	if (numer < 1) numer = 4;
	if (numer > 32) numer = 32;
	if (denom < 1) denom = 4;
	if (denom > 32) denom = 32;
	u->meterNumer = numer;
	u->meterDenom = denom;
}

int ScStaffBpmFromTempoT(int tempoT)
{
	return (int)((13000.0 * 120.0) / (double)max(1, tempoT) + 0.5);
}

double ScStaffSecFromTick(uint32_t tick, int tempoT)
{
	const double bpm = (double)ScStaffBpmFromTempoT(tempoT);
	return ((double)tick / (double)SC_PPQN) * (60.0 / max(1.0, bpm));
}

uint32_t ScStaffTickFromSec(double sec, int tempoT)
{
	if (sec < 0.0) sec = 0.0;
	const double bpm = (double)ScStaffBpmFromTempoT(tempoT);
	const double ticks = sec * (bpm / 60.0) * (double)SC_PPQN;
	if (ticks > 4000000000.0) return 4000000000u;
	return (uint32_t)(ticks + 0.5);
}

int ScStaffContentHeight(const ScStaffUi* u)
{
	if (!u) return 0;
	int h = SC_RULER_H + 8;
	for (int i = 0; i < u->trackCount; i++)
		h += ScStaffRowH(u, i);
	return h;
}

void ScStaffRecomputePlaceDur(ScStaffUi* u)
{
	if (!u) return;
	int d = u->baseDur;
	if (d < 1) d = SC_PPQN;
	if (u->dotted) d += d / 2;
	if (u->triplet) d = (d * 2) / 3;
	if (d < 1) d = 1;
	u->placeDur = d;
}


int ScStaffScrollTrackW()
{
	return max(16, GetSystemMetrics(SM_CXVSCROLL) + 4);
}
int ScStaffScrollTrackH()
{
	return max(16, GetSystemMetrics(SM_CYHSCROLL) + 4);
}
static int ScStaffSbTrackW() { return ScStaffScrollTrackW(); }
static int ScStaffSbTrackH() { return ScStaffScrollTrackH(); }

void ScStaffPaintScrollThumbs(CDC& dc, const CRect& client, const CRect& body,
	const ScStaffUi* u, int pageW, int pageH)
{
	if (!u) return;
	const int trackW = ScStaffSbTrackW();
	const int trackH = ScStaffSbTrackH();
	/* vertical track — solid opaque (acrylic host must not show through) */
	CRect vr(client.right - trackW, body.top, client.right, max(body.bottom, client.bottom - trackH));
	if (vr.Height() > 8) {
		dc.FillSolidRect(vr, RGB(200, 202, 210));
		dc.Draw3dRect(vr, RGB(140, 142, 155), RGB(140, 142, 155));
		int contentH = ScStaffContentHeight(u);
		int maxY = max(0, contentH - max(1, pageH));
		int thumbH = max(28, (pageH * vr.Height()) / max(pageH + maxY, 1));
		if (thumbH > vr.Height() - 4) thumbH = vr.Height() - 4;
		int y0 = vr.top + 2;
		if (maxY > 0)
			y0 = vr.top + 2 + (int)(((__int64)u->scrollY * (vr.Height() - thumbH - 4)) / maxY);
		CRect th(vr.left + 2, y0, vr.right - 2, y0 + thumbH);
		dc.FillSolidRect(th, RGB(70, 95, 160));
		dc.Draw3dRect(th, RGB(120, 145, 200), RGB(40, 50, 80));
	}
	/* horizontal track */
	CRect hr(body.left, client.bottom - trackH, client.right - trackW, client.bottom);
	if (hr.Width() > 8) {
		dc.FillSolidRect(hr, RGB(200, 202, 210));
		dc.Draw3dRect(hr, RGB(140, 142, 155), RGB(140, 142, 155));
		const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
		int contentW = (u->contentTicks * pxBeat) / SC_PPQN + SC_CLEF_MARGIN;
		int maxX = max(0, contentW - max(1, pageW));
		int thumbW = max(36, (pageW * hr.Width()) / max(pageW + maxX, 1));
		if (thumbW > hr.Width() - 4) thumbW = hr.Width() - 4;
		int x0 = hr.left + 2;
		if (maxX > 0)
			x0 = hr.left + 2 + (int)(((__int64)u->scrollX * (hr.Width() - thumbW - 4)) / maxX);
		CRect th(x0, hr.top + 2, x0 + thumbW, hr.bottom - 2);
		dc.FillSolidRect(th, RGB(70, 95, 160));
		dc.Draw3dRect(th, RGB(120, 145, 200), RGB(40, 50, 80));
	}
}

int ScStaffHitScroll(const CRect& client, const CRect& body, const ScStaffUi* u,
	int pageW, int pageH, CPoint pt, int* outPos)
{
	if (!u) return 0;
	if (outPos) *outPos = 0;
	const int trackW = ScStaffSbTrackW();
	const int trackH = ScStaffSbTrackH();
	CRect vr(client.right - trackW, body.top, client.right, max(body.bottom, client.bottom - trackH));
	CRect hr(body.left, client.bottom - trackH, client.right - trackW, client.bottom);
	if (vr.PtInRect(pt) && vr.Height() > 8) {
		int contentH = ScStaffContentHeight(u);
		int maxY = max(0, contentH - max(1, pageH));
		int thumbH = max(28, (pageH * vr.Height()) / max(pageH + maxY, 1));
		if (thumbH > vr.Height() - 4) thumbH = vr.Height() - 4;
		int travel = max(1, vr.Height() - thumbH - 4);
		int rel = pt.y - (vr.top + 2) - thumbH / 2;
		if (rel < 0) rel = 0;
		if (rel > travel) rel = travel;
		int pos = (maxY > 0) ? (int)(((__int64)rel * maxY) / travel) : 0;
		if (outPos) *outPos = pos;
		return 1;
	}
	if (hr.PtInRect(pt) && hr.Width() > 8) {
		const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
		int contentW = (u->contentTicks * pxBeat) / SC_PPQN + SC_CLEF_MARGIN;
		int maxX = max(0, contentW - max(1, pageW));
		int thumbW = max(36, (pageW * hr.Width()) / max(pageW + maxX, 1));
		if (thumbW > hr.Width() - 4) thumbW = hr.Width() - 4;
		int travel = max(1, hr.Width() - thumbW - 4);
		int rel = pt.x - (hr.left + 2) - thumbW / 2;
		if (rel < 0) rel = 0;
		if (rel > travel) rel = travel;
		int pos = (maxX > 0) ? (int)(((__int64)rel * maxX) / travel) : 0;
		if (outPos) *outPos = pos;
		return 2;
	}
	return 0;
}

void ScStaffUpdateContentExtent(ScStaffUi* u, const ScEvent* ev, int evCount)
{
	if (!u) return;
	uint32_t maxT = (uint32_t)(SC_PPQN * SC_MEASURE_BEATS * SC_MEASURES_DEFAULT);
	for (int i = 0; i < evCount; i++) {
		uint32_t end = ev[i].tick + (ev[i].dur ? ev[i].dur : SC_PPQN);
		/* keep ~8 measures of empty runway after last event */
		uint32_t need = end + (uint32_t)(SC_PPQN * SC_MEASURE_BEATS * 8);
		if (need > maxT) maxT = need;
	}
	if (maxT > (uint32_t)(SC_PPQN * SC_MEASURE_BEATS * SC_MEASURES_MAX))
		maxT = (uint32_t)(SC_PPQN * SC_MEASURE_BEATS * SC_MEASURES_MAX);
	u->contentTicks = (int)maxT;
	u->contentTracks = u->trackCount;
}

static const wchar_t* kOpnaRhythmNames[6] = { L"BD", L"SD", L"TOP", L"HH", L"TOM", L"RIM" };

int ScStaffIsOpnaRhythmTrack(int isFm, int track)
{
	return isFm && track == 6;
}

int ScStaffIsFmSsgTrack(int isFm, int track)
{
	return isFm && track >= 3 && track <= 5;
}

static int ScStaffUiOpnaRhythm(const ScStaffUi* u, int track)
{
	return u && u->isFmScore && track == 6;
}

static int ScStaffRhythmPadFromY(int y, int staffTop, int gap)
{
	const int top = staffTop + 8;
	const int bot = staffTop + 8 + gap * 4;
	int h = bot - top;
	if (h < 1) h = 1;
	int lane = ((y - top) * 6) / h;
	if (lane < 0) lane = 0;
	if (lane > 5) lane = 5;
	return lane;
}

static int ScStaffRhythmMidiFromPad(int pad)
{
	if (pad < 0) pad = 0;
	if (pad > 5) pad = 5;
	return 36 + pad * 2;
}

static int ScStaffRhythmPadFromMidi(int midiNote)
{
	int pad = (midiNote - 36) / 2;
	if (pad < 0) pad = 0;
	if (pad > 5) pad = 5;
	return pad;
}

static int ScStaffRhythmPadY(int staffTop, int gap, int pad)
{
	if (pad < 0) pad = 0;
	if (pad > 5) pad = 5;
	const int top = staffTop + 8;
	const int bot = staffTop + 8 + gap * 4;
	return top + (pad * (bot - top) + 3) / 6;
}

int ScStaffMidiNoteY(int noteMidi, int staffTop, int lineGap)
{
	return ScStaffMidiNoteYClef(noteMidi, staffTop, lineGap, 0);
}

int ScStaffYToMidiNote(int y, int staffTop, int lineGap)
{
	return ScStaffYToMidiNoteClef(y, staffTop, lineGap, 0);
}

/* Treble: bottom=E4(64). Bass: bottom=G2(43). Drum: bottom=C1(36) for kits. */
int ScStaffMidiNoteYClef(int noteMidi, int staffTop, int lineGap, int clefBass)
{
	const int base = (clefBass == 2) ? 36 : (clefBass ? 43 : 64);
	const int steps = noteMidi - base;
	return staffTop + 8 + lineGap * 4 - (steps * lineGap) / 2;
}

int ScStaffYToMidiNoteClef(int y, int staffTop, int lineGap, int clefBass)
{
	if (lineGap < 1) lineGap = SC_STAFF_LINE_GAP0;
	const int base = (clefBass == 2) ? 36 : (clefBass ? 43 : 64);
	const int baseY = staffTop + 8 + lineGap * 4;
	const int dy = baseY - y;
	int note = base + (dy * 2) / lineGap;
	if (note < 0) note = 0;
	if (note > 127) note = 127;
	return note;
}

static int ScClefMode(const ScStaffUi* u, int track)
{
	if (!u || track < 0 || track >= 32) return 0;
	int c = u->clef[track];
	if (c < 0) c = 0;
	if (c > 3) c = 3;
	return c;
}

int ScStaffMidiNoteYTrack(const ScStaffUi* u, int track, int staffTop, int noteMidi)
{
	const int gap = ScStaffLineGap(u);
	if (ScStaffUiOpnaRhythm(u, track) && ScClefMode(u, track) == 3) {
		const int pad = ScStaffRhythmPadFromMidi(noteMidi);
		return ScStaffRhythmPadY(staffTop, gap, pad);
	}
	const int cm = ScClefMode(u, track);
	if (cm == 3)
		return ScStaffMidiNoteYClef(noteMidi, staffTop, gap, 2);
	if (cm == 2) {
		const int one = ScStaffH(u);
		if (noteMidi < 60)
			return ScStaffMidiNoteYClef(noteMidi, staffTop + one + SC_GRAND_STAFF_GAP, gap, 1);
		return ScStaffMidiNoteYClef(noteMidi, staffTop, gap, 0);
	}
	return ScStaffMidiNoteYClef(noteMidi, staffTop, gap, cm == 1);
}

int ScStaffYToMidiNoteTrack(const ScStaffUi* u, int track, int staffTop, int y)
{
	const int gap = ScStaffLineGap(u);
	if (ScStaffUiOpnaRhythm(u, track) && ScClefMode(u, track) == 3)
		return ScStaffRhythmMidiFromPad(ScStaffRhythmPadFromY(y, staffTop, gap));
	const int cm = ScClefMode(u, track);
	if (cm == 3)
		return ScStaffYToMidiNoteClef(y, staffTop, gap, 2);
	if (cm == 2) {
		const int one = ScStaffH(u);
		const int bassTop = staffTop + one + SC_GRAND_STAFF_GAP;
		const int splitY = staffTop + one / 2 + SC_GRAND_STAFF_GAP / 2;
		/* Clear split: above mid → treble, below → bass (avoid sticky O5C/MIDI60). */
		if (y >= splitY)
			return ScStaffYToMidiNoteClef(y, bassTop, gap, 1);
		return ScStaffYToMidiNoteClef(y, staffTop, gap, 0);
	}
	return ScStaffYToMidiNoteClef(y, staffTop, gap, cm == 1);
}

int ScStaffTickToX(uint32_t tick, int scrollX, int gridLeft, int pxBeat)
{
	if (pxBeat < 1) pxBeat = SC_PX_BEAT_DEFAULT;
	return gridLeft + (int)((tick * (uint32_t)pxBeat) / (uint32_t)SC_PPQN) - scrollX;
}

uint32_t ScStaffXToTick(int x, int scrollX, int gridLeft, int pxBeat, int quantTicks)
{
	if (pxBeat < 1) pxBeat = SC_PX_BEAT_DEFAULT;
	int px = x - gridLeft + scrollX;
	if (px < 0) px = 0;
	uint32_t tick = (uint32_t)((px * SC_PPQN) / pxBeat);
	int q = quantTicks > 0 ? quantTicks : (SC_PPQN / 4);
	tick = (tick / (uint32_t)q) * (uint32_t)q;
	return tick;
}

const wchar_t* ScStaffStripKindName(int kind)
{
	switch (kind) {
	case SC_STRIP_EXPR: return L"Expression";
	case SC_STRIP_VOL: return L"Volume";
	case SC_STRIP_PITCH: return L"Pitch";
	case SC_STRIP_GATE: return L"Gate%";
	default: return L"?";
	}
}

void ScStaffDrawRestGlyph(CDC& dc, int x, int y, int durTicks, COLORREF col)
{
	/* GDI-only — Segoe UI Symbol musical rests often render as tofu/"I". */
	dc.SetBkMode(TRANSPARENT);
	if (durTicks >= SC_PPQN * 4) {
		/* whole: block hanging from 2nd line */
		dc.FillSolidRect(x + 2, y - 10, 12, 5, col);
	} else if (durTicks >= SC_PPQN * 2) {
		/* half: block sitting on 3rd line */
		dc.FillSolidRect(x + 2, y - 2, 12, 5, col);
	} else if (durTicks >= SC_PPQN) {
		/* quarter: classic zig-zag */
		POINT pts[6] = {
			{ x + 8, y - 14 }, { x + 4, y - 6 }, { x + 10, y - 2 },
			{ x + 4, y + 6 }, { x + 8, y + 12 }, { x + 6, y + 14 }
		};
		CPen pen(PS_SOLID, 2, col);
		CPen* op = dc.SelectObject(&pen);
		dc.Polyline(pts, 6);
		dc.SelectObject(op);
	} else {
		/* 8th / 16th / 32nd: stem + hooks */
		const int hooks = (durTicks <= SC_PPQN / 8) ? 3 : (durTicks <= SC_PPQN / 4 ? 2 : 1);
		dc.FillSolidRect(x + 8, y - 14, 2, 22, col);
		for (int h = 0; h < hooks; h++) {
			const int hy = y - 12 + h * 6;
			POINT tri[3] = { { x + 10, hy }, { x + 18, hy + 4 }, { x + 10, hy + 7 } };
			CBrush br(col);
			CBrush* ob = dc.SelectObject(&br);
			dc.Polygon(tri, 3);
			dc.SelectObject(ob);
		}
	}
}

void ScStaffDrawNoteGlyph(CDC& dc, const CRect& rc, int durTicks, int rest, int selected)
{
	const int cx = (rc.left + rc.right) / 2;
	const int cy = (rc.top + rc.bottom) / 2;
	dc.FillSolidRect(rc, selected ? RGB(200, 215, 245) : RGB(255, 255, 255));
	dc.Draw3dRect(rc, RGB(150, 150, 165), RGB(150, 150, 165));
	dc.SetBkMode(TRANSPARENT);
	if (rest) {
		ScStaffDrawRestGlyph(dc, cx - 6, cy, durTicks, RGB(20, 20, 40));
		CFont tiny;
		tiny.CreateFont(11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		CFont* of = dc.SelectObject(&tiny);
		dc.SetTextColor(RGB(80, 80, 100));
		wchar_t lab[16];
		if (durTicks >= SC_PPQN * 4) wcscpy_s(lab, L"R1");
		else if (durTicks >= SC_PPQN * 2) wcscpy_s(lab, L"R1/2");
		else if (durTicks >= SC_PPQN) wcscpy_s(lab, L"R1/4");
		else if (durTicks >= SC_PPQN / 2) wcscpy_s(lab, L"R1/8");
		else if (durTicks >= SC_PPQN / 4) wcscpy_s(lab, L"R1/16");
		else wcscpy_s(lab, L"R1/32");
		CRect lr(rc.left, rc.bottom - 14, rc.right, rc.bottom - 1);
		dc.DrawText(lab, lr, DT_CENTER | DT_SINGLELINE);
		dc.SelectObject(of);
		return;
	}
	const int hollow = (durTicks >= SC_PPQN * 2);
	const int whole = (durTicks >= SC_PPQN * 4);
	const int flags = (durTicks <= SC_PPQN / 16) ? 4
		: (durTicks <= SC_PPQN / 8) ? 3
		: (durTicks <= SC_PPQN / 4) ? 2
		: (durTicks <= SC_PPQN / 2) ? 1 : 0;
	int hx = cx - 5, hy = cy + (whole ? 0 : 4);
	CRect head(hx, hy - 4, hx + 11, hy + 5);
	if (hollow) {
		CBrush br(RGB(255, 255, 255));
		CPen pen(PS_SOLID, 2, RGB(20, 20, 35));
		CBrush* ob = dc.SelectObject(&br);
		CPen* op = dc.SelectObject(&pen);
		dc.Ellipse(head);
		dc.SelectObject(ob);
		dc.SelectObject(op);
	} else {
		dc.FillSolidRect(head, RGB(20, 20, 35));
	}
	if (!whole) {
		int stemX = hx + 10;
		int stemTop = hy - 18;
		dc.FillSolidRect(stemX, stemTop, 2, hy - stemTop, RGB(20, 20, 35));
		for (int f = 0; f < flags; f++) {
			int fy = stemTop + 2 + f * 5;
			POINT pts[3] = { { stemX + 1, fy }, { stemX + 10, fy + 3 }, { stemX + 1, fy + 6 } };
			CBrush br(RGB(20, 20, 35));
			CBrush* ob = dc.SelectObject(&br);
			dc.Polygon(pts, 3);
			dc.SelectObject(ob);
		}
	}
	CFont tiny;
	tiny.CreateFont(11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	CFont* oldF = dc.SelectObject(&tiny);
	dc.SetTextColor(RGB(80, 80, 100));
	wchar_t lab[16];
	if (durTicks >= SC_PPQN * 4) wcscpy_s(lab, L"1");
	else if (durTicks >= SC_PPQN * 2) wcscpy_s(lab, L"1/2");
	else if (durTicks >= SC_PPQN) wcscpy_s(lab, L"1/4");
	else if (durTicks >= SC_PPQN / 2) wcscpy_s(lab, L"1/8");
	else if (durTicks >= SC_PPQN / 4) wcscpy_s(lab, L"1/16");
	else if (durTicks >= SC_PPQN / 8) wcscpy_s(lab, L"1/32");
	else _snwprintf_s(lab, _TRUNCATE, L"%d", durTicks);
	CRect lr(rc.left, rc.bottom - 14, rc.right, rc.bottom - 1);
	dc.DrawText(lab, lr, DT_CENTER | DT_SINGLELINE);
	dc.SelectObject(oldF);
}

HCURSOR ScStaffCreateBlankCursor(void)
{
	BYTE andBits[32 * 4];
	BYTE xorBits[32 * 4];
	memset(andBits, 0xFF, sizeof(andBits));
	memset(xorBits, 0x00, sizeof(xorBits));
	return ::CreateCursor(::GetModuleHandle(NULL), 0, 0, 32, 32, andBits, xorBits);
}

void ScStaffPaintTracks(CDC& dc, const CRect& rc, const ScStaffUi* u, int curTrack)
{
	dc.FillSolidRect(rc, RGB(236, 238, 244));
	dc.FillSolidRect(rc.right - 1, rc.top, 1, rc.Height(), RGB(190, 190, 200));
	/* SelectStockObject — never SelectObject(GetStockObject) then SelectObject(CFont*). */
	CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
	dc.SetBkMode(TRANSPARENT);
	int y = rc.top + SC_RULER_H + 4 - u->scrollY;
	for (int i = 0; i < u->trackCount; i++) {
		const int rowH = ScStaffRowH(u, i);
		if (y + rowH < rc.top) { y += rowH; continue; }
		if (y > rc.bottom) break;
		if (i == curTrack)
			dc.FillSolidRect(rc.left, y, 5, rowH - 2, RGB(40, 110, 220));
		else if (!u->visible[i])
			dc.FillSolidRect(rc.left + 1, y, rc.Width() - 2, rowH - 1, RGB(220, 222, 228));
		else
			dc.FillSolidRect(rc.left + 1, y, rc.Width() - 2, rowH - 1, RGB(242, 243, 248));

		dc.SetTextColor(u->visible[i] ? RGB(20, 20, 40) : RGB(130, 130, 145));
		CRect nameRc(rc.left + 6, y + 1, rc.left + 88, y + 17);
		dc.DrawText(u->names[i], nameRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		/* Clickable clef chip: G / F / G+F / Dr */
		const wchar_t* clefTag = (u->clef[i] == 3) ? L"Dr"
			: (u->clef[i] == 2) ? L"G+F" : (u->clef[i] == 1 ? L"F" : L"G");
		CRect clefBox(rc.left + 90, y + 1, rc.left + 128, y + 17);
		dc.FillSolidRect(clefBox, RGB(255, 250, 230));
		dc.Draw3dRect(clefBox, RGB(160, 120, 40), RGB(160, 120, 40));
		dc.SetTextColor(RGB(100, 60, 20));
		dc.DrawText(clefTag, clefBox, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		if (u->mute[i] || u->solo[i]) {
			wchar_t ms[8];
			_snwprintf_s(ms, _TRUNCATE, L"%s%s", u->mute[i] ? L"M" : L"", u->solo[i] ? L"S" : L"");
			dc.SetTextColor(RGB(140, 40, 40));
			dc.TextOut(rc.left + 132, y + 2, ms);
		}

		const int gaugeH = u->visible[i] ? SC_PART_GAUGE_H : 0;
		if (gaugeH > 0) {
			/* SSW-like lane labels: Tone / Exc·RPN (event chips live on the score). */
			const int toneTop = y + SC_NAME_BAND_H;
			CRect toneBox(rc.left + 6, toneTop, rc.right - 4, toneTop + SC_CTRL_LANE_H - 2);
			dc.FillSolidRect(toneBox, RGB(255, 255, 255));
			dc.Draw3dRect(toneBox, RGB(100, 110, 140), RGB(100, 110, 140));
			dc.SetTextColor(RGB(24, 28, 48));
			wchar_t tone[96];
			if (u->vstLabel[i][0])
				_snwprintf_s(tone, _TRUNCATE, L"Tone  %s", u->vstLabel[i]);
			else
				_snwprintf_s(tone, _TRUNCATE, L"Tone");
			dc.DrawText(tone, toneBox, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

			const int excTop = toneTop + SC_CTRL_LANE_H;
			CRect excBox(rc.left + 6, excTop, rc.right - 4, excTop + SC_CTRL_LANE_H - 2);
			dc.FillSolidRect(excBox, RGB(244, 248, 252));
			dc.Draw3dRect(excBox, RGB(120, 140, 160), RGB(120, 140, 160));
			dc.SetTextColor(RGB(70, 90, 110));
			dc.DrawText(L"Exc / RPN", excBox, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		y += rowH;
	}
	if (old) dc.SelectObject(old);
}

static void DrawClefG(CDC& dc, int x, int staffTop, int staffH)
{
	dc.SetTextColor(RGB(30, 30, 40));
	CFont font;
	font.CreateFont(staffH - 8, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
	CFont* old = dc.SelectObject(&font);
	dc.TextOut(x, staffTop - 2, L"𝄞");
	dc.SelectObject(old);
}

static void DrawClefF(CDC& dc, int x, int staffTop, int staffH)
{
	dc.SetTextColor(RGB(30, 30, 40));
	CFont font;
	font.CreateFont(staffH - 10, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
	CFont* old = dc.SelectObject(&font);
	dc.TextOut(x, staffTop + 2, L"𝄢");
	dc.SelectObject(old);
}

static void DrawClefDr(CDC& dc, int x, int staffTop, int staffH)
{
	/* Percussion/kit staff — label is clearer than a missing glyph. */
	dc.SetTextColor(RGB(30, 30, 40));
	CFont font;
	font.CreateFont(max(12, staffH / 2), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	CFont* old = dc.SelectObject(&font);
	dc.TextOut(x, staffTop + staffH / 3, L"Dr");
	dc.SelectObject(old);
}

static void DrawFiveLines(CDC& dc, int x0, int w, int staffTop, int gap)
{
	for (int li = 0; li < 5; li++) {
		int y = staffTop + 8 + li * gap;
		dc.FillSolidRect(x0, y, w, 1, RGB(40, 40, 50));
	}
}

static void DrawGrandBrace(CDC& dc, int x, int y0, int y1)
{
	dc.FillSolidRect(x + 2, y0 + 4, 3, max(1, y1 - y0 - 8), RGB(50, 50, 70));
	dc.FillSolidRect(x, y0 + 2, 5, 3, RGB(50, 50, 70));
	dc.FillSolidRect(x, y1 - 5, 5, 3, RGB(50, 50, 70));
}

static void DrawGhostNote(CDC& dc, int x, int y, int dur, int rest)
{
	if (rest) {
		ScStaffDrawRestGlyph(dc, x, y, dur, RGB(100, 140, 220));
		return;
	}
	const int hollow = (dur >= SC_PPQN * 2);
	CRect head(x, y - 3, x + 10, y + 4);
	if (hollow) dc.Draw3dRect(head, RGB(80, 130, 220), RGB(80, 130, 220));
	else dc.FillSolidRect(head, RGB(80, 130, 220));
	if (dur < SC_PPQN * 4)
		dc.FillSolidRect(x + 9, y - 18, 1, 18, RGB(80, 130, 220));
}

static void ScStaffFormatTickPos(uint32_t tick, wchar_t* out, size_t outCch)
{
	const uint32_t mlen = (uint32_t)(SC_PPQN * SC_MEASURE_BEATS);
	const uint32_t meas = tick / mlen;
	const uint32_t rem = tick % mlen;
	const uint32_t beat = rem / (uint32_t)SC_PPQN;
	const uint32_t clock = rem % (uint32_t)SC_PPQN;
	_snwprintf_s(out, outCch, _TRUNCATE, L"%u:%u:%u", meas, beat, clock);
}

static void ScStaffFormatTickPosUi(const ScStaffUi* u, uint32_t tick, wchar_t* out, size_t outCch)
{
	const uint32_t mlen = (uint32_t)ScStaffTicksPerMeasure(u);
	const int beatTicks = max(1, (SC_PPQN * 4) / max(1, u && u->meterDenom > 0 ? u->meterDenom : 4));
	const uint32_t meas = tick / mlen;
	const uint32_t rem = tick % mlen;
	const uint32_t beat = rem / (uint32_t)beatTicks;
	const uint32_t clock = rem % (uint32_t)beatTicks;
	_snwprintf_s(out, outCch, _TRUNCATE, L"%u:%u:%u", meas, beat, clock);
}

/* Bank at/before tick for this track (SC_EV_BANK a=MSB b=LSB). */
static void ScStaffBankAt(const ScEvent* ev, int evCount, int tr, uint32_t tick, int* msb, int* lsb)
{
	if (msb) *msb = 0;
	if (lsb) *lsb = 0;
	if (!ev) return;
	for (int i = 0; i < evCount; i++) {
		if ((int)ev[i].ch != tr) continue;
		if (ev[i].kind != SC_EV_BANK) continue;
		if (ev[i].tick > tick) continue;
		if (msb) *msb = (int)ev[i].a;
		if (lsb) *lsb = (int)ev[i].b;
	}
}

static int ScStaffIsDummyProgName(const wchar_t* name)
{
	if (!name || !name[0]) return 1;
	if (_wcsnicmp(name, L"MIDI Channel", 12) == 0) return 1;
	if (_wcsnicmp(name, L"Program ", 8) == 0) return 1;
	if (_wcsnicmp(name, L"Program", 7) == 0 && name[7] == 0) return 1;
	return 0;
}

/* HALion etc.: prefer live VST program name; else "PluginStem:XXXX" hash. */
static int ScStaffVstProgName(int part1to32, int progIdx, const wchar_t* tipStem,
	wchar_t* out, int outCch)
{
	if (!out || outCch <= 0) return 0;
	out[0] = 0;
	if (part1to32 < 1 || part1to32 > 32) return 0;
	if (!VstLivePartIsLoaded(part1to32)) return 0;

	wchar_t nm[64];
	nm[0] = 0;
	if (progIdx >= 0 && VstLiveProgramName(part1to32, progIdx, nm, 64) && !ScStaffIsDummyProgName(nm)) {
		wcsncpy_s(out, outCch, nm, _TRUNCATE);
		return 1;
	}
	/* Current program if index unknown / empty */
	if (progIdx < 0) {
		const int cur = VstLiveProgramCurrent(part1to32);
		if (cur >= 0 && VstLiveProgramName(part1to32, cur, nm, 64) && !ScStaffIsDummyProgName(nm)) {
			wcsncpy_s(out, outCch, nm, _TRUNCATE);
			return 1;
		}
		progIdx = cur >= 0 ? cur : 0;
	}

	wchar_t path[520];
	path[0] = 0;
	VstLivePartGetPath(part1to32, path, 520);
	const wchar_t* base = tipStem;
	wchar_t stem[40];
	stem[0] = 0;
	if (!base || !base[0]) {
		const wchar_t* p = wcsrchr(path, L'\\');
		if (!p) p = wcsrchr(path, L'/');
		p = p ? p + 1 : path;
		wcsncpy_s(stem, p, _TRUNCATE);
		wchar_t* dot = wcsrchr(stem, L'.');
		if (dot) *dot = 0;
		base = stem[0] ? stem : L"VST";
	}
	unsigned h = 2166136261u;
	for (const wchar_t* p = path[0] ? path : base; *p; ++p)
		h = (h ^ (unsigned)*p) * 16777619u;
	h ^= (unsigned)progIdx * 0x9E3779B9u;
	_snwprintf_s(out, outCch, _TRUNCATE, L"%s:%04X", base, (h >> 16) & 0xFFFFu);
	return 1;
}

static void ScStaffProgLabel(const ScEvent& e, const ScEvent* ev, int evCount, int tr,
	const ScStaffUi* u, wchar_t* label, size_t labelCch)
{
	wchar_t tpos[24];
	ScStaffFormatTickPosUi(u, e.tick, tpos, 24);
	wchar_t name[48];
	name[0] = 0;
	int msb = 0, lsb = 0, pc = (int)e.a & 127;
	if (e.c == 1) {
		msb = (int)e.b & 127;
		lsb = 0;
	} else {
		ScStaffBankAt(ev, evCount, tr, e.tick, &msb, &lsb);
	}

	const wchar_t* tip = (u && tr >= 0 && tr < 32 && u->vstLabel[tr][0]) ? u->vstLabel[tr] : NULL;
	/* VST part loaded / labeled → live name or Plugin:hash (HALion MediaBay etc.) */
	if (tip || VstLivePartIsLoaded(tr + 1)) {
		if (ScStaffVstProgName(tr + 1, pc, tip, name, 48) && name[0]) {
			_snwprintf_s(label, labelCch, _TRUNCATE, L"%s %s", tpos, name);
			return;
		}
	}

	const int isDrum = (tr == 9 || tr == 25);
	SasamiToneLookupAuto(msb, lsb, pc, isDrum, name, 48);
	if (!name[0])
		_snwprintf_s(name, _TRUNCATE, L"PC%u", (unsigned)(pc + 1));
	_snwprintf_s(label, labelCch, _TRUNCATE, L"%s %s", tpos, name);
}

static void ScStaffFormatMidiNoteName(int note, wchar_t* out, size_t outCch)
{
	if (!out || outCch < 2) return;
	static const wchar_t* kN[12] = {
		L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"
	};
	if (note < 0 || note > 127) {
		wcsncpy_s(out, outCch, L"---", _TRUNCATE);
		return;
	}
	_snwprintf_s(out, outCch, _TRUNCATE, L"N=%d (O%d%s)", note, note / 12, kN[note % 12]);
}

static void ScStaffFormatFmNoteByte(int track, uint8_t nb, wchar_t* out, size_t outCch)
{
	if (!out || outCch < 2) return;
	if (track == 6) {
		int pad = nb & 0x0F;
		if (pad < 0) pad = 0;
		if (pad > 5) pad = 5;
		_snwprintf_s(out, outCch, _TRUNCATE, L"%s (pad %d)", kOpnaRhythmNames[pad], pad);
		return;
	}
	static const wchar_t* kN[12] = {
		L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"
	};
	const int scale = nb & 0x0F;
	const int octNib = (nb >> 4) & 0x0F;
	int oct = octNib + 1;
	if (ScStaffIsFmSsgTrack(1, track))
		oct = octNib;
	if (scale < 0 || scale > 11) {
		_snwprintf_s(out, outCch, _TRUNCATE, L"0x%02X", (unsigned)nb);
		return;
	}
	_snwprintf_s(out, outCch, _TRUNCATE, L"O%d%s (0x%02X)", oct, kN[scale], (unsigned)nb);
}

static void ScStaffFormatTrackLabel(const ScStaffUi* u, int isFm, int track, wchar_t* out, size_t outCch)
{
	if (!out || outCch < 2) return;
	if (u && track >= 0 && track < u->trackCount && u->names[track][0]) {
		if (isFm)
			_snwprintf_s(out, outCch, _TRUNCATE, L"[%s]", u->names[track]);
		else
			_snwprintf_s(out, outCch, _TRUNCATE, L"[MIDI %d]", track + 1);
		return;
	}
	if (isFm)
		_snwprintf_s(out, outCch, _TRUNCATE, L"[ch%d]", track + 1);
	else
		_snwprintf_s(out, outCch, _TRUNCATE, L"[MIDI %d]", track + 1);
}

int ScStaffFormatPointerStatus(const ScStaffUi* u, const ScEvent* ev, int evCount,
	int isFm, const CRect& grid, CPoint pt, wchar_t* out, size_t outCch)
{
	if (!u || !out || outCch < 8 || !grid.PtInRect(pt)) return 0;
	if (pt.y < grid.top + SC_RULER_H) return 0;
	if (ScStaffPtInScoreCtrlStrip(grid, u, pt, NULL)) return 0;

	wchar_t tpos[24];
	wchar_t noteLab[64];
	wchar_t trLab[32];
	int hitTr = -1;
	int hit = ScStaffHitNote(grid, u, ev, evCount, isFm, pt, &hitTr);
	if (hit >= 0 && hit < evCount) {
		const ScEvent& e = ev[hit];
		hitTr = (int)e.ch;
		ScStaffFormatTickPosUi(u, e.tick, tpos, 24);
		ScStaffFormatTrackLabel(u, isFm, hitTr, trLab, 32);
		if (isFm) {
			if (e.kind == SC_EV_FM_NOTE)
				ScStaffFormatFmNoteByte(hitTr, e.a, noteLab, 64);
			else if (e.kind == SC_EV_FM_REST)
				wcsncpy_s(noteLab, L"rest", _TRUNCATE);
			else
				_snwprintf_s(noteLab, _TRUNCATE, L"ev %d", (int)e.kind);
		} else if (e.kind == SC_EV_NOTE) {
			ScStaffFormatMidiNoteName((int)e.a, noteLab, 64);
		} else if (e.kind == SC_EV_REST) {
			wcsncpy_s(noteLab, L"rest", _TRUNCATE);
		} else {
			_snwprintf_s(noteLab, _TRUNCATE, L"ev %d", (int)e.kind);
		}
		_snwprintf_s(out, outCch, _TRUNCATE, L"%s %s  %s  tick=%s  dur=%u",
			trLab, noteLab, (hit == u->selEv) ? L"(sel)" : L"",
			tpos, (unsigned)(e.dur ? e.dur : SC_PPQN / 4));
		return 1;
	}

	if (u->hoverValid && u->hoverTrack >= 0) {
		hitTr = u->hoverTrack;
		ScStaffFormatTickPosUi(u, u->hoverTick, tpos, 24);
		ScStaffFormatTrackLabel(u, isFm, hitTr, trLab, 32);
		if (u->placeRest)
			wcsncpy_s(noteLab, L"rest", _TRUNCATE);
		else if (isFm && hitTr == 6) {
			const int pad = ScStaffRhythmPadFromMidi(u->hoverNote);
			_snwprintf_s(noteLab, _TRUNCATE, L"%s (pad %d)", kOpnaRhythmNames[pad], pad);
		} else if (isFm)
			ScStaffFormatFmNoteByte(hitTr, 0, noteLab, 64); /* placeholder */
		else
			ScStaffFormatMidiNoteName(u->hoverNote, noteLab, 64);
		if (isFm && hitTr != 6 && !u->placeRest) {
			uint8_t nb = (uint8_t)(u->hoverNote & 0xFF);
			if (ScStaffIsFmSsgTrack(1, hitTr)) {
				int oct = u->hoverNote / 12;
				int sc = u->hoverNote % 12;
				nb = (uint8_t)(((oct & 0x0F) << 4) | (sc & 0x0F));
			} else {
				int oct = u->hoverNote / 12 - 1;
				int sc = u->hoverNote % 12;
				if (oct < 1) oct = 1;
				nb = (uint8_t)(((oct & 0x0F) << 4) | (sc & 0x0F));
			}
			ScStaffFormatFmNoteByte(hitTr, nb, noteLab, 64);
		}
		_snwprintf_s(out, outCch, _TRUNCATE, L"%s %s  tick=%s  (cursor)",
			trLab, noteLab, tpos);
		return 1;
	}

	/* track-only: Y on a visible staff lane */
	if (hit == -2 && hitTr >= 0) {
		ScStaffFormatTrackLabel(u, isFm, hitTr, trLab, 32);
		_snwprintf_s(out, outCch, _TRUNCATE, L"%s  (move over staff)", trLab);
		return 1;
	}
	return 0;
}

static void ScStaffDrawNoteHead(CDC& dc, int x, int y, int dur, COLORREF col, int selected)
{
	const int hollow = (dur >= SC_PPQN * 2);
	const int whole = (dur >= SC_PPQN * 4);
	CRect head(x, y - 4, x + 11, y + 5);
	if (selected) {
		/* Outline only — opaque fill hid staff/grid lines under the note. */
		dc.Draw3dRect(x - 3, y - 20, 18, 28, RGB(220, 140, 40), RGB(220, 140, 40));
	}
	if (hollow) {
		CBrush br(RGB(252, 252, 255));
		CPen pen(PS_SOLID, 2, col);
		CBrush* ob = dc.SelectObject(&br);
		CPen* op = dc.SelectObject(&pen);
		dc.Ellipse(head);
		dc.SelectObject(ob);
		dc.SelectObject(op);
	} else {
		CBrush br(col);
		CPen pen(PS_SOLID, 1, col);
		CBrush* ob = dc.SelectObject(&br);
		CPen* op = dc.SelectObject(&pen);
		dc.Ellipse(head);
		dc.SelectObject(ob);
		dc.SelectObject(op);
	}
	if (!whole) {
		dc.FillSolidRect(x + 10, y - 18, 2, 18, col);
		const int flags = (dur <= SC_PPQN / 16) ? 4
			: (dur <= SC_PPQN / 8) ? 3
			: (dur <= SC_PPQN / 4) ? 2
			: (dur <= SC_PPQN / 2) ? 1 : 0;
		for (int f = 0; f < flags; f++) {
			int fy = y - 18 + 2 + f * 5;
			POINT pts[3] = { { x + 11, fy }, { x + 20, fy + 3 }, { x + 11, fy + 6 } };
			CBrush br(col);
			CBrush* ob = dc.SelectObject(&br);
			dc.Polygon(pts, 3);
			dc.SelectObject(ob);
		}
	}
}

void ScStaffPaintStaves(CDC& dc, const CRect& grid, const ScStaffUi* u,
	const ScEvent* ev, int evCount, int isFm, int curTrack, int docTempoT)
{
	dc.FillSolidRect(grid, RGB(248, 248, 252));
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gap = ScStaffLineGap(u);
	const int staffH = ScStaffH(u);
	const int beatsVisible = max(16, (grid.Width() + u->scrollX) / pxBeat + 4);
	const int quant = (u->snapFit && u->placeDur > 0) ? u->placeDur : (SC_PPQN / 4);
	const int gridLeft = grid.left + SC_CLEF_MARGIN;
	const int staffOriginY = grid.top + SC_RULER_H;

	/* ---- ruler (tempo / measures / marker / A-B) — above staves, no overlap ---- */
	CRect ruler(grid.left, grid.top, grid.right, grid.top + SC_RULER_H);
	dc.FillSolidRect(ruler, RGB(236, 238, 246));
	dc.FillSolidRect(ruler.left, ruler.bottom - 1, ruler.Width(), 1, RGB(170, 172, 190));
	dc.SetBkMode(TRANSPARENT);
	{
		CFont tf;
		tf.CreateFont(13, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
		CFont* ot = dc.SelectObject(&tf);
		int bpmDef = ScStaffBpmFromTempoT(docTempoT);
		dc.SetTextColor(RGB(150, 40, 40));
		wchar_t ts[48];
		const int mn = u->meterNumer > 0 ? u->meterNumer : 4;
		const int md = u->meterDenom > 0 ? u->meterDenom : 4;
		_snwprintf_s(ts, _TRUNCATE, L"♪=%d  %d/%d", bpmDef, mn, md);
		dc.TextOut(grid.left + 6, ruler.top + 4, ts);

		dc.SetTextColor(RGB(90, 90, 110));
		const int mTicks = ScStaffTicksPerMeasure(u);
		const int measVis = max(4, (beatsVisible * SC_PPQN) / max(1, mTicks) + 2);
		for (int m = 0; m < measVis; m++) {
			uint32_t mt = (uint32_t)(m * mTicks);
			int x = ScStaffTickToX(mt, u->scrollX, gridLeft, pxBeat);
			if (x < gridLeft || x > grid.right) continue;
			wchar_t s[16];
			_snwprintf_s(s, _TRUNCATE, L"%d", m + 1);
			dc.TextOut(x + 2, ruler.top + 3, s);
			dc.FillSolidRect(x, ruler.top + 2, 1, SC_RULER_H - 4, RGB(150, 150, 168));
		}
		for (int i = 0; i < evCount; i++) {
			if (ev[i].kind != SC_EV_TEMPO && ev[i].kind != SC_EV_FM_TEMPO) continue;
			int tval = ev[i].a | (ev[i].b << 8);
			if (tval < 1) continue;
			int bpm = ScStaffBpmFromTempoT(tval);
			int x = ScStaffTickToX(ev[i].tick, u->scrollX, gridLeft, pxBeat);
			if (x < gridLeft || x > grid.right) continue;
			dc.SetTextColor(RGB(170, 50, 50));
			_snwprintf_s(ts, _TRUNCATE, L"♪=%d", bpm);
			dc.TextOut(x + 2, ruler.top + 4, ts);
			dc.FillSolidRect(x, ruler.top + 2, 2, SC_RULER_H - 4, RGB(180, 60, 60));
		}
		/* A-B region */
		if (u->loopATick >= 0 && u->loopBTick > u->loopATick) {
			int xa = ScStaffTickToX((uint32_t)u->loopATick, u->scrollX, gridLeft, pxBeat);
			int xb = ScStaffTickToX((uint32_t)u->loopBTick, u->scrollX, gridLeft, pxBeat);
			if (xb > grid.left && xa < grid.right) {
				int L = max(grid.left, xa);
				int R = min(grid.right, xb);
				dc.FillSolidRect(L, ruler.top + 1, max(1, R - L), SC_RULER_H - 2, RGB(210, 230, 255));
				dc.FillSolidRect(xa, ruler.top + 1, 2, SC_RULER_H - 2, RGB(40, 100, 200));
				dc.FillSolidRect(xb, ruler.top + 1, 2, SC_RULER_H - 2, RGB(40, 100, 200));
				dc.SetTextColor(RGB(30, 80, 160));
				dc.TextOut(xa + 3, ruler.top + 4, L"A");
				dc.TextOut(xb - 10, ruler.top + 4, L"B");
			}
		}
		/* play-from marker */
		{
			int xm = ScStaffTickToX(u->markerTick, u->scrollX, gridLeft, pxBeat);
			if (xm >= grid.left && xm <= grid.right) {
				POINT tri[3] = {
					{ xm, ruler.bottom - 2 },
					{ xm - 5, ruler.top + 3 },
					{ xm + 5, ruler.top + 3 }
				};
				CBrush br(RGB(220, 80, 40));
				CBrush* ob = dc.SelectObject(&br);
				dc.SelectStockObject(NULL_PEN);
				dc.Polygon(tri, 3);
				dc.SelectObject(ob);
			}
		}
		dc.SelectObject(ot);
	}

	int yCursor = staffOriginY - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[tr]) {
			if (rowTop + rowH >= grid.top && rowTop <= grid.bottom)
				dc.FillSolidRect(grid.left, max(rowTop, staffOriginY), grid.Width(),
					min(rowH, grid.bottom - max(rowTop, staffOriginY)), RGB(245, 246, 250));
			continue;
		}
		if (rowTop + rowH < staffOriginY) continue;
		if (rowTop > grid.bottom) break;

		const int staffTop = ScStaffRowStaffTop(rowTop);
		const int isCur = (tr == curTrack);
		const int cm = ScClefMode(u, tr);
		const int lineX0 = gridLeft;
		const int lineW = max(1, grid.right - lineX0);
		if (isCur)
			dc.FillSolidRect(grid.left, rowTop, 6, rowH - 4, RGB(50, 120, 230));

		/* SSW: Tone lane + Exc/RPN lane above the staff */
		{
			const int toneTop = rowTop + SC_NAME_BAND_H;
			const int toneBot = toneTop + SC_CTRL_LANE_H;
			const int excTop = toneBot;
			const int excBot = staffTop;
			if (toneTop > staffOriginY) {
				const int y0 = max(rowTop, staffOriginY);
				if (toneTop > y0)
					dc.FillSolidRect(grid.left, y0, grid.Width(), toneTop - y0, RGB(242, 243, 248));
			}
			const int yClip0 = max(toneTop, staffOriginY);
			if (toneBot > yClip0 && toneBot <= grid.bottom)
				dc.FillSolidRect(grid.left, yClip0, grid.Width(), min(toneBot, grid.bottom) - yClip0, RGB(250, 248, 255));
			if (excBot > max(excTop, staffOriginY) && excTop < grid.bottom)
				dc.FillSolidRect(grid.left, max(excTop, staffOriginY), grid.Width(),
					min(excBot, grid.bottom) - max(excTop, staffOriginY), RGB(244, 248, 252));
			dc.FillSolidRect(grid.left, toneBot, grid.Width(), 1, RGB(210, 208, 220));
			dc.FillSolidRect(grid.left, excBot - 1, grid.Width(), 1, RGB(200, 205, 215));

			dc.SetBkMode(TRANSPARENT);
			CGdiObject* oldF = dc.SelectStockObject(DEFAULT_GUI_FONT);
			for (int i = 0; i < evCount; i++) {
				const ScEvent& e = ev[i];
				if ((int)e.ch != tr) continue;
				wchar_t label[96];
				label[0] = 0;
				int lane = 0; /* 0=Tone 1=Exc */
				COLORREF bg = RGB(230, 225, 250);
				COLORREF fg = RGB(50, 35, 110);
				COLORREF edge = RGB(120, 100, 170);
				wchar_t tpos[24];
				ScStaffFormatTickPosUi(u, e.tick, tpos, 24);
				if (!isFm && e.kind == SC_EV_PROG) {
					lane = 0;
					ScStaffProgLabel(e, ev, evCount, tr, u, label, 96);
				} else if (!isFm && e.kind == SC_EV_BANK) {
					lane = 0;
					_snwprintf_s(label, _TRUNCATE, L"%s Bank %u/%u", tpos, (unsigned)e.a, (unsigned)e.b);
					bg = RGB(235, 242, 250); fg = RGB(40, 70, 110); edge = RGB(100, 130, 160);
				} else if (isFm && e.kind == SC_EV_FM_VOICE) {
					lane = 0;
					_snwprintf_s(label, _TRUNCATE, L"%s Voice %u", tpos, (unsigned)e.a);
					bg = RGB(225, 245, 235); fg = RGB(30, 90, 60); edge = RGB(80, 150, 110);
				} else if (!isFm && e.kind == SC_EV_VOL) {
					lane = 1;
					_snwprintf_s(label, _TRUNCATE, L"%s Vol %u", tpos, (unsigned)e.a);
					bg = RGB(232, 240, 250); fg = RGB(30, 70, 110); edge = RGB(90, 130, 160);
				} else if (!isFm && e.kind == SC_EV_PAN) {
					lane = 1;
					_snwprintf_s(label, _TRUNCATE, L"%s Pan %u", tpos, (unsigned)e.a);
					bg = RGB(232, 240, 250); fg = RGB(30, 70, 110); edge = RGB(90, 130, 160);
				} else if (!isFm && e.kind == SC_EV_VELO) {
					lane = 1;
					_snwprintf_s(label, _TRUNCATE, L"%s Expr %u", tpos, (unsigned)e.a);
					bg = RGB(232, 240, 250); fg = RGB(30, 70, 110); edge = RGB(90, 130, 160);
				} else if (e.kind == SC_EV_JUMP_MARK) {
					lane = 0;
					_snwprintf_s(label, _TRUNCATE, L"%s Q", tpos);
					bg = RGB(255, 242, 200); fg = RGB(130, 70, 10); edge = RGB(200, 140, 50);
				} else if (e.kind == SC_EV_FM_JUMP) {
					lane = 0;
					_snwprintf_s(label, _TRUNCATE, L"%s J", tpos);
					bg = RGB(255, 220, 220); fg = RGB(140, 30, 30); edge = RGB(200, 70, 70);
				} else if (e.kind == SC_EV_FM_LOOP_START) {
					lane = 0;
					_snwprintf_s(label, _TRUNCATE, L"%s |:%u", tpos, (unsigned)(e.a ? e.a : 2));
					bg = RGB(215, 232, 255); fg = RGB(20, 60, 130); edge = RGB(60, 110, 180);
				} else if (e.kind == SC_EV_FM_LOOP_END) {
					lane = 0;
					_snwprintf_s(label, _TRUNCATE, L"%s :|", tpos);
					bg = RGB(215, 232, 255); fg = RGB(20, 60, 130); edge = RGB(60, 110, 180);
				} else
					continue;
				int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat);
				if (x < lineX0 - 40 || x > grid.right) continue;
				CSize sz = dc.GetTextExtent(label);
				const int chipH = SC_CTRL_LANE_H - 4;
				const int ly = (lane == 0) ? (toneTop + 2) : (excTop + 2);
				CRect chip(x, ly, x + sz.cx + 10, ly + chipH);
				const int selected = (u->selEv == i);
				dc.FillSolidRect(chip, selected ? RGB(255, 230, 180) : bg);
				dc.Draw3dRect(chip, selected ? RGB(200, 120, 40) : edge, selected ? RGB(200, 120, 40) : edge);
				dc.SetTextColor(fg);
				dc.DrawText(label, chip, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			}
			if (oldF) dc.SelectObject(oldF);
		}

		if (cm == 2) {
			const int bassTop = staffTop + staffH + SC_GRAND_STAFF_GAP;
			DrawGrandBrace(dc, grid.left + 2, staffTop + 4, bassTop + staffH - 8);
			DrawClefG(dc, grid.left + 10, staffTop, staffH);
			DrawClefF(dc, grid.left + 10, bassTop, staffH);
			DrawFiveLines(dc, lineX0, lineW, staffTop, gap);
			DrawFiveLines(dc, lineX0, lineW, bassTop, gap);
		} else if (cm == 3) {
			DrawClefDr(dc, grid.left + 10, staffTop, staffH);
			DrawFiveLines(dc, lineX0, lineW, staffTop, gap);
			if (ScStaffUiOpnaRhythm(u, tr)) {
				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(RGB(100, 70, 30));
				for (int rp = 0; rp < 6; rp++) {
					const int ly = ScStaffRhythmPadY(staffTop, gap, rp);
					dc.TextOut(lineX0 - 44, ly - 7, kOpnaRhythmNames[rp]);
					dc.FillSolidRect(lineX0 - 4, ly, lineW + 4, 1, RGB(235, 220, 200));
				}
			}
		} else if (cm == 1) {
			DrawClefF(dc, grid.left + 10, staffTop, staffH);
			DrawFiveLines(dc, lineX0, lineW, staffTop, gap);
		} else {
			DrawClefG(dc, grid.left + 10, staffTop, staffH);
			DrawFiveLines(dc, lineX0, lineW, staffTop, gap);
		}

		const int beatH = (cm == 2) ? (staffH * 2 + SC_GRAND_STAFF_GAP - 8) : (gap * 4);
		const int mTicks = ScStaffTicksPerMeasure(u);
		const int beatTicks = max(1, (SC_PPQN * 4) / max(1, u->meterDenom > 0 ? u->meterDenom : 4));
		for (int b = 0; b <= beatsVisible * 4; b++) {
			uint32_t tick = (uint32_t)((b * SC_PPQN) / 4);
			int x = ScStaffTickToX(tick, u->scrollX, gridLeft, pxBeat);
			if (x < lineX0 || x > grid.right) continue;
			const int strong = (mTicks > 0) && ((int)(tick % (uint32_t)mTicks) == 0);
			const int beat = (beatTicks > 0) && ((int)(tick % (uint32_t)beatTicks) == 0);
			COLORREF c = strong ? RGB(70, 70, 95) : (beat ? RGB(185, 185, 200) : RGB(228, 228, 236));
			dc.FillSolidRect(x, staffTop + 8, 1, beatH, c);
		}
		/* Q / J / |: / :| — 五線を貫くマーク（チップと二重表示で位置が分かる） */
		{
			CFont mf;
			mf.CreateFont(12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			CFont* om = dc.SelectObject(&mf);
			dc.SetBkMode(TRANSPARENT);
			for (int i = 0; i < evCount; i++) {
				const ScEvent& e = ev[i];
				if ((int)e.ch != tr) continue;
				COLORREF col = 0;
				const wchar_t* tag = NULL;
				if (e.kind == SC_EV_JUMP_MARK) { col = RGB(200, 130, 30); tag = L"Q"; }
				else if (e.kind == SC_EV_FM_JUMP) { col = RGB(200, 50, 50); tag = L"J"; }
				else if (e.kind == SC_EV_FM_LOOP_START) { col = RGB(40, 100, 190); tag = L"|:"; }
				else if (e.kind == SC_EV_FM_LOOP_END) { col = RGB(40, 100, 190); tag = L":|"; }
				else continue;
				int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat);
				if (x < lineX0 || x > grid.right) continue;
				dc.FillSolidRect(x, staffTop + 6, 2, beatH + 4, col);
				dc.SetTextColor(col);
				dc.TextOut(x + 3, staffTop + 6, tag);
			}
			dc.SelectObject(om);
		}
		if (isCur && u->snapFit && quant > 0) {
			for (uint32_t t = 0; t < (uint32_t)(beatsVisible * SC_PPQN); t += (uint32_t)quant) {
				int x = ScStaffTickToX(t, u->scrollX, gridLeft, pxBeat);
				if (x < lineX0 || x > grid.right) continue;
				dc.FillSolidRect(x, staffTop + 8 + beatH + 1, 1, 3, RGB(120, 160, 220));
			}
		}

		for (int i = 0; i < evCount; i++) {
			const ScEvent& e = ev[i];
			if ((int)e.ch != tr) continue;
			int isNote = isFm ? (e.kind == SC_EV_FM_NOTE) : (e.kind == SC_EV_NOTE);
			int isRest = isFm ? (e.kind == SC_EV_FM_REST) : (e.kind == SC_EV_REST);
			if (!isNote && !isRest) continue;
			int note;
			if (isFm && tr == 6) {
				const int pad = e.a & 0x0F;
				note = ScStaffRhythmMidiFromPad(pad > 5 ? 5 : pad);
			} else if (isFm) {
				note = (((e.a >> 4) & 0x0F) * 12 + (e.a & 0x0F) + 12);
			} else {
				note = (int)e.a;
			}
			int x0 = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat);
			int x1 = ScStaffTickToX(e.tick + (e.dur ? e.dur : SC_PPQN / 4), u->scrollX, gridLeft, pxBeat);
			if (x1 < grid.left || x0 > grid.right) continue;
			int y = isRest ? (staffTop + 8 + gap * 2) : ScStaffMidiNoteYTrack(u, tr, staffTop, note);
			const int selected = (u->selEv == i);
			const uint32_t ph = u->playheadTick;
			const int sounding = u->previewActive && isNote &&
				ph >= e.tick && ph < e.tick + (e.dur ? e.dur : SC_PPQN / 4);
			COLORREF col = selected ? RGB(200, 50, 40) :
				(sounding ? RGB(40, 180, 90) : RGB(18, 18, 28));
			if (sounding) {
				int phase = (int)((ph - e.tick) % 24);
				col = RGB(30 + phase * 4, 200 - phase * 3, 80 + phase * 5);
			}
			if (isRest) {
				ScStaffDrawRestGlyph(dc, x0, y, e.dur ? e.dur : SC_PPQN, col);
			} else {
				ScStaffDrawNoteHead(dc, x0, y, e.dur ? e.dur : SC_PPQN, col, selected);
				int gatePct = (e.c >= 1 && e.c <= 100) ? e.c : 100;
				int gw = max(2, ((x1 - x0) * gatePct) / 100);
				if (x1 - x0 > 12)
					dc.FillSolidRect(x0 + 12, y - 1, min(gw, 48), 2, RGB(100, 120, 180));
			}
		}
	}

	/* full-height playhead (all staves) */
	{
		uint32_t phTick = u->previewActive ? u->playheadTick : u->markerTick;
		int xh = ScStaffTickToX(phTick, u->scrollX, gridLeft, pxBeat);
		if (xh >= gridLeft && xh <= grid.right) {
			COLORREF hc = u->previewActive ? RGB(230, 40, 50) : RGB(200, 120, 120);
			dc.FillSolidRect(xh, staffOriginY, 2, max(1, grid.bottom - staffOriginY), hc);
		}
	}

	if (u->hoverValid && (u->tool == SC_TOOL_PENCIL || u->tool == SC_TOOL_TEMPO) && u->hoverTrack >= 0) {
		int st = ScStaffVisibleLaneStaffTop(grid, u, u->hoverTrack);
		if (st >= 0) {
			int x = ScStaffTickToX(u->hoverTick, u->scrollX, gridLeft, pxBeat);
			if (u->tool == SC_TOOL_TEMPO) {
				dc.SetTextColor(RGB(200, 60, 60));
				dc.TextOut(x, st, L"♪=");
			} else {
				int y = u->placeRest
					? (st + 8 + gap * 2)
					: ScStaffMidiNoteYTrack(u, u->hoverTrack, st, u->hoverNote);
				DrawGhostNote(dc, x, y, u->placeDur, u->placeRest);
			}
		}
	}
}

void ScStaffPaintStrip(CDC& dc, const CRect& rc, const ScStaffUi* u)
{
	dc.FillSolidRect(rc, RGB(245, 246, 250));
	dc.FillSolidRect(rc.left, rc.top, rc.Width(), 1, RGB(180, 180, 190));
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = rc.left + SC_CLEF_MARGIN; /* must match staff gridLeft (caller aligns strip to grid) */
	const int n = max(1, min(SC_STRIP_LANES_MAX, u->stripCount));
	for (int L = 0; L < n; L++) {
		CRect lane(rc.left, rc.top + L * SC_STRIP_LANE_H, rc.right, rc.top + (L + 1) * SC_STRIP_LANE_H);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(60, 60, 80));
		wchar_t title[48];
		_snwprintf_s(title, _TRUNCATE, L"%s [%s]", ScStaffStripKindName(u->stripKind[L]),
			u->stripDraw == SC_STRIP_DRAW_LINE ? L"Line" : L"Pencil");
		dc.TextOut(lane.left + 6, lane.top + 4, title);
		const int top = lane.top + 20;
		const int h = SC_STRIP_LANE_H - 26;
		/* same X mapping as staff: tick = col * (PPQN/2), x = ScStaffTickToX */
		const int beatsVisible = max(16, (lane.Width() + u->scrollX) / pxBeat + 4);
		const int maxCol = min(255, beatsVisible * 2 + 8);
		for (int c = 0; c <= maxCol; c++) {
			uint32_t tick = (uint32_t)(c * SC_PPQN / 2);
			int x = ScStaffTickToX(tick, u->scrollX, gridLeft, pxBeat);
			if (x < gridLeft || x > lane.right) continue;
			int v = u->strip[L][c & 255];
			int bh = (h * v) / 127;
			COLORREF col = (u->stripKind[L] == SC_STRIP_PITCH) ? RGB(180, 120, 60) :
				(u->stripKind[L] == SC_STRIP_GATE) ? RGB(120, 160, 90) : RGB(90, 140, 210);
			int nextX = ScStaffTickToX(tick + SC_PPQN / 2, u->scrollX, gridLeft, pxBeat);
			int bw = max(2, nextX - x - 1);
			dc.FillSolidRect(x, top + h - bh, bw, bh, col);
		}
		/* measure lines to match staff */
		{
			const int mTicks = ScStaffTicksPerMeasure(u);
			const int measVis = max(4, (beatsVisible * SC_PPQN) / max(1, mTicks) + 2);
			for (int m = 0; m < measVis; m++) {
				uint32_t mt = (uint32_t)(m * mTicks);
				int x = ScStaffTickToX(mt, u->scrollX, gridLeft, pxBeat);
				if (x < gridLeft || x > lane.right) continue;
				dc.FillSolidRect(x, top, 1, h, RGB(160, 160, 175));
			}
		}
		dc.FillSolidRect(gridLeft, top, 1, h, RGB(160, 160, 170));
		dc.FillSolidRect(gridLeft, top + h, lane.right - gridLeft, 1, RGB(160, 160, 170));
	}
}

int ScStaffHitTrack(const CRect& trackRc, const ScStaffUi* u, CPoint pt)
{
	if (!trackRc.PtInRect(pt)) return -1;
	int y = trackRc.top + SC_RULER_H + 4 - u->scrollY;
	for (int i = 0; i < u->trackCount; i++) {
		const int rowH = ScStaffRowH(u, i);
		if (pt.y >= y && pt.y < y + rowH) return i;
		y += rowH;
	}
	return -1;
}

int ScStaffHitGauge(const CRect& trackRc, const ScStaffUi* u, CPoint pt, int* outZone)
{
	if (outZone) *outZone = SC_GAUGE_NONE;
	const int tr = ScStaffHitTrack(trackRc, u, pt);
	if (tr < 0 || !u->visible[tr]) return -1;
	const int rowTop = ScStaffTrackRowTop(trackRc, u, tr);
	if (rowTop < 0) return tr;
	const int gy = pt.y - rowTop;
	if (gy < 0 || gy >= SC_PART_GAUGE_H) return tr;
	if (outZone) {
		if (gy < SC_NAME_BAND_H) {
			/* Name band: clef chip at x ~90..128 relative to track column */
			const int lx = pt.x - trackRc.left;
			*outZone = (lx >= 88 && lx < 132) ? SC_GAUGE_CLEF : SC_GAUGE_NONE;
		} else if (gy < SC_NAME_BAND_H + SC_CTRL_LANE_H) *outZone = SC_GAUGE_TONE;
		else if (gy < SC_PART_GAUGE_H) *outZone = SC_GAUGE_PROG;
		else *outZone = SC_GAUGE_NONE;
	}
	return tr;
}

int ScStaffHitRulerTick(const CRect& grid, const ScStaffUi* u, CPoint pt, uint32_t* outTick)
{
	if (!u || !grid.PtInRect(pt)) return 0;
	if (pt.y < grid.top || pt.y >= grid.top + SC_RULER_H) return 0;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int quant = SC_PPQN; /* snap to beat for transport */
	uint32_t tick = ScStaffXToTick(pt.x, u->scrollX, grid.left + SC_CLEF_MARGIN, pxBeat, quant);
	if (outTick) *outTick = tick;
	return 1;
}

int ScStaffHitNote(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int isFm, CPoint pt, int* outTrack)
{
	if (outTrack) *outTrack = -1;
	if (!grid.PtInRect(pt)) return -1;
	if (pt.y < grid.top + SC_RULER_H) return -1;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	int yCursor = grid.top + SC_RULER_H - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[tr]) continue;
		if (pt.y < rowTop || pt.y >= rowTop + rowH) continue;
		if (outTrack) *outTrack = tr;
		const int staffTop = ScStaffRowStaffTop(rowTop);
		/* Control strip (Prog/Bank/Voice): not a note-placement zone */
		if (pt.y < staffTop) {
			if (outTrack) *outTrack = tr;
			return -1;
		}
		for (int i = evCount - 1; i >= 0; i--) {
			const ScEvent& e = ev[i];
			if ((int)e.ch != tr) continue;
			int isNote = isFm ? (e.kind == SC_EV_FM_NOTE) : (e.kind == SC_EV_NOTE);
			int isRest = isFm ? (e.kind == SC_EV_FM_REST) : (e.kind == SC_EV_REST);
			if (!isNote && !isRest) continue;
			int note;
			if (isFm && tr == 6) {
				const int pad = e.a & 0x0F;
				note = ScStaffRhythmMidiFromPad(pad > 5 ? 5 : pad);
			} else if (isFm) {
				note = (((e.a >> 4) & 0x0F) * 12 + (e.a & 0x0F) + 12);
			} else {
				note = (int)e.a;
			}
			int x0 = ScStaffTickToX(e.tick, u->scrollX, grid.left + SC_CLEF_MARGIN, pxBeat);
			int y = isRest ? (staffTop + 8 + ScStaffLineGap(u) * 2)
				: ScStaffMidiNoteYTrack(u, tr, staffTop, note);
			CRect head(x0 - 2, y - 8, x0 + 16, y + 10);
			if (head.PtInRect(pt)) return i;
		}
		return -2;
	}
	return -1;
}

int ScStaffHitScoreCtrl(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int isFm, CPoint pt, int* outTrack)
{
	if (outTrack) *outTrack = -1;
	if (!u || !ev || !grid.PtInRect(pt)) return -1;
	if (pt.y < grid.top + SC_RULER_H) return -1;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = grid.left + SC_CLEF_MARGIN;
	int yCursor = grid.top + SC_RULER_H - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[tr]) continue;
		const int staffTop = ScStaffRowStaffTop(rowTop);
		if (pt.y < rowTop + 2 || pt.y >= staffTop) continue;
		if (outTrack) *outTrack = tr;
		for (int i = evCount - 1; i >= 0; i--) {
			const ScEvent& e = ev[i];
			if ((int)e.ch != tr) continue;
			wchar_t label[96];
			label[0] = 0;
			int lane = 0;
			wchar_t tpos[24];
			ScStaffFormatTickPosUi(u, e.tick, tpos, 24);
			if (!isFm && e.kind == SC_EV_PROG) {
				lane = 0;
				ScStaffProgLabel(e, ev, evCount, tr, u, label, 96);
			} else if (!isFm && e.kind == SC_EV_BANK) {
				lane = 0;
				_snwprintf_s(label, _TRUNCATE, L"%s Bank %u/%u", tpos, (unsigned)e.a, (unsigned)e.b);
			} else if (isFm && e.kind == SC_EV_FM_VOICE) {
				lane = 0;
				_snwprintf_s(label, _TRUNCATE, L"%s Voice %u", tpos, (unsigned)e.a);
			} else if (e.kind == SC_EV_JUMP_MARK) {
				lane = 0;
				_snwprintf_s(label, _TRUNCATE, L"%s Q", tpos);
			} else if (e.kind == SC_EV_FM_JUMP) {
				lane = 0;
				_snwprintf_s(label, _TRUNCATE, L"%s J", tpos);
			} else if (e.kind == SC_EV_FM_LOOP_START) {
				lane = 0;
				_snwprintf_s(label, _TRUNCATE, L"%s |:%u", tpos, (unsigned)(e.a ? e.a : 2));
			} else if (e.kind == SC_EV_FM_LOOP_END) {
				lane = 0;
				_snwprintf_s(label, _TRUNCATE, L"%s :|", tpos);
			} else if (!isFm && (e.kind == SC_EV_VOL || e.kind == SC_EV_PAN || e.kind == SC_EV_VELO)) {
				lane = 1;
				_snwprintf_s(label, _TRUNCATE, L"%s X", tpos);
			} else
				continue;
			int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat);
			const int chipW = max(52, (int)wcslen(label) * 7 + 12);
			const int ly = rowTop + SC_NAME_BAND_H + lane * SC_CTRL_LANE_H + 2;
			CRect chip(x, ly, x + chipW, ly + SC_CTRL_LANE_H - 4);
			if (chip.PtInRect(pt)) return i;
		}
		return -2; /* in strip but no chip — still a control zone */
	}
	return -1;
}

int ScStaffPtInScoreCtrlStrip(const CRect& grid, const ScStaffUi* u, CPoint pt, int* outTrack)
{
	if (outTrack) *outTrack = -1;
	if (!u || !grid.PtInRect(pt)) return 0;
	if (pt.y < grid.top + SC_RULER_H) return 0;
	int yCursor = grid.top + SC_RULER_H - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[tr]) continue;
		const int staffTop = ScStaffRowStaffTop(rowTop);
		if (pt.y >= rowTop + 2 && pt.y < staffTop) {
			if (outTrack) *outTrack = tr;
			return 1;
		}
	}
	return 0;
}

void ScStaffEnsureStripFromDoc(ScStaffUi* u, const ScEvent* ev, int evCount, int track)
{
	if (!u) return;
	for (int L = 0; L < SC_STRIP_LANES_MAX; L++) {
		uint8_t def = (u->stripKind[L] == SC_STRIP_PITCH) ? 64 : 100;
		for (int i = 0; i < 256; i++) u->strip[L][i] = def;
	}
	for (int i = 0; i < evCount; i++) {
		const ScEvent& e = ev[i];
		if ((int)e.ch != track) continue;
		int col = (int)((e.tick * 2) / SC_PPQN);
		if (col < 0 || col > 255) continue;
		for (int L = 0; L < u->stripCount && L < SC_STRIP_LANES_MAX; L++) {
			int k = u->stripKind[L];
			if (k == SC_STRIP_EXPR && (e.kind == SC_EV_VELO || (e.kind == SC_EV_NOTE && e.b)))
				u->strip[L][col] = e.kind == SC_EV_NOTE ? e.b : e.a;
			else if (k == SC_STRIP_VOL && (e.kind == SC_EV_VOL || e.kind == SC_EV_FM_VOL))
				u->strip[L][col] = e.a;
			else if (k == SC_STRIP_PITCH && e.kind == SC_EV_PAN)
				u->strip[L][col] = e.a;
			else if (k == SC_STRIP_GATE && (e.kind == SC_EV_NOTE || e.kind == SC_EV_FM_NOTE) && e.c >= 1 && e.c <= 100)
				u->strip[L][col] = e.c;
		}
	}
}

int ScStaffHitStrip(const CRect& stripRc, const ScStaffUi* u, CPoint pt, int* outLane, int* outCol, int* outVal)
{
	if (!u || !stripRc.PtInRect(pt)) return 0;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int n = max(1, min(SC_STRIP_LANES_MAX, u->stripCount));
	int lane = (pt.y - stripRc.top) / SC_STRIP_LANE_H;
	if (lane < 0) lane = 0;
	if (lane >= n) lane = n - 1;
	CRect lr(stripRc.left, stripRc.top + lane * SC_STRIP_LANE_H, stripRc.right, stripRc.top + (lane + 1) * SC_STRIP_LANE_H);
	const int left = lr.left + SC_CLEF_MARGIN;
	const int top = lr.top + 20;
	const int h = SC_STRIP_LANE_H - 26;
	if (h < 1 || pt.x < left) return 0;
	/* col from same tick mapping as staff */
	uint32_t tick = ScStaffXToTick(pt.x, u->scrollX, left, pxBeat, SC_PPQN / 2);
	int col = (int)((tick * 2) / SC_PPQN);
	if (col < 0) col = 0;
	if (col > 255) col = 255;
	int v = ((top + h - pt.y) * 127) / h;
	if (v < 0) v = 0;
	if (v > 127) v = 127;
	if (outLane) *outLane = lane;
	if (outCol) *outCol = col;
	if (outVal) *outVal = v;
	return 1;
}

static int ScPushEv(ScEvent* ev, int* count, uint32_t tick, uint8_t ch, uint8_t kind, uint8_t a, uint8_t b, uint8_t c, uint16_t dur)
{
	if (!ev || !count || *count >= SC_EV_MAX) return 0;
	ScEvent& e = ev[*count];
	e.tick = tick; e.ch = ch; e.kind = kind; e.a = a; e.b = b; e.c = c; e.dur = dur;
	(*count)++;
	return 1;
}

static void ApplyOneStripMidi(ScMidiDoc* d, int track, int kind, const uint8_t* strip)
{
	uint8_t dropKind = SC_EV_VELO;
	if (kind == SC_STRIP_VOL) dropKind = SC_EV_VOL;
	else if (kind == SC_STRIP_PITCH) dropKind = SC_EV_PAN;
	else if (kind == SC_STRIP_GATE) {
		/* bake into notes' c */
		for (int i = 0; i < d->evCount; i++) {
			if (d->ev[i].ch != (uint8_t)track) continue;
			if (d->ev[i].kind != SC_EV_NOTE) continue;
			int col = (int)((d->ev[i].tick * 2) / SC_PPQN);
			if (col >= 0 && col < 256) {
				int g = strip[col];
				if (g < 1) g = 1;
				if (g > 100) g = 100;
				d->ev[i].c = (uint8_t)g;
			}
		}
		return;
	}
	int w = 0;
	for (int i = 0; i < d->evCount; i++) {
		if (d->ev[i].ch == (uint8_t)track && d->ev[i].kind == dropKind) continue;
		d->ev[w++] = d->ev[i];
	}
	d->evCount = w;
	uint8_t last = 0xFF;
	for (int c = 0; c < 256; c++) {
		uint8_t v = strip[c];
		if (v == last) continue;
		last = v;
		uint32_t tick = (uint32_t)(c * SC_PPQN / 2);
		ScPushEv(d->ev, &d->evCount, tick, (uint8_t)track, dropKind, v, 0, 0, 0);
	}
}

void ScStaffApplyStripToDocMidi(ScMidiDoc* d, int track, const ScStaffUi* u)
{
	if (!d || !u || track < 0 || track >= SC_MIDI_CH) return;
	for (int L = 0; L < u->stripCount && L < SC_STRIP_LANES_MAX; L++)
		ApplyOneStripMidi(d, track, u->stripKind[L], u->strip[L]);
}

void ScStaffApplyStripToDocFm(ScFmDoc* d, int track, const ScStaffUi* u)
{
	if (!d || !u || track < 0 || track >= SC_FM_TOTAL) return;
	int w = 0;
	for (int i = 0; i < d->evCount; i++) {
		if (d->ev[i].ch == (uint8_t)track && d->ev[i].kind == SC_EV_FM_VOL) continue;
		d->ev[w++] = d->ev[i];
	}
	d->evCount = w;
	uint8_t last = 0xFF;
	const uint8_t* strip = u->strip[0];
	for (int c = 0; c < 256; c++) {
		uint8_t v = strip[c];
		if (v == last) continue;
		last = v;
		ScPushEv(d->ev, &d->evCount, (uint32_t)(c * SC_PPQN / 2), (uint8_t)track, SC_EV_FM_VOL, v, 0, 0, 0);
	}
}

int ScStaffVisibleLaneStaffTop(const CRect& grid, const ScStaffUi* u, int track)
{
	if (!u) return -1;
	int y = grid.top + SC_RULER_H - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		if (tr == track) {
			if (!u->visible[tr]) return -1;
			return ScStaffRowStaffTop(y);
		}
		y += rowH;
	}
	return -1;
}

int ScStaffTrackRowTop(const CRect& trackRc, const ScStaffUi* u, int track)
{
	if (!u || track < 0 || track >= u->trackCount) return -1;
	int y = trackRc.top + SC_RULER_H + 4 - u->scrollY;
	for (int i = 0; i < u->trackCount; i++) {
		if (i == track) return y;
		y += ScStaffRowH(u, i);
	}
	return -1;
}

int ScStaffStartHostPreview(LPCTSTR path, const ScStaffUi* u, int tempoT)
{
	if (!path || !path[0] || !pl) return 0;
	pl->AddFilePath(path);
	int idx = pl->FindByPath(path);
	if (idx < 0) {
		/* AddFilePath may normalize; scan last entries */
		for (int i = pl->playcnt - 1; i >= 0 && i >= pl->playcnt - 8; --i) {
			if (_tcsicmp(pl->pc[i].fol, path) == 0) { idx = i; break; }
		}
	}
	if (idx < 0 || idx >= pl->playcnt) return 0;
	pl->Get(idx);
	plcnt = idx;
	gameon = 0;
	if (mp) {
		mp->m_abApos = -1;
		mp->m_abBpos = -1;
		mp->m_abLoopCount = 0;
		if (mp->m_seek.GetSafeHwnd())
			mp->m_seek.SetAB(-1, -1);
	}
	if (u && u->loopATick >= 0 && u->loopBTick > u->loopATick && mp && og && ::IsWindow(og->GetSafeHwnd())) {
		/* defer A-B until duration known — mark intent via slider later in Sync */
		(void)tempoT;
	}
	MpPushPlayHistory(pl->pc[idx].fol, pl->pc[idx].name);
	if (!OggPrepareResumeBeforePlayback(pl->pc[idx].fol))
		return 0;
	if (og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());
	return 1;
}

int ScStaffSyncPreviewPlayhead(ScStaffUi* u, int tempoT)
{
	if (!u || !u->previewActive) return 0;

	uint32_t tick = 0;
	int haveTick = 0;
	/* Engine playSample leads audible output (plugin + DS queue). Subtract
	   reported latency and a small pad so the cursor tracks what you hear. */
	__int64 playSmpl = VstMidiGetPlaySample();
	unsigned vt = 0;
	if (playSmpl >= 0) {
		const int lat = VstMidiGetLatencySamples();
		const int rate = VstMidiGetRate();
		const int pad = (rate > 0) ? (rate / 20) : 2205; /* ~50ms */
		playSmpl -= (__int64)lat + (__int64)pad;
		if (playSmpl < 0) playSmpl = 0;
		if (VstMidiTickAtSample(playSmpl, &vt)) {
			tick = vt;
			haveTick = 1;
		}
	}
	if (!haveTick) {
		const __int64 heard = OggGetHeardPcmFrames();
		if (heard >= 0 && VstMidiTickAtSample(heard, &vt)) {
			tick = vt;
			haveTick = 1;
		}
	}
	const double sec = OggGetGdiPlaybackTimeSec();
	if (!haveTick)
		tick = ScStaffTickFromSec(sec, tempoT);

	/* Keep playhead on marker until one-shot seek lands (avoid jump to 0). */
	if (u->markerSeekArmed && u->markerTick > 0 && sec <= 0.05) {
		tick = u->markerTick;
	}

	int changed = (tick != u->playheadTick);
	u->playheadTick = tick;

	/* soft A-B loop in musical time */
	if (u->loopATick >= 0 && u->loopBTick > u->loopATick &&
		(int)tick >= u->loopBTick && og && ::IsWindow(og->GetSafeHwnd()) &&
		og->m_time.GetSafeHwnd()) {
		const double ta = ScStaffSecFromTick((uint32_t)u->loopATick, tempoT);
		const int cur = og->m_time.GetPos();
		if (sec > 0.05) {
			int target = (int)((double)cur * (ta / sec) + 0.5);
			if (target < og->m_time.GetMinValue()) target = og->m_time.GetMinValue();
			og->m_time.SetPos(target);
			og->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, target),
				(LPARAM)og->m_time.GetSafeHwnd());
			u->playheadTick = (uint32_t)u->loopATick;
			changed = 1;
		}
	}
	/* one-shot seek to marker after playback begins */
	if (u->markerSeekArmed && u->markerTick > 0 && sec > 0.05 &&
		og && ::IsWindow(og->GetSafeHwnd()) && og->m_time.GetSafeHwnd()) {
		const double tm = ScStaffSecFromTick(u->markerTick, tempoT);
		const int mx = og->m_time.GetMaxValue();
		const int mn = og->m_time.GetMinValue();
		if (mx > mn && tm > 0.01) {
			const double total = ScStaffSecFromTick(
				u->contentTicks > 0 ? (uint32_t)u->contentTicks : (uint32_t)(SC_PPQN * 64), tempoT);
			if (total > 0.1) {
				int target = mn + (int)((tm / total) * (mx - mn) + 0.5);
				if (target < mn) target = mn;
				if (target > mx) target = mx;
				og->m_time.SetPos(target);
				og->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, target),
					(LPARAM)og->m_time.GetSafeHwnd());
				u->markerSeekArmed = 0;
				changed = 1;
			}
		}
	}

	/* Keep playhead in view (follow scroll). */
	if (u->followViewW > SC_CLEF_MARGIN + 40 && u->pxBeat > 0) {
		const int x = (int)(((__int64)u->playheadTick * (uint32_t)u->pxBeat) / (uint32_t)SC_PPQN);
		const int viewInner = u->followViewW - SC_CLEF_MARGIN;
		const int margin = viewInner / 3;
		const int left = u->scrollX;
		const int right = u->scrollX + viewInner;
		if (x < left + margin || x > right - margin) {
			int nx = x - margin;
			if (nx < 0) nx = 0;
			const int maxX = (u->contentTicks * u->pxBeat) / SC_PPQN;
			if (maxX > viewInner && nx > maxX - viewInner)
				nx = maxX - viewInner;
			if (nx != u->scrollX) {
				u->scrollX = nx;
				changed = 1;
			}
		}
	}
	return changed;
}

int ScStaffWriteCmdRollEq(double t0Sec, double t1Sec, int bandVal0, int bandVal1)
{
	if (t0Sec < 0.0) t0Sec = 0.0;
	if (t1Sec < t0Sec) t1Sec = t0Sec;
	if (bandVal0 < 0) bandVal0 = 0;
	if (bandVal0 > 200) bandVal0 = 200;
	if (bandVal1 < 0) bandVal1 = 0;
	if (bandVal1 > 200) bandVal1 = 200;

	MpPromptSnapshotEvent ev = {};
	ev.c1 = L'E';
	ev.c2 = 0;
	ev.isPreset = FALSE;
	ev.hasVal = TRUE;
	ev.t0 = t0Sec;
	ev.t1 = t1Sec;
	ev.v0 = bandVal0;
	ev.v1 = bandVal1;
	ev.period = 0.0;
	ev.cmdId = 0;

	CString tok = MpPromptFormatToken(ev);
	CString prev = MpPromptSourceText();
	CString next;
	if (prev.IsEmpty())
		next = tok;
	else {
		CString t = prev;
		t.TrimRight();
		next = t + L" " + tok;
	}
	if (next.GetLength() > 14000)
		next = next.Left(14000);

	_tcsncpy(savedata.mpPromptTextLong, next, _countof(savedata.mpPromptTextLong) - 1);
	savedata.mpPromptTextLong[_countof(savedata.mpPromptTextLong) - 1] = 0;
	_tcsncpy(savedata.mpPromptText, next, _countof(savedata.mpPromptText) - 1);
	savedata.mpPromptText[_countof(savedata.mpPromptText) - 1] = 0;

	CString err;
	if (!MpPromptExecute(next, &err))
		return 0;
	if (CPromptDlg* p = MpPromptDlgInstance()) {
		if (::IsWindow(p->GetSafeHwnd()))
			p->ApplyTextFromRoll(next, 0);
	}
	extern void MpPersistSavedataQuick();
	MpPersistSavedataQuick();
	return 1;
}
