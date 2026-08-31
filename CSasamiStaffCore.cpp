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

int ScStaffClampNote(int noteMidi)
{
	if (noteMidi < SC_NOTE_MIDI_MIN) return SC_NOTE_MIDI_MIN;
	if (noteMidi > SC_NOTE_MIDI_MAX) return SC_NOTE_MIDI_MAX;
	return noteMidi;
}

/* Pixels so O0C–O9C sit inside the row — asymmetric (do not mirror max). */
static int ScStaffNoteYRel(int noteMidi, int base, int gap)
{
	noteMidi = ScStaffClampNote(noteMidi);
	return 8 + gap * 4 - ((noteMidi - base) * gap) / 2;
}

static void ScStaffLedgerPadsCalc(const ScStaffUi* u, int track, int* outAbove, int* outBelow)
{
	const int gap = ScStaffLineGap(u);
	const int staffH = ScStaffH(u);
	int cm = 0;
	if (u && track >= 0 && track < 32) {
		cm = u->clef[track];
		if (cm < 0) cm = 0;
		if (cm > 3) cm = 3;
	}
	int above = SC_STAFF_LEDGER_PAD;
	int below = SC_STAFF_LEDGER_PAD;
	if (cm == 2) {
		/* Grand: high notes on treble, low on bass — pads are independent. */
		const int yHi = ScStaffNoteYRel(SC_NOTE_MIDI_MAX, 64, gap);
		const int yLo = ScStaffNoteYRel(SC_NOTE_MIDI_MIN, 43, gap);
		above = (yHi < 0) ? (-yHi + SC_STAFF_LEDGER_PAD) : SC_STAFF_LEDGER_PAD;
		below = (yLo + SC_STAFF_LEDGER_PAD > staffH) ? (yLo + SC_STAFF_LEDGER_PAD - staffH) : SC_STAFF_LEDGER_PAD;
	} else if (cm == 3) {
		const int base = 36;
		const int yHi = ScStaffNoteYRel(SC_NOTE_MIDI_MAX, base, gap);
		const int yLo = ScStaffNoteYRel(SC_NOTE_MIDI_MIN, base, gap);
		above = (yHi < 0) ? (-yHi + SC_STAFF_LEDGER_PAD) : SC_STAFF_LEDGER_PAD;
		below = (yLo + SC_STAFF_LEDGER_PAD > staffH) ? (yLo + SC_STAFF_LEDGER_PAD - staffH) : SC_STAFF_LEDGER_PAD;
	} else {
		const int base = (cm == 1) ? 43 : 64;
		const int yHi = ScStaffNoteYRel(SC_NOTE_MIDI_MAX, base, gap);
		const int yLo = ScStaffNoteYRel(SC_NOTE_MIDI_MIN, base, gap);
		above = (yHi < 0) ? (-yHi + SC_STAFF_LEDGER_PAD) : SC_STAFF_LEDGER_PAD;
		below = (yLo + SC_STAFF_LEDGER_PAD > staffH) ? (yLo + SC_STAFF_LEDGER_PAD - staffH) : SC_STAFF_LEDGER_PAD;
	}
	if (outAbove) *outAbove = above;
	if (outBelow) *outBelow = below;
}

int ScStaffLedgerPadAbove(const ScStaffUi* u, int track)
{
	int a = 0, b = 0;
	ScStaffLedgerPadsCalc(u, track, &a, &b);
	return a;
}

int ScStaffLedgerPadBelow(const ScStaffUi* u, int track)
{
	int a = 0, b = 0;
	ScStaffLedgerPadsCalc(u, track, &a, &b);
	return b;
}

int ScStaffLedgerPad(const ScStaffUi* u, int track)
{
	int a = 0, b = 0;
	ScStaffLedgerPadsCalc(u, track, &a, &b);
	return max(a, b);
}

int ScStaffRowStaffTop(const ScStaffUi* u, int track, int rowTop)
{
	return rowTop + SC_PART_GAUGE_H + ScStaffLedgerPadAbove(u, track);
}

int ScStaffRowNoteAreaTop(int rowTop)
{
	return rowTop + SC_PART_GAUGE_H;
}

int ScStaffRowH(const ScStaffUi* u, int track)
{
	if (!u || track < 0 || track >= u->trackCount) return ScStaffH(u) + SC_PART_GAUGE_H + SC_PART_GAP;
	if (!u->visible[track]) return 18;
	const int one = ScStaffH(u);
	const int padA = ScStaffLedgerPadAbove(u, track);
	const int padB = ScStaffLedgerPadBelow(u, track);
	if (u->clef[track] == 2)
		return one * 2 + SC_GRAND_STAFF_GAP + SC_PART_GAUGE_H + padA + padB + SC_PART_GAP;
	return one + SC_PART_GAUGE_H + padA + padB + SC_PART_GAP;
}

int ScStaffStripTotalH(const ScStaffUi* u)
{
	if (!u || u->stripCount <= 0) return 0;
	int n = u->stripCount;
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
	u->markStack = 0;
	u->selEv = -1;
	u->nSel = 0;
	u->dragEv = -1;
	u->stripCount = 0; /* none until user enables lanes */
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
	u->marqueeOn = 0;
	u->eraseDrag = 0;
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
	const int tp = u->tuplet;
	if (tp == 3) d = (d * 2) / 3;
	else if (tp == 5) d = (d * 4) / 5;
	else if (tp == 6) d = (d * 4) / 6;
	else if (tp == 8) d = (d * 4) / 8;
	else if (u->triplet) d = (d * 2) / 3;
	if (d < 1) d = 1;
	u->placeDur = d;
	u->triplet = (u->tuplet == 3) ? 1 : 0;
}

/* Position snap: never coarser than one beat — whole/half still land on beat grid. */
int ScStaffPlaceQuant(const ScStaffUi* u)
{
	if (!u || !u->snapFit) return SC_PPQN / 4;
	int q = u->placeDur > 0 ? u->placeDur : (SC_PPQN / 4);
	const int beat = max(1, (SC_PPQN * 4) / max(1, u->meterDenom > 0 ? u->meterDenom : 4));
	if (q > beat) q = beat;
	if (q < 1) q = 1;
	return q;
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
	noteMidi = ScStaffClampNote(noteMidi);
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
	return ScStaffClampNote(note);
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
	case SC_STRIP_PAN: return L"Pan";
	default: return L"?";
	}
}


/* ---- note duration / glyph helpers (beams up to 64th, dotted) ---- */
static int ScStaffBaseDurUndot(int dur)
{
	if (dur <= 0) return SC_PPQN;
	static const int kBase[] = {
		SC_PPQN * 4, SC_PPQN * 2, SC_PPQN, SC_PPQN / 2,
		SC_PPQN / 4, SC_PPQN / 8, SC_PPQN / 16, SC_PPQN / 32
	};
	for (int i = 0; i < 8; i++) {
		const int b = kBase[i];
		if (b < 1) continue;
		if (dur == b || dur == b + b / 2) return b;
	}
	int best = SC_PPQN, bd = abs(dur - SC_PPQN);
	for (int i = 0; i < 8; i++) {
		const int b = kBase[i];
		if (b < 1) continue;
		const int d0 = abs(dur - b);
		const int d1 = abs(dur - (b + b / 2));
		if (d0 < bd) { bd = d0; best = b; }
		if (d1 < bd) { bd = d1; best = b; }
	}
	return best;
}

static int ScStaffDurIsDotted(int dur)
{
	if (dur <= 0) return 0;
	const int b = ScStaffBaseDurUndot(dur);
	return (dur == b + b / 2) ? 1 : 0;
}

static int ScStaffFlagCount(int dur)
{
	const int b = ScStaffBaseDurUndot(dur);
	if (b >= SC_PPQN) return 0;
	if (b >= SC_PPQN / 2) return 1;
	if (b >= SC_PPQN / 4) return 2;
	if (b >= SC_PPQN / 8) return 3;
	return 4;
}

/* Sample a cubic Bezier into ptsOut[0..nSamples] (inclusive). */
static void ScStaffBezierSample(POINT p0, POINT p1, POINT p2, POINT p3, POINT* ptsOut, int nSamples)
{
	for (int i = 0; i <= nSamples; i++) {
		const double t = (double)i / (double)nSamples;
		const double u = 1.0 - t;
		const double a = u * u * u, b = 3.0 * u * u * t, c = 3.0 * u * t * t, d = t * t * t;
		ptsOut[i].x = (LONG)(a * p0.x + b * p1.x + c * p2.x + d * p3.x + 0.5);
		ptsOut[i].y = (LONG)(a * p0.y + b * p1.y + c * p2.y + d * p3.y + 0.5);
	}
}

/*
 * GDI rule: never destroy a CPen/CBrush while it is still selected into the DC.
 * Select → draw → Select old object back, then let CPen/CBrush destruct.
 */
static void ScStaffDrawOvalHead(CDC& dc, int x, int y, int hollow, COLORREF col, int dotted)
{
	const int x0 = x, y0 = y - 4, x1 = x + 13, y1 = y + 5;
	CPen pen(PS_SOLID, hollow ? 2 : 1, col);
	CBrush br(hollow ? RGB(248, 248, 252) : col);
	CPen* op = dc.SelectObject(&pen);
	CBrush* ob = dc.SelectObject(&br);
	dc.Ellipse(x0, y0, x1, y1);
	dc.SelectObject(ob);
	dc.SelectObject(op);
	if (dotted)
		dc.FillSolidRect(x + 15, y - 1, 3, 3, col);
}

static void ScStaffDrawCurvedFlag(CDC& dc, int stemX, int tipY, int stemUp, int flags, COLORREF col)
{
	if (flags <= 0) return;
	CBrush br(col);
	CPen pen(PS_SOLID, 1, col);
	CBrush* ob = dc.SelectObject(&br);
	CPen* op = dc.SelectObject(&pen);
	for (int f = 0; f < flags && f < 4; f++) {
		const int fy = stemUp ? (tipY + 2 + f * 6) : (tipY - 2 - f * 6);
		POINT c0, c1, c2, c3;
		if (stemUp) {
			c0 = { stemX + 1, fy };
			c1 = { stemX + 8, fy + 2 };
			c2 = { stemX + 13, fy + 9 };
			c3 = { stemX + 4, fy + 16 };
		} else {
			c0 = { stemX + 1, fy };
			c1 = { stemX + 8, fy - 2 };
			c2 = { stemX + 13, fy - 9 };
			c3 = { stemX + 4, fy - 16 };
		}
		POINT spine[9];
		ScStaffBezierSample(c0, c1, c2, c3, spine, 8);
		POINT poly[20];
		int n = 0;
		for (int i = 0; i <= 8; i++) poly[n++] = spine[i];
		for (int i = 8; i >= 0; i--) {
			poly[n].x = spine[i].x;
			poly[n].y = spine[i].y + (stemUp ? 3 : -3);
			n++;
		}
		dc.Polygon(poly, n);
	}
	dc.SelectObject(op);
	dc.SelectObject(ob);
}

/* One complete note: oval head + stem + curved flags (ghost / palette / lone notes). */
static void ScStaffDrawCompleteNote(CDC& dc, int x, int y, int dur, COLORREF col, int selected, int stemUp)
{
	const int base = ScStaffBaseDurUndot(dur);
	const int hollow = (base >= SC_PPQN * 2) ? 1 : 0;
	const int whole = (base >= SC_PPQN * 4) ? 1 : 0;
	const int flags = ScStaffFlagCount(dur);
	const int dotted = ScStaffDurIsDotted(dur);

	if (selected)
		dc.Draw3dRect(x - 3, y - 22, 22, 34, RGB(220, 140, 40), RGB(220, 140, 40));

	ScStaffDrawOvalHead(dc, x, y, hollow, col, dotted);

	if (!whole) {
		const int stemX = stemUp ? (x + 11) : x;
		const int tipY = stemUp ? (y - 22) : (y + 22);
		const int y0 = min(y, tipY), y1 = max(y, tipY);
		dc.FillSolidRect(stemX, y0, 2, max(1, y1 - y0), col);
		ScStaffDrawCurvedFlag(dc, stemX, tipY, stemUp, flags, col);
	}
}

void ScStaffDrawSymbol(CDC& dc, int x, int y, int durTicks, int rest, COLORREF col, int selected, int stemUp)
{
	ScStaffDrawSymbolEx(dc, x, y, durTicks, rest, col, selected, stemUp, 0);
}

static void ScStaffDrawAccidentalGlyph(CDC& dc, int x, int y, int accidental, COLORREF col)
{
	if (accidental == 0) return;
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(col);
	CFont f;
	f.CreateFont(18, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
	CFont* of = dc.SelectObject(&f);
	const wchar_t* g = (accidental > 0) ? L"♯" : L"♭";
	dc.TextOut(x - 16, y - 12, g);
	dc.SelectObject(of);
}

void ScStaffDrawSymbolEx(CDC& dc, int x, int y, int durTicks, int rest, COLORREF col,
	int selected, int stemUp, int accidental)
{
	if (!rest && accidental)
		ScStaffDrawAccidentalGlyph(dc, x, y, accidental, col);
	if (rest)
		ScStaffDrawRestGlyph(dc, x, y, durTicks, col);
	else
		ScStaffDrawCompleteNote(dc, x, y, durTicks, col, selected, stemUp);
}

static void ScStaffDrawBeamBand(CDC& dc, int x0, int y0, int x1, int y1, int thick, COLORREF col)
{
	if (x1 < x0) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
	POINT pts[4];
	const int h = max(1, thick / 2);
	pts[0] = { x0, y0 - h };
	pts[1] = { x1, y1 - h };
	pts[2] = { x1, y1 + h + 1 };
	pts[3] = { x0, y0 + h + 1 };
	CBrush br(col);
	CPen pen(PS_SOLID, 1, col);
	CBrush* ob = dc.SelectObject(&br);
	CPen* op = dc.SelectObject(&pen);
	dc.Polygon(pts, 4);
	dc.SelectObject(op);
	dc.SelectObject(ob);
}

static void ScStaffDrawTieArc(CDC& dc, int x0, int y0, int x1, int y1, int archUp, COLORREF col)
{
	if (x1 - x0 < 6) return;
	const int midX = (x0 + x1) / 2;
	const int bulge = max(6, min(14, (x1 - x0) / 3));
	POINT pts[4];
	pts[0] = { x0 + 6, y0 };
	pts[3] = { x1 - 2, y1 };
	if (archUp) {
		pts[1] = { midX - 4, y0 - bulge };
		pts[2] = { midX + 4, y1 - bulge };
	} else {
		pts[1] = { midX - 4, y0 + bulge };
		pts[2] = { midX + 4, y1 + bulge };
	}
	CPen pen(PS_SOLID, 1, col);
	CPen* op = dc.SelectObject(&pen);
	dc.PolyBezier(pts, 4);
	dc.SelectObject(op);
}

void ScStaffDrawRestGlyph(CDC& dc, int x, int y, int durTicks, COLORREF col)
{
	dc.SetBkMode(TRANSPARENT);
	const int base = ScStaffBaseDurUndot(durTicks);
	const int dotted = ScStaffDurIsDotted(durTicks);
	if (base >= SC_PPQN * 4) {
		dc.FillSolidRect(x + 2, y - 10, 12, 5, col);
	} else if (base >= SC_PPQN * 2) {
		dc.FillSolidRect(x + 2, y - 2, 12, 5, col);
	} else if (base >= SC_PPQN) {
		POINT pts[6] = {
			{ x + 8, y - 14 }, { x + 4, y - 6 }, { x + 10, y - 2 },
			{ x + 4, y + 6 }, { x + 8, y + 12 }, { x + 6, y + 14 }
		};
		CPen pen(PS_SOLID, 2, col);
		CPen* op = dc.SelectObject(&pen);
		dc.Polyline(pts, 6);
		dc.SelectObject(op);
	} else {
		const int hooks = ScStaffFlagCount(durTicks);
		dc.FillSolidRect(x + 8, y - 14, 2, 22, col);
		CPen pen(PS_SOLID, 2, col);
		CPen* op = dc.SelectObject(&pen);
		for (int h = 0; h < hooks; h++) {
			const int hy = y - 12 + h * 6;
			POINT pts[4] = {
				{ x + 10, hy }, { x + 16, hy + 2 },
				{ x + 17, hy + 7 }, { x + 11, hy + 9 }
			};
			dc.PolyBezier(pts, 4);
		}
		dc.SelectObject(op);
	}
	if (dotted) {
		CBrush br(col);
		CBrush* ob = dc.SelectObject(&br);
		dc.Ellipse(x + 16, y - 1, x + 20, y + 3);
		dc.SelectObject(ob);
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
		const int b = ScStaffBaseDurUndot(durTicks);
		if (b >= SC_PPQN * 4) wcscpy_s(lab, L"R1");
		else if (b >= SC_PPQN * 2) wcscpy_s(lab, L"R1/2");
		else if (b >= SC_PPQN) wcscpy_s(lab, L"R1/4");
		else if (b >= SC_PPQN / 2) wcscpy_s(lab, L"R1/8");
		else if (b >= SC_PPQN / 4) wcscpy_s(lab, L"R1/16");
		else if (b >= SC_PPQN / 8) wcscpy_s(lab, L"R1/32");
		else wcscpy_s(lab, L"R1/64");
		CRect lr(rc.left, rc.bottom - 14, rc.right, rc.bottom - 1);
		dc.DrawText(lab, lr, DT_CENTER | DT_SINGLELINE);
		dc.SelectObject(of);
		return;
	}
	const int whole = (ScStaffBaseDurUndot(durTicks) >= SC_PPQN * 4);
	const int hx = cx - 6, hy = cy + (whole ? 0 : 4);
	ScStaffDrawSymbol(dc, hx, hy, durTicks, 0, RGB(20, 20, 35), 0, 1);
	CFont tiny;
	tiny.CreateFont(11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	CFont* oldF = dc.SelectObject(&tiny);
	dc.SetTextColor(RGB(80, 80, 100));
	wchar_t lab[16];
	const int b = ScStaffBaseDurUndot(durTicks);
	if (b >= SC_PPQN * 4) wcscpy_s(lab, L"1");
	else if (b >= SC_PPQN * 2) wcscpy_s(lab, L"1/2");
	else if (b >= SC_PPQN) wcscpy_s(lab, L"1/4");
	else if (b >= SC_PPQN / 2) wcscpy_s(lab, L"1/8");
	else if (b >= SC_PPQN / 4) wcscpy_s(lab, L"1/16");
	else if (b >= SC_PPQN / 8) wcscpy_s(lab, L"1/32");
	else wcscpy_s(lab, L"1/64");
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

static void DrawGhostNote(CDC& dc, int x, int y, int dur, int rest, int accidental)
{
	ScStaffDrawSymbolEx(dc, x, y, dur, rest, RGB(80, 130, 220), 0, 1, accidental);
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

/* Live program list IPC must NEVER run from paint — Host64 PROGRAMS freezes the
   UI after tone-map OK (SC-VA VST2). Multi → 0 so caller uses GS/XG names.
   Dedicated VST3 → path hash only (HALion rarely has a usable list anyway). */
static int ScStaffVstProgName(int part1to32, int progIdx, const wchar_t* tipStem,
	wchar_t* out, int outCch)
{
	if (!out || outCch <= 0) return 0;
	out[0] = 0;
	if (part1to32 < 1 || part1to32 > 32) return 0;
	if (!VstLivePartIsLoaded(part1to32)) return 0;

	wchar_t path[520];
	path[0] = 0;
	VstLivePartGetPath(part1to32, path, 520);
	if (VstLivePartIsMulti(part1to32) ||
		(path[0] && VstDetectMultiTimbral(path)))
		return 0; /* GS/XG map names via SasamiToneLookupAuto */

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
	if (progIdx < 0) progIdx = 0;
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
	ScStaffDrawSymbol(dc, x, y, dur, 0, col, selected, 1);
}

enum { SC_BEAM_COL_MAX = 768, SC_BEAM_HEAD_MAX = 12 };

struct ScBeamHead {
	int evIdx;
	int y;
	int midi;
	int dur;
	uint8_t pitchA;
	int selected;
	int isTie;
	COLORREF col;
};

struct ScBeamCol {
	uint32_t tick;
	int x;
	int xEnd;
	int nHeads;
	ScBeamHead heads[SC_BEAM_HEAD_MAX];
	int flags;
	int stemUp;
	int stemX;
	int stemAttachY;
	int stemTipY;
	int grp;
	int gatePct;
};

static int ScStaffEvMidiPitch(const ScEvent& e, int isFm, int tr)
{
	if (isFm && tr == 6) {
		const int pad = e.a & 0x0F;
		return ScStaffRhythmMidiFromPad(pad > 5 ? 5 : pad);
	}
	if (isFm)
		return (((e.a >> 4) & 0x0F) * 12 + (e.a & 0x0F) + 12);
	return (int)e.a;
}

static void ScStaffPaintTrackNotes(CDC& dc, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int isFm, int tr, int staffTop, int gridLeft, int gridRight, int pxBeat, int beatTicks)
{
	/* Heap — stack array of 768 cols was ~320KB and corrupted paint (no notes / no ghost). */
	ScBeamCol* cols = (ScBeamCol*)malloc(sizeof(ScBeamCol) * (size_t)SC_BEAM_COL_MAX);
	if (!cols) return;
	int nCol = 0;
	const int gap = ScStaffLineGap(u);
	const int midY = staffTop + 8 + gap * 2;

	for (int i = 0; i < evCount; i++) {
		const ScEvent& e = ev[i];
		if ((int)e.ch != tr) continue;
		const int isNote = isFm ? (e.kind == SC_EV_FM_NOTE) : (e.kind == SC_EV_NOTE);
		const int isTie = (e.kind == SC_EV_TIE);
		const int isRest = isFm ? (e.kind == SC_EV_FM_REST) : (e.kind == SC_EV_REST);
		if (isRest) {
			int x0 = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat);
			if (x0 >= gridLeft - 20 && x0 <= gridRight + 20) {
				const int y = staffTop + 8 + gap * 2;
				const int selected = ScStaffSelHas(u, i);
				COLORREF col = selected ? RGB(200, 50, 40) : RGB(18, 18, 28);
				ScStaffDrawSymbol(dc, x0, y, e.dur ? e.dur : SC_PPQN, 1, col, selected, 1);
			}
			continue;
		}
		if (!isNote && !isTie) continue;
		const int dur = e.dur ? e.dur : (isTie ? SC_PPQN / 2 : SC_PPQN / 4);
		const int note = ScStaffEvMidiPitch(e, isFm, tr);
		const int x0 = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat);
		const int x1 = ScStaffTickToX(e.tick + (uint32_t)dur, u->scrollX, gridLeft, pxBeat);
		if (x1 < gridLeft - 40 || x0 > gridRight + 40) continue;
		const int y = ScStaffMidiNoteYTrack(u, tr, staffTop, note);
		const int selected = ScStaffSelHas(u, i);
		const uint32_t ph = u->playheadTick;
		const int sounding = u->previewActive && isNote &&
			ph >= e.tick && ph < e.tick + (uint32_t)dur;
		COLORREF col = selected ? RGB(200, 50, 40) :
			(sounding ? RGB(40, 180, 90) : RGB(18, 18, 28));
		if (sounding) {
			int phase = (int)((ph - e.tick) % 24);
			col = RGB(30 + phase * 4, 200 - phase * 3, 80 + phase * 5);
		}

		int ci = -1;
		for (int k = 0; k < nCol; k++) {
			if (cols[k].tick == e.tick) { ci = k; break; }
		}
		if (ci < 0 && nCol < SC_BEAM_COL_MAX) {
			ci = nCol++;
			memset(&cols[ci], 0, sizeof(cols[ci]));
			cols[ci].tick = e.tick;
			cols[ci].x = x0;
			cols[ci].xEnd = x1;
			cols[ci].grp = -1;
			cols[ci].gatePct = 100;
		}
		if (ci < 0) continue;
		if (cols[ci].nHeads < SC_BEAM_HEAD_MAX) {
			ScBeamHead& h = cols[ci].heads[cols[ci].nHeads++];
			h.evIdx = i;
			h.y = y;
			h.midi = note;
			h.dur = dur;
			h.pitchA = e.a;
			h.selected = selected;
			h.isTie = isTie;
			h.col = col;
		}
		if (x1 > cols[ci].xEnd) cols[ci].xEnd = x1;
		if (!isTie) {
			int gatePct = (e.c >= 1 && e.c <= 100) ? e.c : 100;
			cols[ci].gatePct = gatePct;
		}
	}

	/* Sort columns by tick for beaming */
	for (int a = 0; a < nCol; a++)
		for (int b = a + 1; b < nCol; b++)
			if (cols[b].tick < cols[a].tick) {
				ScBeamCol tmp = cols[a];
				cols[a] = cols[b];
				cols[b] = tmp;
			}

	/* Sort heads in each column by Y (top first) */
	for (int c = 0; c < nCol; c++) {
		if (cols[c].nHeads <= 0) continue;
		for (int a = 0; a < cols[c].nHeads; a++)
			for (int b = a + 1; b < cols[c].nHeads; b++)
				if (cols[c].heads[b].y < cols[c].heads[a].y) {
					ScBeamHead tmp = cols[c].heads[a];
					cols[c].heads[a] = cols[c].heads[b];
					cols[c].heads[b] = tmp;
				}
		int maxFl = 0;
		int sumY = 0;
		for (int h = 0; h < cols[c].nHeads; h++) {
			int fl = ScStaffFlagCount(cols[c].heads[h].dur);
			if (fl > maxFl) maxFl = fl;
			sumY += cols[c].heads[h].y;
		}
		cols[c].flags = maxFl;
		const int avgY = sumY / cols[c].nHeads;
		cols[c].stemUp = (avgY >= midY) ? 1 : 0;
		const int topY = cols[c].heads[0].y;
		const int botY = cols[c].heads[cols[c].nHeads - 1].y;
		cols[c].stemX = cols[c].x + (cols[c].stemUp ? 11 : 0);
		cols[c].stemAttachY = cols[c].stemUp ? botY : topY;
		cols[c].stemTipY = cols[c].stemUp ? (topY - 20) : (botY + 20);
	}

	/* Beam groups within beat */
	int c = 0;
	while (c < nCol) {
		if (cols[c].flags < 1) {
			cols[c].grp = -1;
			c++;
			continue;
		}
		const uint32_t beat0 = beatTicks > 0 ? (cols[c].tick / (uint32_t)beatTicks) : 0;
		int g0 = c;
		int c2 = c + 1;
		while (c2 < nCol) {
			if (cols[c2].flags < 1) break;
			const uint32_t beat1 = beatTicks > 0 ? (cols[c2].tick / (uint32_t)beatTicks) : 0;
			if (beat1 != beat0) break;
			/* Allow small gaps: next tick should be near previous end */
			const uint32_t prevEnd = cols[c2 - 1].tick + (uint32_t)cols[c2 - 1].heads[0].dur;
			if (cols[c2].tick > prevEnd + (uint32_t)max(1, beatTicks / 8)) break;
			c2++;
		}
		const int glen = c2 - g0;
		for (int i = g0; i < c2; i++)
			cols[i].grp = (glen >= 2) ? g0 : -1;
		c = c2;
	}

	/* Unify stem direction inside each beam group to the first column's side.
	   Mixed up/down made diagonal Z-beams between opposite tips. */
	for (int i = 0; i < nCol; ) {
		if (cols[i].grp < 0 || cols[i].nHeads <= 0) { i++; continue; }
		const int g0 = cols[i].grp;
		int g1 = g0;
		while (g1 < nCol && cols[g1].grp == g0) g1++;
		const int stemUp = cols[g0].stemUp;
		for (int k = g0; k < g1; k++) {
			cols[k].stemUp = stemUp;
			const int topY = cols[k].heads[0].y;
			const int botY = cols[k].heads[cols[k].nHeads - 1].y;
			cols[k].stemX = cols[k].x + (stemUp ? 11 : 0);
			cols[k].stemAttachY = stemUp ? botY : topY;
			cols[k].stemTipY = stemUp ? (topY - 20) : (botY + 20);
		}
		i = g1;
	}

	/* Draw: lone single notes via ScStaffDrawSymbol(dur); chords/beams share stem. */
	for (int i = 0; i < nCol; i++) {
		ScBeamCol& col = cols[i];
		if (col.nHeads <= 0) continue;

		if (col.grp < 0 && col.nHeads == 1 && !col.heads[0].isTie) {
			ScStaffDrawSymbol(dc, col.x, col.heads[0].y, col.heads[0].dur, 0,
				col.heads[0].col, col.heads[0].selected, col.stemUp);
			if (col.xEnd - col.x > 12) {
				int gw = max(2, ((col.xEnd - col.x) * col.gatePct) / 100);
				dc.FillSolidRect(col.x + 12, col.heads[0].y - 1, min(gw, 48), 2, RGB(100, 120, 180));
			}
			continue;
		}

		for (int h = 0; h < col.nHeads; h++) {
			ScBeamHead& hd = col.heads[h];
			const int hollow = (ScStaffBaseDurUndot(hd.dur) >= SC_PPQN * 2) || hd.isTie;
			if (hd.selected)
				dc.Draw3dRect(col.x - 3, hd.y - 22, 20, 32, RGB(220, 140, 40), RGB(220, 140, 40));
			ScStaffDrawOvalHead(dc, col.x, hd.y, hollow, hd.col, ScStaffDurIsDotted(hd.dur));
		}
		const int anyWhole = (col.flags == 0 && ScStaffBaseDurUndot(col.heads[0].dur) >= SC_PPQN * 4
			&& !col.heads[0].isTie);
		if (!anyWhole) {
			const int y0 = min(col.stemAttachY, col.stemTipY);
			const int y1 = max(col.stemAttachY, col.stemTipY);
			dc.FillSolidRect(col.stemX, y0, 2, max(1, y1 - y0), col.heads[0].col);
		}
		if (col.xEnd - col.x > 12 && !col.heads[0].isTie) {
			int gw = max(2, ((col.xEnd - col.x) * col.gatePct) / 100);
			dc.FillSolidRect(col.x + 12, col.heads[col.nHeads / 2].y - 1, min(gw, 48), 2, RGB(100, 120, 180));
		}
	}

	/* Beams / flags (skip lone singles — already drawn complete) */
	for (int i = 0; i < nCol; ) {
		if (cols[i].grp < 0) {
			if (cols[i].nHeads == 1 && !cols[i].heads[0].isTie) {
				i++;
				continue;
			}
			if (cols[i].flags > 0 && cols[i].nHeads > 0) {
				const int whole = ScStaffBaseDurUndot(cols[i].heads[0].dur) >= SC_PPQN * 4
					&& !cols[i].heads[0].isTie;
				if (!whole)
					ScStaffDrawCurvedFlag(dc, cols[i].stemX, cols[i].stemTipY,
						cols[i].stemUp, cols[i].flags, cols[i].heads[0].col);
			}
			i++;
			continue;
		}
		const int g0 = cols[i].grp;
		int g1 = g0;
		while (g1 < nCol && cols[g1].grp == g0) g1++;
		int maxFl = 0;
		for (int k = g0; k < g1; k++)
			if (cols[k].flags > maxFl) maxFl = cols[k].flags;
		/* Beams always dark — selected noteheads must not tint the beam. */
		const COLORREF bcol = RGB(18, 18, 28);
		const int stemUp = cols[g0].stemUp;

		/*
		 * Per beam level L (0=8th, 1=16th, …): draw across each contiguous run
		 * of columns with flags > L.  Example 8+16+16:
		 *   L0 → all three;  L1 → the two 16ths span a full secondary beam.
		 * Isolated short notes get a half-length hook toward the nearer neighbor.
		 */
		for (int level = 0; level < maxFl; level++) {
			const int off = stemUp ? (level * 4) : -(level * 4);
			int k = g0;
			while (k < g1) {
				if (cols[k].flags <= level) { k++; continue; }
				const int run0 = k;
				while (k < g1 && cols[k].flags > level) k++;
				const int run1 = k; /* exclusive */
				if (run1 - run0 >= 2) {
					ScStaffDrawBeamBand(dc,
						cols[run0].stemX, cols[run0].stemTipY + off,
						cols[run1 - 1].stemX, cols[run1 - 1].stemTipY + off,
						3, bcol);
				} else {
					const int alone = run0;
					int toward = -1;
					const int leftOk = (alone > g0);
					const int rightOk = (alone + 1 < g1);
					if (leftOk && rightOk) {
						/* Hook toward the longer (fewer flags) neighbor. */
						toward = (cols[alone - 1].flags <= cols[alone + 1].flags) ? -1 : 1;
					} else if (rightOk) {
						toward = 1;
					} else {
						toward = -1;
					}
					const int nb = alone + toward;
					if (nb < g0 || nb >= g1) continue;
					const int half = max(6, abs(cols[nb].stemX - cols[alone].stemX) / 2);
					const int y = cols[alone].stemTipY + off;
					ScStaffDrawBeamBand(dc,
						cols[alone].stemX, y,
						cols[alone].stemX + toward * half, y,
						3, bcol);
				}
			}
		}
		/* Tuplet bracket when group size matches active tuplet or 3/5/6/8 equal notes in beat */
		{
			const int glen = g1 - g0;
			int tn = 0;
			if (u->tuplet == glen) tn = u->tuplet;
			else if (glen == 3 || glen == 5 || glen == 6 || glen == 8) {
				/* Equal durations filling one beat */
				int same = 1;
				for (int k = g0 + 1; k < g1; k++)
					if (cols[k].heads[0].dur != cols[g0].heads[0].dur) same = 0;
				const uint32_t span = cols[g1 - 1].tick + (uint32_t)cols[g1 - 1].heads[0].dur - cols[g0].tick;
				if (same && beatTicks > 0 && span >= (uint32_t)(beatTicks * 9 / 10)
					&& span <= (uint32_t)(beatTicks * 11 / 10))
					tn = glen;
			}
			if (tn > 0) {
				const int xL = cols[g0].stemX;
				const int xR = cols[g1 - 1].stemX;
				const int yBr = cols[g0].stemUp
					? (min(cols[g0].stemTipY, cols[g1 - 1].stemTipY) - 10)
					: (max(cols[g0].stemTipY, cols[g1 - 1].stemTipY) + 10);
				CPen pen(PS_SOLID, 1, RGB(40, 40, 60));
				CPen* op = dc.SelectObject(&pen);
				dc.MoveTo(xL, yBr + (cols[g0].stemUp ? 4 : -4));
				dc.LineTo(xL, yBr);
				dc.LineTo(xR, yBr);
				dc.LineTo(xR, yBr + (cols[g0].stemUp ? 4 : -4));
				dc.SelectObject(op);
				CFont f;
				f.CreateFont(12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
					OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
				CFont* of = dc.SelectObject(&f);
				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(RGB(40, 40, 60));
				wchar_t num[8];
				_snwprintf_s(num, _TRUNCATE, L"%d", tn);
				CRect trc((xL + xR) / 2 - 8, yBr - 8, (xL + xR) / 2 + 8, yBr + 8);
				dc.FillSolidRect(trc, RGB(248, 248, 252));
				dc.DrawText(num, trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				dc.SelectObject(of);
			}
		}
		i = g1;
	}

	/* Tie arcs: each TIE head → previous same pitchA on track */
	for (int i = 0; i < nCol; i++) {
		for (int h = 0; h < cols[i].nHeads; h++) {
			if (!cols[i].heads[h].isTie) continue;
			const uint8_t pa = cols[i].heads[h].pitchA;
			int found = 0;
			int px = 0, py = 0;
			for (int j = i - 1; j >= 0 && !found; j--) {
				for (int hh = 0; hh < cols[j].nHeads; hh++) {
					if (cols[j].heads[hh].pitchA == pa) {
						px = cols[j].x;
						py = cols[j].heads[hh].y;
						found = 1;
						break;
					}
				}
			}
			if (!found) continue;
			const int archUp = (cols[i].heads[h].y < midY) ? 1 : 0;
			ScStaffDrawTieArc(dc, px, py, cols[i].x, cols[i].heads[h].y, archUp,
				cols[i].heads[h].col);
		}
	}
	free(cols);
}

void ScStaffPaintStaves(CDC& dc, const CRect& grid, const ScStaffUi* u,
	const ScEvent* ev, int evCount, int isFm, int curTrack, int docTempoT)
{
	dc.FillSolidRect(grid, RGB(248, 248, 252));
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gap = ScStaffLineGap(u);
	const int staffH = ScStaffH(u);
	const int beatsVisible = max(16, (grid.Width() + u->scrollX) / pxBeat + 4);
	const int quant = ScStaffPlaceQuant(u);
	const int gridLeft = grid.left + SC_CLEF_MARGIN;
	const int staffOriginY = grid.top + SC_RULER_H;

	/* ---- ruler (tempo / measures / marker / A-B) — above staves, no overlap ---- */
	CRect ruler(grid.left, grid.top, grid.right, grid.top + SC_RULER_H);
	dc.FillSolidRect(ruler, RGB(236, 238, 246));
	dc.FillSolidRect(ruler.left, ruler.bottom - 1, ruler.Width(), 1, RGB(170, 172, 190));
	dc.SetBkMode(TRANSPARENT);
	{
		CFont tf;
		tf.CreateFont(15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
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
				CPen nullPen(PS_NULL, 0, RGB(0, 0, 0));
				CBrush* ob = dc.SelectObject(&br);
				CPen* op = dc.SelectObject(&nullPen);
				dc.Polygon(tri, 3);
				dc.SelectObject(op);
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

		const int staffTop = ScStaffRowStaffTop(u, tr, rowTop);
		const int isCur = (tr == curTrack);
		const int cm = ScClefMode(u, tr);
		const int lineX0 = gridLeft;
		const int lineW = max(1, grid.right - lineX0);
		if (isCur)
			dc.FillSolidRect(grid.left, rowTop, 6, rowH - 4, RGB(50, 120, 230));

		/* SSW: Tone lane + Exc/RPN lane only in PART_GAUGE_H (not ledger pad) */
		{
			const int gaugeBot = rowTop + SC_PART_GAUGE_H;
			const int toneTop = rowTop + SC_NAME_BAND_H;
			const int toneBot = toneTop + SC_CTRL_LANE_H;
			const int excTop = toneBot;
			const int excBot = gaugeBot;
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
			/* Upper ledger (O7–O9): staff paper, not Exc strip */
			if (staffTop > gaugeBot) {
				const int ly0 = max(gaugeBot, staffOriginY);
				if (staffTop > ly0 && ly0 < grid.bottom)
					dc.FillSolidRect(grid.left, ly0, grid.Width(), min(staffTop, grid.bottom) - ly0, RGB(255, 255, 255));
			}
			dc.FillSolidRect(grid.left, toneBot, grid.Width(), 1, RGB(210, 208, 220));
			dc.FillSolidRect(grid.left, gaugeBot - 1, grid.Width(), 1, RGB(200, 205, 215));

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
				} else if (!isFm && e.kind == SC_EV_RPN) {
					lane = 1;
					_snwprintf_s(label, _TRUNCATE, L"%s RPN %u/%u=%u", tpos, (unsigned)e.a, (unsigned)e.b, (unsigned)e.c);
					bg = RGB(242, 235, 250); fg = RGB(70, 45, 110); edge = RGB(140, 110, 170);
				} else if (!isFm && e.kind == SC_EV_NRPN) {
					lane = 1;
					_snwprintf_s(label, _TRUNCATE, L"%s NRPN %u/%u=%u", tpos, (unsigned)e.a, (unsigned)e.b, (unsigned)e.c);
					bg = RGB(242, 235, 250); fg = RGB(70, 45, 110); edge = RGB(140, 110, 170);
				} else if (!isFm && e.kind == SC_EV_SYSEX) {
					lane = 1;
					_snwprintf_s(label, _TRUNCATE, L"%s SysEx #%u", tpos, (unsigned)e.a);
					bg = RGB(250, 240, 230); fg = RGB(120, 65, 25); edge = RGB(185, 120, 70);
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
				} else if (!isFm && e.kind == SC_EV_PEDAL_ON) {
					lane = 0;
					_snwprintf_s(label, _TRUNCATE, L"%s Ped.", tpos);
					bg = RGB(235, 220, 245); fg = RGB(80, 30, 110); edge = RGB(140, 80, 170);
				} else if (!isFm && e.kind == SC_EV_PEDAL_OFF) {
					lane = 0;
					_snwprintf_s(label, _TRUNCATE, L"%s ＊", tpos);
					bg = RGB(235, 220, 245); fg = RGB(80, 30, 110); edge = RGB(140, 80, 170);
				} else
					continue;
				int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat);
				if (ScStaffIsStaffMarkKind(e.kind, isFm))
					x += ScStaffMarkStackIndex(ev, evCount, i) * 12;
				if (x < lineX0 - 40 || x > grid.right) continue;
				CSize sz = dc.GetTextExtent(label);
				const int chipH = SC_CTRL_LANE_H - 4;
				const int ly = (lane == 0) ? (toneTop + 2) : (excTop + 2);
				CRect chip(x, ly, x + sz.cx + 10, ly + chipH);
				const int selected = ScStaffSelHas(u, i);
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
		/* Q / J / |:n / :| / Ped. / ＊ — marks through the staff */
		{
			CFont mf;
			mf.CreateFont(12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			CFont* om = dc.SelectObject(&mf);
			dc.SetBkMode(TRANSPARENT);
			for (int i = 0; i < evCount; i++) {
				const ScEvent& e = ev[i];
				if ((int)e.ch != tr) continue;
				const int stack = ScStaffMarkStackIndex(ev, evCount, i);
				int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat) + stack * 12;
				if (x < lineX0 - 8 || x > grid.right + 8) continue;
				const int y0 = staffTop + 6;
				const int y1 = y0 + beatH + 4;

				if (e.kind == SC_EV_JUMP_MARK) {
					dc.FillSolidRect(x, y0, 2, y1 - y0, RGB(200, 130, 30));
					dc.SetTextColor(RGB(200, 130, 30));
					dc.TextOut(x + 3, y0, L"Q");
				} else if (e.kind == SC_EV_FM_JUMP) {
					dc.FillSolidRect(x, y0, 2, y1 - y0, RGB(200, 50, 50));
					dc.SetTextColor(RGB(200, 50, 50));
					dc.TextOut(x + 3, y0, L"J");
				} else if (e.kind == SC_EV_FM_LOOP_START) {
					/* Start-repeat barline: thin | thick | then two dots */
					const COLORREF lc = RGB(30, 70, 160);
					dc.FillSolidRect(x - 1, y0, 1, y1 - y0, lc);
					dc.FillSolidRect(x + 2, y0, 3, y1 - y0, lc);
					dc.FillSolidRect(x + 8, y0 + (y1 - y0) / 3 - 1, 3, 3, lc);
					dc.FillSolidRect(x + 8, y0 + 2 * (y1 - y0) / 3 - 1, 3, 3, lc);
					dc.SetTextColor(lc);
					wchar_t cnt[16];
					_snwprintf_s(cnt, _TRUNCATE, L"|:%d", e.a ? (int)e.a : 2);
					dc.TextOut(x + 12, y0 - 2, cnt);
				} else if (e.kind == SC_EV_FM_LOOP_END) {
					/* End-repeat: two dots then thick | thin */
					const COLORREF lc = RGB(30, 70, 160);
					dc.FillSolidRect(x - 8, y0 + (y1 - y0) / 3 - 1, 3, 3, lc);
					dc.FillSolidRect(x - 8, y0 + 2 * (y1 - y0) / 3 - 1, 3, 3, lc);
					dc.FillSolidRect(x - 2, y0, 3, y1 - y0, lc);
					dc.FillSolidRect(x + 3, y0, 1, y1 - y0, lc);
					dc.SetTextColor(lc);
					dc.TextOut(x + 6, y0 - 2, L":|");
				} else if (e.kind == SC_EV_PEDAL_ON) {
					dc.SetTextColor(RGB(90, 40, 120));
					dc.TextOut(x - 2, y1 - 2, L"Ped.");
					dc.FillSolidRect(x, y1 + 12, 18, 2, RGB(90, 40, 120));
				} else if (e.kind == SC_EV_PEDAL_OFF) {
					dc.SetTextColor(RGB(90, 40, 120));
					dc.TextOut(x - 2, y1 - 2, L"＊");
				}
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

		ScStaffPaintTrackNotes(dc, u, ev, evCount, isFm, tr, staffTop,
			gridLeft, grid.right, pxBeat, beatTicks);
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
				DrawGhostNote(dc, x, y, u->placeDur, u->placeRest, u->placeAccidental);
			}
		}
	}
}

void ScStaffPaintStrip(CDC& dc, const CRect& rc, const ScStaffUi* u)
{
	if (!u || u->stripCount <= 0 || rc.Height() < 8) return;
	dc.FillSolidRect(rc, RGB(245, 246, 250));
	dc.FillSolidRect(rc.left, rc.top, rc.Width(), 1, RGB(180, 180, 190));
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = rc.left + SC_CLEF_MARGIN;
	const int n = max(0, min(SC_STRIP_LANES_MAX, u->stripCount));
	const wchar_t* drawName = L"Pencil";
	if (u->stripDraw == SC_STRIP_DRAW_LINE) drawName = L"Line";
	else if (u->stripDraw == SC_STRIP_DRAW_CURVE) drawName = L"Curve";
	for (int L = 0; L < n; L++) {
		CRect lane(rc.left, rc.top + L * SC_STRIP_LANE_H, rc.right, rc.top + (L + 1) * SC_STRIP_LANE_H);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(60, 60, 80));
		wchar_t title[64];
		_snwprintf_s(title, _TRUNCATE, L"%s [%s]", ScStaffStripKindName(u->stripKind[L]), drawName);
		dc.TextOut(lane.left + 6, lane.top + 2, title);
		const int top = lane.top + 18;
		const int h = SC_STRIP_LANE_H - 24;
		const int kind = u->stripKind[L];
		const int bipolar = (kind == SC_STRIP_PITCH) ? 1 : 0;
		/* scale marks */
		dc.SetTextColor(RGB(110, 110, 130));
		CFont* oldF = dc.SelectObject(CFont::FromHandle((HFONT)GetStockObject(DEFAULT_GUI_FONT)));
		if (bipolar) {
			dc.TextOut(lane.left + 4, top, L"+100");
			dc.TextOut(lane.left + 4, top + h / 2 - 6, L"0");
			dc.TextOut(lane.left + 4, top + h - 12, L"-100");
			dc.FillSolidRect(gridLeft, top + h / 2, lane.right - gridLeft, 1, RGB(170, 170, 185));
		} else {
			dc.TextOut(lane.left + 4, top, L"100");
			dc.TextOut(lane.left + 4, top + h / 2 - 6, L"50");
			dc.TextOut(lane.left + 4, top + h - 12, L"0");
		}
		if (oldF) dc.SelectObject(oldF);
		const int beatsVisible = max(16, (lane.Width() + u->scrollX) / pxBeat + 4);
		const int maxCol = min(255, beatsVisible * 2 + 8);
		POINT pts[256];
		int npt = 0;
		for (int c = 0; c <= maxCol; c++) {
			uint32_t tick = (uint32_t)(c * SC_PPQN / 2);
			int x = ScStaffTickToX(tick, u->scrollX, gridLeft, pxBeat);
			if (x < gridLeft || x > lane.right) continue;
			int v = u->strip[L][c & 255];
			int bh = (h * v) / 127;
			COLORREF col = (kind == SC_STRIP_PITCH) ? RGB(180, 120, 60) :
				(kind == SC_STRIP_GATE) ? RGB(120, 160, 90) :
				(kind == SC_STRIP_PAN) ? RGB(160, 100, 180) : RGB(90, 140, 210);
			int nextX = ScStaffTickToX(tick + SC_PPQN / 2, u->scrollX, gridLeft, pxBeat);
			int bw = max(2, nextX - x - 1);
			dc.FillSolidRect(x, top + h - bh, bw, bh, col);
			if (npt < 256) {
				pts[npt].x = x + bw / 2;
				pts[npt].y = top + h - bh;
				npt++;
			}
		}
		if (u->stripDraw == SC_STRIP_DRAW_CURVE && npt >= 2) {
			CPen pen(PS_SOLID, 2, RGB(40, 80, 160));
			CPen* op = dc.SelectObject(&pen);
			dc.Polyline(pts, npt);
			dc.SelectObject(op);
		}
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
		const int staffTop = ScStaffRowStaffTop(u, tr, rowTop);
		const int noteTop = ScStaffRowNoteAreaTop(rowTop);
		/* Tone/Exc gauge only — upper ledger is note-placement zone */
		if (pt.y < noteTop) {
			if (outTrack) *outTrack = tr;
			return -1;
		}
		for (int i = evCount - 1; i >= 0; i--) {
			const ScEvent& e = ev[i];
			if ((int)e.ch != tr) continue;
			int isNote = isFm ? (e.kind == SC_EV_FM_NOTE) : (e.kind == SC_EV_NOTE);
			int isTie = (e.kind == SC_EV_TIE);
			int isRest = isFm ? (e.kind == SC_EV_FM_REST) : (e.kind == SC_EV_REST);
			if (!isNote && !isTie && !isRest) continue;
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

int ScStaffIsStaffMarkKind(uint8_t kind, int isFm)
{
	if (kind == SC_EV_JUMP_MARK || kind == SC_EV_FM_JUMP
		|| kind == SC_EV_FM_LOOP_START || kind == SC_EV_FM_LOOP_END)
		return 1;
	if (!isFm && (kind == SC_EV_PEDAL_ON || kind == SC_EV_PEDAL_OFF))
		return 1;
	return 0;
}

int ScStaffMarkStackIndex(const ScEvent* ev, int evCount, int idx)
{
	if (!ev || idx < 0 || idx >= evCount) return 0;
	const ScEvent& e = ev[idx];
	int n = 0;
	for (int i = 0; i < idx; i++) {
		if (ev[i].ch == e.ch && ev[i].kind == e.kind && ev[i].tick == e.tick)
			n++;
	}
	return n;
}

int ScStaffHitStaffMark(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount,
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
		const int noteTop = ScStaffRowNoteAreaTop(rowTop);
		if (pt.y < noteTop || pt.y >= rowTop + rowH) continue;
		if (outTrack) *outTrack = tr;
		const int staffTop = ScStaffRowStaffTop(u, tr, rowTop);
		const int gap = ScStaffLineGap(u);
		const int cm = u->clef[tr] % 4;
		const int staffH = gap * 4;
		const int beatH = (cm == 2) ? (staffH * 2 + SC_GRAND_STAFF_GAP - 8) : staffH;
		const int y0 = staffTop + 6;
		const int y1 = y0 + beatH + 4;
		int best = -1;
		for (int i = 0; i < evCount; i++) {
			const ScEvent& e = ev[i];
			if ((int)e.ch != tr) continue;
			if (!ScStaffIsStaffMarkKind(e.kind, isFm)) continue;
			const int stack = ScStaffMarkStackIndex(ev, evCount, i);
			const int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat) + stack * 12;
			CRect hr;
			if (e.kind == SC_EV_FM_LOOP_START)
				hr.SetRect(x - 4, y0 - 2, x + 28, y1 + 2);
			else if (e.kind == SC_EV_FM_LOOP_END)
				hr.SetRect(x - 14, y0 - 2, x + 18, y1 + 2);
			else if (e.kind == SC_EV_PEDAL_ON || e.kind == SC_EV_PEDAL_OFF)
				hr.SetRect(x - 4, y1 - 6, x + 32, y1 + 22);
			else
				hr.SetRect(x - 2, y0 - 2, x + 18, y0 + 18);
			if (hr.PtInRect(pt)) best = i; /* last (top of stack) wins */
		}
		return best;
	}
	return -1;
}

int ScStaffCollectStaffMarksAt(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int isFm, CPoint pt, int* outIdx, int maxOut)
{
	if (!outIdx || maxOut <= 0) return 0;
	int n = 0;
	int tr = -1;
	/* Re-scan with same geometry; collect all containing pt in stack order. */
	if (!u || !ev || !grid.PtInRect(pt)) return 0;
	if (pt.y < grid.top + SC_RULER_H) return 0;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = grid.left + SC_CLEF_MARGIN;
	int yCursor = grid.top + SC_RULER_H - u->scrollY;
	for (int t = 0; t < u->trackCount; t++) {
		const int rowH = ScStaffRowH(u, t);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[t]) continue;
		const int noteTop = ScStaffRowNoteAreaTop(rowTop);
		if (pt.y < noteTop || pt.y >= rowTop + rowH) continue;
		tr = t;
		const int staffTop = ScStaffRowStaffTop(u, t, rowTop);
		const int gap = ScStaffLineGap(u);
		const int cm = u->clef[t] % 4;
		const int staffH = gap * 4;
		const int beatH = (cm == 2) ? (staffH * 2 + SC_GRAND_STAFF_GAP - 8) : staffH;
		const int y0 = staffTop + 6;
		const int y1 = y0 + beatH + 4;
		for (int i = 0; i < evCount && n < maxOut; i++) {
			const ScEvent& e = ev[i];
			if ((int)e.ch != tr) continue;
			if (!ScStaffIsStaffMarkKind(e.kind, isFm)) continue;
			const int stack = ScStaffMarkStackIndex(ev, evCount, i);
			const int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat) + stack * 12;
			CRect hr;
			if (e.kind == SC_EV_FM_LOOP_START)
				hr.SetRect(x - 4, y0 - 2, x + 28, y1 + 2);
			else if (e.kind == SC_EV_FM_LOOP_END)
				hr.SetRect(x - 14, y0 - 2, x + 18, y1 + 2);
			else if (e.kind == SC_EV_PEDAL_ON || e.kind == SC_EV_PEDAL_OFF)
				hr.SetRect(x - 4, y1 - 6, x + 32, y1 + 22);
			else
				hr.SetRect(x - 2, y0 - 2, x + 18, y0 + 18);
			if (hr.PtInRect(pt))
				outIdx[n++] = i;
		}
		break;
	}
	(void)tr;
	return n;
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
		const int staffTop = ScStaffRowStaffTop(u, tr, rowTop);
		const int noteTop = ScStaffRowNoteAreaTop(rowTop);
		if (pt.y < rowTop + 2 || pt.y >= noteTop) continue;
		if (outTrack) *outTrack = tr;
		(void)staffTop;
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
			} else if (!isFm && e.kind == SC_EV_RPN) {
				lane = 1;
				_snwprintf_s(label, _TRUNCATE, L"%s RPN %u/%u=%u", tpos, (unsigned)e.a, (unsigned)e.b, (unsigned)e.c);
			} else if (!isFm && e.kind == SC_EV_NRPN) {
				lane = 1;
				_snwprintf_s(label, _TRUNCATE, L"%s NRPN %u/%u=%u", tpos, (unsigned)e.a, (unsigned)e.b, (unsigned)e.c);
			} else if (!isFm && e.kind == SC_EV_SYSEX) {
				lane = 1;
				_snwprintf_s(label, _TRUNCATE, L"%s SysEx #%u", tpos, (unsigned)e.a);
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
			} else if (!isFm && e.kind == SC_EV_PEDAL_ON) {
				lane = 0;
				_snwprintf_s(label, _TRUNCATE, L"%s Ped.", tpos);
			} else if (!isFm && e.kind == SC_EV_PEDAL_OFF) {
				lane = 0;
				_snwprintf_s(label, _TRUNCATE, L"%s ＊", tpos);
			} else if (!isFm && (e.kind == SC_EV_VOL || e.kind == SC_EV_PAN || e.kind == SC_EV_VELO)) {
				lane = 1;
				_snwprintf_s(label, _TRUNCATE, L"%s X", tpos);
			} else
				continue;
			int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat);
			if (ScStaffIsStaffMarkKind(e.kind, isFm))
				x += ScStaffMarkStackIndex(ev, evCount, i) * 12;
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
		const int noteTop = ScStaffRowNoteAreaTop(rowTop);
		/* Only Tone/Exc gauge — not upper ledger */
		if (pt.y >= rowTop + 2 && pt.y < noteTop) {
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
		uint8_t def = (u->stripKind[L] == SC_STRIP_PITCH || u->stripKind[L] == SC_STRIP_PAN) ? 64 : 100;
		for (int i = 0; i < 256; i++) u->strip[L][i] = def;
	}
	if (u->stripCount <= 0) return;
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
			else if (k == SC_STRIP_PITCH && (e.kind == SC_EV_PITCH || e.kind == SC_EV_FM_PITCH))
				u->strip[L][col] = e.a;
			else if (k == SC_STRIP_PAN && e.kind == SC_EV_PAN)
				u->strip[L][col] = e.a;
			else if (k == SC_STRIP_GATE && (e.kind == SC_EV_NOTE || e.kind == SC_EV_FM_NOTE) && e.c >= 1 && e.c <= 100)
				u->strip[L][col] = e.c;
		}
	}
}

int ScStaffHitStrip(const CRect& stripRc, const ScStaffUi* u, CPoint pt, int* outLane, int* outCol, int* outVal)
{
	if (!u || u->stripCount <= 0 || !stripRc.PtInRect(pt)) return 0;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int n = max(1, min(SC_STRIP_LANES_MAX, u->stripCount));
	int lane = (pt.y - stripRc.top) / SC_STRIP_LANE_H;
	if (lane < 0) lane = 0;
	if (lane >= n) lane = n - 1;
	CRect lr(stripRc.left, stripRc.top + lane * SC_STRIP_LANE_H, stripRc.right, stripRc.top + (lane + 1) * SC_STRIP_LANE_H);
	const int left = lr.left + SC_CLEF_MARGIN;
	const int top = lr.top + 18;
	const int h = SC_STRIP_LANE_H - 24;
	if (h < 1 || pt.x < left) return 0;
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
	e.tick = tick;
	e.seq = (uint32_t)(*count);
	e.ch = ch; e.kind = kind; e.a = a; e.b = b; e.c = c; e.dur = dur;
	(*count)++;
	return 1;
}

static void ApplyOneStripMidi(ScMidiDoc* d, int track, int kind, const uint8_t* strip)
{
	uint8_t dropKind = SC_EV_VELO;
	if (kind == SC_STRIP_VOL) dropKind = SC_EV_VOL;
	else if (kind == SC_STRIP_PITCH) dropKind = SC_EV_PITCH;
	else if (kind == SC_STRIP_PAN) dropKind = SC_EV_PAN;
	else if (kind == SC_STRIP_GATE) {
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
	if (!d || !u || track < 0 || track >= SC_MIDI_CH || u->stripCount <= 0) return;
	for (int L = 0; L < u->stripCount && L < SC_STRIP_LANES_MAX; L++)
		ApplyOneStripMidi(d, track, u->stripKind[L], u->strip[L]);
}

void ScStaffApplyStripToDocFm(ScFmDoc* d, int track, const ScStaffUi* u)
{
	if (!d || !u || track < 0 || track >= SC_FM_TOTAL || u->stripCount <= 0) return;
	for (int L = 0; L < u->stripCount && L < SC_STRIP_LANES_MAX; L++) {
		const int kind = u->stripKind[L];
		const uint8_t* strip = u->strip[L];
		uint8_t dropKind = SC_EV_FM_VOL;
		if (kind == SC_STRIP_PITCH) dropKind = SC_EV_FM_PITCH;
		else if (kind == SC_STRIP_GATE) {
			for (int i = 0; i < d->evCount; i++) {
				if (d->ev[i].ch != (uint8_t)track) continue;
				if (d->ev[i].kind != SC_EV_FM_NOTE) continue;
				int col = (int)((d->ev[i].tick * 2) / SC_PPQN);
				if (col >= 0 && col < 256) {
					int g = strip[col];
					if (g < 1) g = 1;
					if (g > 100) g = 100;
					d->ev[i].c = (uint8_t)g;
				}
			}
			continue;
		} else if (kind == SC_STRIP_VOL || kind == SC_STRIP_EXPR)
			dropKind = SC_EV_FM_VOL;
		else if (kind == SC_STRIP_PAN)
			continue; /* FM pan via Misao PPAN only when track is Misao — skip for now on OPN */
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
			ScPushEv(d->ev, &d->evCount, (uint32_t)(c * SC_PPQN / 2), (uint8_t)track, dropKind, v, 0, 0, 0);
		}
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
			return ScStaffRowStaffTop(u, track, y);
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
	unsigned vt = 0;
	/* Prefer audible PCM (DS heard) so the cursor matches what you hear.
	   Fall back to engine playSample minus plugin latency only — no extra pad. */
	const __int64 heard = OggGetHeardPcmFrames();
	if (heard >= 0 && VstMidiTickAtSample(heard, &vt)) {
		tick = vt;
		haveTick = 1;
	}
	if (!haveTick) {
		__int64 playSmpl = VstMidiGetPlaySample();
		if (playSmpl >= 0) {
			playSmpl -= (__int64)VstMidiGetLatencySamples();
			if (playSmpl < 0) playSmpl = 0;
			if (VstMidiTickAtSample(playSmpl, &vt)) {
				tick = vt;
				haveTick = 1;
			}
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

void ScStaffSelClear(ScStaffUi* u)
{
	if (!u) return;
	u->nSel = 0;
	u->selEv = -1;
}

void ScStaffSelAdd(ScStaffUi* u, int evIdx)
{
	if (!u || evIdx < 0) return;
	if (ScStaffSelHas(u, evIdx)) return;
	if (u->nSel >= SC_SEL_MAX) return;
	u->selList[u->nSel++] = evIdx;
	u->selEv = evIdx;
}

int ScStaffSelHas(const ScStaffUi* u, int evIdx)
{
	if (!u || evIdx < 0) return 0;
	for (int i = 0; i < u->nSel; i++)
		if (u->selList[i] == evIdx) return 1;
	if (u->nSel == 0 && u->selEv == evIdx) return 1;
	return 0;
}

void ScStaffSelSetPrimary(ScStaffUi* u, int evIdx)
{
	if (!u) return;
	u->nSel = 0;
	u->selEv = evIdx;
	if (evIdx >= 0)
		ScStaffSelAdd(u, evIdx);
}

int ScStaffSelCopy(const ScEvent* ev, int evCount, const ScStaffUi* u, ScEvent* out, int outMax, uint32_t* outBaseTick)
{
	if (!ev || !u || !out || outMax < 1) return 0;
	int n = 0;
	uint32_t base = 0xFFFFFFFFu;
	auto take = [&](int i) {
		if (i < 0 || i >= evCount || n >= outMax) return;
		out[n++] = ev[i];
		if (ev[i].tick < base) base = ev[i].tick;
	};
	if (u->nSel > 0) {
		for (int i = 0; i < u->nSel; i++) take(u->selList[i]);
	} else if (u->selEv >= 0) {
		take(u->selEv);
	}
	if (n > 0 && outBaseTick) *outBaseTick = (base == 0xFFFFFFFFu) ? 0 : base;
	return n;
}

int ScStaffSelPaste(ScEvent* ev, int* evCount, int evMax, const ScEvent* clip, int clipN, uint32_t baseTick, int trackDelta)
{
	if (!ev || !evCount || !clip || clipN < 1) return 0;
	uint32_t minT = 0xFFFFFFFFu;
	for (int i = 0; i < clipN; i++)
		if (clip[i].tick < minT) minT = clip[i].tick;
	if (minT == 0xFFFFFFFFu) minT = 0;
	int added = 0;
	for (int i = 0; i < clipN; i++) {
		if (*evCount >= evMax) break;
		ScEvent e = clip[i];
		e.tick = baseTick + (e.tick - minT);
		int ch = (int)e.ch + trackDelta;
		if (ch < 0) ch = 0;
		if (ch > 31) ch = 31;
		e.ch = (uint8_t)ch;
		ev[(*evCount)++] = e;
		added++;
	}
	return added;
}

int ScStaffSelDelete(ScEvent* ev, int* evCount, ScStaffUi* u)
{
	if (!ev || !evCount || !u) return 0;
	char drop[SC_EV_MAX];
	memset(drop, 0, sizeof(drop));
	int any = 0;
	if (u->nSel > 0) {
		for (int i = 0; i < u->nSel; i++) {
			int ix = u->selList[i];
			if (ix >= 0 && ix < *evCount) { drop[ix] = 1; any = 1; }
		}
	} else if (u->selEv >= 0 && u->selEv < *evCount) {
		drop[u->selEv] = 1; any = 1;
	}
	if (!any) return 0;
	int w = 0;
	for (int i = 0; i < *evCount; i++)
		if (!drop[i]) ev[w++] = ev[i];
	*evCount = w;
	ScStaffSelClear(u);
	return 1;
}

int ScStaffTieSelected(ScEvent* ev, int evCount, ScStaffUi* u)
{
	if (!ev || !u || u->nSel < 2) return 0;
	/* Keep each note's written duration; convert followers to SC_EV_TIE (MPY cmd 3). */
	int idx[SC_SEL_MAX];
	int n = 0;
	for (int i = 0; i < u->nSel && n < SC_SEL_MAX; i++) {
		int ix = u->selList[i];
		if (ix < 0 || ix >= evCount) continue;
		uint8_t k = ev[ix].kind;
		if (k != SC_EV_NOTE && k != SC_EV_FM_NOTE && k != SC_EV_TIE) continue;
		idx[n++] = ix;
	}
	if (n < 2) return 0;
	for (int a = 0; a < n; a++)
		for (int b = a + 1; b < n; b++)
			if (ev[idx[b]].tick < ev[idx[a]].tick) {
				int tmp = idx[a]; idx[a] = idx[b]; idx[b] = tmp;
			}
	ScEvent& first = ev[idx[0]];
	if (first.kind == SC_EV_TIE)
		first.kind = (u->isFmScore ? SC_EV_FM_NOTE : SC_EV_NOTE);
	int changed = 0;
	for (int i = 1; i < n; i++) {
		ScEvent& e = ev[idx[i]];
		if (e.ch != first.ch || e.a != first.a) continue;
		if (!e.dur) e.dur = first.dur ? first.dur : (uint16_t)(SC_PPQN / 2);
		e.kind = SC_EV_TIE;
		changed = 1;
	}
	return changed;
}

int ScStaffCopyMeasure(const ScEvent* ev, int evCount, int track, uint32_t measTicks, int measIndex, int allTracks,
	ScEvent* out, int outMax, uint32_t* outBaseTick)
{
	if (!ev || !out || outMax < 1 || measTicks < 1) return 0;
	uint32_t t0 = (uint32_t)measIndex * measTicks;
	uint32_t t1 = t0 + measTicks;
	int n = 0;
	for (int i = 0; i < evCount && n < outMax; i++) {
		if (!allTracks && (int)ev[i].ch != track) continue;
		if (ev[i].tick < t0 || ev[i].tick >= t1) continue;
		out[n] = ev[i];
		out[n].tick -= t0;
		n++;
	}
	if (outBaseTick) *outBaseTick = 0;
	return n;
}

void ScStaffPaintMarquee(CDC& dc, const ScStaffUi* u)
{
	if (!u || !u->marqueeOn) return;
	CRect r(min(u->marqueeX0, u->marqueeX1), min(u->marqueeY0, u->marqueeY1),
		max(u->marqueeX0, u->marqueeX1), max(u->marqueeY0, u->marqueeY1));
	CPen pen(PS_DOT, 1, RGB(40, 90, 200));
	CPen* op = dc.SelectObject(&pen);
	CBrush* ob = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
	dc.Rectangle(r);
	dc.SelectObject(op);
	if (ob) dc.SelectObject(ob);
}
