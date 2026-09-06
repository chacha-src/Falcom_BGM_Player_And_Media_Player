#include "stdafx.h"
#include "ogg.h"
#include "CSasamiStaffCore.h"
#include "CSasamiScoreArrange.h"
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

int ScStaffHTrack(const ScStaffUi* u, int track)
{
	if (u && u->isFmScore && track == 6) {
		int s = u->staffScale > 0 ? u->staffScale : 100;
		int lane = (SC_RHYTHM_PAD_LANE * s) / 100;
		if (lane < 12) lane = 12;
		if (lane > 28) lane = 28;
		return lane * 6 + SC_RHYTHM_STAFF_PAD;
	}
	return ScStaffH(u);
}

int ScStaffClampNote(int noteMidi)
{
	if (noteMidi < SC_NOTE_MIDI_MIN) return SC_NOTE_MIDI_MIN;
	if (noteMidi > SC_NOTE_MIDI_MAX) return SC_NOTE_MIDI_MAX;
	return noteMidi;
}

/* Pixels so notes sit inside the row — uses writtenLo/Hi when valid (ottava-aware). */
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
	int noteLo = SC_NOTE_MIDI_MIN;
	int noteHi = SC_NOTE_MIDI_MAX;
	if (u && track >= 0 && track < 32 && u->writtenValid[track]) {
		noteLo = u->writtenLo[track];
		noteHi = u->writtenHi[track];
		/* Keep at least ~1 octave slack around content so pencil has room. */
		const int mid = (noteLo + noteHi) / 2;
		if (noteHi - noteLo < 12) {
			noteLo = mid - 8;
			noteHi = mid + 8;
		}
		noteLo = ScStaffClampNote(noteLo - 2);
		noteHi = ScStaffClampNote(noteHi + 2);
	} else {
		/* Empty track: modest range around clef center (not full O0–O9). */
		if (cm == 1) { noteLo = 36; noteHi = 60; }
		else if (cm == 3) { noteLo = 36; noteHi = 60; }
		else if (cm == 2) { noteLo = 28; noteHi = 84; }
		else { noteLo = 48; noteHi = 84; }
	}
	int above = SC_STAFF_LEDGER_PAD;
	int below = SC_STAFF_LEDGER_PAD;
	if (cm == 2) {
		const int yHi = ScStaffNoteYRel(noteHi, 64, gap);
		const int yLo = ScStaffNoteYRel(noteLo, 43, gap);
		above = (yHi < 0) ? (-yHi + SC_STAFF_LEDGER_PAD) : SC_STAFF_LEDGER_PAD;
		below = (yLo + SC_STAFF_LEDGER_PAD > staffH) ? (yLo + SC_STAFF_LEDGER_PAD - staffH) : SC_STAFF_LEDGER_PAD;
	} else if (cm == 3) {
		/* OPNA RHY: fixed pad lanes — do not inflate with melodic ledger math. */
		if (u && u->isFmScore && track == 6) {
			above = SC_STAFF_LEDGER_PAD;
			below = SC_STAFF_LEDGER_PAD;
		} else {
			const int base = 36;
			const int yHi = ScStaffNoteYRel(noteHi, base, gap);
			const int yLo = ScStaffNoteYRel(noteLo, base, gap);
			above = (yHi < 0) ? (-yHi + SC_STAFF_LEDGER_PAD) : SC_STAFF_LEDGER_PAD;
			below = (yLo + SC_STAFF_LEDGER_PAD > staffH) ? (yLo + SC_STAFF_LEDGER_PAD - staffH) : SC_STAFF_LEDGER_PAD;
		}
	} else {
		const int base = (cm == 1) ? 43 : 64;
		const int yHi = ScStaffNoteYRel(noteHi, base, gap);
		const int yLo = ScStaffNoteYRel(noteLo, base, gap);
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
	return rowTop + SC_PART_GAUGE_H + ScStaffPartBandsH(u, track) + ScStaffLedgerPadAbove(u, track);
}

int ScStaffRowNoteAreaTop(const ScStaffUi* u, int track, int rowTop)
{
	return rowTop + SC_PART_GAUGE_H + ScStaffPartBandsH(u, track);
}

int ScStaffRowH(const ScStaffUi* u, int track)
{
	if (!u || track < 0 || track >= u->trackCount) return ScStaffH(u) + SC_PART_GAUGE_H + SC_PART_GAP;
	if (!u->visible[track]) return SC_PART_DISABLED_H + SC_PART_GAP / 2;
	const int one = ScStaffHTrack(u, track);
	const int padA = ScStaffLedgerPadAbove(u, track);
	const int padB = ScStaffLedgerPadBelow(u, track);
	const int bandsH = ScStaffPartBandsH(u, track);
	if (u->clef[track] == 2)
		return ScStaffH(u) * 2 + SC_GRAND_STAFF_GAP + SC_PART_GAUGE_H + bandsH + padA + padB + SC_PART_GAP;
	return one + SC_PART_GAUGE_H + bandsH + padA + padB + SC_PART_GAP;
}

int ScStaffStripLaneHeightPx(const ScStaffUi* u, int lane)
{
	if (!u || lane < 0 || lane >= SC_STRIP_LANES_MAX) return SC_STRIP_LANE_H;
	switch (u->stripLaneHgt[lane]) {
	case SC_STRIP_HGT_WIDE: return SC_STRIP_H_WIDE;
	case SC_STRIP_HGT_NARROW: return SC_STRIP_H_NARROW;
	default: return SC_STRIP_LANE_H;
	}
}

static int ScStaffStripLaneTopY(const ScStaffUi* u, int lane)
{
	int y = 0;
	for (int L = 0; L < lane && L < SC_STRIP_LANES_MAX; L++)
		y += ScStaffStripLaneHeightPx(u, L);
	return y;
}

int ScStaffStripTotalH(const ScStaffUi* u)
{
	if (!u || u->stripCount <= 0) return 0;
	int h = 4;
	for (int L = 0; L < u->stripCount && L < SC_STRIP_LANES_MAX; L++)
		h += ScStaffStripLaneHeightPx(u, L);
	return h;
}

static int ScStaffHeightFromPreset(int hgt)
{
	switch (hgt) {
	case SC_STRIP_HGT_WIDE: return SC_STRIP_H_WIDE;
	case SC_STRIP_HGT_NARROW: return SC_STRIP_H_NARROW;
	default: return SC_STRIP_LANE_H;
	}
}

int ScStaffGlobalTempoBandH(const ScStaffUi* u)
{
	if (!u || !u->globalTempoBandOn) return 0;
	switch (u->globalTempoBandHgt) {
	case SC_STRIP_HGT_WIDE: return SC_STRIP_LANE_H;
	case SC_STRIP_HGT_NARROW: return SC_GLOBAL_TEMPO_H;
	default: return SC_GLOBAL_TEMPO_H + 8;
	}
}

int ScStaffGridHeaderH(const ScStaffUi* u)
{
	return ScStaffGlobalTempoBandH(u) + SC_RULER_H;
}

int ScStaffGridBodyTop(int gridTop, const ScStaffUi* u)
{
	return gridTop + (u ? ScStaffGridHeaderH(u) : SC_RULER_H);
}

int ScStaffPartBandToStripKind(int band)
{
	static const int kMap[SC_PBAND_KIND_COUNT] = {
		SC_STRIP_EXPR, SC_STRIP_VOL, SC_STRIP_PAN, SC_STRIP_REVERB,
		SC_STRIP_CHORUS, SC_STRIP_DELAY, SC_STRIP_MOD, SC_STRIP_VEL
	};
	if (band < 0 || band >= SC_PBAND_KIND_COUNT) return SC_STRIP_EXPR;
	return kMap[band];
}

int ScStaffPartBandHeightPx(const ScStaffUi* u, int track, int band)
{
	if (!u || track < 0 || track >= 32 || band < 0 || band >= SC_PBAND_KIND_COUNT) return SC_STRIP_H_NARROW;
	return ScStaffHeightFromPreset(u->partBandHgt[track][band]);
}

int ScStaffPartBandsH(const ScStaffUi* u, int track)
{
	if (!u || track < 0 || track >= 32 || !u->visible[track]) return 0;
	int h = 0;
	for (int b = 0; b < SC_PBAND_KIND_COUNT; b++) {
		if (!(u->partBandMask[track] & (1 << b))) continue;
		h += ScStaffPartBandHeightPx(u, track, b);
	}
	return h;
}

const wchar_t* ScStaffPartBandName(int band)
{
	return ScStaffStripKindName(ScStaffPartBandToStripKind(band));
}

const wchar_t* ScStaffPartBandMmlHint(int band)
{
	switch (band) {
	case SC_PBAND_EXPR: return L"@EXP / @CC 11,v";
	case SC_PBAND_VOL: return L"@V / v";
	case SC_PBAND_PAN: return L"P";
	case SC_PBAND_REVERB: return L"@REV";
	case SC_PBAND_CHORUS: return L"@CHO";
	case SC_PBAND_DELAY: return L"@DEL";
	case SC_PBAND_MOD: return L"@MOD";
	case SC_PBAND_VEL: return L"v";
	default: return L"@CC";
	}
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
	u->stripKind[1] = isFm ? SC_STRIP_PITCH : SC_STRIP_PAN;
	u->stripKind[2] = SC_STRIP_PITCH;
	for (int L = 0; L < SC_STRIP_LANES_MAX; L++)
		u->stripLaneHgt[L] = SC_STRIP_HGT_WIDE;
	u->stripDraw = SC_STRIP_DRAW_PENCIL;
	u->stripStepTicks = SC_PPQN / 2; /* default 1/8 */
	u->stripLineAnchorCol = -1;
	u->globalTempoBandOn = 1;
	u->globalTempoBandHgt = SC_STRIP_HGT_NARROW;
	u->bandEditTrack = -1;
	u->bandEditKind = 0;
	u->bandEditCol = -1;
	u->helpTopic = SC_HELP_DEFAULT;
	for (int p = 0; p < 32; p++) {
		u->partStripCount[p] = 0;
		u->partStripKind[p][0] = u->stripKind[0];
		u->partStripKind[p][1] = u->stripKind[1];
		u->partStripKind[p][2] = u->stripKind[2];
		u->partStripLaneHgt[p][0] = u->stripLaneHgt[0];
		u->partStripLaneHgt[p][1] = u->stripLaneHgt[1];
		u->partStripLaneHgt[p][2] = u->stripLaneHgt[2];
		u->partBandMask[p] = SC_PBAND_DEFAULT_MASK;
		for (int b = 0; b < SC_PBAND_KIND_COUNT; b++)
			u->partBandHgt[p][b] = SC_STRIP_HGT_NARROW;
	}
	ScStaffLoadStripPrefsFromSave(u);
	ScStaffLoadBandPrefsFromSave(u);
	u->markerTick = 0;
	u->loopATick = -1;
	u->loopBTick = -1;
	u->playheadTick = 0;
	u->previewActive = 0;
	u->transportMode = 0;
	u->markerSeekArmed = 0;
	u->previewOriginTick = 0;
	u->previewWavMode = 0;
	u->followViewW = 0;
	u->dragMode = 0;
	u->meterNumer = 4;
	u->meterDenom = 4;
	u->isFmScore = isFm ? 1 : 0;
	u->marqueeOn = 0;
	u->eraseDrag = 0;
	for (int i = 0; i < u->trackCount; i++) {
		/* Default off — ScStaffRefreshPartEnabled enables used parts or saved mask. */
		u->visible[i] = 0;
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
		/* FM SSG: single staff by default (phase 6). */
		if (isFm && i >= 3 && i <= 5) u->clef[i] = 0;
		u->writtenLo[i] = 0;
		u->writtenHi[i] = 0;
		u->writtenValid[i] = 0;
	}
	u->followMode = 1;
	u->keySig = 0;
	u->showRollSplit = 0;
	u->chordMode = 0;
	u->chordType = SC_CHORD_MAJOR;
	u->chordVoices = 3;
	u->patternMode = 0;
	u->patternId = 0;
	u->gridEmph = 1;
	u->pasteInsert = 0;
	u->rulerDragOn = 0;
	u->rulerT0 = u->rulerT1 = 0;
	u->markerSolidTrack = -1;
	u->selRangeValid = 0;
	u->selRangeT0 = u->selRangeT1 = 0;
	u->selRangeAllParts = 0;
	for (int L = 0; L < SC_STRIP_LANES_MAX; L++)
		for (int i = 0; i < SC_STRIP_COLS_MAX; i++)
			u->strip[L][i] = (u->stripKind[L] == SC_STRIP_PITCH) ? 64 : 100;
	u->contentTicks = SC_PPQN * SC_MEASURE_BEATS * SC_MEASURES_DEFAULT;
	u->contentTracks = u->trackCount;
}

int ScStaffTicksPerMeasure(const ScStaffUi* u)
{
	int numer = (u && u->meterNumer > 0) ? u->meterNumer : 4;
	int denom = (u && u->meterDenom > 0) ? u->meterDenom : 4;
	return ScStaffMeterTicksPerMeasure(numer, denom);
}

int ScStaffBeatTicks(int denom)
{
	if (denom < 1) denom = 4;
	int beatTicks = (SC_PPQN * 4) / denom;
	return beatTicks < 1 ? 1 : beatTicks;
}

int ScStaffMeterTicksPerMeasure(int numer, int denom)
{
	if (numer < 1) numer = 4;
	int beatTicks = ScStaffBeatTicks(denom);
	int t = numer * beatTicks;
	return t < SC_PPQN ? SC_PPQN : t;
}

void ScStaffMeterAtTick(const ScEvent* ev, int evCount, uint32_t tick,
	int defNumer, int defDenom, int* outNumer, int* outDenom)
{
	int numer = defNumer > 0 ? defNumer : 4;
	int denom = defDenom > 0 ? defDenom : 4;
	if (outNumer) *outNumer = numer;
	if (outDenom) *outDenom = denom;
	if (!ev || evCount <= 0 || !outNumer || !outDenom) return;
	uint32_t best = 0;
	int found = 0;
	for (int i = 0; i < evCount; i++) {
		if (ev[i].kind != SC_EV_METER) continue;
		if (ev[i].tick > tick) continue;
		if (!found || ev[i].tick >= best) {
			best = ev[i].tick;
			numer = ev[i].a > 0 ? (int)ev[i].a : 4;
			denom = ev[i].b > 0 ? (int)ev[i].b : 4;
			found = 1;
		}
	}
	*outNumer = numer;
	*outDenom = denom;
}

uint32_t ScStaffSnapToBarTick(const ScEvent* ev, int evCount, uint32_t tick,
	int defNumer, int defDenom)
{
	if (tick == 0) return 0;
	uint32_t t = 0;
	int numer = defNumer > 0 ? defNumer : 4;
	int denom = defDenom > 0 ? defDenom : 4;
	while (t <= tick) {
		ScStaffMeterAtTick(ev, evCount, t, defNumer, defDenom, &numer, &denom);
		const int mTicks = ScStaffMeterTicksPerMeasure(numer, denom);
		if (mTicks < 1) return tick;
		const uint32_t next = t + (uint32_t)mTicks;
		if (next > tick) return t;
		t = next;
	}
	return t;
}

void ScStaffDrawTimeSignature(CDC& dc, int cx, int staffTop, int gap, int numer, int denom, COLORREF col)
{
	const int fontPx = max(14, min(20, gap * 2 + 4));
	CFont tf;
	tf.CreateFont(fontPx, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
	CFont* of = dc.SelectObject(&tf);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(col);
	wchar_t sn[8], sd[8];
	_snwprintf_s(sn, _TRUNCATE, L"%d", numer > 0 ? numer : 4);
	_snwprintf_s(sd, _TRUNCATE, L"%d", denom > 0 ? denom : 4);
	CSize szN = dc.GetTextExtent(sn);
	CSize szD = dc.GetTextExtent(sd);
	const int w = max(szN.cx, szD.cx);
	const int xN = cx + (w - szN.cx) / 2;
	const int xD = cx + (w - szD.cx) / 2;
	const int numTop = staffTop + 8 + max(2, gap / 3);
	dc.TextOut(xN, numTop, sn);
	const int barY = numTop + szN.cy + 2;
	dc.FillSolidRect(cx, barY, w, 2, col);
	const int denomTop = barY + max(3, gap / 4);
	dc.TextOut(xD, denomTop, sd);
	dc.SelectObject(of);
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

int ScStaffTempoTFromBpm(int bpm)
{
	if (bpm < 20) bpm = 20;
	if (bpm > 400) bpm = 400;
	return (int)((13000.0 * 120.0) / (double)bpm + 0.5);
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
	int h = ScStaffGridHeaderH(u) + 8;
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
int ScStaffScrollGutterW()
{
	return ScStaffScrollTrackW() + SC_SB_ZOOM_BTN;
}
int ScStaffScrollGutterH()
{
	return ScStaffScrollTrackH() + SC_SB_ZOOM_BTN;
}

static int ScStaffSbTrackW() { return ScStaffScrollTrackW(); }
static int ScStaffSbTrackH() { return ScStaffScrollTrackH(); }

static int ScStaffScrollContentWidthPx(const ScStaffUi* u, const ScEvent* ev, int evCount)
{
	if (!u) return 0;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	if (ev && evCount > 0)
		return ScStaffScoreContentWidthPx(u, ev, evCount, u->contentTicks);
	return (u->contentTicks * pxBeat) / SC_PPQN + ScStaffClefMarginPx(u, ev, evCount);
}

static CRect ScStaffVertTrackRc(const CRect& client, const CRect& view, int gutterW, int kZ)
{
	return CRect(client.right - gutterW, view.top, client.right - kZ, view.bottom);
}

static CRect ScStaffHorzTrackRc(const CRect& client, const CRect& body, int gutterW, int gutterH)
{
	return CRect(body.left, body.bottom - gutterH, client.right - gutterW, body.bottom);
}

static void ScStaffVertScrollMetrics(const ScStaffUi* u, int pageH, const CRect& vr,
	int& maxY, int& thumbH, int& travel)
{
	maxY = 0;
	thumbH = 28;
	travel = 1;
	if (!u || vr.Height() <= 8) return;
	const int contentH = ScStaffContentHeight(u);
	maxY = max(0, contentH - max(1, pageH));
	thumbH = max(28, (pageH * vr.Height()) / max(pageH + maxY, 1));
	if (thumbH > vr.Height() - 4) thumbH = vr.Height() - 4;
	travel = max(1, vr.Height() - thumbH - 4);
}

static CRect ScStaffVertThumbRc(const ScStaffUi* u, int pageH, const CRect& vr)
{
	int maxY = 0, thumbH = 28, travel = 1;
	ScStaffVertScrollMetrics(u, pageH, vr, maxY, thumbH, travel);
	int y0 = vr.top + 2;
	if (maxY > 0)
		y0 = vr.top + 2 + (int)(((__int64)u->scrollY * travel) / maxY);
	return CRect(vr.left + 2, y0, vr.right - 2, y0 + thumbH);
}

static void ScStaffHorzScrollMetrics(const ScStaffUi* u, int pageW, const CRect& hr,
	const ScEvent* ev, int evCount, int& maxX, int& thumbW, int& travel)
{
	maxX = 0;
	thumbW = 36;
	travel = 1;
	if (!u || hr.Width() <= 8) return;
	const int contentW = ScStaffScrollContentWidthPx(u, ev, evCount);
	maxX = max(0, contentW - max(1, pageW));
	thumbW = max(36, (pageW * hr.Width()) / max(pageW + maxX, 1));
	if (thumbW > hr.Width() - 4) thumbW = hr.Width() - 4;
	travel = max(1, hr.Width() - thumbW - 4);
}

static CRect ScStaffHorzThumbRc(const ScStaffUi* u, int pageW, const CRect& hr,
	const ScEvent* ev, int evCount)
{
	int maxX = 0, thumbW = 36, travel = 1;
	ScStaffHorzScrollMetrics(u, pageW, hr, ev, evCount, maxX, thumbW, travel);
	int x0 = hr.left + 2;
	if (maxX > 0)
		x0 = hr.left + 2 + (int)(((__int64)u->scrollX * travel) / maxX);
	return CRect(x0, hr.top + 2, x0 + thumbW, hr.bottom - 2);
}

void ScStaffZoomPxBeat(ScStaffUi* u, int delta)
{
	if (!u) return;
	int nb = u->pxBeat + delta;
	if (nb < SC_PX_BEAT_MIN) nb = SC_PX_BEAT_MIN;
	if (nb > SC_PX_BEAT_MAX) nb = SC_PX_BEAT_MAX;
	u->pxBeat = nb;
}

void ScStaffZoomStaffScale(ScStaffUi* u, int delta)
{
	if (!u) return;
	int ns = u->staffScale + delta;
	if (ns < SC_STAFF_SCALE_MIN) ns = SC_STAFF_SCALE_MIN;
	if (ns > SC_STAFF_SCALE_MAX) ns = SC_STAFF_SCALE_MAX;
	u->staffScale = ns;
}

static void ScStaffPaintZoomBtn(CDC& dc, const CRect& rc, const wchar_t* lab, BOOL boxed)
{
	if (rc.Width() < 2 || rc.Height() < 2) return;
	dc.FillSolidRect(rc, RGB(248, 248, 252));
	if (boxed)
		dc.Draw3dRect(rc, RGB(140, 142, 155), RGB(140, 142, 155));
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(40, 40, 55));
	dc.DrawText(lab, (LPRECT)&rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void ScStaffPaintScrollThumbs(CDC& dc, const CRect& client, const CRect& body, const CRect& view,
	const ScStaffUi* u, int pageW, int pageH, const ScEvent* ev, int evCount)
{
	if (!u) return;
	const int trackW = ScStaffSbTrackW();
	const int trackH = ScStaffSbTrackH();
	const int kZ = SC_SB_ZOOM_BTN;
	const int gutterW = trackW + kZ;
	const int gutterH = trackH + kZ;
	CRect vr = ScStaffVertTrackRc(client, view, gutterW, kZ);
	if (vr.Height() > 8) {
		dc.FillSolidRect(vr, RGB(200, 202, 210));
		dc.Draw3dRect(vr, RGB(140, 142, 155), RGB(140, 142, 155));
		CRect th = ScStaffVertThumbRc(u, pageH, vr);
		dc.FillSolidRect(th, RGB(70, 95, 160));
		dc.Draw3dRect(th, RGB(120, 145, 200), RGB(40, 50, 80));
	}
	CRect hr = ScStaffHorzTrackRc(client, body, gutterW, gutterH);
	if (hr.Width() > 8) {
		dc.FillSolidRect(hr, RGB(200, 202, 210));
		dc.Draw3dRect(hr, RGB(140, 142, 155), RGB(140, 142, 155));
		CRect th = ScStaffHorzThumbRc(u, pageW, hr, ev, evCount);
		dc.FillSolidRect(th, RGB(70, 95, 160));
		dc.Draw3dRect(th, RGB(120, 145, 200), RGB(40, 50, 80));
	}
	/* horizontal zoom − + (boxed) */
	{
		CRect box(client.right - gutterW - kZ * 2, client.bottom - trackH,
			client.right - gutterW, client.bottom);
		dc.Draw3dRect(box, RGB(140, 142, 155), RGB(140, 142, 155));
		CRect hm(box.left, box.top, box.left + kZ, box.bottom);
		CRect hp(box.left + kZ, box.top, box.right, box.bottom);
		ScStaffPaintZoomBtn(dc, hm, L"−", FALSE);
		ScStaffPaintZoomBtn(dc, hp, L"+", FALSE);
	}
	/* vertical zoom − + (boxed) */
	{
		CRect box(client.right - kZ, client.bottom - gutterH - kZ * 2,
			client.right, client.bottom - gutterH);
		dc.Draw3dRect(box, RGB(140, 142, 155), RGB(140, 142, 155));
		CRect vm(box.left, box.top, box.right, box.top + kZ);
		CRect vp(box.left, box.top + kZ, box.right, box.bottom);
		ScStaffPaintZoomBtn(dc, vm, L"−", FALSE);
		ScStaffPaintZoomBtn(dc, vp, L"+", FALSE);
	}
	/* corner resize grip */
	CRect corner(client.right - kZ, client.bottom - kZ, client.right, client.bottom);
	dc.FillSolidRect(corner, RGB(236, 238, 244));
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3 - i; j++)
			dc.FillSolidRect(corner.right - 4 - i * 4, corner.bottom - 4 - j * 4, 2, 2, RGB(120, 120, 135));
}

int ScStaffHitScroll(const CRect& client, const CRect& body, const CRect& view, const ScStaffUi* u,
	int pageW, int pageH, CPoint pt, int* outPos, const ScEvent* ev, int evCount)
{
	if (!u) return 0;
	if (outPos) *outPos = 0;
	const int trackW = ScStaffSbTrackW();
	const int trackH = ScStaffSbTrackH();
	const int kZ = SC_SB_ZOOM_BTN;
	const int gutterW = trackW + kZ;
	const int gutterH = trackH + kZ;
	const int zPad = 2;
	CRect hZoomMinus(client.right - gutterW - kZ * 2 - zPad, client.bottom - trackH - zPad,
		client.right - gutterW - kZ + zPad, client.bottom + zPad);
	CRect hZoomPlus(client.right - gutterW - kZ - zPad, client.bottom - trackH - zPad,
		client.right - gutterW + zPad, client.bottom + zPad);
	CRect vZoomMinus(client.right - kZ - zPad, client.bottom - gutterH - kZ * 2 - zPad,
		client.right + zPad, client.bottom - gutterH - kZ + zPad);
	CRect vZoomPlus(client.right - kZ - zPad, client.bottom - gutterH - kZ - zPad,
		client.right + zPad, client.bottom - gutterH + zPad);
	if (hZoomMinus.PtInRect(pt)) return 3;
	if (hZoomPlus.PtInRect(pt)) return 4;
	if (vZoomMinus.PtInRect(pt)) return 5;
	if (vZoomPlus.PtInRect(pt)) return 6;
	CRect vr = ScStaffVertTrackRc(client, view, gutterW, kZ);
	CRect hr = ScStaffHorzTrackRc(client, body, gutterW, gutterH);
	if (vr.PtInRect(pt) && vr.Height() > 8) {
		const int pos = ScStaffMapVertScrollJump(u, pageH, client, view, pt.y);
		if (outPos) *outPos = pos;
		return 1;
	}
	if (hr.PtInRect(pt) && hr.Width() > 8) {
		const int pos = ScStaffMapHorzScrollJump(u, pageW, client, body, pt.x, ev, evCount);
		if (outPos) *outPos = pos;
		return 2;
	}
	return 0;
}

BOOL ScStaffPtOnVertThumb(const ScStaffUi* u, int pageH, const CRect& client, const CRect& view, CPoint pt)
{
	if (!u) return FALSE;
	const int kZ = SC_SB_ZOOM_BTN;
	const int gutterW = ScStaffSbTrackW() + kZ;
	CRect vr = ScStaffVertTrackRc(client, view, gutterW, kZ);
	return ScStaffVertThumbRc(u, pageH, vr).PtInRect(pt);
}

BOOL ScStaffPtOnHorzThumb(const ScStaffUi* u, int pageW, const CRect& client, const CRect& body,
	CPoint pt, const ScEvent* ev, int evCount)
{
	if (!u) return FALSE;
	const int kZ = SC_SB_ZOOM_BTN;
	const int gutterW = ScStaffSbTrackW() + kZ;
	const int gutterH = ScStaffSbTrackH() + kZ;
	CRect hr = ScStaffHorzTrackRc(client, body, gutterW, gutterH);
	return ScStaffHorzThumbRc(u, pageW, hr, ev, evCount).PtInRect(pt);
}

int ScStaffMapVertScrollJump(const ScStaffUi* u, int pageH, const CRect& client, const CRect& view, int ptY)
{
	if (!u) return 0;
	const int kZ = SC_SB_ZOOM_BTN;
	const int gutterW = ScStaffSbTrackW() + kZ;
	CRect vr = ScStaffVertTrackRc(client, view, gutterW, kZ);
	if (vr.Height() <= 8) return u->scrollY;
	int maxY = 0, thumbH = 28, travel = 1;
	ScStaffVertScrollMetrics(u, pageH, vr, maxY, thumbH, travel);
	int y = ptY;
	if (y < vr.top + 2 + thumbH / 2) y = vr.top + 2 + thumbH / 2;
	if (y > vr.top + 2 + thumbH / 2 + travel) y = vr.top + 2 + thumbH / 2 + travel;
	const int rel = y - (vr.top + 2) - thumbH / 2;
	if (maxY <= 0) return 0;
	return (int)(((__int64)rel * maxY) / travel);
}

int ScStaffMapHorzScrollJump(const ScStaffUi* u, int pageW, const CRect& client, const CRect& body,
	int ptX, const ScEvent* ev, int evCount)
{
	if (!u) return 0;
	const int kZ = SC_SB_ZOOM_BTN;
	const int gutterW = ScStaffSbTrackW() + kZ;
	const int gutterH = ScStaffSbTrackH() + kZ;
	CRect hr = ScStaffHorzTrackRc(client, body, gutterW, gutterH);
	if (hr.Width() <= 8) return u->scrollX;
	int maxX = 0, thumbW = 36, travel = 1;
	ScStaffHorzScrollMetrics(u, pageW, hr, ev, evCount, maxX, thumbW, travel);
	int x = ptX;
	if (x < hr.left + 2 + thumbW / 2) x = hr.left + 2 + thumbW / 2;
	if (x > hr.left + 2 + thumbW / 2 + travel) x = hr.left + 2 + thumbW / 2 + travel;
	const int rel = x - (hr.left + 2) - thumbW / 2;
	if (maxX <= 0) return 0;
	return (int)(((__int64)rel * maxX) / travel);
}

int ScStaffMapVertScrollDrag(const ScStaffUi* u, int pageH, const CRect& client, const CRect& view,
	int ptY, int anchorY, int scroll0)
{
	if (!u) return 0;
	const int kZ = SC_SB_ZOOM_BTN;
	const int gutterW = ScStaffSbTrackW() + kZ;
	CRect vr = ScStaffVertTrackRc(client, view, gutterW, kZ);
	if (vr.Height() <= 8) return scroll0;
	int maxY = 0, thumbH = 28, travel = 1;
	ScStaffVertScrollMetrics(u, pageH, vr, maxY, thumbH, travel);
	if (maxY <= 0) return 0;
	const int deltaY = ptY - anchorY;
	const int deltaScroll = (int)(((__int64)deltaY * maxY) / travel);
	int pos = scroll0 + deltaScroll;
	if (pos < 0) pos = 0;
	if (pos > maxY) pos = maxY;
	return pos;
}

int ScStaffMapHorzScrollDrag(const ScStaffUi* u, int pageW, const CRect& client, const CRect& body,
	int ptX, int anchorX, int scroll0, const ScEvent* ev, int evCount)
{
	if (!u) return 0;
	const int kZ = SC_SB_ZOOM_BTN;
	const int gutterW = ScStaffSbTrackW() + kZ;
	const int gutterH = ScStaffSbTrackH() + kZ;
	CRect hr = ScStaffHorzTrackRc(client, body, gutterW, gutterH);
	if (hr.Width() <= 8) return scroll0;
	int maxX = 0, thumbW = 36, travel = 1;
	ScStaffHorzScrollMetrics(u, pageW, hr, ev, evCount, maxX, thumbW, travel);
	if (maxX <= 0) return 0;
	const int deltaX = ptX - anchorX;
	const int deltaScroll = (int)(((__int64)deltaX * maxX) / travel);
	int pos = scroll0 + deltaScroll;
	if (pos < 0) pos = 0;
	if (pos > maxX) pos = maxX;
	return pos;
}

int ScStaffOttavaOctaves(const ScEvent* ev, int evCount, int ch, uint32_t tick);
int ScStaffEvWrittenMidi(const ScEvent* ev, int evCount, const ScEvent& e, int isFm, int tr);

void ScStaffUpdateContentExtent(ScStaffUi* u, const ScEvent* ev, int evCount)
{
	if (!u) return;
	uint32_t maxT = (uint32_t)(SC_PPQN * SC_MEASURE_BEATS * SC_MEASURES_DEFAULT);
	for (int i = 0; i < 32; i++) {
		u->writtenValid[i] = 0;
		u->writtenLo[i] = 60;
		u->writtenHi[i] = 60;
	}
	const int isFm = u->isFmScore ? 1 : 0;
	for (int i = 0; i < evCount; i++) {
		uint32_t end = ev[i].tick + (ev[i].dur ? ev[i].dur : SC_PPQN);
		/* keep ~8 measures of empty runway after last event */
		uint32_t need = end + (uint32_t)(SC_PPQN * SC_MEASURE_BEATS * 8);
		if (need > maxT) maxT = need;

		const ScEvent& e = ev[i];
		const int tr = (int)e.ch;
		if (tr < 0 || tr >= u->trackCount || tr >= 32) continue;
		const int isNote = isFm ? (e.kind == SC_EV_FM_NOTE) : (e.kind == SC_EV_NOTE);
		if (!isNote && e.kind != SC_EV_TIE) continue;
		const int written = ScStaffEvWrittenMidi(ev, evCount, e, isFm, tr);
		if (!u->writtenValid[tr]) {
			u->writtenLo[tr] = written;
			u->writtenHi[tr] = written;
			u->writtenValid[tr] = 1;
		} else {
			if (written < u->writtenLo[tr]) u->writtenLo[tr] = written;
			if (written > u->writtenHi[tr]) u->writtenHi[tr] = written;
		}
	}
	if (maxT > (uint32_t)(SC_PPQN * SC_MEASURE_BEATS * SC_MEASURES_MAX))
		maxT = (uint32_t)(SC_PPQN * SC_MEASURE_BEATS * SC_MEASURES_MAX);
	u->contentTicks = (int)maxT;
	u->contentTracks = u->trackCount;
}

int ScStaffOttavaOctaves(const ScEvent* ev, int evCount, int ch, uint32_t tick)
{
	if (!ev || evCount <= 0 || ch < 0) return 0;
	int best = -1;
	uint32_t bestTick = 0;
	uint32_t bestSeq = 0;
	for (int i = 0; i < evCount; i++) {
		const ScEvent& e = ev[i];
		if ((int)e.ch != ch) continue;
		if (e.kind != SC_EV_OTTAVA && e.kind != SC_EV_OTTAVA_END) continue;
		if (e.tick > tick) continue;
		if (best < 0 || e.tick > bestTick || (e.tick == bestTick && e.seq >= bestSeq)) {
			best = i;
			bestTick = e.tick;
			bestSeq = e.seq;
		}
	}
	if (best < 0) return 0;
	if (ev[best].kind == SC_EV_OTTAVA_END) return 0;
	int o = (int)(int8_t)ev[best].a;
	if (o < -3) o = -3;
	if (o > 3) o = 3;
	return o;
}

const wchar_t* ScStaffOttavaLabel(int octaves)
{
	switch (octaves) {
	case 1: return L"8va";
	case -1: return L"8vb";
	case 2: return L"16va";
	case -2: return L"16vb";
	case 3: return L"32va";
	case -3: return L"32vb";
	default: return L"loco";
	}
}

int ScStaffSoundingToWritten(int soundingMidi, int octaves)
{
	return ScStaffClampNote(soundingMidi - 12 * octaves);
}

int ScStaffWrittenToSounding(int writtenMidi, int octaves)
{
	return ScStaffClampNote(writtenMidi + 12 * octaves);
}

static int ScStaffRhythmMidiFromPad(int pad); /* defined below with rhythm helpers */

int ScStaffEvSoundingMidi(const ScEvent& e, int isFm, int tr)
{
	if (isFm && tr == 6) {
		/* Score UI stores pad 0..5; MML may leave FmNoteByte (oct<<4|scale). */
		int pad = (int)(e.a & 0x0F);
		if (pad > 5) pad = pad % 6;
		return ScStaffRhythmMidiFromPad(pad);
	}
	if (isFm)
		return (((e.a >> 4) & 0x0F) * 12 + (e.a & 0x0F) + 12);
	return (int)e.a;
}

int ScStaffEvWrittenMidi(const ScEvent* ev, int evCount, const ScEvent& e, int isFm, int tr)
{
	const int sounding = ScStaffEvSoundingMidi(e, isFm, tr);
	const int oct = ScStaffOttavaOctaves(ev, evCount, tr, e.tick);
	return ScStaffSoundingToWritten(sounding, oct);
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

static int ScStaffRhythmLaneH(const ScStaffUi* u)
{
	int s = u && u->staffScale > 0 ? u->staffScale : 100;
	int lane = (SC_RHYTHM_PAD_LANE * s) / 100;
	if (lane < 12) lane = 12;
	if (lane > 28) lane = 28;
	return lane;
}

static int ScStaffRhythmPadFromY(int y, int staffTop, int gap)
{
	(void)gap;
	const int lane = ScStaffRhythmLaneH(NULL);
	const int top = staffTop + SC_RHYTHM_STAFF_PAD / 2;
	int idx = (y - top) / (lane > 0 ? lane : 1);
	if (idx < 0) idx = 0;
	if (idx > 5) idx = 5;
	return idx;
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
	(void)gap;
	if (pad < 0) pad = 0;
	if (pad > 5) pad = 5;
	const int lane = ScStaffRhythmLaneH(NULL);
	const int top = staffTop + SC_RHYTHM_STAFF_PAD / 2;
	return top + pad * lane + lane / 2;
}

/* Prefer UI scale when available. */
static int ScStaffRhythmPadYUi(const ScStaffUi* u, int staffTop, int pad)
{
	if (pad < 0) pad = 0;
	if (pad > 5) pad = 5;
	const int lane = ScStaffRhythmLaneH(u);
	const int top = staffTop + SC_RHYTHM_STAFF_PAD / 2;
	return top + pad * lane + lane / 2;
}

static int ScStaffRhythmPadFromYUi(const ScStaffUi* u, int y, int staffTop)
{
	const int lane = ScStaffRhythmLaneH(u);
	const int top = staffTop + SC_RHYTHM_STAFF_PAD / 2;
	int idx = (y - top) / (lane > 0 ? lane : 1);
	if (idx < 0) idx = 0;
	if (idx > 5) idx = 5;
	return idx;
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

int ScStaffMidiNoteYTrack(const ScStaffUi* u, int track, int staffTop, int noteMidi,
	uint32_t atTick, const ScEvent* ev, int evCount)
{
	const int gap = ScStaffLineGap(u);
	const int cm = ScStaffClefModeAt(u, track, atTick, ev, evCount);
	/* OPNA RHY always uses dedicated pad lanes (ignore mid-score clef flips). */
	if (ScStaffUiOpnaRhythm(u, track)) {
		const int pad = ScStaffRhythmPadFromMidi(noteMidi);
		return ScStaffRhythmPadYUi(u, staffTop, pad);
	}
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

int ScStaffYToMidiNoteTrack(const ScStaffUi* u, int track, int staffTop, int y,
	uint32_t atTick, const ScEvent* ev, int evCount)
{
	const int gap = ScStaffLineGap(u);
	const int cm = ScStaffClefModeAt(u, track, atTick, ev, evCount);
	if (ScStaffUiOpnaRhythm(u, track))
		return ScStaffRhythmMidiFromPad(ScStaffRhythmPadFromYUi(u, y, staffTop));
	if (cm == 3)
		return ScStaffYToMidiNoteClef(y, staffTop, gap, 2);
	if (cm == 2) {
		const int one = ScStaffH(u);
		const int bassTop = staffTop + one + SC_GRAND_STAFF_GAP;
		const int splitY = staffTop + one / 2 + SC_GRAND_STAFF_GAP / 2;
		if (y >= splitY)
			return ScStaffYToMidiNoteClef(y, bassTop, gap, 1);
		return ScStaffYToMidiNoteClef(y, staffTop, gap, 0);
	}
	return ScStaffYToMidiNoteClef(y, staffTop, gap, cm == 1);
}

int ScStaffClefModeAt(const ScStaffUi* u, int track, uint32_t tick, const ScEvent* ev, int evCount)
{
	int c = ScClefMode(u, track);
	if (!ev || evCount <= 0) return c;
	uint32_t best = 0;
	int found = 0;
	for (int i = 0; i < evCount; i++) {
		if (ev[i].kind != SC_EV_CLEF) continue;
		if ((int)ev[i].ch != track) continue;
		if (ev[i].tick > tick) continue;
		if (!found || ev[i].tick >= best) {
			best = ev[i].tick;
			c = (int)ev[i].a;
			if (c < 0) c = 0;
			if (c > 3) c = 3;
			found = 1;
		}
	}
	return c;
}

int ScStaffKeySigAtTick(const ScEvent* ev, int evCount, uint32_t tick, int defKey)
{
	int ks = defKey;
	if (!ev || evCount <= 0) return ks;
	uint32_t best = 0;
	int found = 0;
	for (int i = 0; i < evCount; i++) {
		if (ev[i].kind != SC_EV_KEY) continue;
		if (ev[i].tick > tick) continue;
		if (!found || ev[i].tick >= best) {
			best = ev[i].tick;
			ks = (int)(int8_t)ev[i].a;
			found = 1;
		}
	}
	return ks;
}

int ScStaffBarHasLayoutZone(const ScEvent* ev, int evCount, uint32_t barTick)
{
	if (!ev || barTick == 0) return 0;
	for (int i = 0; i < evCount; i++) {
		if (ev[i].tick != barTick) continue;
		if (ev[i].kind == SC_EV_METER || ev[i].kind == SC_EV_CLEF || ev[i].kind == SC_EV_KEY)
			return 1;
	}
	return 0;
}

int ScStaffLayoutPrefixPx(const ScEvent* ev, int evCount, uint32_t tick, const ScStaffUi* u)
{
	if (!ev || !u || tick == 0) return 0;
	const int defN = u->meterNumer > 0 ? u->meterNumer : 4;
	const int defD = u->meterDenom > 0 ? u->meterDenom : 4;
	int prefix = 0;
	uint32_t barTick = 0;
	int numer = defN, denom = defD;
	while (barTick < tick) {
		ScStaffMeterAtTick(ev, evCount, barTick, defN, defD, &numer, &denom);
		const int mTicks = ScStaffMeterTicksPerMeasure(numer, denom);
		if (mTicks < 1) break;
		const uint32_t nextBar = barTick + (uint32_t)mTicks;
		if (nextBar > tick) break;
		if (ScStaffBarHasLayoutZone(ev, evCount, nextBar))
			prefix += SC_LAYOUT_ZONE_W;
		barTick = nextBar;
	}
	return prefix;
}

int ScStaffScoreContentWidthPx(const ScStaffUi* u, const ScEvent* ev, int evCount, uint32_t contentTicks)
{
	if (!u) return SC_CLEF_MARGIN;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int margin = ScStaffClefMarginPx(u, ev, evCount);
	int w = margin + (int)((contentTicks * (uint32_t)pxBeat) / (uint32_t)SC_PPQN);
	if (ev && evCount > 0)
		w += ScStaffLayoutPrefixPx(ev, evCount, contentTicks, u);
	return w;
}

int ScStaffIsInLayoutZone(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount, CPoint pt)
{
	if (!u || !ev || !grid.PtInRect(pt) || pt.y < ScStaffGridBodyTop(grid.top, u)) return 0;
	const int gridLeft = ScStaffGridLeftPx(grid.left, u, ev, evCount);
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const uint32_t tick = ScStaffXToTick(pt.x, u->scrollX, gridLeft, pxBeat, 1, u, ev, evCount);
	const int defN = u->meterNumer > 0 ? u->meterNumer : 4;
	const int defD = u->meterDenom > 0 ? u->meterDenom : 4;
	const uint32_t barTick = ScStaffSnapToBarTick(ev, evCount, tick, defN, defD);
	if (!ScStaffBarHasLayoutZone(ev, evCount, barTick)) return 0;
	const int xEnd = ScStaffTickToX(barTick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
	const int xStart = xEnd - SC_LAYOUT_ZONE_W;
	return (pt.x >= xStart && pt.x < xEnd) ? 1 : 0;
}

int ScStaffTickToX(uint32_t tick, int scrollX, int gridLeft, int pxBeat,
	const ScStaffUi* u, const ScEvent* ev, int evCount)
{
	if (pxBeat < 1) pxBeat = SC_PX_BEAT_DEFAULT;
	int x = gridLeft + (int)((tick * (uint32_t)pxBeat) / (uint32_t)SC_PPQN) - scrollX;
	if (u && ev && evCount > 0)
		x += ScStaffLayoutPrefixPx(ev, evCount, tick, u);
	return x;
}

uint32_t ScStaffXToTick(int x, int scrollX, int gridLeft, int pxBeat, int quantTicks,
	const ScStaffUi* u, const ScEvent* ev, int evCount)
{
	if (pxBeat < 1) pxBeat = SC_PX_BEAT_DEFAULT;
	const int px = x - gridLeft + scrollX;
	if (px < 0) return 0;
	uint32_t tick = 0;
	if (u && ev && evCount > 0) {
		uint32_t maxTick = u->contentTicks > 0 ? u->contentTicks : (uint32_t)(SC_PPQN * SC_MEASURES_DEFAULT * 4);
		uint32_t lo = 0, hi = maxTick;
		while (lo < hi) {
			const uint32_t mid = lo + (hi - lo + 1) / 2;
			if (ScStaffTickToX(mid, scrollX, gridLeft, pxBeat, u, ev, evCount) <= px)
				lo = mid;
			else
				hi = mid - 1;
		}
		tick = lo;
	} else {
		tick = (uint32_t)((px * SC_PPQN) / pxBeat);
	}
	const int q = quantTicks > 0 ? quantTicks : (SC_PPQN / 4);
	tick = (tick / (uint32_t)q) * (uint32_t)q;
	return tick;
}

int ScStaffStripKindBipolar(int kind)
{
	return (kind == SC_STRIP_PITCH || kind == SC_STRIP_PAN) ? 1 : 0;
}

static int ScStaffStripKindCc(int kind)
{
	switch (kind) {
	case SC_STRIP_MOD: return 1;
	case SC_STRIP_PORTA_T: return 5;
	case SC_STRIP_HOLD: return 64;
	case SC_STRIP_PORTA: return 65;
	case SC_STRIP_SOST: return 66;
	case SC_STRIP_SOFT: return 67;
	case SC_STRIP_REVERB: return 91;
	case SC_STRIP_CHORUS: return 93;
	case SC_STRIP_DELAY: return 94;
	case SC_STRIP_EXPR: return 11;
	case SC_STRIP_VOL: return 7;
	case SC_STRIP_PAN: return 10;
	default: return -1;
	}
}

int ScStaffStripKindFromCc(int cc)
{
	switch (cc) {
	case 1: return SC_STRIP_MOD;
	case 5: return SC_STRIP_PORTA_T;
	case 7: return SC_STRIP_VOL;
	case 10: return SC_STRIP_PAN;
	case 11: return SC_STRIP_EXPR;
	case 64: return SC_STRIP_HOLD;
	case 65: return SC_STRIP_PORTA;
	case 66: return SC_STRIP_SOST;
	case 67: return SC_STRIP_SOFT;
	case 91: return SC_STRIP_REVERB;
	case 93: return SC_STRIP_CHORUS;
	case 94: return SC_STRIP_DELAY;
	default: return -1;
	}
}

static int ScStaffStripEventMatches(int kind, const ScEvent& e)
{
	if (kind == SC_STRIP_GATE)
		return (e.kind == SC_EV_NOTE || e.kind == SC_EV_FM_NOTE) && e.c >= 1 && e.c <= 100;
	if (kind == SC_STRIP_PITCH)
		return e.kind == SC_EV_PITCH || e.kind == SC_EV_FM_PITCH;
	if (kind == SC_STRIP_VEL)
		return e.kind == SC_EV_VELO;
	if (kind == SC_STRIP_EXPR)
		return e.kind == SC_EV_VELO || (e.kind == SC_EV_CC && e.a == 11);
	if (kind == SC_STRIP_VOL)
		return e.kind == SC_EV_VOL || e.kind == SC_EV_FM_VOL || (e.kind == SC_EV_CC && e.a == 7);
	if (kind == SC_STRIP_PAN)
		return e.kind == SC_EV_PAN || (e.kind == SC_EV_CC && e.a == 10);
	const int cc = ScStaffStripKindCc(kind);
	if (cc >= 0)
		return e.kind == SC_EV_CC && (int)e.a == cc;
	return 0;
}

static uint8_t ScStaffStripEventValue(int kind, const ScEvent& e)
{
	if (kind == SC_STRIP_GATE)
		return e.c;
	if (e.kind == SC_EV_CC)
		return e.b;
	return e.a;
}

const wchar_t* ScStaffStripHeightName(int hgt)
{
	switch (hgt) {
	case SC_STRIP_HGT_WIDE:
		return LL14(L"広", L"Wide", L"Large", L"Largo", L"Ancho", L"넓게", L"宽", L"واسع", L"Широк.", L"Weit", L"Largo", L"Breed", L"Szer.", L"Geniş");
	case SC_STRIP_HGT_NARROW:
		return LL14(L"狭", L"Narrow", L"Étroit", L"Stretto", L"Estrecho", L"좁게", L"窄", L"ضيق", L"Узк.", L"Schmal", L"Estreito", L"Smal", L"Wąski", L"Dar");
	default:
		return LL14(L"基本", L"Normal", L"Normal", L"Normale", L"Normal", L"기본", L"基本", L"عادي", L"Обычн.", L"Normal", L"Normal", L"Normaal", L"Normalny", L"Normal");
	}
}

void ScStaffLoadStripPrefsFromSave(ScStaffUi* u)
{
	if (!u) return;
	for (int p = 0; p < 32; p++) {
		int c = (int)savedata.sasamiPartStripCount[p];
		if (c < 0) c = 0;
		if (c > SC_STRIP_LANES_MAX) c = SC_STRIP_LANES_MAX;
		u->partStripCount[p] = c;
		for (int L = 0; L < SC_STRIP_LANES_MAX; L++) {
			int k = (int)savedata.sasamiPartStripKind[p][L];
			if (k < 0 || k >= SC_STRIP_KIND_COUNT) k = (L == 0) ? SC_STRIP_EXPR : SC_STRIP_PAN;
			u->partStripKind[p][L] = k;
			int h = (int)savedata.sasamiPartStripHgt[p][L];
			if (h < 0 || h >= SC_STRIP_HGT_COUNT) h = SC_STRIP_HGT_WIDE;
			u->partStripLaneHgt[p][L] = h;
		}
	}
}

void ScStaffSaveStripPrefsToSave(const ScStaffUi* u)
{
	if (!u) return;
	for (int p = 0; p < 32; p++) {
		int c = u->partStripCount[p];
		if (c < 0) c = 0;
		if (c > SC_STRIP_LANES_MAX) c = SC_STRIP_LANES_MAX;
		savedata.sasamiPartStripCount[p] = (unsigned char)c;
		for (int L = 0; L < SC_STRIP_LANES_MAX; L++) {
			int k = u->partStripKind[p][L];
			if (k < 0 || k >= SC_STRIP_KIND_COUNT) k = SC_STRIP_EXPR;
			savedata.sasamiPartStripKind[p][L] = (unsigned char)k;
			int h = u->partStripLaneHgt[p][L];
			if (h < 0 || h >= SC_STRIP_HGT_COUNT) h = SC_STRIP_HGT_WIDE;
			savedata.sasamiPartStripHgt[p][L] = (unsigned char)h;
		}
	}
}

void ScStaffLoadBandPrefsFromSave(ScStaffUi* u)
{
	if (!u) return;
	u->globalTempoBandOn = savedata.sasamiGlobalTempoBandOn ? 1 : 0;
	int gh = (int)savedata.sasamiGlobalTempoBandHgt;
	if (gh < 0 || gh >= SC_STRIP_HGT_COUNT) gh = SC_STRIP_HGT_WIDE;
	u->globalTempoBandHgt = gh;
	for (int p = 0; p < 32; p++) {
		unsigned m = savedata.sasamiPartBandMask[p];
		if (m == 0) m = (unsigned)SC_PBAND_DEFAULT_MASK;
		u->partBandMask[p] = (int)m;
		for (int b = 0; b < SC_PBAND_KIND_COUNT; b++) {
			int h = (int)savedata.sasamiPartBandHgt[p][b];
			if (h < 0 || h >= SC_STRIP_HGT_COUNT) h = SC_STRIP_HGT_NARROW;
			u->partBandHgt[p][b] = h;
		}
	}
}

void ScStaffSaveBandPrefsToSave(const ScStaffUi* u)
{
	if (!u) return;
	savedata.sasamiGlobalTempoBandOn = (unsigned char)(u->globalTempoBandOn ? 1 : 0);
	int gh = u->globalTempoBandHgt;
	if (gh < 0 || gh >= SC_STRIP_HGT_COUNT) gh = SC_STRIP_HGT_WIDE;
	savedata.sasamiGlobalTempoBandHgt = (unsigned char)gh;
	for (int p = 0; p < 32; p++) {
		savedata.sasamiPartBandMask[p] = (unsigned)u->partBandMask[p];
		for (int b = 0; b < SC_PBAND_KIND_COUNT; b++) {
			int h = u->partBandHgt[p][b];
			if (h < 0 || h >= SC_STRIP_HGT_COUNT) h = SC_STRIP_HGT_NARROW;
			savedata.sasamiPartBandHgt[p][b] = (unsigned char)h;
		}
	}
}

int ScStaffStripKindEmitMml(const ScEvent& e, wchar_t* out, int outCch)
{
	if (!out || outCch < 8) return 0;
	out[0] = 0;
	if (e.kind == SC_EV_VELO)
		_snwprintf_s(out, outCch, _TRUNCATE, L"v%d ", (int)e.a);
	else if (e.kind == SC_EV_VOL || e.kind == SC_EV_FM_VOL)
		_snwprintf_s(out, outCch, _TRUNCATE, L"@V%d ", (int)e.a);
	else if (e.kind == SC_EV_PAN)
		_snwprintf_s(out, outCch, _TRUNCATE, L"P%d ", (int)e.a);
	else if (e.kind == SC_EV_PITCH || e.kind == SC_EV_FM_PITCH) {
		int bent = 0x2000 + ((int)e.a - 64) * (0x1FFF / 64);
		if (bent < 0) bent = 0;
		if (bent > 0x3FFF) bent = 0x3FFF;
		_snwprintf_s(out, outCch, _TRUNCATE, L"@P%d ", bent);
	} else if (e.kind == SC_EV_CC) {
		switch ((int)e.a) {
		case 1: _snwprintf_s(out, outCch, _TRUNCATE, L"@MOD %d ", (int)e.b); break;
		case 5: _snwprintf_s(out, outCch, _TRUNCATE, L"@PORT %d ", (int)e.b); break;
		case 11: _snwprintf_s(out, outCch, _TRUNCATE, L"@EXP %d ", (int)e.b); break;
		case 65: _snwprintf_s(out, outCch, _TRUNCATE, L"@PORTA %d ", (int)e.b); break;
		case 66: _snwprintf_s(out, outCch, _TRUNCATE, L"@SOST %d ", (int)e.b); break;
		case 67: _snwprintf_s(out, outCch, _TRUNCATE, L"@SOFT %d ", (int)e.b); break;
		case 91: _snwprintf_s(out, outCch, _TRUNCATE, L"@REV %d ", (int)e.b); break;
		case 93: _snwprintf_s(out, outCch, _TRUNCATE, L"@CHO %d ", (int)e.b); break;
		case 94: _snwprintf_s(out, outCch, _TRUNCATE, L"@DEL %d ", (int)e.b); break;
		default: _snwprintf_s(out, outCch, _TRUNCATE, L"@CC %d,%d ", (int)e.a, (int)e.b); break;
		}
	} else
		return 0;
	return out[0] != 0;
}

const wchar_t* ScStaffStripKindName(int kind)
{
	switch (kind) {
	case SC_STRIP_EXPR:
		return LL14(L"Exp(CC11)", L"Exp (CC11)", L"Exp (CC11)", L"Esp (CC11)", L"Exp (CC11)",
			L"표정(CC11)", L"表情(CC11)", L"تعبير (CC11)", L"Экспр. (CC11)", L"Exp (CC11)",
			L"Exp (CC11)", L"Exp (CC11)", L"Exp (CC11)", L"Exp (CC11)");
	case SC_STRIP_VOL:
		return LL14(L"Vol(CC7/TL)", L"Vol (CC7/TL)", L"Vol (CC7/TL)", L"Vol (CC7/TL)", L"Vol (CC7/TL)",
			L"볼륨(CC7/TL)", L"音量(CC7/TL)", L"صوت (CC7/TL)", L"Громк. (CC7/TL)", L"Vol (CC7/TL)",
			L"Vol (CC7/TL)", L"Vol (CC7/TL)", L"Vol (CC7/TL)", L"Vol (CC7/TL)");
	case SC_STRIP_PITCH:
		return LL14(L"ピッチ", L"Pitch bend", L"Hauteur", L"Pitch", L"Tono",
			L"피치", L"音高", L"طبقة", L"Высота", L"Pitch", L"Tom", L"Toonhoogte", L"Wysokość", L"Perde");
	case SC_STRIP_GATE:
		return LL14(L"Gate%", L"Gate %", L"Gate %", L"Gate %", L"Gate %",
			L"게이트%", L"门控%", L"بوابة%", L"Gate %", L"Gate %", L"Gate %", L"Gate %", L"Gate %", L"Gate %");
	case SC_STRIP_PAN:
		return LL14(L"Pan(CC10)", L"Pan (CC10)", L"Pan (CC10)", L"Pan (CC10)", L"Pan (CC10)",
			L"팬(CC10)", L"声像(CC10)", L"بان (CC10)", L"Пан (CC10)", L"Pan (CC10)",
			L"Pan (CC10)", L"Pan (CC10)", L"Pan (CC10)", L"Pan (CC10)");
	case SC_STRIP_MOD:
		return LL14(L"Mod(CC1)", L"Mod (CC1)", L"Mod (CC1)", L"Mod (CC1)", L"Mod (CC1)",
			L"모듈레이션(CC1)", L"调制(CC1)", L"تعديل (CC1)", L"Мод. (CC1)", L"Mod (CC1)",
			L"Mod (CC1)", L"Mod (CC1)", L"Mod (CC1)", L"Mod (CC1)");
	case SC_STRIP_PORTA_T:
		return LL14(L"Port.T(CC5)", L"Port.time (CC5)", L"Port.t (CC5)", L"Port.t (CC5)", L"Port.t (CC5)",
			L"포르타멘토T(CC5)", L"滑音T(CC5)", L"زمن بورت. (CC5)", L"Port.t (CC5)", L"Port.t (CC5)",
			L"Port.t (CC5)", L"Port.t (CC5)", L"Port.t (CC5)", L"Port.t (CC5)");
	case SC_STRIP_HOLD:
		return LL14(L"Hold(CC64)", L"Hold (CC64)", L"Hold (CC64)", L"Hold (CC64)", L"Hold (CC64)",
			L"홀드(CC64)", L"延音(CC64)", L"استدامة (CC64)", L"Hold (CC64)", L"Hold (CC64)",
			L"Hold (CC64)", L"Hold (CC64)", L"Hold (CC64)", L"Hold (CC64)");
	case SC_STRIP_PORTA:
		return LL14(L"Port.(CC65)", L"Port. (CC65)", L"Port. (CC65)", L"Port. (CC65)", L"Port. (CC65)",
			L"포르타멘토(CC65)", L"滑音开(CC65)", L"بورت. (CC65)", L"Port. (CC65)", L"Port. (CC65)",
			L"Port. (CC65)", L"Port. (CC65)", L"Port. (CC65)", L"Port. (CC65)");
	case SC_STRIP_SOST:
		return LL14(L"Sost.(CC66)", L"Sost. (CC66)", L"Sost. (CC66)", L"Sost. (CC66)", L"Sost. (CC66)",
			L"소스테누토(CC66)", L"持续踏板(CC66)", L"سوست. (CC66)", L"Sost. (CC66)", L"Sost. (CC66)",
			L"Sost. (CC66)", L"Sost. (CC66)", L"Sost. (CC66)", L"Sost. (CC66)");
	case SC_STRIP_SOFT:
		return LL14(L"Soft(CC67)", L"Soft (CC67)", L"Soft (CC67)", L"Soft (CC67)", L"Soft (CC67)",
			L"소프트(CC67)", L"柔音(CC67)", L"ناعم (CC67)", L"Soft (CC67)", L"Soft (CC67)",
			L"Soft (CC67)", L"Soft (CC67)", L"Soft (CC67)", L"Soft (CC67)");
	case SC_STRIP_REVERB:
		return LL14(L"Reverb(CC91)", L"Reverb (CC91)", L"Reverb (CC91)", L"Reverb (CC91)", L"Reverb (CC91)",
			L"리버브(CC91)", L"混响(CC91)", L"صدى (CC91)", L"Реверб (CC91)", L"Reverb (CC91)",
			L"Reverb (CC91)", L"Reverb (CC91)", L"Reverb (CC91)", L"Reverb (CC91)");
	case SC_STRIP_CHORUS:
		return LL14(L"Chorus(CC93)", L"Chorus (CC93)", L"Chorus (CC93)", L"Chorus (CC93)", L"Chorus (CC93)",
			L"코러스(CC93)", L"合唱(CC93)", L"كورس (CC93)", L"Хорус (CC93)", L"Chorus (CC93)",
			L"Chorus (CC93)", L"Chorus (CC93)", L"Chorus (CC93)", L"Chorus (CC93)");
	case SC_STRIP_DELAY:
		return LL14(L"Delay(CC94)", L"Delay (CC94)", L"Delay (CC94)", L"Delay (CC94)", L"Delay (CC94)",
			L"딜레이(CC94)", L"延迟(CC94)", L"تأخير (CC94)", L"Дилей (CC94)", L"Delay (CC94)",
			L"Delay (CC94)", L"Delay (CC94)", L"Delay (CC94)", L"Delay (CC94)");
	case SC_STRIP_VEL:
		return LL14(L"ベロシティ", L"Velocity", L"Vélocité", L"Velocità", L"Velocidad",
			L"벨로시티", L"力度", L"سرعة", L"Скорость", L"Anschlag",
			L"Velocidade", L"Velocity", L"Velocity", L"Velocity");
	default: return L"?";
	}
}

const wchar_t* ScStaffStripKindNameFm(int kind)
{
	switch (kind) {
	case SC_STRIP_EXPR:
		return LL14(L"@EXP", L"@EXP", L"@EXP", L"@EXP", L"@EXP",
			L"@EXP", L"@EXP", L"@EXP", L"@EXP", L"@EXP",
			L"@EXP", L"@EXP", L"@EXP", L"@EXP");
	case SC_STRIP_VOL:
		return LL14(L"FVOL/v", L"FVOL/v", L"FVOL/v", L"FVOL/v", L"FVOL/v",
			L"FVOL/v", L"FVOL/v", L"FVOL/v", L"FVOL/v", L"FVOL/v",
			L"FVOL/v", L"FVOL/v", L"FVOL/v", L"FVOL/v");
	case SC_STRIP_PAN:
		return LL14(L"FLR", L"FLR", L"FLR", L"FLR", L"FLR",
			L"FLR", L"FLR", L"FLR", L"FLR", L"FLR",
			L"FLR", L"FLR", L"FLR", L"FLR");
	case SC_STRIP_MOD:
		return LL14(L"@MOD", L"@MOD", L"@MOD", L"@MOD", L"@MOD",
			L"@MOD", L"@MOD", L"@MOD", L"@MOD", L"@MOD",
			L"@MOD", L"@MOD", L"@MOD", L"@MOD");
	case SC_STRIP_PORTA_T:
		return LL14(L"@PORT", L"@PORT", L"@PORT", L"@PORT", L"@PORT",
			L"@PORT", L"@PORT", L"@PORT", L"@PORT", L"@PORT",
			L"@PORT", L"@PORT", L"@PORT", L"@PORT");
	case SC_STRIP_HOLD:
		return LL14(L"Hold", L"Hold", L"Hold", L"Sostenuto", L"Hold",
			L"홀드", L"延音", L"استدامة", L"Hold", L"Hold",
			L"Hold", L"Hold", L"Hold", L"Hold");
	case SC_STRIP_PORTA:
		return LL14(L"@PORTA", L"@PORTA", L"@PORTA", L"@PORTA", L"@PORTA",
			L"@PORTA", L"@PORTA", L"@PORTA", L"@PORTA", L"@PORTA",
			L"@PORTA", L"@PORTA", L"@PORTA", L"@PORTA");
	default:
		return ScStaffStripKindName(kind);
	}
}

const wchar_t* ScStaffStripKindNameJp(int kind)
{
	return ScStaffStripKindName(kind);
}

const wchar_t* ScStaffStripDrawModeName(int mode)
{
	switch (mode) {
	case SC_STRIP_DRAW_LINE:
		return LL14(L"直線", L"Line", L"Ligne", L"Linea", L"Linea",
			L"직선", L"直线", L"خط", L"Линия", L"Linie", L"Linha", L"Lijn", L"Linia", L"Çizgi");
	case SC_STRIP_DRAW_CURVE:
		return LL14(L"曲線", L"Curve", L"Courbe", L"Curva", L"Curva",
			L"곡선", L"曲线", L"منحنى", L"Кривая", L"Kurve", L"Curva", L"Kromme", L"Krzywa", L"Eğri");
	default:
		return LL14(L"鉛筆", L"Pencil", L"Crayon", L"Matita", L"Lapiz",
			L"연필", L"铅笔", L"قلم", L"Карандаш", L"Stift", L"Lapis", L"Potlood", L"Olowek", L"Kalem");
	}
}

const wchar_t* ScStaffStripLanesLabel(int lanes)
{
	switch (lanes) {
	case 1:
		return LL14(L"レーン×1", L"Lanes ×1", L"Pistes ×1", L"Corsie ×1", L"Pistas ×1",
			L"레인×1", L"车道×1", L"ممرات ×1", L"Дорожки ×1", L"Spuren ×1", L"Faixas ×1", L"Banen ×1", L"Tory ×1", L"Şerit ×1");
	case 2:
		return LL14(L"レーン×2", L"Lanes ×2", L"Pistes ×2", L"Corsie ×2", L"Pistas ×2",
			L"레인×2", L"车道×2", L"ممرات ×2", L"Дорожки ×2", L"Spuren ×2", L"Faixas ×2", L"Banen ×2", L"Tory ×2", L"Şerit ×2");
	case 3:
		return LL14(L"レーン×3", L"Lanes ×3", L"Pistes ×3", L"Corsie ×3", L"Pistas ×3",
			L"레인×3", L"车道×3", L"ممرات ×3", L"Дорожки ×3", L"Spuren ×3", L"Faixas ×3", L"Banen ×3", L"Tory ×3", L"Şerit ×3");
	default:
		return LL14(L"レーンなし", L"No lanes", L"Aucune piste", L"Nessuna corsia", L"Sin pistas",
			L"레인 없음", L"无车道", L"بلا ممرات", L"Без дорожек", L"Keine Spuren", L"Sem faixas", L"Geen banen", L"Bez torów", L"Şerit yok");
	}
}

const wchar_t* ScStaffToolName(int tool)
{
	switch (tool) {
	case SC_TOOL_PENCIL:
		return LL14(L"鉛筆", L"Pencil", L"Crayon", L"Matita", L"Lapiz",
			L"연필", L"铅笔", L"قلم", L"Карандаш", L"Stift", L"Lapis", L"Potlood", L"Olowek", L"Kalem");
	case SC_TOOL_ERASER:
		return LL14(L"消しゴム", L"Eraser", L"Gomme", L"Gomma", L"Borrar",
			L"지우개", L"橡皮", L"ممحاة", L"Ластик", L"Radierer", L"Borracha", L"Gum", L"Gumka", L"Silgi");
	case SC_TOOL_SELECT:
		return LL14(L"選択", L"Select", L"Selection", L"Selezione", L"Seleccionar",
			L"선택", L"选择", L"تحديد", L"Выбор", L"Auswahl", L"Selecionar", L"Selecteren", L"Zaznacz", L"Seç");
	case SC_TOOL_TEMPO:
		return LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo",
			L"템포", L"速度", L"إيقاع", L"Темп", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo");
	case SC_TOOL_METER:
		return LL14(L"拍子", L"Meter", L"Mesure", L"Misura", L"Compás",
			L"박자", L"拍号", L"إيقاع", L"Размер", L"Taktart", L"Compasso", L"Maatsoort", L"Metrum", L"Ölçü");
	case SC_TOOL_TIE:
		return LL14(L"タイ", L"Tie", L"Liaison", L"Legatura", L"Ligadura",
			L"타이", L"连音线", L"ربط", L"Лига", L"Bindebogen", L"Ligadura", L"Boog", L"Legato", L"Bağ");
	default:
		return LL14(L"ツール", L"Tool", L"Outil", L"Strumento", L"Herramienta",
			L"도구", L"工具", L"أداة", L"Инструмент", L"Werkzeug", L"Ferramenta", L"Gereedschap", L"Narzędzie", L"Araç");
	}
}

void ScStaffSavePartStrip(ScStaffUi* u, int part)
{
	if (!u || part < 0 || part >= 32) return;
	u->partStripCount[part] = u->stripCount;
	for (int L = 0; L < SC_STRIP_LANES_MAX; L++) {
		u->partStripKind[part][L] = u->stripKind[L];
		u->partStripLaneHgt[part][L] = u->stripLaneHgt[L];
	}
	ScStaffSaveStripPrefsToSave(u);
	ScStaffSaveBandPrefsToSave(u);
}

void ScStaffLoadPartStrip(ScStaffUi* u, int part)
{
	if (!u || part < 0 || part >= 32) return;
	u->stripCount = u->partStripCount[part];
	if (u->stripCount < 0) u->stripCount = 0;
	if (u->stripCount > SC_STRIP_LANES_MAX) u->stripCount = SC_STRIP_LANES_MAX;
	for (int L = 0; L < SC_STRIP_LANES_MAX; L++) {
		int k = u->partStripKind[part][L];
		if (k < 0 || k >= SC_STRIP_KIND_COUNT) k = (L == 0) ? SC_STRIP_EXPR : SC_STRIP_PAN;
		u->stripKind[L] = k;
		int h = u->partStripLaneHgt[part][L];
		if (h < 0 || h >= SC_STRIP_HGT_COUNT) h = SC_STRIP_HGT_WIDE;
		u->stripLaneHgt[L] = h;
	}
}

void ScStaffFormatHelpBar(wchar_t* out, int cch, const ScStaffUi* u, int isFm, int curPart)
{
	if (!out || cch <= 0) return;
	out[0] = 0;
	if (!u) return;
	(void)isFm;
	const int topic = u->helpTopic;
	const wchar_t* tool = ScStaffToolName(u->tool);
	wchar_t stripInfo[128];
	stripInfo[0] = 0;
	if (u->stripCount > 0) {
		_snwprintf_s(stripInfo, _TRUNCATE,
			LL14(L" ストリップ×%d:", L" Strip×%d:", L" Piste×%d:", L" Corsia×%d:", L" Pista×%d:",
				L" 스트립×%d:", L" 条带×%d:", L" شريط×%d:", L" Полоса×%d:", L" Strip×%d:",
				L" Faixa×%d:", L" Strook×%d:", L" Pas×%d:", L" Şerit×%d:"),
			u->stripCount);
		for (int L = 0; L < u->stripCount && L < SC_STRIP_LANES_MAX; L++) {
			wchar_t bit[48];
			_snwprintf_s(bit, _TRUNCATE, L" [%d]%s", L + 1, ScStaffStripKindName(u->stripKind[L]));
			wcsncat_s(stripInfo, bit, _TRUNCATE);
		}
	} else {
		wcscpy_s(stripInfo, LL14(
			L" ストリップなし（レーン×1〜で Exp/Vol/Pan/Pitch/Gate をパート別に表示）",
			L" No strip (set Lanes ×1–3 for Exp/Vol/Pan/Pitch/Gate per part)",
			L" Pas de piste (×1–3 pour Exp/Vol/Pan/Pitch/Gate par partie)",
			L" Nessuna corsia (×1–3 Exp/Vol/Pan/Pitch/Gate per parte)",
			L" Sin pistas (×1–3 Exp/Vol/Pan/Pitch/Gate por parte)",
			L" 스트립 없음 (레인×1–3로 파트별 Exp/Vol/Pan/Pitch/Gate)",
			L" 无条带（车道×1–3 按声部显示 Exp/Vol/Pan/Pitch/Gate）",
			L" بلا شريط (ممرات ×1–3 لـ Exp/Vol/Pan/Pitch/Gate لكل جزء)",
			L" Нет полосы (×1–3 Exp/Vol/Pan/Pitch/Gate по партии)",
			L" Kein Strip (Spuren ×1–3 für Exp/Vol/Pan/Pitch/Gate je Part)",
			L" Sem faixa (×1–3 Exp/Vol/Pan/Pitch/Gate por parte)",
			L" Geen strook (banen ×1–3 Exp/Vol/Pan/Pitch/Gate per partij)",
			L" Brak pasa (tory ×1–3 Exp/Vol/Pan/Pitch/Gate na partię)",
			L" Şerit yok (×1–3 Exp/Vol/Pan/Pitch/Gate parti başına)"));
	}

	switch (topic) {
	case SC_HELP_LOOP_START:
		wcscpy_s(out, cch, LL14(
			L"【|: ループ開始】赤バー位置に |:n を置きます。n=繰り返し回数。消しゴム選択中にパレット|:＝その位置の|:を削除。五線の記号／Tone行チップを選択→Deleteでも消えます。MMLは |:n … :| 。",
			L"[|: Loop start] Places |:n at the red bar (n=repeat count). Eraser+palette |: deletes that mark. Or select the glyph/Tone chip → Delete. MML: |:n … :|.",
			L"【|: Début de boucle】Place |:n sur la barre rouge (n=répétitions). Gomme+palette |: = supprimer. Ou sélectionner le glyphe → Suppr. MML: |:n … :|.",
			L"【|: Inizio loop】Piazza |:n sulla barra rossa (n=ripetizioni). Gomma+tavolozza |: = elimina. Oppure seleziona glifo → Canc. MML: |:n … :|.",
			L"【|: Inicio de bucle】Coloca |:n en la barra roja (n=repeticiones). Borrar+paleta |: = borrar. O seleccionar glifo → Supr. MML: |:n … :|.",
			L"【|: 루프 시작】빨간 바에 |:n 배치(n=반복). 지우개+팔레트 |: = 삭제. 기호/Tone 칩 선택→Delete. MML: |:n … :|.",
			L"【|: 循环开始】在红条处放置 |:n（n=重复次数）。橡皮+调色板|:＝删除。或选中符号/音色条→Delete。MML：|:n … :|。",
			L"【|: بداية الحلقة】يضع |:n عند الشريط الأحمر (n=مرات التكرار). الممحاة+لوحة |: للحذف. أو حدد الرمز ثم Delete. MML: |:n … :|.",
			L"【|: Начало цикла】Ставит |:n на красной метке (n=повторы). Ластик+палитра |: = удалить. Или выбрать знак → Delete. MML: |:n … :|.",
			L"【|: Loop-Start】Setzt |:n an der roten Markierung (n=Wiederholungen). Radierer+Palette |: = löschen. Oder Glyph wählen → Entf. MML: |:n … :|.",
			L"【|: Início do loop】Coloca |:n na barra vermelha (n=repetições). Borracha+paleta |: = apagar. Ou selecionar glifo → Del. MML: |:n … :|.",
			L"【|: Lusstart】Plaatst |:n op de rode balk (n=herhalingen). Gum+palet |: = wissen. Of glyph selecteren → Del. MML: |:n … :|.",
			L"【|: Początek pętli】Stawia |:n na czerwonym pasku (n=powtórzenia). Gumka+paleta |: = usuń. Lub zaznacz glif → Del. MML: |:n … :|.",
			L"【|: Döngü başlangıcı】Kırmızı çubuğa |:n koyar (n=tekrar). Silgi+palet |: = sil. Veya glifi seç → Del. MML: |:n … :|."));
		break;
	case SC_HELP_LOOP_END:
		wcscpy_s(out, cch, LL14(
			L"【:| ループ終了】赤バー位置にループ終端を置きます。開始|:と対で使います。消しゴム+パレット:|、または記号を選択→Deleteで削除。ネスト時は「ネスト」配置モードで同じtickに積み上げ可能。",
			L"[:| Loop end] Places loop end at the red bar (pairs with |:). Eraser+palette :|, or select glyph → Delete. Nest mode stacks at the same tick.",
			L"【:| Fin de boucle】Place la fin sur la barre rouge (avec |:). Gomme+palette :|, ou sélection → Suppr. Mode nid empile au même tick.",
			L"【:| Fine loop】Fine sulla barra rossa (con |:). Gomma+tavolozza :|, o seleziona → Canc. Modalità nest impila sullo stesso tick.",
			L"【:| Fin de bucle】Fin en la barra roja (con |:). Borrar+paleta :|, o seleccionar → Supr. Modo anidado apila en el mismo tick.",
			L"【:| 루프 끝】빨간 바에 종료 배치(|:와 쌍). 지우개+팔레트 :| 또는 선택→Delete. 중첩 모드는 같은 tick에 쌓음.",
			L"【:| 循环结束】在红条放置结束（与|:成对）。橡皮+调色板:|，或选中→Delete。嵌套模式可在同一tick叠加。",
			L"【:| نهاية الحلقة】يضع النهاية عند الشريط الأحمر (مع |:). الممحاة+لوحة :| أو حدد ثم Delete. وضع التداخل يكدّس على نفس tick.",
			L"【:| Конец цикла】Конец на красной метке (с |:). Ластик+палитра :| или выбор → Delete. Режим вложения складывает на одном tick.",
			L"【:| Loop-Ende】Ende an der roten Markierung (mit |:). Radierer+Palette :| oder Auswahl → Entf. Nest-Modus stapelt am gleichen Tick.",
			L"【:| Fim do loop】Fim na barra vermelha (com |:). Borracha+paleta :| ou selecionar → Del. Modo ninho empilha no mesmo tick.",
			L"【:| Luseinde】Einde op rode balk (met |:). Gum+palet :| of selecteren → Del. Nest-modus stapelt op dezelfde tick.",
			L"【:| Koniec pętli】Koniec na czerwonym pasku (z |:). Gumka+paleta :| lub zaznacz → Del. Tryb zagnieżdżenia układa na tym samym tick.",
			L"【:| Döngü sonu】Kırmızı çubuğa son koyar (|: ile çift). Silgi+palet :| veya seç → Del. Yuva modu aynı tick’te yığar."));
		break;
	case SC_HELP_OTTAVA:
		wcscpy_s(out, cch, LL14(
			L"【8va/8vb/16/32】赤バー位置からオッターバ開始。譜面上の書かれた高さ≠実音（実音は±1〜3oct）。同じ位置にlocoを置くと打ち消し。再度8vaを置けば置き換わります。表示用記号で、再生ピッチはイベントの sounding MIDI に従います。",
			L"[8va/8vb/16/32] Ottava from the red bar. Written pitch ≠ sounding (±1–3 oct). loco cancels; another 8va replaces. Display mark; playback uses sounding MIDI.",
			L"【8va/8vb/16/32】Ottava depuis la barre rouge. Hauteur écrite ≠ sonore (±1–3 oct). loco annule; un autre 8va remplace. Marque d’affichage; lecture = MIDI sonore.",
			L"【8va/8vb/16/32】Ottava dalla barra rossa. Altezza scritta ≠ sonora (±1–3 ott). loco annulla; altro 8va sostituisce. Segno di visualizzazione; playback = MIDI sonoro.",
			L"【8va/8vb/16/32】Ottava desde la barra roja. Altura escrita ≠ sonora (±1–3 oct). loco cancela; otro 8va reemplaza. Marca visual; reproducción = MIDI sonoro.",
			L"【8va/8vb/16/32】빨간 바부터 옥타바. 기보 높이≠실음(±1–3oct). loco로 해제, 다시 8va면 교체. 표시용; 재생은 sounding MIDI.",
			L"【8va/8vb/16/32】从红条开始八度记号。记谱高度≠实音（±1–3八度）。loco取消；再放8va则替换。显示用；播放跟 sounding MIDI。",
			L"【8va/8vb/16/32】أوكتافا من الشريط الأحمر. الارتفاع المكتوب ≠ الصوت (±1–3 أوكتاف). loco يلغي؛ 8va آخر يستبدل. علامة عرض؛ التشغيل = MIDI الصوتي.",
			L"【8va/8vb/16/32】Оттава с красной метки. Написанная высота ≠ звучащая (±1–3 окт). loco отменяет; новый 8va заменяет. Знак отображения; playback = sounding MIDI.",
			L"【8va/8vb/16/32】Ottava ab roter Markierung. Notierte Höhe ≠ klingend (±1–3 Okt). loco hebt auf; neues 8va ersetzt. Anzeigezeichen; Wiedergabe = sounding MIDI.",
			L"【8va/8vb/16/32】Ottava a partir da barra vermelha. Altura escrita ≠ soante (±1–3 oct). loco cancela; outro 8va substitui. Marca visual; playback = MIDI soante.",
			L"【8va/8vb/16/32】Ottava vanaf rode balk. Geschreven hoogte ≠ klinkend (±1–3 oct). loco heft op; andere 8va vervangt. Weergavemarkering; playback = sounding MIDI.",
			L"【8va/8vb/16/32】Ottava od czerwonego paska. Wysokość zapisana ≠ brzmiąca (±1–3 okt). loco anuluje; kolejna 8va zastępuje. Znak wizualny; playback = sounding MIDI.",
			L"【8va/8vb/16/32】Kırmızı çubuktan ottava. Yazılan yükseklik ≠ duyulan (±1–3 okt). loco iptal; yeni 8va değiştirir. Görsel işaret; çalma = sounding MIDI."));
		break;
	case SC_HELP_LOCO:
		wcscpy_s(out, cch, LL14(
			L"【loco】赤バー位置でオッターバを解除（通常位置に戻す）。8va等のあと同じtickに置くと置き換え。別の場所に8vaを再開できます。削除は消しゴムでラベルクリック、または選択→Delete。",
			L"[loco] Cancels ottava at the red bar (back to normal). Same-tick after 8va replaces it. Restart 8va elsewhere. Delete: Eraser on label or Select→Delete.",
			L"【loco】Annule l’ottava sur la barre rouge. Au même tick qu’un 8va = remplace. Reprendre 8va ailleurs. Suppr: Gomme sur le label ou Sélection→Suppr.",
			L"【loco】Annulla ottava sulla barra rossa. Stesso tick dopo 8va = sostituisce. Riprendi 8va altrove. Elimina: Gomma sul label o Seleziona→Canc.",
			L"【loco】Cancela ottava en la barra roja. Mismo tick tras 8va = reemplaza. Reinicia 8va en otro sitio. Borrar: Borrar en etiqueta o Seleccionar→Supr.",
			L"【loco】빨간 바에서 옥타바 해제. 8va 같은 tick에 두면 교체. 다른 곳에서 8va 재개. 삭제: 지우개로 라벨 또는 선택→Delete.",
			L"【loco】在红条取消八度（恢复正常）。与8va同tick则替换。可在别处再开8va。删除：橡皮点标签或选择→Delete。",
			L"【loco】يلغي الأوكتافا عند الشريط الأحمر. نفس tick بعد 8va = استبدال. أعد 8va في مكان آخر. الحذف: ممحاة على التسمية أو تحديد→Delete.",
			L"【loco】Снимает оттаву на красной метке. Тот же tick после 8va = замена. Снова 8va в другом месте. Удаление: ластик по метке или Выбор→Delete.",
			L"【loco】Hebt Ottava an der roten Markierung auf. Gleicher Tick nach 8va = Ersetzen. 8va woanders neu. Löschen: Radierer auf Label oder Auswahl→Entf.",
			L"【loco】Cancela ottava na barra vermelha. Mesmo tick após 8va = substitui. Reinicie 8va noutro sítio. Apagar: borracha no rótulo ou Selecionar→Del.",
			L"【loco】Heft ottava op de rode balk op. Zelfde tick na 8va = vervangen. Start 8va elders opnieuw. Wissen: gum op label of Selecteren→Del.",
			L"【loco】Anuluje ottavę na czerwonym pasku. Ten sam tick po 8va = zamiana. Wznów 8va gdzie indziej. Usuń: gumka na etykiecie lub Zaznacz→Del.",
			L"【loco】Kırmızı çubukta ottavayı kaldırır. 8va sonrası aynı tick = değiştirir. Başka yerde 8va yeniden. Sil: etikette silgi veya Seç→Del."));
		break;
	case SC_HELP_PED_ON:
		wcscpy_s(out, cch, LL14(
			L"【Ped. ペダルON】MIDI sustain CC64=127。赤バー位置に配置。FM譜面では未使用。消しゴム+パレット、または記号削除でOFF。",
			L"[Ped. ON] MIDI sustain CC64=127 at the red bar. Unused on FM score. Eraser+palette or delete the mark to clear.",
			L"【Ped. ON】Sustain MIDI CC64=127 sur barre rouge. Inutilisé en partition FM. Gomme+palette ou supprimer le signe.",
			L"【Ped. ON】Sustain MIDI CC64=127 sulla barra rossa. Non usato su spartito FM. Gomma+tavolozza o elimina il segno.",
			L"【Ped. ON】Sustain MIDI CC64=127 en barra roja. No se usa en partitura FM. Borrar+paleta o eliminar el signo.",
			L"【Ped. ON】MIDI 서스테인 CC64=127을 빨간 바에. FM 악보에서는 미사용. 지우개+팔레트 또는 기호 삭제.",
			L"【Ped. ON】在红条放置 MIDI sustain CC64=127。FM谱面不用。橡皮+调色板或删除符号。",
			L"【Ped. ON】استدامة MIDI CC64=127 عند الشريط الأحمر. غير مستخدم في FM. الممحاة+لوحة أو حذف الرمز.",
			L"【Ped. ON】MIDI sustain CC64=127 на красной метке. Не для FM-партитуры. Ластик+палитра или удалить знак.",
			L"【Ped. ON】MIDI-Sustain CC64=127 an roter Markierung. Nicht für FM-Partitur. Radierer+Palette oder Zeichen löschen.",
			L"【Ped. ON】Sustain MIDI CC64=127 na barra vermelha. Não usado na partitura FM. Borracha+paleta ou apagar o signo.",
			L"【Ped. ON】MIDI-sustain CC64=127 op rode balk. Niet op FM-partituur. Gum+palet of teken wissen.",
			L"【Ped. ON】MIDI sustain CC64=127 na czerwonym pasku. Nieużywane w partyturze FM. Gumka+paleta lub usuń znak.",
			L"【Ped. ON】Kırmızı çubukta MIDI sustain CC64=127. FM partisyonda yok. Silgi+palet veya işareti sil."));
		break;
	case SC_HELP_PED_OFF:
		wcscpy_s(out, cch, LL14(
			L"【＊ ペダルOFF】MIDI sustain CC64=0。Ped.と対で使います。",
			L"[＊ Pedal OFF] MIDI sustain CC64=0. Use with Ped. ON.",
			L"【＊ Pedale OFF】Sustain MIDI CC64=0. À utiliser avec Ped. ON.",
			L"【＊ Pedale OFF】Sustain MIDI CC64=0. Usare con Ped. ON.",
			L"【＊ Pedal OFF】Sustain MIDI CC64=0. Usar con Ped. ON.",
			L"【＊ 페달 OFF】MIDI 서스테인 CC64=0. Ped. ON과 쌍.",
			L"【＊ 踏板OFF】MIDI sustain CC64=0。与 Ped. ON 成对。",
			L"【＊ دواسة OFF】استدامة MIDI CC64=0. مع Ped. ON.",
			L"【＊ Педаль OFF】MIDI sustain CC64=0. Вместе с Ped. ON.",
			L"【＊ Pedal OFF】MIDI-Sustain CC64=0. Mit Ped. ON verwenden.",
			L"【＊ Pedal OFF】Sustain MIDI CC64=0. Usar com Ped. ON.",
			L"【＊ Pedaal OFF】MIDI-sustain CC64=0. Gebruik met Ped. ON.",
			L"【＊ Pedal OFF】MIDI sustain CC64=0. Używać z Ped. ON.",
			L"【＊ Pedal OFF】MIDI sustain CC64=0. Ped. ON ile birlikte."));
		break;
	case SC_HELP_ERASER:
		wcscpy_s(out, cch, LL14(
			L"【消しゴム】音符・|:・:|・Ped.・8va/loco・Progチップをドラッグ/クリックで削除。パレットの|:や8vaを消しゴム中に押すと「赤バー上の同種マーク」を消します。ストリップ上では値を0/中央にリセット。",
			L"[Eraser] Click/drag to delete notes, |: :| Ped. 8va/loco, Prog chips. Eraser+palette |:/8va deletes that kind at the red bar. On strip: reset to 0/center.",
			L"【Gomme】Clic/glisser pour supprimer notes, |: :| Ped. 8va/loco, puces Prog. Gomme+palette |:/8va = même type sur barre rouge. Sur piste: reset 0/centre.",
			L"【Gomma】Clic/trascina per cancellare note, |: :| Ped. 8va/loco, chip Prog. Gomma+tavolozza |:/8va = stesso tipo sulla barra rossa. Su corsia: reset 0/centro.",
			L"【Borrar】Clic/arrastrar para borrar notas, |: :| Ped. 8va/loco, chips Prog. Borrar+paleta |:/8va = mismo tipo en barra roja. En pista: reset 0/centro.",
			L"【지우개】노트·|:·:|·Ped.·8va/loco·Prog 칩 클릭/드래그 삭제. 지우개+팔레트 |:/8va = 빨간 바 동종 삭제. 스트립은 0/중앙 리셋.",
			L"【橡皮】点击/拖动删除音符、|: :| Ped. 8va/loco、音色条。橡皮+调色板|:/8va＝删除红条上同种。条带上重置为0/中心。",
			L"【ممحاة】انقر/اسحب لحذف النغمات و|: :| Ped. 8va/loco ورقائق Prog. الممحاة+لوحة |:/8va تحذف نفس النوع عند الشريط الأحمر. على الشريط: إعادة 0/الوسط.",
			L"【Ластик】Клик/перетаскивание удаляет ноты, |: :| Ped. 8va/loco, чипы Prog. Ластик+палитра |:/8va = тот же тип на красной метке. На полосе: сброс 0/центр.",
			L"【Radierer】Klick/Ziehen löscht Noten, |: :| Ped. 8va/loco, Prog-Chips. Radierer+Palette |:/8va = gleicher Typ an roter Markierung. Im Strip: Reset 0/Mitte.",
			L"【Borracha】Clique/arraste para apagar notas, |: :| Ped. 8va/loco, chips Prog. Borracha+paleta |:/8va = mesmo tipo na barra vermelha. Na faixa: reset 0/centro.",
			L"【Gum】Klik/sleep om noten, |: :| Ped. 8va/loco, Prog-chips te wissen. Gum+palet |:/8va = zelfde soort op rode balk. Op strook: reset 0/midden.",
			L"【Gumka】Klik/przeciągnij by usunąć nuty, |: :| Ped. 8va/loco, chipy Prog. Gumka+paleta |:/8va = ten sam typ na czerwonym pasku. Na pasie: reset 0/środek.",
			L"【Silgi】Tıkla/sürükle: notalar, |: :| Ped. 8va/loco, Prog çipleri. Silgi+palet |:/8va = kırmızı çubukta aynı tür. Şeritte: 0/merkeze sıfırla."));
		break;
	case SC_HELP_SELECT:
		wcscpy_s(out, cch, LL14(
			L"【選択】矢印=範囲/移動。ESC・右クリで通常カーソル。Ctrl+C/X/V・Shift+V=貼付挿入、Ctrl+Z/Y(10)。\r\nルーラー=全パート範囲。Delete=削除。範囲に空白挿入(後ろずらし)=Ctrl+Shift+I / 右クリック。",
			L"[Select] Arrow=range/move. ESC/RMB→arrow. Ctrl+C/X/V; Shift+V=paste-insert; Ctrl+Z/Y(10).\r\nRuler=all parts. Delete removes. Insert blank (shift later)=Ctrl+Shift+I / right-click.",
			L"[Select] Flèche=plage. ESC/RMB. Ctrl+C/X/V; Shift+V; Ctrl+Z/Y.\r\nRègle=toutes parties. Insérer vide=Ctrl+Shift+I.",
			L"[Select] Freccia=intervallo. ESC/RMB. Ctrl+C/X/V; Shift+V; Ctrl+Z/Y.\r\nRighello=tutte. Inserisci vuoto=Ctrl+Shift+I.",
			L"[Select] Flecha=rango. ESC/RMB. Ctrl+C/X/V; Shift+V; Ctrl+Z/Y.\r\nRegla=todas. Insertar vacío=Ctrl+Shift+I.",
			L"[선택] 화살표=범위/이동. ESC/우클릭. Ctrl+C/X/V; Shift+V; Ctrl+Z/Y.\r\n눈금자=전체. 빈 구간 삽입=Ctrl+Shift+I.",
			L"[选择] 箭头=范围/移动。ESC/右键。Ctrl+C/X/V；Shift+V；Ctrl+Z/Y。\r\n标尺=全部声部。插入空白=Ctrl+Shift+I。",
			L"[تحديد] سهم=نطاق/نقل. ESC/زر أيمن. Ctrl+C/X/V؛ Shift+V؛ Ctrl+Z/Y.\r\nالمسطرة=كل الأجزاء. إدراج فراغ=Ctrl+Shift+I.",
			L"[Select] Диапазон/перенос. ESC/ПКМ. Ctrl+C/X/V; Shift+V; Ctrl+Z/Y.\r\nЛинейка=все. Вставить пустоту=Ctrl+Shift+I.",
			L"[Select] Bereich/Bewegen. ESC/RMB. Ctrl+C/X/V; Shift+V; Ctrl+Z/Y.\r\nLineal=alle. Leerraum=Ctrl+Shift+I.",
			L"[Select] Intervalo/mover. ESC/RMB. Ctrl+C/X/V; Shift+V; Ctrl+Z/Y.\r\nRégua=todas. Inserir vazio=Ctrl+Shift+I.",
			L"[Select] Bereik/verplaatsen. ESC/RMB. Ctrl+C/X/V; Shift+V; Ctrl+Z/Y.\r\nLiniaal=alle. Leeg invoegen=Ctrl+Shift+I.",
			L"[Select] Zakres/przenoszenie. ESC/RMB. Ctrl+C/X/V; Shift+V; Ctrl+Z/Y.\r\nLinijka=wszystkie. Wstaw pustkę=Ctrl+Shift+I.",
			L"[Select] Aralık/taşı. ESC/sağ tık. Ctrl+C/X/V; Shift+V; Ctrl+Z/Y.\r\nCetvel=tümü. Boş ekle=Ctrl+Shift+I."));
		break;
	case SC_HELP_PENCIL:
		wcscpy_s(out, cch, LL14(
			L"【鉛筆】五線クリックで音符/休符を配置（MIDIは配置時にその場発音）。音長はパレット。Space=赤バーからwavout再生。Home=マーカー先頭。",
			L"[Pencil] Click staff to place notes/rests (MIDI auditions on place). Duration from palette. Space=wavout from red bar. Home=marker to start.",
			L"【Crayon】Clic portée = notes/silences (MIDI audition à la pose). Durée = palette. Espace=wavout depuis barre rouge. Home=début.",
			L"【Matita】Clic pentagramma = note/pause (MIDI audizione al click). Durata = tavolozza. Spazio=wavout da barra rossa. Home=inizio.",
			L"【Lápiz】Clic pentagrama = notas/silencios (MIDI audición al colocar). Duración = paleta. Espacio=wavout desde barra roja. Inicio=Home.",
			L"【연필】보표 클릭으로 음표/쉼표(MIDI는 배치 시 미리듣기). 길이는 팔레트. Space=빨간 바부터 wavout. Home=마커 맨앞.",
			L"【铅笔】点击五线放置音符/休止（MIDI放置时试听）。时值用调色板。Space=从红条wavout。Home=标记到开头。",
			L"【قلم】انقر المدرج لوضع نغمات/سكتات (MIDI يسمع عند الوضع). المدة من اللوحة. Space=wavout من الشريط الأحمر. Home=بداية العلامة.",
			L"【Карандаш】Клик по нотоносцу — ноты/паузы (MIDI звучит при вводе). Длительность из палитры. Space=wavout с красной метки. Home=в начало.",
			L"【Stift】Klick auf Notensystem = Noten/Pausen (MIDI hörbar beim Setzen). Dauer aus Palette. Leertaste=wavout ab roter Markierung. Pos1=Anfang.",
			L"【Lápis】Clique na pauta = notas/pausas (MIDI audiciona ao colocar). Duração = paleta. Espaço=wavout desde a barra vermelha. Home=início.",
			L"【Potlood】Klik notenbalk = noten/rusten (MIDI auditeert bij plaatsen). Duur = palet. Spatie=wavout vanaf rode balk. Home=begin.",
			L"【Ołówek】Klik pięciolinii = nuty/pauzy (MIDI gra przy wstawieniu). Wartość z palety. Spacja=wavout od czerwonego paska. Home=początek.",
			L"【Kalem】Portede tıkla = nota/es (MIDI yerleştirirken duyulur). Süre paletten. Space=kırmızı çubuktan wavout. Home=işaret başı."));
		break;
	case SC_HELP_TEMPO:
		wcscpy_s(out, cch, LL14(
			L"【テンポツール】五線クリックでテンポイベントを配置。ツールバーTempoでBPMプリセットも切替。",
			L"[Tempo tool] Click staff to place tempo events. Toolbar Tempo switches BPM presets.",
			L"【Outil Tempo】Clic portée = événement tempo. Bouton Tempo = préréglages BPM.",
			L"【Strumento Tempo】Clic pentagramma = evento tempo. Pulsante Tempo = preset BPM.",
			L"【Herramienta Tempo】Clic pentagrama = evento tempo. Botón Tempo = presets BPM.",
			L"【템포 도구】보표 클릭으로 템포 이벤트. 툴바 Tempo로 BPM 프리셋.",
			L"【速度工具】点击五线放置速度事件。工具栏 Tempo 切换 BPM 预设。",
			L"【أداة الإيقاع】انقر المدرج لوضع حدث الإيقاع. زر Tempo يبدّل إعدادات BPM.",
			L"【Инструмент темпа】Клик по нотоносцу — событие темпа. Кнопка Tempo — пресеты BPM.",
			L"【Tempo-Werkzeug】Klick auf Notensystem setzt Tempo-Events. Toolbar-Tempo wechselt BPM-Presets.",
			L"【Ferramenta Tempo】Clique na pauta = evento de tempo. Botão Tempo = presets de BPM.",
			L"【Tempo-gereedschap】Klik notenbalk = tempo-event. Knop Tempo = BPM-presets.",
			L"【Narzędzie Tempo】Klik pięciolinii = zdarzenie tempa. Przycisk Tempo = presety BPM.",
			L"【Tempo aracı】Portede tıkla = tempo olayı. Tempo düğmesi BPM önayarları."));
		break;
	case SC_HELP_METER:
		wcscpy_s(out, cch, LL14(
			L"【譜表ボタン / 右クリック】拍子・調号・移調。拍子はマーカー位置の小節頭に縦書き表示（4/4ではなく 4 上 4 下）。曲途中の @METER / @TS も可。",
			L"[Layout button / right-click] Meter, key, transpose. Meter at marker bar — stacked digits like notation (not “4/4” text). Mid-song @METER / @TS supported.",
			L"【Bouton Portée / clic droit】Mesure, armure, transposition. Chiffres empilés sur la portée.",
			L"【Impag. / menu】Misura, armatura, trasposizione. Cifre verticali sulla pentagramma.",
			L"【Layout / menú】Compás, armadura, transposición. Dígitos apilados en el pentagrama.",
			L"【보표 버튼】박자·조표·이조. 세로 배치 박자표.",
			L"【谱表按钮】拍号、调号、移调。竖排拍号显示。",
			L"【زر التخطيط】إيقاع ومفتاح ونقل.",
			L"【Партитура】Размер, тональность, транспонирование. Вертикальные цифры.",
			L"【Notation】Taktart, Tonart, Transponieren. Vertikale Ziffern.",
			L"【Layout】Compasso, armadura, transpor. Dígitos verticais.",
			L"【Layout】Maat, toonsoort, transponeren.",
			L"【Układ】Metrum, tonacja, transpozycja.",
			L"【Düzen】Ölçü, armatür, transpoz. Dikey rakamlar."));
		break;
	case SC_HELP_MARK:
		wcscpy_s(out, cch, LL14(
			L"【マーカー】ルーラークリック＝赤バー（再生開始位置）。Play/Spaceはその位置から。パレットの|:・8va等も赤バー基準で入ります。",
			L"[Marker] Ruler click = red bar (play-from). Play/Space start there. Palette |:/8va… also insert at the red bar.",
			L"【Marqueur】Clic règle = barre rouge (départ lecture). Play/Espace depuis là. Palette |:/8va… aussi à la barre rouge.",
			L"【Marcatore】Clic righello = barra rossa (inizio play). Play/Spazio da lì. Tavolozza |:/8va… anch’essa sulla barra rossa.",
			L"【Marcador】Clic regla = barra roja (inicio). Play/Espacio desde ahí. Paleta |:/8va… también en la barra roja.",
			L"【마커】눈금자 클릭=빨간 바(재생 시작). Play/Space는 그 위치부터. 팔레트 |:/8va도 빨간 바 기준.",
			L"【标记】点标尺＝红条（播放起点）。Play/Space从那里开始。调色板|:/8va等也按红条插入。",
			L"【علامة】نقر المسطرة = الشريط الأحمر (بداية التشغيل). Play/Space من هناك. لوحة |:/8va أيضاً عند الشريط الأحمر.",
			L"【Маркер】Клик по линейке = красная метка (старт). Play/Space оттуда. Палитра |:/8va… тоже на красной метке.",
			L"【Markierung】Klick auf Lineal = rote Markierung (Start). Play/Leertaste ab dort. Palette |:/8va… ebenfalls an roter Markierung.",
			L"【Marcador】Clique na régua = barra vermelha (início). Play/Espaço a partir daí. Paleta |:/8va… também na barra vermelha.",
			L"【Markering】Klik liniaal = rode balk (start). Play/Spatie vanaf daar. Palet |:/8va… ook op rode balk.",
			L"【Znacznik】Klik linijki = czerwony pasek (start). Play/Spacja stamtąd. Paleta |:/8va… też na czerwonym pasku.",
			L"【İşaret】Cetvele tıkla = kırmızı çubuk (başlangıç). Play/Space oradan. Palet |:/8va… da kırmızı çubuğa."));
		break;
	case SC_HELP_TIE:
		wcscpy_s(out, cch, LL14(
			L"【タイ】選択した音符同士をタイでつなぎます（同一音程の隣接が対象）。",
			L"[Tie] Ties selected adjacent notes of the same pitch.",
			L"【Liaison】Relie les notes sélectionnées adjacentes de même hauteur.",
			L"【Legatura】Collega note selezionate adiacenti della stessa altezza.",
			L"【Ligadura】Une notas seleccionadas adyacentes del mismo tono.",
			L"【타이】선택한 인접 동일 음정을 타이로 연결.",
			L"【连音线】连接所选相邻同音高音符。",
			L"【ربط】يربط النغمات المحددة المتجاورة بنفس الطبقة.",
			L"【Лига】Связывает выбранные соседние ноты одной высоты.",
			L"【Bindebogen】Bindet ausgewählte benachbarte Noten gleicher Höhe.",
			L"【Ligadura】Liga notas selecionadas adjacentes da mesma altura.",
			L"【Boog】Bindt geselecteerde aangrenzende noten van dezelfde toonhoogte.",
			L"【Legato】Łączy zaznaczone sąsiednie nuty tej samej wysokości.",
			L"【Bağ】Seçili bitişik aynı perdeli notaları bağlar."));
		break;
	case SC_HELP_FIT:
		wcscpy_s(out, cch, LL14(
			L"【Fit】スナップON/OFF。ON時は配置が音長グリッドに吸着します。",
			L"[Fit] Snap ON/OFF. When ON, placement sticks to the duration grid.",
			L"【Fit】Aimantation ON/OFF. ON = pose collée à la grille de durée.",
			L"【Fit】Snap ON/OFF. ON = inserimento sulla griglia di durata.",
			L"【Fit】Ajuste ON/OFF. ON = colocación en la rejilla de duración.",
			L"【Fit】스냅 ON/OFF. ON이면 음가 그리드에 흡착.",
			L"【Fit】吸附开/关。开时贴合时值网格。",
			L"【Fit】التقاط ON/OFF. عند ON يلتصق بشبكة المدة.",
			L"【Fit】Привязка ON/OFF. При ON — к сетке длительности.",
			L"【Fit】Snap ON/OFF. Bei ON haftet an der Dauern-Raster.",
			L"【Fit】Encaixe ON/OFF. ON = cola na grelha de duração.",
			L"【Fit】Snap ON/OFF. Bij ON kleven aan duur-raster.",
			L"【Fit】Przyciąganie ON/OFF. ON = do siatki wartości.",
			L"【Fit】Yapışma ON/OFF. ON iken süre ızgarasına tutunur."));
		break;
	case SC_HELP_STRIP:
		_snwprintf_s(out, cch, _TRUNCATE, LL14(
			L"【ストリップ】現在パート(ch%d)専用の CC/パラメータ曲線。%s Exp=表情(CC11) Vol=音量 Pan=定位 Pitch=ベンド Gate=ゲート%%。レーン数・種類はパートごとに記憶。解像度1/4〜1/64。",
			L"[Strip] CC/param curves for current part (ch%d).%s Exp=CC11 Vol=level Pan=position Pitch=bend Gate=%%. Lane count/kinds remembered per part. Res 1/4–1/64.",
			L"【Piste】Courbes CC/param de la partie (ch%d).%s Exp=CC11 Vol=niveau Pan=position Pitch=bend Gate=%%. Nb/types mémorisés par partie. Rés. 1/4–1/64.",
			L"【Corsia】Curve CC/param della parte (ch%d).%s Exp=CC11 Vol=livello Pan=posizione Pitch=bend Gate=%%. Numero/tipi memorizzati per parte. Ris. 1/4–1/64.",
			L"【Pista】Curvas CC/param de la parte (ch%d).%s Exp=CC11 Vol=nivel Pan=posición Pitch=bend Gate=%%. Nº/tipos recordados por parte. Res. 1/4–1/64.",
			L"【스트립】현재 파트(ch%d) CC/파라미터 곡선.%s Exp=CC11 Vol=음량 Pan=위치 Pitch=벤드 Gate=%%. 레인 수/종류는 파트별 기억. 해상도 1/4–1/64.",
			L"【条带】当前声部(ch%d)的 CC/参数曲线。%s Exp=CC11 Vol=音量 Pan=声像 Pitch=弯音 Gate=%%。车道数/种类按声部记忆。分辨率1/4–1/64。",
			L"【شريط】منحنيات CC/معلمات للجزء الحالي (ch%d).%s Exp=CC11 Vol=مستوى Pan=موضع Pitch=انحناء Gate=%%. عدد/أنواع الممرات تُحفظ لكل جزء. الدقة 1/4–1/64.",
			L"【Полоса】Кривые CC/параметров партии (ch%d).%s Exp=CC11 Vol=громкость Pan=панорама Pitch=бенд Gate=%%. Число/типы дорожек помнятся по партии. Разр. 1/4–1/64.",
			L"【Strip】CC/Param-Kurven des Parts (ch%d).%s Exp=CC11 Vol=Pegel Pan=Position Pitch=Bend Gate=%%. Spuranzahl/-arten je Part gespeichert. Auflösung 1/4–1/64.",
			L"【Faixa】Curvas CC/param da parte (ch%d).%s Exp=CC11 Vol=nível Pan=posição Pitch=bend Gate=%%. Nº/tipos lembrados por parte. Res. 1/4–1/64.",
			L"【Strook】CC/param-curven van partij (ch%d).%s Exp=CC11 Vol=niveau Pan=positie Pitch=bend Gate=%%. Aantal/soorten banen per partij. Res. 1/4–1/64.",
			L"【Pas】Krzywe CC/param partii (ch%d).%s Exp=CC11 Vol=poziom Pan=pozycja Pitch=bend Gate=%%. Liczba/rodzaje torów zapamiętane per partię. Rozdz. 1/4–1/64.",
			L"【Şerit】Geçerli parti (ch%d) CC/param eğrileri.%s Exp=CC11 Vol=seviye Pan=konum Pitch=bend Gate=%%. Şerit sayısı/türleri parti başına. Çözünürlük 1/4–1/64."),
			curPart + 1, stripInfo);
		break;
	case SC_HELP_PLAY:
		wcscpy_s(out, cch, LL14(
			L"【再生】赤バー以降をwavout→一時WAVで譜面内再生（Spaceで停止）。VST binds付きは書き出し経路で反映。",
			L"[Play] From red bar: wavout → temp WAV in-score preview (Space stops). VST binds applied via export path.",
			L"【Lecture】Depuis barre rouge: wavout → WAV temp (Espace = stop). Binds VST via chemin d’export.",
			L"【Play】Da barra rossa: wavout → WAV temp (Spazio = stop). Bind VST via percorso export.",
			L"【Reproducir】Desde barra roja: wavout → WAV temp (Espacio = stop). Binds VST vía exportación.",
			L"【재생】빨간 바부터 wavout→임시 WAV 미리듣기(Space 정지). VST binds는보내기 경로로 반영.",
			L"【播放】从红条：wavout→临时WAV预览（Space停止）。带VST绑定经导出路径生效。",
			L"【تشغيل】من الشريط الأحمر: wavout → WAV مؤقت (Space يوقف). روابط VST عبر مسار التصدير.",
			L"【Воспроизведение】С красной метки: wavout → временный WAV (Space = стоп). VST binds через экспорт.",
			L"【Wiedergabe】Ab roter Markierung: wavout → Temp-WAV (Leertaste = Stop). VST-Binds über Exportpfad.",
			L"【Reproduzir】Desde a barra vermelha: wavout → WAV temp (Espaço = parar). Binds VST via exportação.",
			L"【Afspelen】Vanaf rode balk: wavout → tijdelijke WAV (Spatie = stop). VST-binds via exportpad.",
			L"【Odtwarzanie】Od czerwonego paska: wavout → tymczasowy WAV (Spacja = stop). Bindings VST przez eksport.",
			L"【Çal】Kırmızı çubuktan: wavout → geçici WAV (Space = dur). VST bağları dışa aktarma yoluyla."));
		break;
	case SC_HELP_SPACE:
		wcscpy_s(out, cch, LL14(
			L"【Space】再生/停止トグル。Homeで赤バーを先頭へ。編集ボックス入力中は無効。",
			L"[Space] Play/stop toggle. Home moves red bar to start. Disabled while typing in edit boxes.",
			L"【Espace】Lecture/arrêt. Home = début. Inactif dans les champs d’édition.",
			L"【Spazio】Play/stop. Home = inizio. Disattivo nelle caselle di testo.",
			L"【Espacio】Play/stop. Inicio = principio. Inactivo en cajas de edición.",
			L"【Space】재생/정지. Home=빨간 바 맨앞. 편집 상자 입력 중엔 무효.",
			L"【Space】播放/停止。Home将红条移到开头。在编辑框输入时无效。",
			L"【Space】تشغيل/إيقاف. Home ينقل الشريط الأحمر للبداية. معطّل أثناء الكتابة في الحقول.",
			L"【Space】Старт/стоп. Home — метка в начало. Не работает при вводе в поля.",
			L"【Leertaste】Play/Stop. Pos1 = Anfang. Inaktiv in Editfeldern.",
			L"【Espaço】Play/parar. Home = início. Inativo em caixas de edição.",
			L"【Spatie】Play/stop. Home = begin. Uitgeschakeld in bewerkingsvakken.",
			L"【Spacja】Play/stop. Home = początek. Nieaktywne w polach edycji.",
			L"【Space】Çal/dur. Home = kırmızı çubuk başa. Düzenleme kutularında geçersiz."));
		break;
	case SC_HELP_CH_PART:
		_snwprintf_s(out, cch, _TRUNCATE, LL14(
			L"【パート ch%d】編集対象。ストリップ設定はこのパート専用に保存。%s Mute/Soloは左リスト。",
			L"[Part ch%d] Edit target. Strip settings saved per part.%s Mute/Solo in the left list.",
			L"【Partie ch%d】Cible d’édition. Réglages de piste mémorisés.%s Mute/Solo à gauche.",
			L"【Parte ch%d】Destinazione modifica. Impostazioni corsia salvate.%s Mute/Solo a sinistra.",
			L"【Parte ch%d】Objetivo de edición. Ajustes de pista guardados.%s Mute/Solo a la izquierda.",
			L"【파트 ch%d】편집 대상. 스트립 설정은 이 파트에 저장.%s Mute/Solo는 왼쪽 목록.",
			L"【声部 ch%d】编辑对象。条带设置按声部保存。%s Mute/Solo在左侧列表。",
			L"【جزء ch%d】هدف التحرير. إعدادات الشريط تُحفظ لهذا الجزء.%s كتم/منفرد في القائمة اليسرى.",
			L"【Партия ch%d】Цель правки. Настройки полосы сохраняются.%s Mute/Solo слева.",
			L"【Part ch%d】Bearbeitungsziel. Strip-Einstellungen je Part.%s Mute/Solo links.",
			L"【Parte ch%d】Alvo de edição. Definições de faixa guardadas.%s Mute/Solo à esquerda.",
			L"【Partij ch%d】Bewerkdoel. Strookinstellingen per partij.%s Mute/Solo links.",
			L"【Partia ch%d】Cel edycji. Ustawienia pasa zapamiętane.%s Mute/Solo po lewej.",
			L"【Parti ch%d】Düzenleme hedefi. Şerit ayarları partiye özel.%s Mute/Solo solda."),
			curPart + 1, stripInfo);
		break;
	case SC_HELP_REST:
		wcscpy_s(out, cch, LL14(
			L"【休符】パレットの休符を選ぶと鉛筆が休符配置になります。音長は選択中の音価。",
			L"[Rest] Choosing a rest in the palette makes Pencil place rests. Duration = selected value.",
			L"【Silence】Choisir un silence dans la palette = pose de silences. Durée = valeur sélectionnée.",
			L"【Pausa】Scegliere una pausa in tavolozza = inserimento pause. Durata = valore selezionato.",
			L"【Silencio】Elegir un silencio en la paleta = colocar silencios. Duración = valor seleccionado.",
			L"【쉼표】팔레트에서 쉼표 선택 시 연필이 쉼표 배치. 길이는 선택 음가.",
			L"【休止符】在调色板选休止后，铅笔放置休止。时值=当前音值。",
			L"【سكتة】اختيار سكتة في اللوحة يجعل القلم يضعها. المدة = القيمة المحددة.",
			L"【Пауза】Выбор паузы в палитре — карандаш ставит паузы. Длительность = выбранная.",
			L"【Pause】Pause in Palette wählen = Stift setzt Pausen. Dauer = gewählter Wert.",
			L"【Pausa】Escolher pausa na paleta = lápis coloca pausas. Duração = valor selecionado.",
			L"【Rust】Rust in palet kiezen = potlood plaatst rusten. Duur = geselecteerde waarde.",
			L"【Pauza】Wybór pauzy w palecie = ołówek wstawia pauzy. Wartość = wybrana.",
			L"【Es】Paletten es seçmek kalemi es koyar. Süre = seçili değer."));
		break;
	case SC_HELP_TUPLET:
		wcscpy_s(out, cch, LL14(
			L"【連符 3/5/6/8】音長を連符化。もう一度押すと解除。付点と併用可。",
			L"[Tuplet 3/5/6/8] Makes duration a tuplet. Press again to clear. Combinable with dotted.",
			L"【Nolet 3/5/6/8】Durée en nolet. Rappuyer pour annuler. Combinable avec point.",
			L"【Gruppetto 3/5/6/8】Durata a gruppo. Ripremi per annullare. Combinabile col punto.",
			L"【Grupeto 3/5/6/8】Duración en grupeto. Pulsar otra vez para quitar. Combinable con puntillo.",
			L"【연음 3/5/6/8】음가를 연음화. 다시 누르면 해제. 점음과 병용 가능.",
			L"【连音 3/5/6/8】时值连音化。再按取消。可与附点并用。",
			L"【تجميع 3/5/6/8】يجعل المدة تجميعاً. اضغط مجدداً للإلغاء. مع النقطة ممكن.",
			L"【Триоль 3/5/6/8】Длительность в группу. Повтор — сброс. С точкой совместимо.",
			L"【N-tole 3/5/6/8】Dauer als N-tole. Nochmal drücken = aus. Mit Punkt kombinierbar.",
			L"【Quiáltera 3/5/6/8】Duração em quiáltera. Pressione de novo para limpar. Com pontilhado ok.",
			L"【N-ool 3/5/6/8】Duur als n-ool. Opnieuw = uit. Combineerbaar met punt.",
			L"【N-ola 3/5/6/8】Wartość jako n-ola. Ponownie = wyłącz. Z kropką OK.",
			L"【N-ol 3/5/6/8】Süreyi n-ol yapar. Tekrar basınca kalkar. Noktalı ile kullanılabilir."));
		break;
	case SC_HELP_ACCIDENTAL:
		wcscpy_s(out, cch, LL14(
			L"【♯♮♭】次に置く音符の臨時記号。♮で解除。書かれた高さに加算してからオッターバで実音化。",
			L"[♯♮♭] Accidental for the next note. ♮ clears. Added to written pitch, then ottava to sounding.",
			L"【♯♮♭】Altération de la prochaine note. ♮ annule. Ajoutée à la hauteur écrite, puis ottava.",
			L"【♯♮♭】Alterazione della prossima nota. ♮ annulla. Aggiunta all’altezza scritta, poi ottava.",
			L"【♯♮♭】Alteración de la siguiente nota. ♮ limpia. Se suma a la altura escrita, luego ottava.",
			L"【♯♮♭】다음 음의 임시표. ♮로 해제. 기보 높이에 가산 후 옥타바로 실음화.",
			L"【♯♮♭】下一音符的临时记号。♮清除。加在记谱高度上，再经八度得实音。",
			L"【♯♮♭】عرضية للنغمة التالية. ♮ يلغي. تُضاف للارتفاع المكتوب ثم الأوكتافا.",
			L"【♯♮♭】Знак альтерации следующей ноты. ♮ сбрасывает. К написанной высоте, затем оттава.",
			L"【♯♮♭】Vorzeichen für die nächste Note. ♮ löscht. Zur notierten Höhe, dann Ottava.",
			L"【♯♮♭】Acidente da próxima nota. ♮ limpa. Soma à altura escrita, depois ottava.",
			L"【♯♮♭】Toevallig teken voor volgende noot. ♮ wist. Bij geschreven hoogte, dan ottava.",
			L"【♯♮♭】Znak chromatyczny następnej nuty. ♮ czyści. Do wysokości zapisanej, potem ottava.",
			L"【♯♮♭】Sonraki notanın arızi işareti. ♮ temizler. Yazılan yüksekliğe eklenir, sonra ottava."));
		break;
	case SC_HELP_NEST:
		wcscpy_s(out, cch, LL14(
			L"【ネスト】同じtickにマークを積み上げ（|:の多重など）。「1葉」は同種を置換。",
			L"[Nest] Stack marks at the same tick (nested |:…). “Replace” keeps one of each kind.",
			L"【Nid】Empile les marques au même tick (|: imbriqués…). « Remplacer » = une par type.",
			L"【Nest】Impila segni sullo stesso tick (|: annidati…). « Sostituisci » = uno per tipo.",
			L"【Anidar】Apila marcas en el mismo tick (|: anidados…). « Reemplazar » = una por tipo.",
			L"【중첩】같은 tick에 마크 쌓기(|: 다중). 「1중」은 동종 치환.",
			L"【嵌套】在同一tick叠加标记（多重|:）。「单层」同种替换。",
			L"【تداخل】تكديس العلامات على نفس tick (|: متداخلة…). «استبدال» = واحدة لكل نوع.",
			L"【Вложение】Складывает знаки на одном tick (вложенные |:…). «Замена» — один на тип.",
			L"【Nest】Stapelt Zeichen am gleichen Tick (verschachtelte |:…). „Ersetzen“ = eins pro Art.",
			L"【Ninho】Empilha marcas no mesmo tick (|: aninhados…). «Substituir» = uma por tipo.",
			L"【Nest】Stapelt markeringen op dezelfde tick (geneste |:…). «Vervangen» = één per soort.",
			L"【Zagnieżdżenie】Układa znaki na tym samym tick (zagnieżdżone |:…). «Zamień» = jeden na typ.",
			L"【Yuva】Aynı tick’te işaret yığar (içiçe |:…). «Değiştir» = tür başına bir."));
		break;
	case SC_HELP_REPLACE:
		wcscpy_s(out, cch, LL14(
			L"【1葉】同じtick+同種マークは1つだけ（置換）。普段の|: / 8va配置向き。",
			L"[Replace] One mark of each kind per tick (replace). Best for normal |: / 8va placement.",
			L"【Remplacer】Une marque par type et tick (remplace). Idéal pour |: / 8va courants.",
			L"【Sostituisci】Un segno per tipo e tick (sostituisce). Ideale per |: / 8va normali.",
			L"【Reemplazar】Una marca por tipo y tick (reemplaza). Ideal para |: / 8va habituales.",
			L"【1중】같은 tick+동종 마크 하나만(치환). 일반 |: / 8va 배치용.",
			L"【单层】同tick同种标记仅一个（替换）。适合常规|: / 8va。",
			L"【استبدال】علامة واحدة لكل نوع وtick (استبدال). مناسب لـ |: / 8va العادي.",
			L"【Замена】Один знак каждого типа на tick (замена). Для обычных |: / 8va.",
			L"【Ersetzen】Ein Zeichen je Art und Tick (ersetzen). Für normales |: / 8va.",
			L"【Substituir】Uma marca por tipo e tick (substitui). Ideal para |: / 8va normais.",
			L"【Vervangen】Eén markering per soort en tick (vervangt). Voor normale |: / 8va.",
			L"【Zamień】Jeden znak danego typu na tick (zamiana). Do zwykłych |: / 8va.",
			L"【Değiştir】Tick+tür başına bir işaret (değiştirir). Normal |: / 8va için."));
		break;
	case SC_HELP_PAL_NOTE:
		_snwprintf_s(out, cch, _TRUNCATE, LL14(
			L"【音価】全音符〜64分。付点・連符・休符と組み合わせ。現在ツール=%s。",
			L"[Duration] Whole–64th. Combine with dotted/tuplet/rest. Current tool=%s.",
			L"【Durée】Ronde–64e. Point/nolet/silence. Outil actuel=%s.",
			L"【Durata】Semibreve–64esimo. Punto/gruppetto/pausa. Strumento=%s.",
			L"【Duración】Redonda–64avo. Puntillo/grupeto/silencio. Herramienta=%s.",
			L"【음가】온음표~64분. 점음/연음/쉼표와 조합. 현재 도구=%s.",
			L"【时值】全音符〜64分。可与附点/连音/休止组合。当前工具=%s。",
			L"【مدة】مستديرة–64. مع نقطة/تجميع/سكتة. الأداة الحالية=%s.",
			L"【Длительность】Целая–1/64. С точкой/группой/паузой. Инструмент=%s.",
			L"【Dauer】Ganze–64stel. Mit Punkt/N-tole/Pause. Aktuelles Werkzeug=%s.",
			L"【Duração】Semibreve–64. Com pontilhado/quiáltera/pausa. Ferramenta=%s.",
			L"【Duur】Hele–64ste. Met punt/n-ool/rust. Huidig gereedschap=%s.",
			L"【Wartość】Cała–1/64. Z kropką/n-olą/pauzą. Narzędzie=%s.",
			L"【Süre】Birlik–64’lük. Noktalı/n-ol/es ile. Geçerli araç=%s."), tool);
		break;
	default:
		_snwprintf_s(out, cch, _TRUNCATE, LL14(
			L"操作: %s | パート ch%d | Space=赤バーから再生 / Home=先頭 | パレットの|: :| 8va Ped.は赤バーに配置、消しゴム中なら削除。%s マークは選択→Deleteでも消えます。下のストリップで Exp/Vol/Pan/Pitch/Gate をパート別に描けます。",
			L"Tool: %s | Part ch%d | Space=play from red bar / Home=start | Palette |: :| 8va Ped. insert at red bar (Eraser deletes).%s Marks also via Select→Delete. Draw Exp/Vol/Pan/Pitch/Gate per part in the strip below.",
			L"Outil: %s | Partie ch%d | Espace=lecture / Home=début | Palette |: :| 8va Ped. sur barre rouge (Gomme = supprimer).%s Marques aussi Sélection→Suppr. Dessinez Exp/Vol/Pan/Pitch/Gate par partie sous la portée.",
			L"Strumento: %s | Parte ch%d | Spazio=play / Home=inizio | Tavolozza |: :| 8va Ped. sulla barra rossa (Gomma = elimina).%s Segni anche Seleziona→Canc. Disegna Exp/Vol/Pan/Pitch/Gate per parte sotto.",
			L"Herramienta: %s | Parte ch%d | Espacio=play / Inicio=principio | Paleta |: :| 8va Ped. en barra roja (Borrar = borrar).%s Marcas también Seleccionar→Supr. Dibuje Exp/Vol/Pan/Pitch/Gate por parte abajo.",
			L"도구: %s | 파트 ch%d | Space=빨간 바부터 재생 / Home=맨앞 | 팔레트 |: :| 8va Ped.는 빨간 바에(지우개면 삭제).%s 마크는 선택→Delete도 가능. 아래 스트립에서 파트별 Exp/Vol/Pan/Pitch/Gate.",
			L"工具: %s | 声部 ch%d | Space=从红条播放 / Home=开头 | 调色板|: :| 8va Ped.按红条插入（橡皮则删除）。%s 标记也可选择→Delete。下方条带按声部绘制 Exp/Vol/Pan/Pitch/Gate。",
			L"أداة: %s | جزء ch%d | Space=تشغيل من الشريط الأحمر / Home=بداية | لوحة |: :| 8va Ped. عند الشريط الأحمر (الممحاة تحذف).%s العلامات أيضاً تحديد→Delete. ارسم Exp/Vol/Pan/Pitch/Gate لكل جزء في الشريط أسفل.",
			L"Инструмент: %s | Партия ch%d | Space=с красной метки / Home=начало | Палитра |: :| 8va Ped. на метке (ластик удаляет).%s Знаки также Выбор→Delete. Рисуйте Exp/Vol/Pan/Pitch/Gate по партии в полосе ниже.",
			L"Werkzeug: %s | Part ch%d | Leertaste=ab Markierung / Pos1=Anfang | Palette |: :| 8va Ped. an roter Markierung (Radierer löscht).%s Zeichen auch Auswahl→Entf. Zeichnen Sie Exp/Vol/Pan/Pitch/Gate je Part im Strip unten.",
			L"Ferramenta: %s | Parte ch%d | Espaço=play / Home=início | Paleta |: :| 8va Ped. na barra vermelha (Borracha apaga).%s Marcas também Selecionar→Del. Desenhe Exp/Vol/Pan/Pitch/Gate por parte na faixa abaixo.",
			L"Gereedschap: %s | Partij ch%d | Spatie=afspelen / Home=begin | Palet |: :| 8va Ped. op rode balk (Gum wist).%s Markeringen ook Selecteren→Del. Teken Exp/Vol/Pan/Pitch/Gate per partij in de strook onder.",
			L"Narzędzie: %s | Partia ch%d | Spacja=odtwarzaj / Home=początek | Paleta |: :| 8va Ped. na czerwonym pasku (Gumka usuwa).%s Znaki też Zaznacz→Del. Rysuj Exp/Vol/Pan/Pitch/Gate per partię na pasie poniżej.",
			L"Araç: %s | Parti ch%d | Space=kırmızı çubuktan çal / Home=baş | Palet |: :| 8va Ped. kırmızı çubuğa (Silgi siler).%s İşaretler Seç→Del ile de. Alt şeritte parti başına Exp/Vol/Pan/Pitch/Gate çizin."),
			tool, curPart + 1, stripInfo);
		break;
	}
}

void ScStaffNormalizeStripStep(ScStaffUi* u)
{
	if (!u) return;
	u->stripStepTicks = ScStaffStripStepTicks(u);
}

int ScStaffStripStepTicks(const ScStaffUi* u)
{
	static const int kSteps[] = {
		SC_PPQN, SC_PPQN / 2, SC_PPQN / 4, SC_PPQN / 8, SC_PPQN / 16
	};
	int s = (u && u->stripStepTicks > 0) ? u->stripStepTicks : (SC_PPQN / 2);
	int best = kSteps[1], bestD = abs(s - best);
	for (int i = 0; i < 5; i++) {
		int d = abs(s - kSteps[i]);
		if (d < bestD) { bestD = d; best = kSteps[i]; }
	}
	return best;
}

int ScStaffStripColCount(const ScStaffUi* u)
{
	const int step = ScStaffStripStepTicks(u);
	uint32_t ticks = (u && u->contentTicks > 0) ? u->contentTicks :
		(uint32_t)(SC_PPQN * SC_MEASURE_BEATS * SC_MEASURES_DEFAULT);
	int n = (int)((ticks + (uint32_t)step - 1) / (uint32_t)step) + 4;
	if (n < 16) n = 16;
	if (n > SC_STRIP_COLS_MAX) n = SC_STRIP_COLS_MAX;
	return n;
}

int ScStaffStripTickToCol(const ScStaffUi* u, uint32_t tick)
{
	const int step = ScStaffStripStepTicks(u);
	int col = (int)(tick / (uint32_t)step);
	if (col < 0) col = 0;
	if (col >= SC_STRIP_COLS_MAX) col = SC_STRIP_COLS_MAX - 1;
	return col;
}

uint32_t ScStaffStripColToTick(const ScStaffUi* u, int col)
{
	const int step = ScStaffStripStepTicks(u);
	if (col < 0) col = 0;
	return (uint32_t)col * (uint32_t)step;
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

enum { SC_MARGIN_CLEF_W = 28, SC_MARGIN_ACC_W = 13, SC_MARGIN_TS_PAD = 10, SC_MARGIN_TS_W = 22, SC_MARGIN_TAIL = 8 };

static void ScStaffDrawAccidentalGlyph(CDC& dc, int cx, int cy, int accidental, COLORREF col, int fontPx)
{
	if (accidental == 0) return;
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(col);
	CFont f;
	f.CreateFont(max(12, fontPx), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
	CFont* of = dc.SelectObject(&f);
	const wchar_t* g = L"";
	if (accidental >= 2) g = L"♯♯";
	else if (accidental == 1) g = L"♯";
	else if (accidental <= -2) g = L"♭♭";
	else g = L"♭";
	CSize sz = dc.GetTextExtent(g, (int)wcslen(g));
	int drawY = cy - sz.cy / 2;
	if (accidental < 0)
		drawY -= max(2, fontPx / 5);
	dc.TextOut(cx - sz.cx / 2, drawY, g);
	dc.SelectObject(of);
}

void ScStaffDrawSymbolEx(CDC& dc, int x, int y, int durTicks, int rest, COLORREF col,
	int selected, int stemUp, int accidental)
{
	if (!rest && accidental)
		ScStaffDrawAccidentalGlyph(dc, x - 14, y, accidental, col, 16);
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

void ScStaffDrawPalCell(CDC& dc, const CRect& rc, int selected)
{
	if (rc.Width() < 2 || rc.Height() < 2) return;
	const COLORREF top = selected ? RGB(80, 150, 230) : RGB(50, 85, 155);
	const COLORREF bot = selected ? RGB(35, 90, 165) : RGB(25, 40, 85);
	const int h = max(1, rc.Height());
	for (int y = 0; y < h; y++) {
		const int t = y * 256 / h;
		const COLORREF c = RGB(
			GetRValue(top) + (GetRValue(bot) - GetRValue(top)) * t / 256,
			GetGValue(top) + (GetGValue(bot) - GetGValue(top)) * t / 256,
			GetBValue(top) + (GetBValue(bot) - GetBValue(top)) * t / 256);
		dc.FillSolidRect(rc.left, rc.top + y, rc.Width(), 1, c);
	}
	if (h > 4)
		dc.FillSolidRect(rc.left + 1, rc.top + 1, rc.Width() - 2, 2, RGB(170, 200, 245));
	dc.Draw3dRect(rc, RGB(110, 145, 200), RGB(12, 18, 38));
}

void ScStaffDrawPalTab(CDC& dc, const CRect& rc, const wchar_t* label, int selected)
{
	if (rc.Width() < 4 || rc.Height() < 4) return;
	const COLORREF top = selected ? RGB(100, 160, 235) : RGB(55, 70, 105);
	const COLORREF bot = selected ? RGB(60, 110, 190) : RGB(35, 42, 68);
	const int h = max(1, rc.Height());
	for (int y = 0; y < h; y++) {
		const int t = y * 256 / h;
		dc.FillSolidRect(rc.left, rc.top + y, rc.Width(), 1, RGB(
			GetRValue(top) + (GetRValue(bot) - GetRValue(top)) * t / 256,
			GetGValue(top) + (GetGValue(bot) - GetGValue(top)) * t / 256,
			GetBValue(top) + (GetBValue(bot) - GetBValue(top)) * t / 256));
	}
	dc.SetBkMode(TRANSPARENT);
	CFont f;
	f.CreateFont(max(11, rc.Height() - 4), 0, 0, 0, selected ? FW_BOLD : FW_NORMAL, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Meiryo UI");
	CFont* of = dc.SelectObject(&f);
	dc.SetTextColor(selected ? RGB(255, 255, 255) : RGB(200, 210, 230));
	CRect tr = rc;
	dc.DrawText(label ? label : L"", tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	dc.SelectObject(of);
	if (selected)
		dc.FillSolidRect(rc.left, rc.bottom - 2, rc.Width(), 2, RGB(140, 200, 255));
	else
		dc.Draw3dRect(rc, RGB(80, 95, 130), RGB(20, 25, 40));
}

void ScStaffDrawPalRadioCell(CDC& dc, const CRect& rc, const wchar_t* label, int selected)
{
	ScStaffDrawPalCell(dc, rc, selected);
	if (selected) {
		const int cx = rc.left + 8, cy = rc.top + 8;
		dc.FillSolidRect(cx - 4, cy - 4, 8, 8, RGB(255, 255, 255));
		dc.FillSolidRect(cx - 2, cy - 2, 4, 4, RGB(80, 150, 230));
	}
	dc.SetBkMode(TRANSPARENT);
	CFont f;
	f.CreateFont(max(12, min(rc.Width(), rc.Height()) - 8), 0, 0, 0, FW_BOLD, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Meiryo UI");
	CFont* of = dc.SelectObject(&f);
	dc.SetTextColor(RGB(245, 248, 255));
	CRect tr = rc;
	tr.left += selected ? 10 : 0;
	dc.DrawText(label ? label : L"", tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	dc.SelectObject(of);
}

void ScStaffDrawPalRadioRow(CDC& dc, const CRect& rc, const wchar_t* label, int selected, int hot)
{
	if (rc.Width() < 8 || rc.Height() < 8) return;
	const COLORREF bg = selected ? RGB(55, 95, 160) : (hot ? RGB(45, 55, 80) : RGB(32, 38, 54));
	dc.FillSolidRect(rc, bg);
	if (selected)
		dc.Draw3dRect(rc, RGB(120, 170, 230), RGB(20, 30, 50));
	const int cy = (rc.top + rc.bottom) / 2;
	const int rx = rc.left + 12;
	dc.FillSolidRect(rx - 5, cy - 5, 10, 10, RGB(240, 244, 252));
	dc.Draw3dRect(CRect(rx - 5, cy - 5, rx + 5, cy + 5), RGB(90, 100, 120), RGB(30, 35, 50));
	if (selected)
		dc.FillSolidRect(rx - 2, cy - 2, 4, 4, RGB(70, 140, 220));
	dc.SetBkMode(TRANSPARENT);
	CFont f;
	f.CreateFont(max(12, rc.Height() - 6), 0, 0, 0, selected ? FW_BOLD : FW_NORMAL, 0, 0, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Meiryo UI");
	CFont* of = dc.SelectObject(&f);
	dc.SetTextColor(selected ? RGB(255, 255, 255) : RGB(220, 228, 240));
	CRect tr = rc;
	tr.left = rx + 12;
	dc.DrawText(label ? label : L"", tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
	dc.SelectObject(of);
}

const wchar_t* ScStaffKeySigMajorName(int keySig)
{
	static const wchar_t* names[] = {
		L"Cb major", L"Gb major", L"Db major", L"Ab major", L"Eb major", L"Bb major", L"F major", L"C major",
		L"G major", L"D major", L"A major", L"E major", L"B major", L"F# major", L"C# major"
	};
	const int i = keySig + 7;
	if (i < 0 || i > 14) return L"? major";
	return names[i];
}

int ScStaffKeySigPixelWidth(int keySig)
{
	if (keySig == 0) return 0;
	return abs(keySig) * SC_MARGIN_ACC_W;
}

int ScStaffMarginPixelWidth(int clefMode, int keySig, int drawMeter)
{
	if (clefMode == 3)
		return max(SC_CLEF_MARGIN, SC_MARGIN_CLEF_W + SC_MARGIN_TAIL);
	int w = SC_MARGIN_CLEF_W + ScStaffKeySigPixelWidth(keySig);
	if (drawMeter)
		w += SC_MARGIN_TS_PAD + SC_MARGIN_TS_W;
	w += SC_MARGIN_TAIL;
	return max(SC_CLEF_MARGIN, w);
}

int ScStaffClefMarginPx(const ScStaffUi* u, const ScEvent* ev, int evCount)
{
	if (!u) return SC_CLEF_MARGIN;
	int cm = 0;
	if (ev && evCount > 0)
		cm = ScStaffClefModeAt(u, 0, 0, ev, evCount);
	else if (u->trackCount > 0)
		cm = u->clef[0] % 4;
	const int ks = (ev && evCount > 0) ? ScStaffKeySigAtTick(ev, evCount, 0, u->keySig) : u->keySig;
	return ScStaffMarginPixelWidth(cm, ks, 1);
}

int ScStaffGridLeftPx(int gridLeftEdge, const ScStaffUi* u, const ScEvent* ev, int evCount)
{
	return gridLeftEdge + ScStaffClefMarginPx(u, ev, evCount);
}

static int ScStaffMarginTsX(int marginLeft, int keySig)
{
	return marginLeft + SC_MARGIN_CLEF_W + ScStaffKeySigPixelWidth(keySig) + SC_MARGIN_TS_PAD;
}

static int ScStaffKeySigAccidentalHalf(int clefPart, int index, int sharp)
{
	static const int kTrebleSharp[] = { 2, 3, 4, 5, 6, 7, 8 };
	static const int kTrebleFlat[] = { 4, 8, 6, 5, 8, 7, 5 };
	static const int kBassSharp[] = { 6, 7, 4, 5, 2, 3, 0 };
	static const int kBassFlat[] = { 4, 5, 2, 6, 3, 4, 1 };
	if (index < 0 || index > 6) return 4;
	if (sharp) return clefPart ? kBassSharp[index] : kTrebleSharp[index];
	return clefPart ? kBassFlat[index] : kTrebleFlat[index];
}

void ScStaffPaintKeySignature(CDC& dc, int x0, int staffTop, int gap, int staffH, int clefMode, int keySig)
{
	if (keySig == 0 || clefMode == 3) return;
	const int n = abs(keySig);
	const int sharp = keySig > 0;
	const int accFont = max(14, min(22, gap * 2 + 6));
	auto drawRow = [&](int stTop, int clefPart) {
		for (int ki = 0; ki < n && ki < 7; ki++) {
			const int cx = x0 + ki * SC_MARGIN_ACC_W + SC_MARGIN_ACC_W / 2;
			const int ky = stTop + 8 + (ScStaffKeySigAccidentalHalf(clefPart, ki, sharp) * gap) / 2;
			ScStaffDrawAccidentalGlyph(dc, cx, ky, sharp ? 1 : -1, RGB(40, 40, 55), accFont);
		}
	};
	if (clefMode == 2) {
		drawRow(staffTop, 0);
		drawRow(staffTop + staffH + SC_GRAND_STAFF_GAP, 1);
	} else {
		drawRow(staffTop, clefMode == 1 ? 1 : 0);
	}
}

void ScStaffDrawMarginKeyAndMeter(CDC& dc, int marginLeft, int staffTop, int gap, int staffH,
	int clefMode, int keySig, int meterN, int meterD, COLORREF col)
{
	const int keyX = marginLeft + SC_MARGIN_CLEF_W;
	ScStaffPaintKeySignature(dc, keyX, staffTop, gap, staffH, clefMode, keySig);
	const int tsX = ScStaffMarginTsX(marginLeft, keySig);
	if (clefMode == 2) {
		const int bassTop = staffTop + staffH + SC_GRAND_STAFF_GAP;
		ScStaffDrawTimeSignature(dc, tsX, staffTop, gap, meterN, meterD, col);
		ScStaffDrawTimeSignature(dc, tsX, bassTop, gap, meterN, meterD, col);
	} else {
		ScStaffDrawTimeSignature(dc, tsX, staffTop, gap, meterN, meterD, col);
	}
}

void ScStaffTimeSigOrigin(int clefMode, int staffTop, int staffH, int gap, int keySig, int gridLeft, int* outX, int* outY)
{
	if (!outX || !outY) return;
	*outX = ScStaffMarginTsX(gridLeft, keySig);
	*outY = staffTop + 8 + gap;
}

const wchar_t* ScStaffKeySigMinorName(int keySig)
{
	static const wchar_t* names[] = {
		L"Ab min", L"Eb min", L"Bb min", L"F min", L"C min", L"G min", L"D min", L"A min",
		L"E min", L"B min", L"F# min", L"C# min", L"G# min", L"D# min", L"A# min"
	};
	const int i = keySig + 7;
	if (i < 0 || i > 14) return L"? min";
	return names[i];
}

const wchar_t* ScStaffKeySigName(int keySig)
{
	static const wchar_t* names[] = {
		L"Cb", L"Gb", L"Db", L"Ab", L"Eb", L"Bb", L"F", L"C",
		L"G", L"D", L"A", L"E", L"B", L"F#", L"C#"
	};
	const int i = keySig + 7;
	if (i < 0 || i > 14) return L"?";
	return names[i];
}

void ScStaffDrawNoteGlyphPal(CDC& dc, const CRect& rc, int durTicks, int rest, int selected, int accidental)
{
	ScStaffDrawPalCell(dc, rc, selected);
	const int cx = (rc.left + rc.right) / 2;
	const int cy = (rc.top + rc.bottom) / 2 - 2;
	const COLORREF sym = RGB(245, 248, 255);
	dc.SetBkMode(TRANSPARENT);
	if (rest) {
		ScStaffDrawRestGlyph(dc, cx - 6, cy, durTicks, sym);
	} else {
		const int whole = (ScStaffBaseDurUndot(durTicks) >= SC_PPQN * 4);
		const int hx = cx - 6, hy = cy + (whole ? 0 : 4);
		ScStaffDrawSymbolEx(dc, hx, hy, durTicks, 0, sym, selected, 1, accidental);
	}
}

int ScStaffMeterFromPalCmd(int cmdId, int* numer, int* denom)
{
	if (!numer || !denom) return 0;
	struct { int cmd, n, d; } tbl[] = {
		{ 30, 2, 4 }, { 31, 3, 4 }, { 32, 4, 4 }, { 33, 5, 4 },
		{ 34, 6, 8 }, { 35, 7, 8 }, { 36, 9, 8 },
		{ 52, 1, 4 }, { 53, 2, 2 }, { 54, 3, 8 }, { 55, 4, 8 },
		{ 56, 5, 8 }, { 57, 8, 8 }, { 58, 10, 8 }, { 59, 11, 8 },
		{ 60, 12, 8 }, { 61, 13, 8 }, { 62, 15, 8 },
		{ 63, 6, 4 }, { 64, 3, 2 }, { 65, 7, 4 },
	};
	for (int i = 0; i < (int)(sizeof(tbl) / sizeof(tbl[0])); i++) {
		if (cmdId == tbl[i].cmd) {
			*numer = tbl[i].n;
			*denom = tbl[i].d;
			return 1;
		}
	}
	return 0;
}

HCURSOR ScStaffCreateBlankCursor(void)
{
	BYTE andBits[32 * 4];
	BYTE xorBits[32 * 4];
	memset(andBits, 0xFF, sizeof(andBits));
	memset(xorBits, 0x00, sizeof(xorBits));
	return ::CreateCursor(::GetModuleHandle(NULL), 0, 0, 32, 32, andBits, xorBits);
}

static void ScStaffCursorPixelWhite(BYTE* andM, BYTE* xorM, int x, int y)
{
	if (x < 0 || x >= 32 || y < 0 || y >= 32) return;
	const int i = y * 4 + (x >> 3);
	const int bit = 7 - (x & 7);
	andM[i] &= ~(1 << bit);
	xorM[i] |= (1 << bit);
}

static void ScStaffCursorPixelBlack(BYTE* andM, BYTE* xorM, int x, int y)
{
	if (x < 0 || x >= 32 || y < 0 || y >= 32) return;
	const int i = y * 4 + (x >> 3);
	const int bit = 7 - (x & 7);
	andM[i] &= ~(1 << bit);
	xorM[i] &= ~(1 << bit);
}

static HCURSOR ScStaffCreateShapeCursor(int kind, int hx, int hy)
{
	BYTE andM[32 * 4], xorM[32 * 4];
	memset(andM, 0xFF, sizeof(andM));
	memset(xorM, 0, sizeof(xorM));
	auto dotOutline = [&](int x, int y) {
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++)
				if (dx || dy)
					ScStaffCursorPixelBlack(andM, xorM, x + dx, y + dy);
		ScStaffCursorPixelWhite(andM, xorM, x, y);
	};
	auto line = [&](int x0, int y0, int x1, int y1) {
		const int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
		const int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
		int err = dx + dy, x = x0, y = y0;
		for (;;) {
			dotOutline(x, y);
			if (x == x1 && y == y1) break;
			const int e2 = err << 1;
			if (e2 >= dy) { err += dy; x += sx; }
			if (e2 <= dx) { err += dx; y += sy; }
		}
	};
	if (kind == 0) {
		line(4, 28, 10, 22);
		line(10, 22, 18, 14);
		line(18, 14, 24, 8);
		dotOutline(5, 27);
		dotOutline(6, 26);
	} else if (kind == 1) {
		line(6, 26, 26, 6);
		dotOutline(7, 25);
		dotOutline(25, 7);
	} else {
		for (int t = 0; t <= 16; t++) {
			const double u = (double)t / 16.0;
			const int x = 6 + (int)(u * 20.0 + 0.5);
			const int y = 24 - (int)((1.0 - u) * (1.0 - u) * 18.0 + 0.5);
			dotOutline(x, y);
			dotOutline(x, y + 1);
		}
	}
	return ::CreateCursor(::GetModuleHandle(NULL), hx, hy, 32, 32, andM, xorM);
}

void ScStaffDrawCursorsInit(ScStaffDrawCursors* c)
{
	if (!c) return;
	c->pencil = ScStaffCreateShapeCursor(0, 4, 28);
	c->line = ScStaffCreateShapeCursor(1, 6, 26);
	c->curve = ScStaffCreateShapeCursor(2, 8, 22);
}

void ScStaffDrawCursorsFree(ScStaffDrawCursors* c)
{
	if (!c) return;
	if (c->pencil) { ::DestroyCursor(c->pencil); c->pencil = NULL; }
	if (c->line) { ::DestroyCursor(c->line); c->line = NULL; }
	if (c->curve) { ::DestroyCursor(c->curve); c->curve = NULL; }
}

HCURSOR ScStaffDrawCursorPick(const ScStaffDrawCursors* c, int stripDraw)
{
	if (!c) return NULL;
	if (stripDraw == SC_STRIP_DRAW_LINE && c->line) return c->line;
	if (stripDraw == SC_STRIP_DRAW_CURVE && c->curve) return c->curve;
	return c->pencil ? c->pencil : NULL;
}

int ScStaffPtInBandEditZone(const CRect& grid, const CRect& stripRc, const ScStaffUi* u, CPoint pt,
	const ScEvent* ev, int evCount)
{
	if (!u) return 0;
	int dummy = 0;
	if (ScStaffHitGlobalTempoBand(grid, u, pt, &dummy, &dummy, ev, evCount))
		return 1;
	if (ScStaffHitPartBand(grid, u, pt, &dummy, &dummy, &dummy, &dummy, ev, evCount))
		return 2;
	if (ScStaffHitStrip(stripRc, u, pt, &dummy, &dummy, &dummy))
		return 3;
	return 0;
}

static int ScStaffToneLaneConfigured(const ScStaffUi* u, int tr, int isFm)
{
	if (!u || tr < 0 || tr >= 32) return 0;
	const wchar_t* s = u->vstLabel[tr];
	if (!s[0]) return 0;
	if (wcsncmp(s, L"(no ", 4) == 0) return 0;
	(void)isFm;
	return 1;
}

static int ScStaffExcLaneUsed(const ScEvent* ev, int evCount, int tr, int isFm)
{
	if (!ev || tr < 0) return 0;
	for (int i = 0; i < evCount; i++) {
		if ((int)ev[i].ch != tr) continue;
		const uint8_t k = ev[i].kind;
		if (!isFm) {
			if (k == SC_EV_RPN || k == SC_EV_NRPN || k == SC_EV_SYSEX
				|| k == SC_EV_VOL || k == SC_EV_PAN || k == SC_EV_VELO)
				return 1;
		} else {
			if (k == SC_EV_FM_EX || k == SC_EV_FM_LFO || k == SC_EV_FM_DETUNE || k == SC_EV_FM_FLR)
				return 1;
		}
	}
	return 0;
}

static void ScStaffLaneFillColors(int toneCfg, int excUsed, COLORREF* toneBg, COLORREF* toneEdge,
	COLORREF* excBg, COLORREF* excEdge)
{
	if (toneCfg) {
		*toneBg = RGB(218, 248, 228);
		*toneEdge = RGB(100, 170, 120);
	} else {
		*toneBg = RGB(255, 246, 232);
		*toneEdge = RGB(220, 190, 150);
	}
	if (excUsed) {
		*excBg = RGB(220, 236, 252);
		*excEdge = RGB(90, 130, 180);
	} else {
		*excBg = RGB(236, 244, 255);
		*excEdge = RGB(140, 170, 210);
	}
}

void ScStaffPaintTracks(CDC& dc, const CRect& rc, const ScStaffUi* u, int curTrack,
	const ScEvent* ev, int evCount)
{
	const int hdrH = ScStaffGridHeaderH(u);
	dc.FillSolidRect(rc, RGB(236, 238, 244));
	if (hdrH > 0)
		dc.FillSolidRect(rc.left, rc.top, rc.Width(), hdrH, RGB(236, 238, 246));
	dc.FillSolidRect(rc.right - 1, rc.top, 1, rc.Height(), RGB(190, 190, 200));
	CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
	dc.SetBkMode(TRANSPARENT);
	const int bodyTop = rc.top + hdrH;
	int y = bodyTop + 4 - u->scrollY;
	for (int i = 0; i < u->trackCount; i++) {
		const int rowH = ScStaffRowH(u, i);
		if (y + rowH <= bodyTop) { y += rowH; continue; }
		if (y > rc.bottom) break;
		if (i == curTrack)
			dc.FillSolidRect(rc.left, max(y, bodyTop), 5, y + rowH - max(y, bodyTop) - 2, RGB(40, 110, 220));
		else if (!u->visible[i])
			dc.FillSolidRect(rc.left + 1, max(y, bodyTop), rc.Width() - 2, y + rowH - max(y, bodyTop) - 1, RGB(228, 230, 236));
		else
			dc.FillSolidRect(rc.left + 1, max(y, bodyTop), rc.Width() - 2, y + rowH - max(y, bodyTop) - 1, RGB(242, 243, 248));

		if (y + 16 <= bodyTop) { y += rowH; continue; }

		CRect enRc(rc.left + 2, y + 2, rc.left + SC_PART_ENABLE_W, y + 16);
		dc.SetTextColor(u->visible[i] ? RGB(24, 130, 58) : RGB(150, 152, 165));
		dc.DrawText(u->visible[i] ? L"●" : L"○", enRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

		dc.SetTextColor(u->visible[i] ? RGB(20, 20, 40) : RGB(130, 130, 145));
		CRect nameRc(rc.left + SC_PART_ENABLE_W + 4, y + 1, rc.left + 88, y + 17);
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
			const int toneCfg = ScStaffToneLaneConfigured(u, i, u->isFmScore);
			const int excUsed = ScStaffExcLaneUsed(ev, evCount, i, u->isFmScore);
			COLORREF toneBg, toneEdge, excBg, excEdge;
			ScStaffLaneFillColors(toneCfg, excUsed, &toneBg, &toneEdge, &excBg, &excEdge);
			CRect toneBox(rc.left + 6, toneTop, rc.right - 4, toneTop + SC_CTRL_LANE_H - 2);
			dc.FillSolidRect(toneBox, toneBg);
			dc.Draw3dRect(toneBox, toneEdge, toneEdge);
			dc.SetTextColor(toneCfg ? RGB(24, 90, 50) : RGB(120, 90, 50));
			wchar_t tone[96];
			if (u->vstLabel[i][0])
				_snwprintf_s(tone, _TRUNCATE, L"Tone  %s", u->vstLabel[i]);
			else
				_snwprintf_s(tone, _TRUNCATE, L"Tone  —");
			dc.DrawText(tone, toneBox, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

			const int excTop = toneTop + SC_CTRL_LANE_H;
			CRect excBox(rc.left + 6, excTop, rc.right - 4, excTop + SC_CTRL_LANE_H - 2);
			dc.FillSolidRect(excBox, excBg);
			dc.Draw3dRect(excBox, excEdge, excEdge);
			dc.SetTextColor(excUsed ? RGB(40, 70, 110) : RGB(90, 110, 140));
			dc.DrawText(L"Exc / RPN / SysEx", excBox, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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

void ScStaffDrawLayoutPreview(CDC& dc, const CRect& rc, int clefMode, int keySig, int meterN, int meterD)
{
	if (rc.IsRectEmpty()) return;
	dc.FillSolidRect(rc, RGB(255, 252, 246));
	dc.Draw3dRect(rc, RGB(100, 115, 145), RGB(50, 58, 75));
	const int gap = max(7, min(10, (rc.Height() - 10) / 5));
	const int staffH = gap * 4 + 16;
	const int staffTop = rc.top + max(4, (rc.Height() - staffH) / 2);
	const int x0 = rc.left + 8;
	const int lineW = max(40, rc.Width() - 16);
	DrawFiveLines(dc, x0, lineW, staffTop, gap);
	const COLORREF col = RGB(35, 38, 52);
	int cm = clefMode;
	if (cm < 0 || cm > 3) cm = 0;
	if (cm == 2) {
		const int halfH = max(28, (rc.Height() - SC_GRAND_STAFF_GAP - 8) / 2);
		const int trebleTop = rc.top + 4;
		const int bassTop = trebleTop + halfH + SC_GRAND_STAFF_GAP;
		DrawFiveLines(dc, x0, lineW, trebleTop, gap);
		DrawClefG(dc, x0 + 2, trebleTop, halfH);
		DrawFiveLines(dc, x0, lineW, bassTop, gap);
		DrawClefF(dc, x0 + 2, bassTop, halfH);
		ScStaffDrawMarginKeyAndMeter(dc, x0, trebleTop, gap, halfH, 2, keySig, meterN, meterD, col);
	} else {
		if (cm == 1) DrawClefF(dc, x0 + 2, staffTop, staffH);
		else if (cm == 3) DrawClefDr(dc, x0 + 2, staffTop, staffH);
		else DrawClefG(dc, x0 + 2, staffTop, staffH);
		ScStaffDrawMarginKeyAndMeter(dc, x0, staffTop, gap, staffH, cm, keySig, meterN, meterD, col);
		int noteX = ScStaffMarginTsX(x0, keySig) + 24;
		static const int demo[] = { 60, 62, 64, 65 };
		for (int i = 0; i < 4 && noteX + 16 < rc.right; i++) {
			const int ny = ScStaffMidiNoteYClef(demo[i], staffTop, gap, cm == 1 ? 1 : 0);
			ScStaffDrawSymbolEx(dc, noteX + i * 16, ny, SC_PPQN, 0, col, 0, 1, 0);
		}
	}
}

static void DrawGrandBrace(CDC& dc, int x, int y0, int y1)
{
	dc.FillSolidRect(x + 2, y0 + 4, 3, max(1, y1 - y0 - 8), RGB(50, 50, 70));
	dc.FillSolidRect(x, y0 + 2, 5, 3, RGB(50, 50, 70));
	dc.FillSolidRect(x, y1 - 5, 5, 3, RGB(50, 50, 70));
}

static void ScStaffPaintMarginClef(CDC& dc, int cm, int gridLeft, int staffTop, int staffH, int gap,
	const ScStaffUi* u, int tr, int lineX0, int gridRight)
{
	if (cm == 2) {
		const int bassTop = staffTop + staffH + SC_GRAND_STAFF_GAP;
		DrawGrandBrace(dc, gridLeft + 2, staffTop + 4, bassTop + staffH - 8);
		DrawClefG(dc, gridLeft + 10, staffTop, staffH);
		DrawClefF(dc, gridLeft + 10, bassTop, staffH);
	} else if (cm == 3) {
		DrawClefDr(dc, gridLeft + 10, staffTop, staffH);
		if (ScStaffUiOpnaRhythm(u, tr)) {
			dc.SetBkMode(TRANSPARENT);
			CFont labF;
			labF.CreateFont(12, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
				OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			CFont* of = dc.SelectObject(&labF);
			dc.SetTextColor(RGB(100, 70, 30));
			for (int rp = 0; rp < 6; rp++) {
				const int ly = ScStaffRhythmPadYUi(u, staffTop, rp);
				dc.TextOut(lineX0 - 44, ly - 7, kOpnaRhythmNames[rp]);
			}
			dc.SelectObject(of);
		}
	} else if (cm == 1) {
		DrawClefF(dc, gridLeft + 10, staffTop, staffH);
	} else {
		DrawClefG(dc, gridLeft + 10, staffTop, staffH);
	}
	(void)gap;
	(void)gridRight;
}

static void ScStaffPaintStaffLinesRange(CDC& dc, int cm, int x0, int x1, int staffTop, int staffH, int gap,
	const ScStaffUi* u, int tr)
{
	if (x1 <= x0) return;
	const int w = x1 - x0;
	if (ScStaffUiOpnaRhythm(u, tr)) {
		/* Dedicated rhythm: 6 pad guides (five-line staff is too short for labels). */
		const int lane = ScStaffRhythmLaneH(u);
		for (int rp = 0; rp < 6; rp++) {
			const int ly = ScStaffRhythmPadYUi(u, staffTop, rp);
			dc.FillSolidRect(x0, ly, w, 1, RGB(200, 185, 165));
			if (rp % 2 == 0)
				dc.FillSolidRect(x0, ly - lane / 2 + 1, w, lane - 2, RGB(252, 246, 236));
		}
		(void)staffH;
		(void)gap;
		(void)cm;
		return;
	}
	if (cm == 2) {
		const int bassTop = staffTop + staffH + SC_GRAND_STAFF_GAP;
		DrawFiveLines(dc, x0, w, staffTop, gap);
		DrawFiveLines(dc, x0, w, bassTop, gap);
	} else {
		DrawFiveLines(dc, x0, w, staffTop, gap);
	}
}

static void ScStaffPaintClefSegments(CDC& dc, int lineX0, int gridRight, int staffTop, int staffH, int gap,
	const ScStaffUi* u, int tr, int scrollX, int gridLeft, int pxBeat,
	const ScEvent* ev, int evCount, uint32_t tVis0)
{
	int segCm = ScStaffClefModeAt(u, tr, tVis0, ev, evCount);
	int segX0 = lineX0;
	for (int ci = 0; ci < evCount; ci++) {
		if (ev[ci].kind != SC_EV_CLEF || (int)ev[ci].ch != tr || ev[ci].tick == 0) continue;
		const uint32_t bt = ev[ci].tick;
		const int xC = ScStaffTickToX(bt, scrollX, gridLeft, pxBeat, u, ev, evCount);
		const int hasZ = bt > 0 && ScStaffBarHasLayoutZone(ev, evCount, bt);
		const int xSplit = hasZ ? xC - SC_LAYOUT_ZONE_W : xC;
		if (xSplit <= segX0) {
			segCm = (int)ev[ci].a % 4;
			continue;
		}
		if (xSplit > gridRight) break;
		ScStaffPaintStaffLinesRange(dc, segCm, segX0, xSplit, staffTop, staffH, gap, u, tr);
		segCm = (int)ev[ci].a % 4;
		segX0 = xC;
	}
	if (segX0 < gridRight)
		ScStaffPaintStaffLinesRange(dc, segCm, segX0, gridRight, staffTop, staffH, gap, u, tr);
}

static void ScStaffPaintLayoutClefGlyph(CDC& dc, int cm, int lx, int cy, int staffH, int gap)
{
	if (cm == 2) {
		DrawClefG(dc, lx, cy - gap - 4, staffH);
		DrawClefF(dc, lx, cy + gap + 2, staffH);
	} else if (cm == 3) {
		DrawClefDr(dc, lx, cy - gap, staffH);
	} else if (cm == 1) {
		DrawClefF(dc, lx, cy - gap - 4, staffH);
	} else {
		DrawClefG(dc, lx, cy - gap - 4, staffH);
	}
}

static void DrawGhostNote(CDC& dc, int x, int y, int dur, int rest, int accidental)
{
	ScStaffDrawSymbolEx(dc, x, y, dur, rest, RGB(200, 50, 40), 0, 1, accidental);
}

/* Chord ghost: multi-head, grand-staff split via ScStaffMidiNoteYTrack, 2nds side-by-side. */
static void DrawGhostChord(CDC& dc, const ScStaffUi* u, int track, int staffTop, int x,
	int rootSounding, int ottavaOct, int dur, int accidental)
{
	if (!u) return;
	int pitches[8];
	int nv = ScChordBuildPitches(rootSounding, u->chordType, u->chordVoices, pitches, 8);
	if (nv < 1) {
		const int written = ScStaffSoundingToWritten(rootSounding, ottavaOct);
		const int y = ScStaffMidiNoteYTrack(u, track, staffTop, written);
		DrawGhostNote(dc, x, y, dur, 0, accidental);
		return;
	}
	const int gap = ScStaffLineGap(u);
	int ys[8], xOff[8], flags[8];
	static const int kBlack[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
	for (int i = 0; i < nv; i++) {
		const int written = ScStaffSoundingToWritten(pitches[i], ottavaOct);
		ys[i] = ScStaffMidiNoteYTrack(u, track, staffTop, written);
		xOff[i] = 0;
		flags[i] = 0;
		if (i == 0 && accidental)
			flags[i] = (accidental > 0) ? SC_EF_ACC_SHARP : (accidental < 0) ? SC_EF_ACC_FLAT : 0;
		else if (kBlack[pitches[i] % 12])
			flags[i] = SC_EF_ACC_SHARP;
	}
	for (int a = 0; a < nv; a++)
		for (int b = a + 1; b < nv; b++)
			if (ys[b] < ys[a]) {
				int t = ys[a]; ys[a] = ys[b]; ys[b] = t;
				t = pitches[a]; pitches[a] = pitches[b]; pitches[b] = t;
				t = flags[a]; flags[a] = flags[b]; flags[b] = t;
			}
	const int thresh = max(1, gap);
	for (int i = 0; i + 1 < nv; i++) {
		if (abs(ys[i + 1] - ys[i]) <= thresh)
			xOff[i + 1] = xOff[i] + 11;
	}
	for (int i = 0; i < nv; i++) {
		for (int j = i + 1; j < nv; j++) {
			if (abs(ys[j] - ys[i]) > thresh) continue;
			if (xOff[j] == xOff[i]) xOff[j] = xOff[i] + 11;
		}
	}
	int topY = ys[0], botY = ys[nv - 1];
	const int stemUp = ((topY + botY) / 2 >= staffTop + 8 + gap * 2) ? 1 : 0;
	const int stemX = x + (stemUp ? 11 : 0);
	dc.FillSolidRect(stemX, min(topY, botY) - (stemUp ? 20 : 0), 2,
		max(1, abs(botY - topY) + 20), RGB(80, 130, 220));
	for (int i = 0; i < nv; i++) {
		const int hx = x + xOff[i];
		const int hollow = ScStaffBaseDurUndot(dur) >= SC_PPQN * 2;
		ScStaffDrawOvalHead(dc, hx, ys[i], hollow, RGB(80, 130, 220), ScStaffDurIsDotted(dur));
		if (flags[i] & SC_EF_ACC_MASK) {
			dc.SetTextColor(RGB(80, 130, 220));
			dc.SetBkMode(TRANSPARENT);
			const wchar_t* g =
				(flags[i] == SC_EF_ACC_SHARP) ? L"♯" :
				(flags[i] == SC_EF_ACC_FLAT) ? L"♭" : L"♮";
			dc.TextOut(hx - 12, ys[i] - 8, g);
		}
	}
}

/* Pattern ghost: translucent heads at pattern onsets for one placement span. */
static void DrawGhostPattern(CDC& dc, const ScStaffUi* u, int track, int staffTop, int gridLeft,
	int pxBeat, uint32_t tick0, int noteSounding, int ottavaOct, const ScEvent* ev, int evCount)
{
	if (!u) return;
	const int written = ScStaffSoundingToWritten(noteSounding, ottavaOct);
	const int y = ScStaffMidiNoteYTrack(u, track, staffTop, written);
	const int span = ScPatternSpanTicks(u->patternId, 1, u->meterNumer, u->meterDenom, SC_PPQN);
	const int beatTicks = (SC_PPQN * 4) / max(1, u->meterDenom);
	const int meterNumer = max(1, u->meterNumer);
	auto ghostAt = [&](uint32_t t, int dur) {
		int x = ScStaffTickToX(t, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		DrawGhostNote(dc, x, y, dur > 0 ? dur : SC_PPQN / 4, 0, 0);
	};
	const int useBars = (u->patternId == SC_PAT_2BAR_E) ? 2 :
		(u->patternId == SC_PAT_4BAR_Q) ? 4 :
		(u->patternId == SC_PAT_8BAR_Q) ? 8 : 1;
	for (int b = 0; b < useBars; b++) {
		const uint32_t bar0 = tick0 + (uint32_t)(b * beatTicks * meterNumer);
		for (int beat = 0; beat < meterNumer; beat++) {
			const uint32_t bt = bar0 + (uint32_t)(beat * beatTicks);
			switch (u->patternId) {
			case SC_PAT_Q_1BAR: case SC_PAT_4BAR_Q: case SC_PAT_8BAR_Q:
				ghostAt(bt, beatTicks); break;
			case SC_PAT_E_1BAR: case SC_PAT_2BAR_E:
				ghostAt(bt, beatTicks / 2);
				ghostAt(bt + beatTicks / 2, beatTicks / 2); break;
			case SC_PAT_S_1BAR:
				for (int s = 0; s < 4; s++)
					ghostAt(bt + (beatTicks * s) / 4, beatTicks / 4);
				break;
			case SC_PAT_E_SS:
				ghostAt(bt, beatTicks / 2);
				ghostAt(bt + beatTicks / 2, beatTicks / 4);
				ghostAt(bt + (beatTicks * 3) / 4, beatTicks / 4); break;
			case SC_PAT_SS_E:
				ghostAt(bt, beatTicks / 4);
				ghostAt(bt + beatTicks / 4, beatTicks / 4);
				ghostAt(bt + beatTicks / 2, beatTicks / 2); break;
			case SC_PAT_TRIPLET_1BAR:
				for (int t = 0; t < 3; t++)
					ghostAt(bt + (beatTicks * t) / 3, beatTicks / 3);
				break;
			case SC_PAT_Q_REST_Q:
				if ((beat & 1) != 0) ghostAt(bt, beatTicks);
				break;
			case SC_PAT_DOTTED_E:
				ghostAt(bt, (beatTicks * 3) / 4);
				ghostAt(bt + (beatTicks * 3) / 4, beatTicks / 4); break;
			default:
				ghostAt(bt, beatTicks); break;
			}
		}
	}
	(void)span;
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
	if (pt.y < ScStaffGridBodyTop(grid.top, u)) return 0;
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
		{
			const int oct = ScStaffOttavaOctaves(ev, evCount, hitTr, u->hoverTick);
			if (oct != 0 && !u->placeRest) {
				const int written = ScStaffSoundingToWritten(u->hoverNote, oct);
				wchar_t wlab[32];
				ScStaffFormatMidiNoteName(written, wlab, 32);
				_snwprintf_s(out, outCch, _TRUNCATE, L"%s %s  %s  write=%s  tick=%s  (cursor)",
					trLab, noteLab, ScStaffOttavaLabel(oct), wlab, tpos);
			} else {
				_snwprintf_s(out, outCch, _TRUNCATE, L"%s %s  tick=%s  (cursor)",
					trLab, noteLab, tpos);
			}
		}
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

static void ScStaffDrawRhythmHit(CDC& dc, int x, int y, COLORREF col, int selected)
{
	if (selected)
		col = RGB(200, 50, 40);
	CPen pen(PS_SOLID, 2, col);
	CPen* op = dc.SelectObject(&pen);
	dc.MoveTo(x - 5, y - 5);
	dc.LineTo(x + 6, y + 6);
	dc.MoveTo(x + 5, y - 5);
	dc.LineTo(x - 6, y + 6);
	dc.SelectObject(op);
	CBrush br(col);
	CBrush* ob = dc.SelectObject(&br);
	dc.Ellipse(x - 2, y - 2, x + 3, y + 3);
	dc.SelectObject(ob);
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
	uint8_t flags;
	int xOff; /* side-by-side for 2nds / unisons */
};

/* Offset colliding heads (2nds / same degree) so C and C# sit left/right. */
static void ScStaffAssignHeadXOff(ScBeamHead* heads, int n, int gap)
{
	if (!heads || n < 1) return;
	for (int i = 0; i < n; i++)
		heads[i].xOff = 0;
	if (n < 2) return;
	const int thresh = max(1, gap);
	const int dx = 11;
	for (int i = 0; i + 1 < n; i++) {
		if (abs(heads[i + 1].y - heads[i].y) <= thresh)
			heads[i + 1].xOff = heads[i].xOff + dx;
	}
	/* Non-adjacent same staff degree (rare cluster) */
	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++) {
			if (abs(heads[j].y - heads[i].y) > thresh) continue;
			if (heads[j].xOff == heads[i].xOff)
				heads[j].xOff = heads[i].xOff + dx;
		}
	}
}

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
	return ScStaffEvSoundingMidi(e, isFm, tr);
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
			int x0 = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
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
		const int note = ScStaffEvWrittenMidi(ev, evCount, e, isFm, tr);
		const int x0 = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		const int x1 = ScStaffTickToX(e.tick + (uint32_t)dur, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		if (x1 < gridLeft - 40 || x0 > gridRight + 40) continue;
		const int y = ScStaffMidiNoteYTrack(u, tr, staffTop, note, e.tick, ev, evCount);
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
			h.flags = e.flags;
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
				ScBeamCol tmp = cols[a]; cols[a] = cols[b]; cols[b] = tmp;
			}

	for (int c = 0; c < nCol; c++) {
		/* sort heads by y */
		for (int a = 0; a < cols[c].nHeads; a++)
			for (int b = a + 1; b < cols[c].nHeads; b++)
				if (cols[c].heads[b].y < cols[c].heads[a].y) {
					ScBeamHead tmp = cols[c].heads[a];
					cols[c].heads[a] = cols[c].heads[b];
					cols[c].heads[b] = tmp;
				}
		ScStaffAssignHeadXOff(cols[c].heads, cols[c].nHeads, gap);
		int maxFl = 0;
		int sumY = 0;
		int stemForce = 0;
		int beamBreak = 0;
		for (int h = 0; h < cols[c].nHeads; h++) {
			int fl = ScStaffFlagCount(cols[c].heads[h].dur);
			if (fl > maxFl) maxFl = fl;
			sumY += cols[c].heads[h].y;
			const int sf = cols[c].heads[h].flags & SC_EF_STEM_MASK;
			if (sf) stemForce = sf;
			if (cols[c].heads[h].flags & SC_EF_BEAM_BREAK) beamBreak = 1;
		}
		cols[c].flags = maxFl;
		const int avgY = sumY / max(1, cols[c].nHeads);
		cols[c].stemUp = (stemForce == SC_EF_STEM_UP) ? 1 :
			(stemForce == SC_EF_STEM_DOWN) ? 0 : ((avgY >= midY) ? 1 : 0);
		if (beamBreak) cols[c].grp = -2; /* force break before — handled below */
		const int topY = cols[c].heads[0].y;
		const int botY = cols[c].heads[cols[c].nHeads - 1].y;
		cols[c].stemX = cols[c].x + (cols[c].stemUp ? 11 : 0);
		cols[c].stemAttachY = cols[c].stemUp ? botY : topY;
		cols[c].stemTipY = cols[c].stemUp ? (topY - 20) : (botY + 20);
	}

	/* Beam groups within beat */
	int c = 0;
	while (c < nCol) {
		if (cols[c].flags < 1 || cols[c].grp == -2) {
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

	/* Draw: lone single notes via ScStaffDrawSymbol(dur); chords/beams share stem.
	   OPNA RHY: X hits on pad lanes (no stems/beams). */
	const int rhy = (isFm && tr == 6) ? 1 : 0;
	for (int i = 0; i < nCol; i++) {
		ScBeamCol& col = cols[i];
		if (col.nHeads <= 0) continue;

		if (rhy) {
			for (int h = 0; h < col.nHeads; h++)
				ScStaffDrawRhythmHit(dc, col.x + col.heads[h].xOff + 5, col.heads[h].y,
					col.heads[h].col, col.heads[h].selected);
			continue;
		}

		if (col.grp < 0 && col.nHeads == 1 && !col.heads[0].isTie) {
			ScStaffDrawSymbol(dc, col.x, col.heads[0].y, col.heads[0].dur, 0,
				col.heads[0].col, col.heads[0].selected, col.stemUp);
			{
				int hp = 0, hpan = 0, hvol = 0, hexp = 0;
				if (ScStaffNoteHasFx(ev, evCount, col.heads[0].evIdx, isFm, &hp, &hpan, &hvol, &hexp)) {
					int bx = col.x + 14, by = col.heads[0].y - 12;
					if (hp) { dc.FillSolidRect(bx, by, 3, 8, RGB(0, 170, 150)); bx += 4; }
					if (hpan) { dc.FillSolidRect(bx, by, 3, 8, RGB(150, 70, 190)); bx += 4; }
					if (hvol) { dc.FillSolidRect(bx, by, 3, 8, RGB(220, 130, 40)); bx += 4; }
					if (hexp) { dc.FillSolidRect(bx, by, 3, 8, RGB(240, 150, 50)); bx += 4; }
				}
			}
			if (col.xEnd - col.x > 12) {
				int gw = max(2, ((col.xEnd - col.x) * col.gatePct) / 100);
				dc.FillSolidRect(col.x + 12, col.heads[0].y - 1, min(gw, 48), 2, RGB(100, 120, 180));
			}
			continue;
		}

		for (int h = 0; h < col.nHeads; h++) {
			ScBeamHead& hd = col.heads[h];
			const int hx = col.x + hd.xOff;
			const int hollow = (ScStaffBaseDurUndot(hd.dur) >= SC_PPQN * 2) || hd.isTie;
			if (hd.selected)
				dc.Draw3dRect(hx - 3, hd.y - 22, 20, 32, RGB(220, 140, 40), RGB(220, 140, 40));
			ScStaffDrawOvalHead(dc, hx, hd.y, hollow, hd.col, ScStaffDurIsDotted(hd.dur));
			{
				int hp = 0, hpan = 0, hvol = 0, hexp = 0;
				if (ScStaffNoteHasFx(ev, evCount, hd.evIdx, isFm, &hp, &hpan, &hvol, &hexp)) {
					int bx = hx + 14, by = hd.y - 12;
					if (hp) { dc.FillSolidRect(bx, by, 3, 8, RGB(0, 170, 150)); bx += 4; }
					if (hpan) { dc.FillSolidRect(bx, by, 3, 8, RGB(150, 70, 190)); bx += 4; }
					if (hvol) { dc.FillSolidRect(bx, by, 3, 8, RGB(220, 130, 40)); bx += 4; }
					if (hexp) { dc.FillSolidRect(bx, by, 3, 8, RGB(240, 150, 50)); bx += 4; }
				}
			}
			const int acc = hd.flags & SC_EF_ACC_MASK;
			if (acc) {
				dc.SetTextColor(hd.col);
				dc.SetBkMode(TRANSPARENT);
				const wchar_t* g =
					(acc == SC_EF_ACC_SHARP) ? L"♯" :
					(acc == SC_EF_ACC_FLAT) ? L"♭" : L"♮";
				dc.TextOut(hx - 12, hd.y - 8, g);
			}
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

	/* Beams / flags (skip lone singles — already drawn complete; RHY uses X hits only) */
	for (int i = 0; i < nCol; ) {
		if (rhy) { i++; continue; }
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

static void ScStaffPaintLayoutZone(CDC& dc, int xStart, int xEnd, int yTop, int yBot,
	const ScEvent* ev, int evCount, uint32_t barTick, const ScStaffUi* u, int tr,
	int staffTop, int gap)
{
	if (xEnd <= xStart || yBot <= yTop) return;
	dc.FillSolidRect(xStart, yTop, xEnd - xStart, yBot - yTop, RGB(245, 240, 252));
	dc.Draw3dRect(CRect(xStart, yTop, xEnd, yBot), RGB(195, 185, 215), RGB(195, 185, 215));
	int lx = xStart + 4;
	const int cy = staffTop + gap + 2;
	const int staffH = ScStaffH(u);
	dc.SetBkMode(TRANSPARENT);
	if (tr == 0) {
		for (int i = 0; i < evCount; i++) {
			if (ev[i].tick != barTick) continue;
			if (ev[i].kind == SC_EV_METER) {
				ScStaffDrawTimeSignature(dc, lx, staffTop, gap,
					ev[i].a > 0 ? (int)ev[i].a : 4, ev[i].b > 0 ? (int)ev[i].b : 4, RGB(40, 40, 55));
				lx += 30;
			}
		}
	}
	for (int i = 0; i < evCount; i++) {
		if (ev[i].tick != barTick || ev[i].kind != SC_EV_CLEF) continue;
		if ((int)ev[i].ch != tr) continue;
		const int cm = (int)ev[i].a % 4;
		ScStaffPaintLayoutClefGlyph(dc, cm, lx, cy, staffH, gap);
		lx += 22;
	}
	if (tr == 0) {
		for (int i = 0; i < evCount; i++) {
			if (ev[i].tick != barTick || ev[i].kind != SC_EV_KEY) continue;
			const int k = (int)(int8_t)ev[i].a;
			wchar_t lab[16];
			if (k == 0) wcscpy_s(lab, L"C");
			else if (k > 0) _snwprintf_s(lab, _TRUNCATE, L"+%d", k);
			else _snwprintf_s(lab, _TRUNCATE, L"%d", k);
			dc.SetTextColor(RGB(40, 40, 55));
			dc.TextOut(lx, cy + 16, lab);
			break;
		}
	}
	(void)u;
}

/* Measure bar lines + beat/sub-beat grid — painted last so they stay visible on staff. */
static void ScStaffPaintRowMeasureGrid(CDC& dc, const CRect& grid, const ScStaffUi* u,
	int rowTop, int rowH, int staffTop, int cm, int staffOriginY,
	int gridLeft, int pxBeat, int gridRight,
	const ScEvent* ev, int evCount, int beatsVisible, int defNumer, int defDenom)
{
	if (!u) return;
	const int gap = ScStaffLineGap(u);
	const int staffH = ScStaffH(u);
	const int beatH = (cm == 2) ? (staffH * 2 + SC_GRAND_STAFF_GAP - 8) : (gap * 4);
	const int toneTop = rowTop + SC_NAME_BAND_H;
	const int rowBot = min(rowTop + rowH - SC_PART_GAP, grid.bottom);
	const int yTop = max(toneTop, staffOriginY);
	const int staffY0 = staffTop + 8;
	const int staffY1 = staffTop + 8 + beatH;
	if (rowBot <= yTop && staffY1 <= staffY0) return;

	const int emph = u->gridEmph ? 1 : 0;
	const COLORREF barC = RGB(18, 18, 32);
	const int barW = 3;
	const COLORREF beatC = emph ? RGB(155, 155, 178) : RGB(205, 205, 218);

	uint32_t tVis1 = ScStaffXToTick(gridRight + pxBeat, u->scrollX, gridLeft, pxBeat, 0, u, ev, evCount);
	uint32_t barTick = 0;
	int barNumer = defNumer, barDenom = defDenom;
	while (barTick <= tVis1 + (uint32_t)ScStaffMeterTicksPerMeasure(barNumer, barDenom)) {
		ScStaffMeterAtTick(ev, evCount, barTick, defNumer, defDenom, &barNumer, &barDenom);
		const int beatTicks = ScStaffBeatTicks(barDenom);
		const int mTicks = ScStaffMeterTicksPerMeasure(barNumer, barDenom);
		const int xContent = ScStaffTickToX(barTick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		const int hasZone = (barTick > 0) && ScStaffBarHasLayoutZone(ev, evCount, barTick);
		const int xBar = hasZone ? xContent - SC_LAYOUT_ZONE_W : xContent;
		if (barTick > 0 && xBar >= grid.left && xBar <= gridRight) {
			if (rowBot > yTop)
				dc.FillSolidRect(xBar, yTop, barW, rowBot - yTop, barC);
			if (staffY1 > staffY0)
				dc.FillSolidRect(xBar, staffY0, barW, staffY1 - staffY0, barC);
		}
		for (int beat = 1; beat < barNumer; beat++) {
			const uint32_t bt = barTick + (uint32_t)(beat * beatTicks);
			const int xBeat = ScStaffTickToX(bt, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
			if (xBeat < grid.left || xBeat > gridRight) continue;
			if (staffY1 > staffY0)
				dc.FillSolidRect(xBeat, staffY0, emph ? 2 : 1, staffY1 - staffY0, beatC);
		}
		if (mTicks < 1) break;
		barTick += (uint32_t)mTicks;
	}
	for (int b = 0; b <= beatsVisible * 4; b++) {
		const uint32_t tick = (uint32_t)((b * SC_PPQN) / 4);
		if (tick % (uint32_t)ScStaffBeatTicks(defDenom) == 0) continue;
		const int x = ScStaffTickToX(tick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		if (x < grid.left || x > gridRight) continue;
		int barNumer2 = defNumer, barDenom2 = defDenom;
		ScStaffMeterAtTick(ev, evCount, tick, defNumer, defDenom, &barNumer2, &barDenom2);
		const int beatTicks = ScStaffBeatTicks(barDenom2);
		if (beatTicks > 0 && (int)(tick % (uint32_t)beatTicks) == 0) continue;
		const int mTicks = ScStaffMeterTicksPerMeasure(barNumer2, barDenom2);
		if (mTicks > 0 && (int)(tick % (uint32_t)mTicks) == 0) continue;
		if (staffY1 > staffY0)
			dc.FillSolidRect(x, staffY0, 1, staffY1 - staffY0, RGB(238, 240, 246));
	}
}

static void ScStaffPaintBandCurve(CDC& dc, const CRect& lane, const ScStaffUi* u, const ScEvent* ev, int evCount,
	const uint8_t* data, int colMax, int kind, int isTempo, const wchar_t* title, const wchar_t* mmlHint);

static void ScStaffPaintStickyHeader(CDC& dc, const CRect& grid, const ScStaffUi* u,
	const ScEvent* ev, int evCount, int docTempoT, int gridLeft, int pxBeat, int colMaxBand)
{
	const int globalH = ScStaffGlobalTempoBandH(u);
	const int rulerTop = grid.top + globalH;
	if (u->globalTempoBandOn && globalH > 0) {
		CRect tempoLane(grid.left, grid.top, grid.right, grid.top + globalH);
		dc.FillSolidRect(tempoLane, RGB(252, 248, 246));
		ScStaffPaintBandCurve(dc, tempoLane, u, ev, evCount, u->globalTempoStrip, colMaxBand, 0, 1,
			LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo",
				L"템포", L"速度", L"إيقاع", L"Темп", L"Tempo", L"Tempo",
				L"Tempo", L"Tempo", L"Tempo"),
			L"t120");
	}
	CRect ruler(grid.left, rulerTop, grid.right, rulerTop + SC_RULER_H);
	dc.FillSolidRect(ruler, RGB(236, 238, 246));
	dc.FillSolidRect(ruler.left, ruler.bottom - 1, ruler.Width(), 1, RGB(170, 172, 190));
	dc.SetBkMode(TRANSPARENT);
	CFont tf;
	tf.CreateFont(15, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Symbol");
	CFont* ot = dc.SelectObject(&tf);
	int bpmDef = ScStaffBpmFromTempoT(docTempoT);
	dc.SetTextColor(RGB(150, 40, 40));
	wchar_t ts[48];
	_snwprintf_s(ts, _TRUNCATE, L"♪=%d", bpmDef);
	dc.TextOut(grid.left + 6, ruler.top + 4, ts);
	dc.SetTextColor(RGB(90, 90, 110));
	const int defNumer = u->meterNumer > 0 ? u->meterNumer : 4;
	const int defDenom = u->meterDenom > 0 ? u->meterDenom : 4;
	uint32_t tVis1 = ScStaffXToTick(grid.right + pxBeat, u->scrollX, gridLeft, pxBeat, 0, u, ev, evCount);
	uint32_t mt = 0;
	int measNum = 1;
	int numer = defNumer, denom = defDenom;
	while (mt <= tVis1 + (uint32_t)ScStaffMeterTicksPerMeasure(numer, denom)) {
		ScStaffMeterAtTick(ev, evCount, mt, defNumer, defDenom, &numer, &denom);
		const int mTicks = ScStaffMeterTicksPerMeasure(numer, denom);
		int x = ScStaffTickToX(mt, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		if (x >= gridLeft && x <= grid.right) {
			wchar_t s[16];
			_snwprintf_s(s, _TRUNCATE, L"%d", measNum);
			dc.TextOut(x + 3, ruler.top + 3, s);
			dc.FillSolidRect(x, ruler.top + 2, 3, SC_RULER_H - 4, RGB(50, 50, 72));
		}
		if (mTicks < 1) break;
		mt += (uint32_t)mTicks;
		measNum++;
		if (measNum > 9999) break;
	}
	for (int i = 0; i < evCount; i++) {
		if (ev[i].kind != SC_EV_TEMPO && ev[i].kind != SC_EV_FM_TEMPO) continue;
		int tval = ev[i].a | (ev[i].b << 8);
		if (tval < 1) continue;
		int bpm = ScStaffBpmFromTempoT(tval);
		int x = ScStaffTickToX(ev[i].tick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		if (x < gridLeft || x > grid.right) continue;
		dc.SetTextColor(RGB(170, 50, 50));
		_snwprintf_s(ts, _TRUNCATE, L"♪=%d", bpm);
		dc.TextOut(x + 2, ruler.top + 4, ts);
		dc.FillSolidRect(x, ruler.top + 2, 2, SC_RULER_H - 4, RGB(180, 60, 60));
	}
	if (u->loopATick >= 0 && u->loopBTick > u->loopATick) {
		int xa = ScStaffTickToX((uint32_t)u->loopATick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		int xb = ScStaffTickToX((uint32_t)u->loopBTick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
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
	{
		int xm = ScStaffTickToX(u->markerTick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
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

void ScStaffPaintStaves(CDC& dc, const CRect& grid, const ScStaffUi* u,
	const ScEvent* ev, int evCount, int isFm, int curTrack, int docTempoT)
{
	dc.FillSolidRect(grid, RGB(248, 248, 252));
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gap = ScStaffLineGap(u);
	const int beatsVisible = max(16, (grid.Width() + u->scrollX) / pxBeat + 4);
	const int quant = ScStaffPlaceQuant(u);
	const int gridLeft = ScStaffGridLeftPx(grid.left, u, ev, evCount);
	const int staffOriginY = ScStaffGridBodyTop(grid.top, u);
	const int colMaxBand = ScStaffStripColCount(u);

	/* Sticky header background (tempo + ruler painted on top after scroll content). */
	if (staffOriginY > grid.top)
		dc.FillSolidRect(grid.left, grid.top, grid.Width(), staffOriginY - grid.top, RGB(248, 248, 252));

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
		const int staffH = ScStaffHTrack(u, tr);
		const int isCur = (tr == curTrack);
		const int lineX0 = gridLeft;
		const int lineW = max(1, grid.right - lineX0);
		uint32_t tVis0 = ScStaffXToTick(lineX0, u->scrollX, gridLeft, pxBeat, 0, u, ev, evCount);
		const int cmMargin = ScStaffClefModeAt(u, tr, 0, ev, evCount);
		const int cm = ScStaffClefModeAt(u, tr, tVis0, ev, evCount);
		if (isCur)
			dc.FillSolidRect(grid.left, rowTop, 6, rowH - 4, RGB(50, 120, 230));

		/* SSW: Tone lane + Exc/RPN lane only in PART_GAUGE_H (not ledger pad) */
		{
			const int gaugeBot = rowTop + SC_PART_GAUGE_H;
			const int toneTop = rowTop + SC_NAME_BAND_H;
			const int toneBot = toneTop + SC_CTRL_LANE_H;
			const int excTop = toneBot;
			const int excBot = gaugeBot;
			const int toneCfg = ScStaffToneLaneConfigured(u, tr, isFm);
			const int excUsed = ScStaffExcLaneUsed(ev, evCount, tr, isFm);
			COLORREF toneBg, toneEdge, excBg, excEdge;
			ScStaffLaneFillColors(toneCfg, excUsed, &toneBg, &toneEdge, &excBg, &excEdge);
			const int y0 = max(rowTop, staffOriginY);
			if (toneTop > y0)
				dc.FillSolidRect(grid.left, y0, grid.Width(), toneTop - y0, RGB(242, 243, 248));
			const int yClip0 = max(toneTop, staffOriginY);
			if (toneBot > yClip0 && toneBot <= grid.bottom)
				dc.FillSolidRect(grid.left, yClip0, grid.Width(), min(toneBot, grid.bottom) - yClip0, toneBg);
			if (excBot > max(excTop, staffOriginY) && excTop < grid.bottom)
				dc.FillSolidRect(grid.left, max(excTop, staffOriginY), grid.Width(),
					min(excBot, grid.bottom) - max(excTop, staffOriginY), excBg);
			/* Lane hint labels when empty */
			dc.SetBkMode(TRANSPARENT);
			if (!toneCfg) {
				dc.SetTextColor(RGB(200, 175, 140));
				dc.TextOut(lineX0 + 4, toneTop + 4, isFm ? L"Tone / Voice" : L"Tone / Prog");
			}
			if (!excUsed) {
				dc.SetTextColor(RGB(150, 170, 200));
				dc.TextOut(lineX0 + 4, excTop + 4, isFm ? L"Exc / EX" : L"Exc / RPN / SysEx");
			}
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
					_snwprintf_s(label, _TRUNCATE,
						(e.b == 0) ? L"%s @%u" : L"%s Voice %u", tpos, (unsigned)e.a);
					bg = RGB(225, 245, 235); fg = RGB(30, 90, 60); edge = RGB(80, 150, 110);
				} else if (isFm && e.kind == SC_EV_FM_EX) {
					lane = 1;
					_snwprintf_s(label, _TRUNCATE, L"%s EX%u=%u", tpos, (unsigned)e.a, (unsigned)e.b);
					bg = RGB(242, 235, 250); fg = RGB(70, 45, 110); edge = RGB(140, 110, 170);
				} else if (isFm && e.kind == SC_EV_FM_LFO) {
					lane = 1;
					_snwprintf_s(label, _TRUNCATE, L"%s LFO %u/%u", tpos, (unsigned)e.a, (unsigned)e.b);
					bg = RGB(232, 240, 250); fg = RGB(30, 70, 110); edge = RGB(90, 130, 160);
				} else if (isFm && e.kind == SC_EV_FM_DETUNE) {
					lane = 1;
					_snwprintf_s(label, _TRUNCATE, L"%s Detune", tpos);
					bg = RGB(232, 240, 250); fg = RGB(30, 70, 110); edge = RGB(90, 130, 160);
				} else if (isFm && e.kind == SC_EV_FM_FLR) {
					lane = 1;
					_snwprintf_s(label, _TRUNCATE, L"%s FLR %u", tpos, (unsigned)e.a);
					bg = RGB(232, 240, 250); fg = RGB(30, 70, 110); edge = RGB(90, 130, 160);
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
				} else if (e.kind == SC_EV_OTTAVA) {
					lane = 0;
					_snwprintf_s(label, _TRUNCATE, L"%s %s", tpos, ScStaffOttavaLabel((int)(int8_t)e.a));
					bg = RGB(255, 250, 210); fg = RGB(100, 70, 10); edge = RGB(180, 140, 40);
				} else if (e.kind == SC_EV_OTTAVA_END) {
					lane = 0;
					_snwprintf_s(label, _TRUNCATE, L"%s loco", tpos);
					bg = RGB(255, 250, 210); fg = RGB(100, 70, 10); edge = RGB(180, 140, 40);
				} else
					continue;
				int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
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

		/* Per-part CC bands (Reverb/Chorus/Expr/…) — SSW-style inline strips */
		{
			const int gaugeBot = rowTop + SC_PART_GAUGE_H;
			int yBand = gaugeBot;
			uint8_t bandBuf[SC_STRIP_COLS_MAX];
			for (int b = 0; b < SC_PBAND_KIND_COUNT; b++) {
				if (!(u->partBandMask[tr] & (1 << b))) continue;
				const int bh = ScStaffPartBandHeightPx(u, tr, b);
				if (yBand + bh < staffOriginY || yBand > grid.bottom) {
					yBand += bh;
					continue;
				}
				const int bandTop = max(yBand, staffOriginY);
				const int bandBot = min(yBand + bh, grid.bottom);
				CRect lane(grid.left, bandTop, grid.right, bandBot);
				dc.FillSolidRect(lane, RGB(248, 249, 252));
				ScStaffEnsurePartBandFromDoc(u, ev, evCount, tr, b, bandBuf);
				const uint8_t* src = bandBuf;
				if (u->bandEditTrack == tr && u->bandEditKind == b)
					src = u->bandEditBuf;
				ScStaffPaintBandCurve(dc, lane, u, ev, evCount, src, colMaxBand, ScStaffPartBandToStripKind(b), 0,
					ScStaffPartBandName(b), ScStaffPartBandMmlHint(b));
				yBand += bh;
			}
		}

		ScStaffPaintStaffLinesRange(dc, cmMargin, grid.left, lineX0, staffTop, staffH, gap, u, tr);
		ScStaffPaintMarginClef(dc, cmMargin, grid.left, staffTop, staffH, gap, u, tr, lineX0, grid.right);
		ScStaffPaintClefSegments(dc, lineX0, grid.right, staffTop, staffH, gap,
			u, tr, u->scrollX, gridLeft, pxBeat, ev, evCount, tVis0);

		const int defNumer = u->meterNumer > 0 ? u->meterNumer : 4;
		const int defDenom = u->meterDenom > 0 ? u->meterDenom : 4;
		if (tr == 0) {
			int numer0 = defNumer, denom0 = defDenom;
			ScStaffMeterAtTick(ev, evCount, 0, defNumer, defDenom, &numer0, &denom0);
			const int ks0 = ScStaffKeySigAtTick(ev, evCount, 0, u->keySig);
			ScStaffDrawMarginKeyAndMeter(dc, grid.left, staffTop, gap, staffH, cmMargin,
				ks0, numer0, denom0, RGB(40, 40, 55));
		}

		const int beatH = (cm == 2) ? (staffH * 2 + SC_GRAND_STAFF_GAP - 8) : (gap * 4);
		{
			uint32_t tVis1 = ScStaffXToTick(grid.right + pxBeat, u->scrollX, gridLeft, pxBeat, 0, u, ev, evCount);
			uint32_t barTick = 0;
			int barNumer = defNumer, barDenom = defDenom;
			while (barTick <= tVis1 + (uint32_t)ScStaffMeterTicksPerMeasure(barNumer, barDenom)) {
				ScStaffMeterAtTick(ev, evCount, barTick, defNumer, defDenom, &barNumer, &barDenom);
				const int mTicks = ScStaffMeterTicksPerMeasure(barNumer, barDenom);
				if (barTick > 0 && ScStaffBarHasLayoutZone(ev, evCount, barTick)) {
					const int xContent = ScStaffTickToX(barTick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
					const int xBarLine = xContent - SC_LAYOUT_ZONE_W;
					if (xContent > grid.left && xBarLine < grid.right) {
						const int zTop = rowTop + SC_NAME_BAND_H;
						const int zBot = rowTop + rowH - SC_PART_GAP;
						ScStaffPaintLayoutZone(dc, xBarLine, xContent, zTop, zBot,
							ev, evCount, barTick, u, tr, staffTop, gap);
					}
				}
				if (mTicks < 1) break;
				barTick += (uint32_t)mTicks;
			}
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
				int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat, u, ev, evCount) + stack * 12;
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
					/* Line to matching ＊ (or +1 measure if none). */
					uint32_t endTick = e.tick + (uint32_t)(SC_PPQN * SC_MEASURE_BEATS);
					for (int j = 0; j < evCount; j++) {
						if ((int)ev[j].ch != tr) continue;
						if (ev[j].tick <= e.tick) continue;
						if (ev[j].kind == SC_EV_PEDAL_OFF) { endTick = ev[j].tick; break; }
						if (ev[j].kind == SC_EV_PEDAL_ON) { endTick = ev[j].tick; break; }
					}
					int x1 = ScStaffTickToX(endTick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
					if (x1 < x + 20) x1 = x + 20;
					dc.FillSolidRect(x, y1 + 12, x1 - x, 2, RGB(90, 40, 120));
					dc.FillSolidRect(x1 - 1, y1 + 8, 2, 10, RGB(90, 40, 120));
				} else if (e.kind == SC_EV_PEDAL_OFF) {
					dc.SetTextColor(RGB(90, 40, 120));
					dc.TextOut(x - 2, y1 - 2, L"＊");
				} else if (e.kind == SC_EV_OTTAVA || e.kind == SC_EV_OTTAVA_END) {
					const int oct = (e.kind == SC_EV_OTTAVA_END) ? 0 : (int)(int8_t)e.a;
					const wchar_t* lab = ScStaffOttavaLabel(oct);
					const COLORREF oc = RGB(140, 90, 20);
					/* Find end tick for dashed bracket (next ottava/loco or +2 measures). */
					uint32_t endTick = e.tick + (uint32_t)(SC_PPQN * SC_MEASURE_BEATS * 2);
					for (int j = 0; j < evCount; j++) {
						if ((int)ev[j].ch != tr) continue;
						if (ev[j].tick <= e.tick) continue;
						if (ev[j].kind != SC_EV_OTTAVA && ev[j].kind != SC_EV_OTTAVA_END) continue;
						endTick = ev[j].tick;
						break;
					}
					int x1 = ScStaffTickToX(endTick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
					if (x1 < x + 24) x1 = x + 24;
					const int above = (oct >= 0);
					const int ly = above ? (y0 - 14) : (y1 + 2);
					dc.SetTextColor(oc);
					dc.TextOut(x + 2, ly, lab);
					/* Dashed horizontal */
					for (int dx = x + 28; dx < x1; dx += 6)
						dc.FillSolidRect(dx, ly + 8, 3, 1, oc);
					dc.FillSolidRect(x1 - 1, ly + 4, 1, 8, oc);
				} else if (e.kind == SC_EV_SLUR_START) {
					uint32_t endTick = e.tick + (uint32_t)SC_PPQN * 2;
					for (int j = 0; j < evCount; j++) {
						if ((int)ev[j].ch != tr) continue;
						if (ev[j].tick <= e.tick) continue;
						if (ev[j].kind == SC_EV_SLUR_END) { endTick = ev[j].tick; break; }
					}
					int x1 = ScStaffTickToX(endTick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
					if (x1 < x + 20) x1 = x + 20;
					const COLORREF sc = RGB(50, 90, 140);
					const int mid = (x + x1) / 2;
					const int ly = y0 - 6;
					CPen pen(PS_SOLID, 1, sc);
					CPen* op = dc.SelectObject(&pen);
					dc.MoveTo(x, ly + 6);
					dc.LineTo(mid, ly);
					dc.LineTo(x1, ly + 6);
					dc.SelectObject(op);
				} else if (e.kind == SC_EV_CRESC || e.kind == SC_EV_DIM) {
					int span = e.dur > 0 ? (int)e.dur : SC_PPQN * 2;
					int x1 = ScStaffTickToX(e.tick + (uint32_t)span, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
					if (x1 < x + 24) x1 = x + 24;
					const COLORREF hc = RGB(120, 50, 50);
					const int hy = y1 + 4;
					CPen pen(PS_SOLID, 1, hc);
					CPen* op = dc.SelectObject(&pen);
					if (e.kind == SC_EV_CRESC) {
						dc.MoveTo(x, hy);
						dc.LineTo(x1, hy - 6);
						dc.MoveTo(x, hy);
						dc.LineTo(x1, hy + 6);
					} else {
						dc.MoveTo(x, hy - 6);
						dc.LineTo(x1, hy);
						dc.MoveTo(x, hy + 6);
						dc.LineTo(x1, hy);
					}
					dc.SelectObject(op);
				}
			}
			dc.SelectObject(om);
		}
		if (isCur && u->snapFit && quant > 0) {
			for (uint32_t t = 0; t < (uint32_t)(beatsVisible * SC_PPQN); t += (uint32_t)quant) {
				int x = ScStaffTickToX(t, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
				if (x < lineX0 || x > grid.right) continue;
				dc.FillSolidRect(x, staffTop + 8 + beatH + 1, 1, 3, RGB(120, 160, 220));
			}
		}

		ScStaffPaintTrackNotes(dc, u, ev, evCount, isFm, tr, staffTop,
			gridLeft, grid.right, pxBeat, ScStaffBeatTicks(defDenom));

		ScStaffPaintRowMeasureGrid(dc, grid, u, rowTop, rowH, staffTop, cm, staffOriginY,
			gridLeft, pxBeat, grid.right, ev, evCount, beatsVisible, defNumer, defDenom);
	}

	/* Sticky tempo band + ruler on top of scrolled content. */
	ScStaffPaintStickyHeader(dc, grid, u, ev, evCount, docTempoT, gridLeft, pxBeat, colMaxBand);

	/* Marker / playhead: dotted full-height; solid only on active (markerSolidTrack) row.
	   Preview playhead stays solid full-height for visibility. */
	{
		uint32_t phTick = u->previewActive ? u->playheadTick : u->markerTick;
		int xh = ScStaffTickToX(phTick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		if (xh >= gridLeft && xh <= grid.right) {
			COLORREF hc = u->previewActive ? RGB(230, 40, 50) : RGB(200, 50, 50);
			const int y0 = staffOriginY;
			const int y1 = grid.bottom;
			if (u->previewActive) {
				dc.FillSolidRect(xh, y0, 2, max(1, y1 - y0), hc);
			} else {
				/* dotted full height */
				for (int y = y0; y < y1; y += 6)
					dc.FillSolidRect(xh, y, 1, min(3, y1 - y), RGB(200, 120, 120));
				const int solidTr = u->markerSolidTrack;
				if (solidTr >= 0 && solidTr < u->trackCount && u->visible[solidTr]) {
					int ry = staffOriginY - u->scrollY;
					for (int tr = 0; tr < u->trackCount; tr++) {
						const int rowH = ScStaffRowH(u, tr);
						if (tr == solidTr) {
							const int top = max(ry, staffOriginY);
							const int bot = min(ry + rowH, grid.bottom);
							if (bot > top)
								dc.FillSolidRect(xh, top, 2, bot - top, hc);
							break;
						}
						ry += rowH;
					}
				}
			}
		}
	}

	if (u->hoverValid && (u->tool == SC_TOOL_PENCIL || u->tool == SC_TOOL_TEMPO) && u->hoverTrack >= 0) {
		int st = ScStaffVisibleLaneStaffTop(grid, u, u->hoverTrack);
		if (st >= 0) {
			int x = ScStaffTickToX(u->hoverTick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
			if (u->tool == SC_TOOL_TEMPO) {
				dc.SetTextColor(RGB(200, 60, 60));
				dc.TextOut(x, st, L"♪=");
			} else {
			const int oct = ScStaffOttavaOctaves(ev, evCount, u->hoverTrack, u->hoverTick);
			if (u->placeRest) {
				const int y = st + 8 + gap * 2;
				DrawGhostNote(dc, x, y, u->placeDur, 1, 0);
			} else if (u->patternMode) {
				DrawGhostPattern(dc, u, u->hoverTrack, st, gridLeft, pxBeat,
					u->hoverTick, u->hoverNote, oct, ev, evCount);
			} else if (u->chordMode) {
				DrawGhostChord(dc, u, u->hoverTrack, st, x, u->hoverNote, oct,
					u->placeDur, u->placeAccidental);
			} else {
				/* hoverNote is sounding MIDI; draw at written pitch under ottava. */
				const int written = ScStaffSoundingToWritten(u->hoverNote, oct);
				const int y = ScStaffMidiNoteYTrack(u, u->hoverTrack, st, written, u->hoverTick, ev, evCount);
				DrawGhostNote(dc, x, y, u->placeDur, 0, u->placeAccidental);
			}
			if (oct != 0 && !u->placeRest) {
				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(RGB(140, 90, 20));
				CFont of;
				of.CreateFont(12, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
					OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
				CFont* old = dc.SelectObject(&of);
				const wchar_t* lab = ScStaffOttavaLabel(oct);
				const int written0 = ScStaffSoundingToWritten(u->hoverNote, oct);
				const int y0 = ScStaffMidiNoteYTrack(u, u->hoverTrack, st, written0, u->hoverTick, ev, evCount);
				const int ly = (oct > 0) ? (y0 - 18) : (y0 + 10);
				dc.TextOut(x + 14, ly, lab);
				dc.SelectObject(old);
			}
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
	const int gridLeft = rc.left + ScStaffClefMarginPx(u, NULL, 0);
	const int n = max(0, min(SC_STRIP_LANES_MAX, u->stripCount));
	const int step = ScStaffStripStepTicks(u);
	const int colMax = ScStaffStripColCount(u);
		const wchar_t* drawName = ScStaffStripDrawModeName(u->stripDraw);
	wchar_t stepLbl[16];
	if (step >= SC_PPQN) wcscpy_s(stepLbl, L"1/4");
	else if (step >= SC_PPQN / 2) wcscpy_s(stepLbl, L"1/8");
	else if (step >= SC_PPQN / 4) wcscpy_s(stepLbl, L"1/16");
	else if (step >= SC_PPQN / 8) wcscpy_s(stepLbl, L"1/32");
	else wcscpy_s(stepLbl, L"1/64");
	for (int L = 0; L < n; L++) {
		const int laneTop = rc.top + ScStaffStripLaneTopY(u, L);
		const int laneH = ScStaffStripLaneHeightPx(u, L);
		CRect lane(rc.left, laneTop, rc.right, laneTop + laneH);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(60, 60, 80));
		wchar_t title[96];
		_snwprintf_s(title, _TRUNCATE, L"%s [%s] %s %s", ScStaffStripKindName(u->stripKind[L]),
			drawName, stepLbl, ScStaffStripHeightName(u->stripLaneHgt[L]));
		dc.TextOut(lane.left + 6, lane.top + 2, title);
		const int top = lane.top + 18;
		const int h = max(12, laneH - 24);
		const int kind = u->stripKind[L];
		const int bipolar = ScStaffStripKindBipolar(kind);
		dc.SetTextColor(RGB(110, 110, 130));
		CFont* oldF = dc.SelectObject(CFont::FromHandle((HFONT)GetStockObject(DEFAULT_GUI_FONT)));
		if (bipolar) {
			if (kind == SC_STRIP_PAN) {
				dc.TextOut(lane.left + 4, top, L"R");
				dc.TextOut(lane.left + 4, top + h / 2 - 6, L"C");
				dc.TextOut(lane.left + 4, top + h - 12, L"L");
			} else {
				dc.TextOut(lane.left + 4, top, L"+100");
				dc.TextOut(lane.left + 4, top + h / 2 - 6, L"0");
				dc.TextOut(lane.left + 4, top + h - 12, L"-100");
			}
			dc.FillSolidRect(gridLeft, top + h / 2, lane.right - gridLeft, 1, RGB(170, 170, 185));
		} else {
			dc.TextOut(lane.left + 4, top, L"100");
			dc.TextOut(lane.left + 4, top + h / 2 - 6, L"50");
			dc.TextOut(lane.left + 4, top + h - 12, L"0");
		}
		if (oldF) dc.SelectObject(oldF);
		const int beatsVisible = max(16, (lane.Width() + u->scrollX) / pxBeat + 4);
		uint32_t tVis0 = ScStaffXToTick(gridLeft, u->scrollX, gridLeft, pxBeat, step);
		uint32_t tVis1 = ScStaffXToTick(lane.right + pxBeat, u->scrollX, gridLeft, pxBeat, step);
		int c0 = ScStaffStripTickToCol(u, tVis0);
		int c1 = ScStaffStripTickToCol(u, tVis1);
		if (c0 < 0) c0 = 0;
		if (c1 >= colMax) c1 = colMax - 1;
		if (c1 < c0) c1 = c0;
		enum { kPtsMax = 512 };
		POINT pts[kPtsMax];
		int npt = 0;
		for (int c = c0; c <= c1; c++) {
			uint32_t tick = ScStaffStripColToTick(u, c);
			int x = ScStaffTickToX(tick, u->scrollX, gridLeft, pxBeat, NULL, NULL, 0);
			if (x < gridLeft - 2 || x > lane.right + 2) continue;
			int v = u->strip[L][c];
			int bh = (h * v) / 127;
			COLORREF col = (kind == SC_STRIP_PITCH) ? RGB(180, 120, 60) :
				(kind == SC_STRIP_GATE) ? RGB(120, 160, 90) :
				(kind == SC_STRIP_PAN) ? RGB(160, 100, 180) : RGB(90, 140, 210);
			int nextX = ScStaffTickToX(tick + (uint32_t)step, u->scrollX, gridLeft, pxBeat, NULL, NULL, 0);
			int bw = max(2, nextX - x - 1);
			dc.FillSolidRect(x, top + h - bh, bw, bh, col);
			if (npt < kPtsMax) {
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
				int x = ScStaffTickToX(mt, u->scrollX, gridLeft, pxBeat, NULL, NULL, 0);
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
	int y = trackRc.top + ScStaffGridHeaderH(u) + 4 - u->scrollY;
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
	if (tr < 0) return -1;
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

int ScStaffHitRulerTick(const CRect& grid, const ScStaffUi* u, CPoint pt, uint32_t* outTick,
	const ScEvent* ev, int evCount)
{
	if (!u || !grid.PtInRect(pt)) return 0;
	if (pt.y < grid.top || pt.y >= ScStaffGridBodyTop(grid.top, u)) return 0;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int quant = ScStaffPlaceQuant(u);
	uint32_t tick = ScStaffXToTick(pt.x, u->scrollX, ScStaffGridLeftPx(grid.left, u, ev, evCount),
		pxBeat, quant, u, ev, evCount);
	if (outTick) *outTick = tick;
	return 1;
}

int ScStaffHitNote(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int isFm, CPoint pt, int* outTrack)
{
	if (outTrack) *outTrack = -1;
	if (!grid.PtInRect(pt)) return -1;
	if (pt.y < ScStaffGridBodyTop(grid.top, u)) return -1;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	int yCursor = ScStaffGridBodyTop(grid.top, u) - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[tr]) continue;
		if (pt.y < rowTop || pt.y >= rowTop + rowH) continue;
		if (outTrack) *outTrack = tr;
		const int staffTop = ScStaffRowStaffTop(u, tr, rowTop);
		const int noteTop = ScStaffRowNoteAreaTop(u, tr, rowTop);
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
			int note = isRest ? 0 : ScStaffEvWrittenMidi(ev, evCount, e, isFm, tr);
			int x0 = ScStaffTickToX(e.tick, u->scrollX, ScStaffGridLeftPx(grid.left, u, ev, evCount), pxBeat, u, ev, evCount);
			int y = isRest ? (staffTop + 8 + ScStaffLineGap(u) * 2)
				: ScStaffMidiNoteYTrack(u, tr, staffTop, note, e.tick, ev, evCount);
			CRect head(x0 - 2, y - 8, x0 + 16, y + 10);
			if (head.PtInRect(pt)) return i;
		}
		return -2;
	}
	return -1;
}

int ScStaffIsStaffMarkKind(uint8_t kind, int isFm)
{
	(void)isFm;
	if (kind == SC_EV_JUMP_MARK || kind == SC_EV_FM_JUMP
		|| kind == SC_EV_FM_LOOP_START || kind == SC_EV_FM_LOOP_END)
		return 1;
	if (kind == SC_EV_PEDAL_ON || kind == SC_EV_PEDAL_OFF)
		return 1;
	if (kind == SC_EV_OTTAVA || kind == SC_EV_OTTAVA_END)
		return 1;
	if (kind == SC_EV_SLUR_START || kind == SC_EV_SLUR_END
		|| kind == SC_EV_CRESC || kind == SC_EV_DIM)
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
	if (pt.y < ScStaffGridBodyTop(grid.top, u)) return -1;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = ScStaffGridLeftPx(grid.left, u, ev, evCount);
	int yCursor = ScStaffGridBodyTop(grid.top, u) - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[tr]) continue;
		const int noteTop = ScStaffRowNoteAreaTop(u, tr, rowTop);
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
			const int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat, u, ev, evCount) + stack * 12;
			CRect hr;
			if (e.kind == SC_EV_FM_LOOP_START)
				hr.SetRect(x - 6, y0 - 8, x + 48, y1 + 8);
			else if (e.kind == SC_EV_FM_LOOP_END)
				hr.SetRect(x - 20, y0 - 8, x + 28, y1 + 8);
			else if (e.kind == SC_EV_PEDAL_ON || e.kind == SC_EV_PEDAL_OFF)
				hr.SetRect(x - 4, y1 - 6, x + 32, y1 + 22);
			else if (e.kind == SC_EV_OTTAVA || e.kind == SC_EV_OTTAVA_END) {
				const int oct = (e.kind == SC_EV_OTTAVA_END) ? 0 : (int)(int8_t)e.a;
				const int above = (oct >= 0);
				const int ly = above ? (y0 - 18) : (y1 - 2);
				hr.SetRect(x - 4, ly, x + 52, ly + 22);
			} else
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
	if (pt.y < ScStaffGridBodyTop(grid.top, u)) return 0;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = ScStaffGridLeftPx(grid.left, u, ev, evCount);
	int yCursor = ScStaffGridBodyTop(grid.top, u) - u->scrollY;
	for (int t = 0; t < u->trackCount; t++) {
		const int rowH = ScStaffRowH(u, t);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[t]) continue;
		const int noteTop = ScStaffRowNoteAreaTop(u, tr, rowTop);
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
			const int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat, u, ev, evCount) + stack * 12;
			CRect hr;
			if (e.kind == SC_EV_FM_LOOP_START)
				hr.SetRect(x - 6, y0 - 8, x + 48, y1 + 8);
			else if (e.kind == SC_EV_FM_LOOP_END)
				hr.SetRect(x - 20, y0 - 8, x + 28, y1 + 8);
			else if (e.kind == SC_EV_PEDAL_ON || e.kind == SC_EV_PEDAL_OFF)
				hr.SetRect(x - 4, y1 - 6, x + 32, y1 + 22);
			else if (e.kind == SC_EV_OTTAVA || e.kind == SC_EV_OTTAVA_END) {
				const int oct = (e.kind == SC_EV_OTTAVA_END) ? 0 : (int)(int8_t)e.a;
				const int above = (oct >= 0);
				const int ly = above ? (y0 - 18) : (y1 - 2);
				hr.SetRect(x - 4, ly, x + 52, ly + 22);
			} else
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
	if (pt.y < ScStaffGridBodyTop(grid.top, u)) return -1;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = ScStaffGridLeftPx(grid.left, u, ev, evCount);
	int yCursor = ScStaffGridBodyTop(grid.top, u) - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[tr]) continue;
		const int staffTop = ScStaffRowStaffTop(u, tr, rowTop);
		const int noteTop = ScStaffRowNoteAreaTop(u, tr, rowTop);
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
				_snwprintf_s(label, _TRUNCATE,
					(e.b == 0) ? L"%s @%u" : L"%s Voice %u", tpos, (unsigned)e.a);
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
			} else if (e.kind == SC_EV_OTTAVA) {
				lane = 0;
				_snwprintf_s(label, _TRUNCATE, L"%s %s", tpos, ScStaffOttavaLabel((int)(int8_t)e.a));
			} else if (e.kind == SC_EV_OTTAVA_END) {
				lane = 0;
				_snwprintf_s(label, _TRUNCATE, L"%s loco", tpos);
			} else if (!isFm && (e.kind == SC_EV_VOL || e.kind == SC_EV_PAN || e.kind == SC_EV_VELO)) {
				lane = 1;
				_snwprintf_s(label, _TRUNCATE, L"%s X", tpos);
			} else
				continue;
			int x = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
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
	if (pt.y < ScStaffGridBodyTop(grid.top, u)) return 0;
	int yCursor = ScStaffGridBodyTop(grid.top, u) - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[tr]) continue;
		const int noteTop = ScStaffRowNoteAreaTop(u, tr, rowTop);
		/* Tone/Exc gauge chips only — not CC bands below the gauge. */
		if (pt.y >= rowTop + SC_NAME_BAND_H && pt.y < rowTop + SC_PART_GAUGE_H) {
			if (outTrack) *outTrack = tr;
			return 1;
		}
	}
	return 0;
}

int ScStaffMidiChInUse(const ScMidiDoc* d, const ScEvent* ev, int evCount, int ch)
{
	if (ch < 0 || ch >= SC_MIDI_CH) return 0;
	for (int i = 0; i < evCount; i++)
		if (ev[i].ch == ch) return 1;
	if (!d) return 0;
	if (d->trackPart[ch] != 0xFF) return 1;
	if (ch < 32) {
		if (d->bind.vstPath[ch][0]) return 1;
		if (d->bind.vstCompLen[ch] > 0) return 1;
		if (d->bind.vstProg[ch] || d->bind.vstBankMsb[ch] || d->bind.vstBankLsb[ch]) return 1;
	}
	return 0;
}

int ScStaffFmChInUse(const ScFmDoc* d, const ScEvent* ev, int evCount, int ch)
{
	if (ch < 0) return 0;
	for (int i = 0; i < evCount; i++) {
		if (ev[i].ch == ch) return 1;
	}
	if (!d || ch >= SC_FM_TOTAL) return 0;
	for (int i = 0; i < evCount; i++) {
		const ScEvent& e = ev[i];
		if (e.kind == SC_EV_FM_VOICE && e.ch == ch) return 1;
	}
	if (ch >= SC_FM_CH && ch < SC_FM_TOTAL) {
		const int slot = ch - SC_FM_CH;
		if (slot >= 0 && slot < SC_FM_MISAO && d->pcmRelPath[slot][0]) return 1;
	}
	return 0;
}

void ScStaffAutoEnableUsedParts(ScStaffUi* u, const ScMidiDoc* md, const ScFmDoc* fd,
	const ScEvent* ev, int evCount)
{
	if (!u) return;
	for (int i = 0; i < u->trackCount; i++)
		u->visible[i] = 0;
	if (u->isFmScore && fd) {
		for (int ch = 0; ch < u->trackCount; ch++)
			if (ScStaffFmChInUse(fd, ev, evCount, ch))
				u->visible[ch] = 1;
	} else if (md) {
		for (int ch = 0; ch < u->trackCount; ch++)
			if (ScStaffMidiChInUse(md, ev, evCount, ch))
				u->visible[ch] = 1;
	}
	int any = 0;
	for (int i = 0; i < u->trackCount; i++)
		if (u->visible[i]) any = 1;
	if (!any && u->trackCount > 0)
		u->visible[0] = 1;
}

void ScStaffApplyPartMask(ScStaffUi* u, unsigned partMask)
{
	if (!u || !partMask) return;
	for (int i = 0; i < u->trackCount && i < 32; i++)
		u->visible[i] = (partMask >> i) & 1u;
}

unsigned ScStaffPackPartMask(const ScStaffUi* u)
{
	unsigned m = 0;
	if (!u) return 0;
	for (int i = 0; i < u->trackCount && i < 32; i++)
		if (u->visible[i]) m |= (1u << i);
	return m;
}

void ScStaffRefreshPartEnabled(ScStaffUi* u, const ScMidiDoc* md, const ScFmDoc* fd,
	const ScEvent* ev, int evCount, unsigned savedMask)
{
	if (!u) return;
	if (savedMask)
		ScStaffApplyPartMask(u, savedMask);
	else
		ScStaffAutoEnableUsedParts(u, md, fd, ev, evCount);
}

int ScStaffIsExtendedChannelView(const ScStaffUi* u)
{
	if (!u) return 0;
	for (int i = 16; i < u->trackCount; i++)
		if (u->visible[i]) return 1;
	return 0;
}

void ScStaffSetChannelView16(ScStaffUi* u)
{
	if (!u) return;
	for (int i = 0; i < u->trackCount; i++)
		u->visible[i] = (i < 16) ? 1 : 0;
}

void ScStaffSetChannelViewAll(ScStaffUi* u)
{
	if (!u) return;
	for (int i = 0; i < u->trackCount; i++)
		u->visible[i] = 1;
}

int ScStaffHitPartEnable(const CRect& trackRc, const ScStaffUi* u, CPoint pt, int* outTrack)
{
	if (outTrack) *outTrack = -1;
	if (!u || !trackRc.PtInRect(pt)) return -1;
	const int tr = ScStaffHitTrack(trackRc, u, pt);
	if (tr < 0) return -1;
	const int rowTop = ScStaffTrackRowTop(trackRc, u, tr);
	if (rowTop < 0) return -1;
	CRect enRc(trackRc.left + 2, rowTop + 2, trackRc.left + SC_PART_ENABLE_W, rowTop + 16);
	if (!enRc.PtInRect(pt)) return -1;
	if (outTrack) *outTrack = tr;
	return tr;
}

int ScStaffHitToneExcLane(const CRect& grid, const ScStaffUi* u, CPoint pt, int* outTrack, int* outLane,
	const CRect* trackCol)
{
	if (outTrack) *outTrack = -1;
	if (outLane) *outLane = -1;
	if (!u) return -1;
	if (trackCol && trackCol->Width() > 0 && trackCol->PtInRect(pt)) {
		const int tr = ScStaffHitTrack(*trackCol, u, pt);
		if (tr < 0) return -1;
		const int rowTop = ScStaffTrackRowTop(*trackCol, u, tr);
		if (rowTop < 0) return -1;
		const int gy = pt.y - rowTop;
		if (gy < SC_NAME_BAND_H || gy >= SC_PART_GAUGE_H) return -1;
		const int lane = (gy < SC_NAME_BAND_H + SC_CTRL_LANE_H) ? 0 : 1;
		if (outTrack) *outTrack = tr;
		if (outLane) *outLane = lane;
		return lane;
	}
	if (!grid.PtInRect(pt) || pt.y < ScStaffGridBodyTop(grid.top, u)) return -1;
	int yCursor = ScStaffGridBodyTop(grid.top, u) - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[tr]) continue;
		const int noteTop = ScStaffRowNoteAreaTop(u, tr, rowTop);
		if (pt.y < rowTop + SC_NAME_BAND_H || pt.y >= noteTop) continue;
		const int lane = (pt.y < rowTop + SC_NAME_BAND_H + SC_CTRL_LANE_H) ? 0 : 1;
		if (outTrack) *outTrack = tr;
		if (outLane) *outLane = lane;
		return lane;
	}
	return -1;
}

uint32_t ScStaffMarkerTickAtGridX(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount, int x)
{
	if (!u) return 0;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	return ScStaffXToTick(x, u->scrollX, ScStaffGridLeftPx(grid.left, u, ev, evCount), pxBeat,
		ScStaffPlaceQuant(u), u, ev, evCount);
}

int ScStaffSnapMarkerToToneExcClick(const CRect& grid, const CRect& trackCol,
	ScStaffUi* u, const ScEvent* ev, int evCount, CPoint pt, int* outTrack)
{
	if (!u) return 0;
	int tr = -1;
	if (ScStaffHitToneExcLane(grid, u, pt, &tr, NULL, &trackCol) < 0) return 0;
	int xTick = pt.x;
	if (trackCol.Width() > 0 && trackCol.PtInRect(pt) && !grid.PtInRect(pt)) {
		const int rel = pt.x - trackCol.left;
		const int w = max(1, trackCol.Width());
		xTick = grid.left + (rel * max(1, grid.Width())) / w;
	}
	u->markerTick = ScStaffMarkerTickAtGridX(grid, u, ev, evCount, xTick);
	u->transportMode = 0;
	if (outTrack) *outTrack = tr;
	return 1;
}

static int ScPushEv(ScEvent* ev, int* count, uint32_t tick, uint8_t ch, uint8_t kind, uint8_t a, uint8_t b, uint8_t c, uint16_t dur);
static void ApplyOneStripMidi(ScMidiDoc* d, int track, int kind, const uint8_t* strip, const ScStaffUi* u);
static int ScStaffStripKindCc(int kind);

static void ScStaffFillBandBuf(uint8_t* out, int colMax, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int track, int stripKind, int isTempo, int defBpm)
{
	if (!out || !u || colMax <= 0) return;
	uint8_t def = 100;
	if (isTempo) {
		def = (uint8_t)max(20, min(255, defBpm));
	} else if (ScStaffStripKindBipolar(stripKind)) {
		def = 64;
	}
	for (int i = 0; i < colMax && i < SC_STRIP_COLS_MAX; i++) out[i] = def;
	for (int i = 0; i < evCount; i++) {
		const ScEvent& e = ev[i];
		if (isTempo) {
			if (e.kind != SC_EV_TEMPO && e.kind != SC_EV_FM_TEMPO) continue;
		} else if ((int)e.ch != track) {
			continue;
		}
		int col = ScStaffStripTickToCol(u, e.tick);
		if (col < 0 || col >= colMax) continue;
		if (isTempo) {
			int tval = e.a | (e.b << 8);
			out[col] = (uint8_t)max(20, min(255, ScStaffBpmFromTempoT(tval)));
		} else if (ScStaffStripEventMatches(stripKind, e)) {
			out[col] = ScStaffStripEventValue(stripKind, e);
		}
	}
	/* Hold-last-value forward fill (automation lane). */
	uint8_t hold = def;
	for (int c = 0; c < colMax && c < SC_STRIP_COLS_MAX; c++) {
		for (int i = 0; i < evCount; i++) {
			const ScEvent& e = ev[i];
			if (isTempo) {
				if (e.kind != SC_EV_TEMPO && e.kind != SC_EV_FM_TEMPO) continue;
			} else if ((int)e.ch != track) continue;
			int ec = ScStaffStripTickToCol(u, e.tick);
			if (ec != c) continue;
			if (isTempo) {
				int tval = e.a | (e.b << 8);
				hold = (uint8_t)max(20, min(255, ScStaffBpmFromTempoT(tval)));
			} else if (ScStaffStripEventMatches(stripKind, e)) {
				hold = ScStaffStripEventValue(stripKind, e);
			}
			break;
		}
		out[c] = hold;
	}
}

void ScStaffEnsureGlobalTempoFromDoc(ScStaffUi* u, const ScEvent* ev, int evCount, int defBpm)
{
	if (!u) return;
	ScStaffNormalizeStripStep(u);
	const int colMax = ScStaffStripColCount(u);
	ScStaffFillBandBuf(u->globalTempoStrip, colMax, u, ev, evCount, 0, 0, 1, defBpm);
}

void ScStaffEnsurePartBandFromDoc(const ScStaffUi* u, const ScEvent* ev, int evCount, int track, int band, uint8_t* out)
{
	if (!u || !out || band < 0 || band >= SC_PBAND_KIND_COUNT) return;
	const int colMax = ScStaffStripColCount(u);
	ScStaffFillBandBuf(out, colMax, u, ev, evCount, track, ScStaffPartBandToStripKind(band), 0, 120);
}

void ScStaffApplyGlobalTempoToDoc(ScMidiDoc* d, const ScStaffUi* u)
{
	if (!d || !u) return;
	const int colMax = ScStaffStripColCount(u);
	int w = 0;
	for (int i = 0; i < d->evCount; i++) {
		if (d->ev[i].kind == SC_EV_TEMPO) continue;
		d->ev[w++] = d->ev[i];
	}
	d->evCount = w;
	uint8_t last = 0xFF;
	for (int c = 0; c < colMax; c++) {
		uint8_t bpm = u->globalTempoStrip[c];
		if (bpm == last) continue;
		last = bpm;
		if (bpm < 20) bpm = 120;
		int t = ScStaffTempoTFromBpm((int)bpm);
		ScPushEv(d->ev, &d->evCount, ScStaffStripColToTick(u, c), 0, SC_EV_TEMPO,
			(uint8_t)(t & 0xFF), (uint8_t)((t >> 8) & 0xFF), 0, 0);
	}
	if (colMax > 0) {
		int t = ScStaffTempoTFromBpm((int)max(20, (int)u->globalTempoStrip[0]));
		d->tempoT = t;
	}
}

void ScStaffApplyGlobalTempoToDocFm(ScFmDoc* d, const ScStaffUi* u)
{
	if (!d || !u) return;
	const int colMax = ScStaffStripColCount(u);
	int w = 0;
	for (int i = 0; i < d->evCount; i++) {
		if (d->ev[i].kind == SC_EV_FM_TEMPO) continue;
		d->ev[w++] = d->ev[i];
	}
	d->evCount = w;
	uint8_t last = 0xFF;
	for (int c = 0; c < colMax; c++) {
		uint8_t bpm = u->globalTempoStrip[c];
		if (bpm == last) continue;
		last = bpm;
		if (bpm < 20) bpm = 120;
		int t = ScStaffTempoTFromBpm((int)bpm);
		ScPushEv(d->ev, &d->evCount, ScStaffStripColToTick(u, c), 0, SC_EV_FM_TEMPO,
			(uint8_t)(t & 0xFF), (uint8_t)((t >> 8) & 0xFF), 0, 0);
	}
	if (colMax > 0) {
		int t = ScStaffTempoTFromBpm((int)max(20, (int)u->globalTempoStrip[0]));
		d->tempoT = t;
	}
}

void ScStaffApplyPartBandToDoc(ScMidiDoc* d, int track, int band, const uint8_t* strip, const ScStaffUi* u)
{
	if (!d || !u || !strip || track < 0 || band < 0 || band >= SC_PBAND_KIND_COUNT) return;
	ApplyOneStripMidi(d, track, ScStaffPartBandToStripKind(band), strip, u);
}

void ScStaffApplyPartBandToDocFm(ScFmDoc* d, int track, int band, const uint8_t* strip, const ScStaffUi* u)
{
	if (!d || !u || !strip || track < 0 || band < 0 || band >= SC_PBAND_KIND_COUNT) return;
	const int kind = ScStaffPartBandToStripKind(band);
	const int colMax = ScStaffStripColCount(u);
	if (kind == SC_STRIP_GATE) {
		for (int i = 0; i < d->evCount; i++) {
			if (d->ev[i].ch != (uint8_t)track || d->ev[i].kind != SC_EV_FM_NOTE) continue;
			int col = ScStaffStripTickToCol(u, d->ev[i].tick);
			if (col >= 0 && col < colMax) {
				int g = strip[col];
				if (g < 1) g = 1;
				if (g > 100) g = 100;
				d->ev[i].c = (uint8_t)g;
			}
		}
		return;
	}
	if (kind == SC_STRIP_PAN) return;
	int w = 0;
	for (int i = 0; i < d->evCount; i++) {
		if ((int)d->ev[i].ch != track) continue;
		if (ScStaffStripEventMatches(kind, d->ev[i])) continue;
		d->ev[w++] = d->ev[i];
	}
	d->evCount = w;
	const int cc = ScStaffStripKindCc(kind);
	uint8_t last = 0xFF;
	for (int c = 0; c < colMax; c++) {
		uint8_t v = strip[c];
		if (v == last) continue;
		last = v;
		const uint32_t tick = ScStaffStripColToTick(u, c);
		if (kind == SC_STRIP_VEL) {
			ScPushEv(d->ev, &d->evCount, tick, (uint8_t)track, SC_EV_VELO, v, 0, 0, 0);
		} else if (kind == SC_STRIP_PITCH) {
			ScPushEv(d->ev, &d->evCount, tick, (uint8_t)track, SC_EV_FM_PITCH, v, 0, 0, 0);
		} else if (kind == SC_STRIP_VOL) {
			ScPushEv(d->ev, &d->evCount, tick, (uint8_t)track, SC_EV_FM_VOL, v, 0, 0, 0);
		} else if (cc >= 0) {
			ScPushEv(d->ev, &d->evCount, tick, (uint8_t)track, SC_EV_CC, (uint8_t)cc, v, 0, 0);
		}
	}
}

static void ScStaffPaintBandCurve(CDC& dc, const CRect& lane, const ScStaffUi* u, const ScEvent* ev, int evCount,
	const uint8_t* data, int colMax, int kind, int isTempo, const wchar_t* title, const wchar_t* mmlHint)
{
	if (lane.Height() < 8 || !data || !u) return;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = ScStaffGridLeftPx(lane.left, u, ev, evCount);
	const int hdrH = min(13, max(10, lane.Height() / 4));
	const int step = ScStaffStripStepTicks(u);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(60, 60, 80));
	wchar_t hdr[128];
	if (mmlHint && mmlHint[0])
		_snwprintf_s(hdr, _TRUNCATE, L"%s [%s]", title, mmlHint);
	else
		wcsncpy_s(hdr, title, _TRUNCATE);
	dc.TextOut(gridLeft + 2, lane.top + 1, hdr);
	const int top = lane.top + hdrH;
	const int h = max(8, lane.bottom - top - 1);
	const int bipolar = (!isTempo && ScStaffStripKindBipolar(kind));
	dc.SetTextColor(RGB(110, 110, 130));
	if (isTempo) {
		dc.TextOut(lane.left + 2, top, L"300");
		if (h >= 24) dc.TextOut(lane.left + 2, top + h / 2 - 5, L"120");
		dc.TextOut(lane.left + 2, top + h - 10, L"40");
	} else if (bipolar) {
		dc.TextOut(lane.left + 2, top, L"127");
		if (h >= 24) dc.TextOut(lane.left + 2, top + h / 2 - 5, L"64");
		dc.TextOut(lane.left + 2, top + h - 10, L"0");
		if (h > 4)
			dc.FillSolidRect(gridLeft, top + h / 2, lane.right - gridLeft, 1, RGB(170, 170, 185));
	} else {
		dc.TextOut(lane.left + 2, top, L"127");
		if (h >= 24) dc.TextOut(lane.left + 2, top + h / 2 - 5, L"64");
		dc.TextOut(lane.left + 2, top + h - 10, L"0");
	}
	uint32_t tVis0 = ScStaffXToTick(gridLeft, u->scrollX, gridLeft, pxBeat, step);
	uint32_t tVis1 = ScStaffXToTick(lane.right + pxBeat, u->scrollX, gridLeft, pxBeat, step);
	int c0 = ScStaffStripTickToCol(u, tVis0);
	int c1 = ScStaffStripTickToCol(u, tVis1);
	if (c0 < 0) c0 = 0;
	if (c1 >= colMax) c1 = colMax - 1;
	COLORREF col = isTempo ? RGB(200, 80, 80) :
		(kind == SC_STRIP_REVERB) ? RGB(100, 140, 200) :
		(kind == SC_STRIP_CHORUS) ? RGB(140, 100, 190) :
		(kind == SC_STRIP_DELAY) ? RGB(90, 160, 140) :
		(kind == SC_STRIP_PAN) ? RGB(160, 100, 180) : RGB(90, 140, 210);
	enum { kPtsMax = 512 };
	POINT pts[kPtsMax];
	int npt = 0;
	for (int c = c0; c <= c1; c++) {
		uint32_t tick = ScStaffStripColToTick(u, c);
		int x = ScStaffTickToX(tick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		if (x < gridLeft - 2 || x > lane.right + 2) continue;
		int v = data[c];
		int bh;
		if (isTempo) {
			int bpm = max(40, min(300, (int)v));
			bh = (h * (bpm - 40)) / 260;
		} else {
			bh = (h * v) / 127;
		}
		int nextX = ScStaffTickToX(tick + (uint32_t)step, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		int bw = max(2, nextX - x - 1);
		dc.FillSolidRect(x, top + h - bh, bw, bh, col);
		if (npt < kPtsMax) {
			pts[npt].x = x + bw / 2;
			pts[npt].y = top + h - bh;
			npt++;
		}
	}
	if (u->stripDraw == SC_STRIP_DRAW_CURVE && npt >= 2) {
		CPen pen(PS_SOLID, 2, isTempo ? RGB(160, 50, 50) : RGB(40, 80, 160));
		CPen* op = dc.SelectObject(&pen);
		dc.Polyline(pts, npt);
		dc.SelectObject(op);
	}
	dc.FillSolidRect(gridLeft, top, 1, h, RGB(160, 160, 170));
	dc.FillSolidRect(gridLeft, top + h, lane.right - gridLeft, 1, RGB(160, 160, 170));
	dc.FillSolidRect(lane.left, lane.bottom - 1, lane.Width(), 1, RGB(190, 190, 200));
}

int ScStaffHasBandEditing(const ScStaffUi* u)
{
	if (!u) return 0;
	if (u->globalTempoBandOn) return 1;
	if (u->stripCount >= 1) return 1;
	for (int p = 0; p < u->trackCount && p < 32; p++)
		if (u->partBandMask[p]) return 1;
	return 0;
}

int ScStaffBandSegmentEndCol(const uint8_t* buf, int colMax, int colStart)
{
	if (!buf || colMax <= 0) return 0;
	if (colStart < 0) colStart = 0;
	if (colStart >= colMax) colStart = colMax - 1;
	const uint8_t segVal = buf[colStart];
	int colEnd = colStart;
	for (int c = colStart + 1; c < colMax; c++) {
		if (buf[c] != segVal) break;
		colEnd = c;
	}
	return colEnd;
}

void ScStaffBandFillHoldSegment(uint8_t* buf, int colMax, int colStart, int newVal, int vmin, int vmax)
{
	if (!buf || colMax <= 0) return;
	if (colStart < 0) colStart = 0;
	if (colStart >= colMax) colStart = colMax - 1;
	if (vmin > vmax) { int t = vmin; vmin = vmax; vmax = t; }
	if (newVal < vmin) newVal = vmin;
	if (newVal > vmax) newVal = vmax;
	const int colEnd = ScStaffBandSegmentEndCol(buf, colMax, colStart);
	for (int c = colStart; c <= colEnd; c++)
		buf[c] = (uint8_t)newVal;
}

void ScStaffApplyTempoAtMarker(ScMidiDoc* d, ScStaffUi* u, const ScEvent* ev, int evCount, uint32_t markerTick, int bpm)
{
	if (!d || !u) return;
	ScStaffEnsureGlobalTempoFromDoc(u, ev, evCount, ScStaffBpmFromTempoT(d->tempoT));
	const int colMax = ScStaffStripColCount(u);
	int col = ScStaffStripTickToCol(u, markerTick);
	if (col < 0) col = 0;
	if (col >= colMax) col = colMax - 1;
	ScStaffBandFillHoldSegment(u->globalTempoStrip, colMax, col, bpm, 40, 255);
	ScStaffApplyGlobalTempoToDoc(d, u);
}

void ScStaffApplyTempoAtMarkerFm(ScFmDoc* d, ScStaffUi* u, const ScEvent* ev, int evCount, uint32_t markerTick, int bpm)
{
	if (!d || !u) return;
	ScStaffEnsureGlobalTempoFromDoc(u, ev, evCount, ScStaffBpmFromTempoT(d->tempoT));
	const int colMax = ScStaffStripColCount(u);
	int col = ScStaffStripTickToCol(u, markerTick);
	if (col < 0) col = 0;
	if (col >= colMax) col = colMax - 1;
	ScStaffBandFillHoldSegment(u->globalTempoStrip, colMax, col, bpm, 40, 255);
	ScStaffApplyGlobalTempoToDocFm(d, u);
}

void ScStaffBandFillRange(uint8_t* buf, int colMax, int c0, int v0, int c1, int v1, int vmin, int vmax)
{
	if (!buf || colMax <= 0) return;
	if (vmin > vmax) { int t = vmin; vmin = vmax; vmax = t; }
	if (c0 > c1) { int t = c0; c0 = c1; c1 = t; t = v0; v0 = v1; v1 = t; }
	if (c0 < 0) c0 = 0;
	if (c1 >= colMax) c1 = colMax - 1;
	if (c0 == c1) {
		int v = v0;
		if (v < vmin) v = vmin;
		if (v > vmax) v = vmax;
		buf[c0] = (uint8_t)v;
		return;
	}
	for (int c = c0; c <= c1; c++) {
		int v = v0 + (v1 - v0) * (c - c0) / (c1 - c0);
		if (v < vmin) v = vmin;
		if (v > vmax) v = vmax;
		buf[c] = (uint8_t)v;
	}
}

void ScStaffBandFillCurve(uint8_t* buf, int colMax, int c0, int v0, int c1, int v1, int vmin, int vmax)
{
	if (!buf || colMax <= 0) return;
	if (vmin > vmax) { int t = vmin; vmin = vmax; vmax = t; }
	if (c0 > c1) { int t = c0; c0 = c1; c1 = t; t = v0; v0 = v1; v1 = t; }
	if (c0 < 0) c0 = 0;
	if (c1 >= colMax) c1 = colMax - 1;
	if (c0 == c1) {
		int v = v0;
		if (v < vmin) v = vmin;
		if (v > vmax) v = vmax;
		buf[c0] = (uint8_t)v;
		return;
	}
	const int span = c1 - c0;
	for (int c = c0; c <= c1; c++) {
		const double t = (double)(c - c0) / (double)span;
		const double te = t * t * (3.0 - 2.0 * t);
		int v = v0 + (int)((v1 - v0) * te + 0.5);
		if (v < vmin) v = vmin;
		if (v > vmax) v = vmax;
		buf[c] = (uint8_t)v;
	}
}

void ScStaffBandDragStroke(ScStaffUi* u, uint8_t* buf, int anchorCol, int anchorVal, int col, int val, int isTempo)
{
	if (!u || !buf) return;
	const int colMax = ScStaffStripColCount(u);
	const int vmin = isTempo ? 40 : 0;
	const int vmax = isTempo ? 255 : 127;
	if (u->stripDraw == SC_STRIP_DRAW_LINE && anchorCol >= 0) {
		ScStaffBandFillRange(buf, colMax, anchorCol, anchorVal, col, val, vmin, vmax);
		return;
	}
	if (u->stripDraw == SC_STRIP_DRAW_CURVE && anchorCol >= 0) {
		ScStaffBandFillCurve(buf, colMax, anchorCol, anchorVal, col, val, vmin, vmax);
		return;
	}
	int c0 = min(anchorCol, col), c1 = max(anchorCol, col);
	if (anchorCol < 0) c0 = c1 = col;
	for (int c = c0; c <= c1; c++) {
		int v = val;
		if (v < vmin) v = vmin;
		if (v > vmax) v = vmax;
		buf[c] = (uint8_t)v;
	}
}

static int ScStaffBandValFromPt(const CRect& lane, const ScStaffUi* u, const ScEvent* ev, int evCount,
	CPoint pt, int bipolar, int isTempo)
{
	const int gridLeft = ScStaffGridLeftPx(lane.left, u, ev, evCount);
	const int hdrH = min(13, max(10, lane.Height() / 4));
	const int top = lane.top + hdrH;
	const int h = max(8, lane.bottom - top - 1);
	if (h < 1 || pt.x < gridLeft) return -1;
	CPoint p = pt;
	if (p.y < top) p.y = top;
	if (p.y > top + h) p.y = top + h;
	if (isTempo) {
		int bpm = 40 + ((top + h - p.y) * 260) / h;
		return max(40, min(300, bpm));
	}
	int v = ((top + h - p.y) * 127) / h;
	if (v < 0) v = 0;
	if (v > 127) v = 127;
	(void)bipolar;
	return v;
}

static int ScStaffPartBandLaneRect(const CRect& grid, const ScStaffUi* u, int track, int band, CRect* outLane)
{
	if (!u || !outLane || track < 0 || band < 0 || band >= SC_PBAND_KIND_COUNT) return 0;
	const int bodyTop = ScStaffGridBodyTop(grid.top, u);
	int yCursor = bodyTop - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (tr != track) continue;
		const int gaugeBot = rowTop + SC_PART_GAUGE_H;
		int yBand = gaugeBot;
		for (int b = 0; b < SC_PBAND_KIND_COUNT; b++) {
			if (!(u->partBandMask[track] & (1 << b))) continue;
			const int bh = ScStaffPartBandHeightPx(u, track, b);
			if (b == band) {
				outLane->SetRect(grid.left, yBand, grid.right, yBand + bh);
				return 1;
			}
			yBand += bh;
		}
	}
	return 0;
}

int ScStaffBandColValFromPt(const CRect& grid, const ScStaffUi* u, int track, int band, int isTempo, CPoint pt, int* outCol, int* outVal,
	const ScEvent* ev, int evCount)
{
	if (!u || !outCol || !outVal) return 0;
	CRect lane;
	if (isTempo) {
		const int gh = ScStaffGlobalTempoBandH(u);
		if (gh <= 0) return 0;
		lane.SetRect(grid.left, grid.top, grid.right, grid.top + gh);
	} else {
		if (!ScStaffPartBandLaneRect(grid, u, track, band, &lane)) return 0;
	}
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int step = ScStaffStripStepTicks(u);
	const int gridLeft = ScStaffGridLeftPx(lane.left, u, ev, evCount);
	if (pt.x < gridLeft) return 0;
	uint32_t tick = ScStaffXToTick(pt.x, u->scrollX, gridLeft, pxBeat, step, u, ev, evCount);
	int col = ScStaffStripTickToCol(u, tick);
	if (col < 0) col = 0;
	const int kind = isTempo ? 0 : ScStaffPartBandToStripKind(band);
	int val = ScStaffBandValFromPt(lane, u, ev, evCount, pt, ScStaffStripKindBipolar(kind), isTempo);
	if (val < 0) return 0;
	*outCol = col;
	*outVal = val;
	return 1;
}

int ScStaffHitGlobalTempoBand(const CRect& grid, const ScStaffUi* u, CPoint pt, int* outCol, int* outBpm,
	const ScEvent* ev, int evCount)
{
	if (!u || !u->globalTempoBandOn) return 0;
	const int gh = ScStaffGlobalTempoBandH(u);
	if (gh <= 0 || !grid.PtInRect(pt) || pt.y < grid.top || pt.y >= grid.top + gh) return 0;
	CRect lane(grid.left, grid.top, grid.right, grid.top + gh);
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int step = ScStaffStripStepTicks(u);
	const int gridLeft = ScStaffGridLeftPx(lane.left, u, ev, evCount);
	if (pt.x < gridLeft) return 0;
	uint32_t tick = ScStaffXToTick(pt.x, u->scrollX, gridLeft, pxBeat, step, u, ev, evCount);
	int col = ScStaffStripTickToCol(u, tick);
	if (col < 0) col = 0;
	int bpm = ScStaffBandValFromPt(lane, u, ev, evCount, pt, 0, 1);
	if (bpm < 0 && col >= 0 && col < SC_STRIP_COLS_MAX)
		bpm = max(40, (int)u->globalTempoStrip[col]);
	if (bpm < 0) bpm = 120;
	if (outCol) *outCol = col;
	if (outBpm) *outBpm = bpm;
	return 1;
}

int ScStaffHitPartBand(const CRect& grid, const ScStaffUi* u, CPoint pt, int* outTrack, int* outBand, int* outCol, int* outVal,
	const ScEvent* ev, int evCount)
{
	if (!u || !grid.PtInRect(pt)) return 0;
	const int bodyTop = ScStaffGridBodyTop(grid.top, u);
	if (pt.y < bodyTop) return 0;
	int yCursor = bodyTop - u->scrollY;
	for (int tr = 0; tr < u->trackCount; tr++) {
		const int rowH = ScStaffRowH(u, tr);
		const int rowTop = yCursor;
		yCursor += rowH;
		if (!u->visible[tr]) continue;
		if (pt.y < rowTop || pt.y >= rowTop + rowH) continue;
		const int gaugeBot = rowTop + SC_PART_GAUGE_H;
		const int bandsBot = gaugeBot + ScStaffPartBandsH(u, tr);
		if (pt.y < gaugeBot || pt.y >= bandsBot) continue;
		int yBand = gaugeBot;
		for (int b = 0; b < SC_PBAND_KIND_COUNT; b++) {
			if (!(u->partBandMask[tr] & (1 << b))) continue;
			const int bh = ScStaffPartBandHeightPx(u, tr, b);
			if (pt.y >= yBand && pt.y < yBand + bh) {
				CRect lane(grid.left, yBand, grid.right, yBand + bh);
				const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
				const int step = ScStaffStripStepTicks(u);
				const int gridLeft = ScStaffGridLeftPx(lane.left, u, ev, evCount);
				if (pt.x < gridLeft) return 0;
				uint32_t tick = ScStaffXToTick(pt.x, u->scrollX, gridLeft, pxBeat, step, u, ev, evCount);
				int col = ScStaffStripTickToCol(u, tick);
				if (col < 0) col = 0;
				const int kind = ScStaffPartBandToStripKind(b);
				int val = ScStaffBandValFromPt(lane, u, ev, evCount, pt, ScStaffStripKindBipolar(kind), 0);
				if (val < 0) val = ScStaffStripKindBipolar(kind) ? 64 : 100;
				if (outTrack) *outTrack = tr;
				if (outBand) *outBand = b;
				if (outCol) *outCol = col;
				if (outVal) *outVal = val;
				return 1;
			}
			yBand += bh;
		}
	}
	return 0;
}

void ScStaffEnsureStripFromDoc(ScStaffUi* u, const ScEvent* ev, int evCount, int track)
{
	if (!u) return;
	ScStaffNormalizeStripStep(u);
	const int colMax = ScStaffStripColCount(u);
	for (int L = 0; L < SC_STRIP_LANES_MAX; L++)
		ScStaffFillBandBuf(u->strip[L], colMax, u, ev, evCount, track, u->stripKind[L], 0, 120);
}

int ScStaffHitStrip(const CRect& stripRc, const ScStaffUi* u, CPoint pt, int* outLane, int* outCol, int* outVal)
{
	if (!u || u->stripCount <= 0 || !stripRc.PtInRect(pt)) return 0;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int step = ScStaffStripStepTicks(u);
	const int colMax = ScStaffStripColCount(u);
	const int n = max(1, min(SC_STRIP_LANES_MAX, u->stripCount));
	const int yRel = pt.y - stripRc.top;
	int lane = n - 1;
	for (int L = 0; L < n; L++) {
		if (yRel < ScStaffStripLaneTopY(u, L) + ScStaffStripLaneHeightPx(u, L)) {
			lane = L;
			break;
		}
	}
	const int laneTop = stripRc.top + ScStaffStripLaneTopY(u, lane);
	const int laneH = ScStaffStripLaneHeightPx(u, lane);
	CRect lr(stripRc.left, laneTop, stripRc.right, laneTop + laneH);
	const int left = lr.left + ScStaffClefMarginPx(u, NULL, 0);
	const int top = lr.top + 18;
	const int h = max(12, laneH - 24);
	if (h < 1 || pt.x < left) return 0;
	uint32_t tick = ScStaffXToTick(pt.x, u->scrollX, left, pxBeat, step);
	int col = ScStaffStripTickToCol(u, tick);
	if (col < 0) col = 0;
	if (col >= colMax) col = colMax - 1;
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

static void ApplyOneStripMidi(ScMidiDoc* d, int track, int kind, const uint8_t* strip, const ScStaffUi* u)
{
	const int colMax = ScStaffStripColCount(u);
	if (kind == SC_STRIP_GATE) {
		for (int i = 0; i < d->evCount; i++) {
			if (d->ev[i].ch != (uint8_t)track) continue;
			if (d->ev[i].kind != SC_EV_NOTE) continue;
			int col = ScStaffStripTickToCol(u, d->ev[i].tick);
			if (col >= 0 && col < colMax) {
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
		if ((int)d->ev[i].ch != track) continue;
		if (ScStaffStripEventMatches(kind, d->ev[i])) continue;
		d->ev[w++] = d->ev[i];
	}
	d->evCount = w;
	const int cc = ScStaffStripKindCc(kind);
	uint8_t last = 0xFF;
	for (int c = 0; c < colMax; c++) {
		uint8_t v = strip[c];
		if (v == last) continue;
		last = v;
		const uint32_t tick = ScStaffStripColToTick(u, c);
		if (kind == SC_STRIP_VEL) {
			ScPushEv(d->ev, &d->evCount, tick, (uint8_t)track, SC_EV_VELO, v, 0, 0, 0);
		} else if (kind == SC_STRIP_PITCH) {
			ScPushEv(d->ev, &d->evCount, tick, (uint8_t)track, SC_EV_PITCH, v, 0, 0, 0);
		} else if (kind == SC_STRIP_VOL) {
			ScPushEv(d->ev, &d->evCount, tick, (uint8_t)track, SC_EV_VOL, v, 0, 0, 0);
		} else if (kind == SC_STRIP_PAN) {
			ScPushEv(d->ev, &d->evCount, tick, (uint8_t)track, SC_EV_PAN, v, 0, 0, 0);
		} else if (cc >= 0) {
			ScPushEv(d->ev, &d->evCount, tick, (uint8_t)track, SC_EV_CC, (uint8_t)cc, v, 0, 0);
		}
	}
}

void ScStaffApplyStripToDocMidi(ScMidiDoc* d, int track, const ScStaffUi* u)
{
	if (!d || !u || track < 0 || track >= SC_MIDI_CH || u->stripCount <= 0) return;
	for (int L = 0; L < u->stripCount && L < SC_STRIP_LANES_MAX; L++)
		ApplyOneStripMidi(d, track, u->stripKind[L], u->strip[L], u);
}

void ScStaffApplyStripToDocFm(ScFmDoc* d, int track, const ScStaffUi* u)
{
	if (!d || !u || track < 0 || track >= SC_FM_TOTAL || u->stripCount <= 0) return;
	const int colMax = ScStaffStripColCount(u);
	for (int L = 0; L < u->stripCount && L < SC_STRIP_LANES_MAX; L++) {
		const int kind = u->stripKind[L];
		const uint8_t* strip = u->strip[L];
		uint8_t dropKind = SC_EV_FM_VOL;
		if (kind == SC_STRIP_PITCH) dropKind = SC_EV_FM_PITCH;
		else if (kind == SC_STRIP_GATE) {
			for (int i = 0; i < d->evCount; i++) {
				if (d->ev[i].ch != (uint8_t)track) continue;
				if (d->ev[i].kind != SC_EV_FM_NOTE) continue;
				int col = ScStaffStripTickToCol(u, d->ev[i].tick);
				if (col >= 0 && col < colMax) {
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
		for (int c = 0; c < colMax; c++) {
			uint8_t v = strip[c];
			if (v == last) continue;
			last = v;
			ScPushEv(d->ev, &d->evCount, ScStaffStripColToTick(u, c), (uint8_t)track, dropKind, v, 0, 0, 0);
		}
	}
}

int ScStaffVisibleLaneStaffTop(const CRect& grid, const ScStaffUi* u, int track)
{
	if (!u) return -1;
	int y = ScStaffGridBodyTop(grid.top, u) - u->scrollY;
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
	int y = trackRc.top + ScStaffGridHeaderH(u) + 4 - u->scrollY;
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
	/* Temp score bakes must not block on resume-prompt (returns FALSE = deferred). */
	const int tempScore = (wcsstr(path, L"ogg_sasami_score") != NULL) ? 1 : 0;
	if (!tempScore && !OggPrepareResumeBeforePlayback(pl->pc[idx].fol))
		return 0;
	if (og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());
	return 1;
}

void ScStaffStopHostPreview(ScStaffUi* u)
{
	if (u) {
		u->previewActive = 0;
		u->markerSeekArmed = 0;
		u->previewOriginTick = 0;
		u->previewWavMode = 0;
	}
	extern int endflg;
	endflg = 1;
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->stop1();
}

int ScStaffCopyEventsFromMarker(const ScEvent* src, int srcN, uint32_t marker,
	ScEvent* dst, int dstMax, int* outN)
{
	if (!src || !dst || !outN || dstMax <= 0) return 0;
	*outN = 0;
	if (marker == 0) {
		for (int i = 0; i < srcN && *outN < dstMax; i++) {
			ScEvent t = src[i];
			t.seq = (uint32_t)(*outN);
			dst[(*outN)++] = t;
		}
		return 1;
	}

	enum { kCh = 32 };
	int lastQ[kCh];
	int lastTempo = -1, lastFmTempo = -1;
	int lastProg[kCh], lastBank[kCh], lastVol[kCh], lastPan[kCh], lastVelo[kCh], lastPitch[kCh];
	int lastFmVoice[kCh], lastFmVol[kCh], lastFmPitch[kCh];
	int pedal[kCh]; /* -1 unknown, 0 off, 1 on */
	int needQ[kCh];
	int loopSp[kCh];
	int loopIdx[kCh][SC_LOOP_NEST_MAX];
	for (int c = 0; c < kCh; c++) {
		lastQ[c] = lastProg[c] = lastBank[c] = lastVol[c] = lastPan[c] = lastVelo[c] = lastPitch[c] = -1;
		lastFmVoice[c] = lastFmVol[c] = lastFmPitch[c] = -1;
		pedal[c] = -1;
		needQ[c] = 0;
		loopSp[c] = 0;
	}

	for (int i = 0; i < srcN; i++) {
		const ScEvent& e = src[i];
		const int ch = (int)e.ch;
		if (ch < 0 || ch >= kCh) continue;
		if (e.tick >= marker) {
			if (e.kind == SC_EV_FM_JUMP) needQ[ch] = 1;
			continue;
		}
		switch (e.kind) {
		case SC_EV_JUMP_MARK: lastQ[ch] = i; break;
		case SC_EV_TEMPO: lastTempo = i; break;
		case SC_EV_FM_TEMPO: lastFmTempo = i; break;
		case SC_EV_PROG: lastProg[ch] = i; break;
		case SC_EV_BANK: lastBank[ch] = i; break;
		case SC_EV_VOL: lastVol[ch] = i; break;
		case SC_EV_PAN: lastPan[ch] = i; break;
		case SC_EV_VELO: lastVelo[ch] = i; break;
		case SC_EV_PITCH: lastPitch[ch] = i; break;
		case SC_EV_FM_VOICE: lastFmVoice[ch] = i; break;
		case SC_EV_FM_VOL: lastFmVol[ch] = i; break;
		case SC_EV_FM_PITCH: lastFmPitch[ch] = i; break;
		case SC_EV_PEDAL_ON: pedal[ch] = 1; break;
		case SC_EV_PEDAL_OFF: pedal[ch] = 0; break;
		case SC_EV_FM_LOOP_START:
			if (loopSp[ch] < SC_LOOP_NEST_MAX)
				loopIdx[ch][loopSp[ch]++] = i;
			break;
		case SC_EV_FM_LOOP_END:
			if (loopSp[ch] > 0) loopSp[ch]--;
			break;
		default: break;
		}
	}

	auto pushChase = [&](int idx) -> int {
		if (idx < 0 || idx >= srcN || *outN >= dstMax) return 0;
		ScEvent t = src[idx];
		t.tick = 0;
		t.seq = (uint32_t)(*outN);
		dst[(*outN)++] = t;
		return 1;
	};

	if (lastTempo >= 0) pushChase(lastTempo);
	if (lastFmTempo >= 0) pushChase(lastFmTempo);
	for (int ch = 0; ch < kCh; ch++) {
		if (lastProg[ch] >= 0) pushChase(lastProg[ch]);
		if (lastBank[ch] >= 0) pushChase(lastBank[ch]);
		if (lastVol[ch] >= 0) pushChase(lastVol[ch]);
		if (lastPan[ch] >= 0) pushChase(lastPan[ch]);
		if (lastVelo[ch] >= 0) pushChase(lastVelo[ch]);
		if (lastPitch[ch] >= 0) pushChase(lastPitch[ch]);
		if (lastFmVoice[ch] >= 0) pushChase(lastFmVoice[ch]);
		if (lastFmVol[ch] >= 0) pushChase(lastFmVol[ch]);
		if (lastFmPitch[ch] >= 0) pushChase(lastFmPitch[ch]);
		if (pedal[ch] == 1) {
			ScEvent t = {};
			t.tick = 0;
			t.ch = (uint8_t)ch;
			t.kind = SC_EV_PEDAL_ON;
			t.seq = (uint32_t)(*outN);
			if (*outN < dstMax) dst[(*outN)++] = t;
		}
		/* Q before red bar is required so J after the bar still lands. */
		if (needQ[ch] && lastQ[ch] >= 0) pushChase(lastQ[ch]);
		for (int s = 0; s < loopSp[ch]; s++)
			pushChase(loopIdx[ch][s]);
	}

	for (int i = 0; i < srcN && *outN < dstMax; i++) {
		const ScEvent& e = src[i];
		if (e.tick < marker) {
			const int isNote = (e.kind == SC_EV_NOTE || e.kind == SC_EV_FM_NOTE
				|| e.kind == SC_EV_REST || e.kind == SC_EV_FM_REST || e.kind == SC_EV_TIE);
			if (!isNote || e.dur == 0) continue;
			if (e.tick + e.dur <= marker) continue;
			ScEvent t = e;
			t.dur = (uint16_t)(e.tick + e.dur - marker);
			t.tick = 0;
			t.seq = (uint32_t)(*outN);
			dst[(*outN)++] = t;
			continue;
		}
		ScEvent t = e;
		t.tick = e.tick - marker;
		t.seq = (uint32_t)(*outN);
		dst[(*outN)++] = t;
	}
	return 1;
}

int ScStaffPreviewViaHost(LPCTSTR builtPath, ScStaffUi* u, int tempoT)
{
	if (!builtPath || !builtPath[0] || !u) return 0;
	u->previewOriginTick = u->markerTick;
	u->previewWavMode = 0;
	u->markerSeekArmed = 0;
	u->playheadTick = u->markerTick;
	u->previewActive = 1;
	if (!ScStaffStartHostPreview(builtPath, u, tempoT)) {
		u->previewActive = 0;
		u->previewOriginTick = 0;
		return 0;
	}
	(void)tempoT;
	return 1;
}

int ScStaffPreviewViaWavout(LPCTSTR builtPath, ScStaffUi* u, int tempoT)
{
	if (!builtPath || !builtPath[0] || !og || !::IsWindow(og->GetSafeHwnd())) return 0;
	playlistdata0 item;
	memset(&item, 0, sizeof(item));
	_tcsncpy_s(item.fol, builtPath, _TRUNCATE);
	{
		const wchar_t* slash = wcsrchr(builtPath, L'\\');
		_tcsncpy_s(item.name, slash ? slash + 1 : builtPath, _TRUNCATE);
	}
	if (pl) {
		TCHAR kpiBuf[MAX_PATH];
		kpiBuf[0] = 0;
		BYTE kv = 0;
		playlistdata tmp;
		memset(&tmp, 0, sizeof(tmp));
		_tcsncpy_s(tmp.fol, builtPath, _TRUNCATE);
		_tcsncpy_s(tmp.name, item.name, _TRUNCATE);
		pl->plugs(CString(builtPath), &tmp, kpiBuf, kv);
		_tcsncpy_s(item.fol, tmp.fol, _TRUNCATE);
		_tcsncpy_s(item.name, tmp.name, _TRUNCATE);
		item.sub = tmp.sub;
		item.loop1 = tmp.loop1;
		item.loop2 = tmp.loop2;
		item.ret2 = tmp.ret2;
	}
	wchar_t dir[MAX_PATH], wav[MAX_PATH];
	GetTempPathW(MAX_PATH, dir);
	_snwprintf_s(wav, _TRUNCATE, L"%sogg_sasami_score_prev.wav", dir);
	/* Previous preview may still hold the file. */
	DeleteFileW(wav);
	WavExportOptions opts = {};
	opts.fadeEnable = 0;
	opts.trimLeadEnable = 0;
	opts.copyTags = 0;
	opts.applyPrompt = 0;
	opts.multiFile = 0;
	opts.startFrame = 0;
	opts.endFrame = 0;
	if (!og->ExportToWav(&item, wav, 1, &opts, false))
		return 0;
	if (GetFileAttributesW(wav) == INVALID_FILE_ATTRIBUTES)
		return 0;
	if (u) {
		u->previewOriginTick = u->markerTick;
		u->previewWavMode = 1;
		u->markerSeekArmed = 0;
		u->playheadTick = u->markerTick;
		u->previewActive = 1;
	}
	(void)tempoT;
	if (!ScStaffStartHostPreview(wav, u, tempoT)) {
		if (u) {
			u->previewActive = 0;
			u->previewWavMode = 0;
			u->previewOriginTick = 0;
		}
		return 0;
	}
	return 1;
}

int ScStaffSyncPreviewPlayhead(ScStaffUi* u, int tempoT)
{
	if (!u || !u->previewActive) return 0;

	uint32_t tick = 0;
	int haveTick = 0;
	unsigned vt = 0;
	const double sec = OggGetGdiPlaybackTimeSec();

	/* Wavout score preview: map elapsed seconds onto marker origin. */
	if (u->previewWavMode) {
		tick = u->previewOriginTick + ScStaffTickFromSec(sec > 0 ? sec : 0, tempoT);
		haveTick = 1;
	} else {
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
		if (!haveTick)
			tick = ScStaffTickFromSec(sec, tempoT);

		/* Baked-from-marker files start at tick 0; map back onto staff time. */
		tick = u->previewOriginTick + tick;

		/* Keep playhead on marker until one-shot seek lands (avoid jump to 0). */
		if (u->markerSeekArmed && u->markerTick > 0 && sec <= 0.05) {
			tick = u->markerTick;
		}
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
	const int clefMargin = ScStaffClefMarginPx(u, NULL, 0);
	if (u->followViewW > clefMargin + 40 && u->pxBeat > 0) {
		const int x = (int)(((__int64)u->playheadTick * (uint32_t)u->pxBeat) / (uint32_t)SC_PPQN);
		const int viewInner = u->followViewW - clefMargin;
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
	u->selRangeValid = 0;
	u->selRangeAllParts = 0;
	u->selRangeT0 = u->selRangeT1 = 0;
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
	if (r.Width() < 2 || r.Height() < 2) return;
	/* Acrylic-safe XOR stand-in: hatch fill + dotted border (offscreen blit path). */
	CBrush hatch;
	hatch.CreateHatchBrush(HS_DIAGCROSS, RGB(60, 120, 220));
	int oldRop = dc.SetROP2(R2_MASKPEN);
	CBrush* ob = dc.SelectObject(&hatch);
	dc.PatBlt(r.left, r.top, r.Width(), r.Height(), PATCOPY);
	dc.SelectObject(ob);
	dc.SetROP2(oldRop);
	CPen pen(PS_DOT, 1, RGB(40, 90, 200));
	CPen* op = dc.SelectObject(&pen);
	CBrush* nb = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
	dc.Rectangle(r);
	dc.SelectObject(op);
	if (nb) dc.SelectObject(nb);
}

int ScStaffIsNoteLike(uint8_t kind, int isFm)
{
	if (isFm)
		return kind == SC_EV_FM_NOTE || kind == SC_EV_FM_REST || kind == SC_EV_TIE;
	return kind == SC_EV_NOTE || kind == SC_EV_REST || kind == SC_EV_TIE;
}

int ScStaffIsCtrlAttachKind(uint8_t kind, int isFm)
{
	if (kind == SC_EV_VOL || kind == SC_EV_VELO || kind == SC_EV_PAN || kind == SC_EV_PITCH
		|| kind == SC_EV_CC || kind == SC_EV_SLUR_START || kind == SC_EV_SLUR_END)
		return 1;
	if (isFm && (kind == SC_EV_FM_VOL || kind == SC_EV_FM_PITCH))
		return 1;
	return 0;
}

uint32_t ScStaffEvEndTick(const ScEvent& e)
{
	uint32_t d = e.dur ? e.dur : (uint32_t)(SC_PPQN / 4);
	return e.tick + d;
}

int ScStaffNotePitchKey(const ScEvent& e, int isFm, int tr)
{
	if (e.kind == SC_EV_REST || e.kind == SC_EV_FM_REST) return -1;
	if (e.kind == SC_EV_FM_NOTE || (isFm && e.kind == SC_EV_TIE))
		return ScStaffEvSoundingMidi(e, 1, tr);
	return (int)e.a;
}

void ScStaffEnterSelectTool(ScStaffUi* u)
{
	if (!u) return;
	u->tool = SC_TOOL_SELECT;
	u->hoverValid = 0;
	u->marqueeOn = 0;
	u->rulerDragOn = 0;
	u->helpTopic = SC_HELP_SELECT;
}

static int ScStaffFindPrevNoteSamePitch(const ScEvent* ev, int evCount, int idx, int isFm)
{
	if (!ev || idx < 0 || idx >= evCount) return -1;
	const int ch = (int)ev[idx].ch;
	const int pitch = ScStaffNotePitchKey(ev[idx], isFm, ch);
	if (pitch < 0) return -1;
	uint32_t bestEnd = 0;
	int best = -1;
	for (int i = 0; i < evCount; i++) {
		if (i == idx) continue;
		if ((int)ev[i].ch != ch) continue;
		if (!ScStaffIsNoteLike(ev[i].kind, isFm)) continue;
		if (ev[i].kind == SC_EV_REST || ev[i].kind == SC_EV_FM_REST) continue;
		if (ScStaffNotePitchKey(ev[i], isFm, ch) != pitch) continue;
		const uint32_t end = ScStaffEvEndTick(ev[i]);
		if (end <= ev[idx].tick && end >= bestEnd) {
			bestEnd = end;
			best = i;
		}
	}
	return best;
}

static int ScStaffFindNextNoteSamePitch(const ScEvent* ev, int evCount, int idx, int isFm)
{
	if (!ev || idx < 0 || idx >= evCount) return -1;
	const int ch = (int)ev[idx].ch;
	const int pitch = ScStaffNotePitchKey(ev[idx], isFm, ch);
	if (pitch < 0) return -1;
	const uint32_t myEnd = ScStaffEvEndTick(ev[idx]);
	uint32_t bestTick = 0xFFFFFFFFu;
	int best = -1;
	for (int i = 0; i < evCount; i++) {
		if (i == idx) continue;
		if ((int)ev[i].ch != ch) continue;
		if (!ScStaffIsNoteLike(ev[i].kind, isFm)) continue;
		if (ev[i].kind == SC_EV_REST || ev[i].kind == SC_EV_FM_REST) continue;
		if (ScStaffNotePitchKey(ev[i], isFm, ch) != pitch) continue;
		if (ev[i].tick >= myEnd && ev[i].tick < bestTick) {
			bestTick = ev[i].tick;
			best = i;
		}
	}
	return best;
}

int ScStaffSelAddTieChain(ScEvent* ev, int evCount, ScStaffUi* u, int seedIdx, int isFm)
{
	if (!ev || !u || seedIdx < 0 || seedIdx >= evCount) return 0;
	if (!ScStaffIsNoteLike(ev[seedIdx].kind, isFm)) {
		ScStaffSelAdd(u, seedIdx);
		return 1;
	}
	int added = 0;
	int cur = seedIdx;
	/* walk backward through ties */
	for (;;) {
		int prev = ScStaffFindPrevNoteSamePitch(ev, evCount, cur, isFm);
		if (prev < 0) break;
		/* contiguous if prev ends at/near cur start (allow 1 tick slack) */
		uint32_t pend = ScStaffEvEndTick(ev[prev]);
		if (pend + 1 < ev[cur].tick) break;
		if (ev[cur].kind != SC_EV_TIE && ev[prev].kind != SC_EV_TIE
			&& pend != ev[cur].tick && pend + 1 != ev[cur].tick)
			break;
		cur = prev;
	}
	/* walk forward collecting chain */
	for (;;) {
		if (!ScStaffSelHas(u, cur)) {
			ScStaffSelAdd(u, cur);
			added++;
		}
		int next = ScStaffFindNextNoteSamePitch(ev, evCount, cur, isFm);
		if (next < 0) break;
		uint32_t cend = ScStaffEvEndTick(ev[cur]);
		if (ev[next].tick > cend + 1) break;
		if (ev[next].kind != SC_EV_TIE && cend != ev[next].tick && cend + 1 != ev[next].tick)
			break;
		cur = next;
	}
	return added;
}

void ScStaffExpandSelToFullNotes(ScEvent* ev, int evCount, ScStaffUi* u, int isFm,
	uint32_t* ioT0, uint32_t* ioT1, int trackFilter)
{
	if (!ev || !u || !ioT0 || !ioT1) return;
	uint32_t t0 = *ioT0, t1 = *ioT1;
	if (t1 < t0) { uint32_t t = t0; t0 = t1; t1 = t; }
	int grew = 1;
	while (grew) {
		grew = 0;
		for (int i = 0; i < evCount; i++) {
			const ScEvent& e = ev[i];
			if (trackFilter >= 0 && (int)e.ch != trackFilter) continue;
			if (!ScStaffIsNoteLike(e.kind, isFm)) continue;
			const uint32_t n0 = e.tick;
			const uint32_t n1 = ScStaffEvEndTick(e);
			if (n1 <= t0 || n0 >= t1) continue; /* no overlap */
			if (!ScStaffSelHas(u, i)) {
				ScStaffSelAddTieChain(ev, evCount, u, i, isFm);
				grew = 1;
			}
			/* expand range to cover full selected notes on this pass */
			if (n0 < t0) { t0 = n0; grew = 1; }
			if (n1 > t1) { t1 = n1; grew = 1; }
		}
		/* also expand by already-selected chain members */
		for (int s = 0; s < u->nSel; s++) {
			int i = u->selList[s];
			if (i < 0 || i >= evCount) continue;
			if (!ScStaffIsNoteLike(ev[i].kind, isFm)) continue;
			uint32_t n0 = ev[i].tick, n1 = ScStaffEvEndTick(ev[i]);
			if (n0 < t0) { t0 = n0; grew = 1; }
			if (n1 > t1) { t1 = n1; grew = 1; }
		}
	}
	*ioT0 = t0;
	*ioT1 = t1;
	u->selRangeValid = 1;
	u->selRangeT0 = t0;
	u->selRangeT1 = t1;
}

int ScStaffSelectTickRange(ScStaffUi* u, const ScEvent* ev, int evCount, int isFm,
	uint32_t t0, uint32_t t1, int trackFilter)
{
	if (!u || !ev) return 0;
	if (t1 < t0) { uint32_t t = t0; t0 = t1; t1 = t; }
	ScStaffSelClear(u);
	u->selRangeAllParts = (trackFilter < 0) ? 1 : 0;
	for (int i = 0; i < evCount; i++) {
		const ScEvent& e = ev[i];
		if (trackFilter >= 0 && (int)e.ch != trackFilter) continue;
		if (!ScStaffIsNoteLike(e.kind, isFm)) continue;
		const uint32_t n0 = e.tick, n1 = ScStaffEvEndTick(e);
		if (n1 <= t0 || n0 >= t1) continue;
		ScStaffSelAdd((ScStaffUi*)u, i);
	}
	ScStaffExpandSelToFullNotes((ScEvent*)ev, evCount, u, isFm, &t0, &t1, trackFilter);
	return u->nSel;
}

int ScStaffSelectInRect(const CRect& grid, ScStaffUi* u, const ScEvent* ev, int evCount, int isFm,
	const CRect& r, int expandFull)
{
	if (!u || !ev) return 0;
	const int pxBeat = u->pxBeat > 0 ? u->pxBeat : SC_PX_BEAT_DEFAULT;
	const int gridLeft = ScStaffGridLeftPx(grid.left, u, ev, evCount);
	uint32_t t0 = ScStaffXToTick(r.left, u->scrollX, gridLeft, pxBeat, 0, u, ev, evCount);
	uint32_t t1 = ScStaffXToTick(r.right, u->scrollX, gridLeft, pxBeat, 0, u, ev, evCount);
	if (t1 < t0) { uint32_t t = t0; t0 = t1; t1 = t; }
	ScStaffSelClear(u);
	u->selRangeAllParts = 0;
	for (int i = 0; i < evCount; i++) {
		const ScEvent& e = ev[i];
		if (!ScStaffIsNoteLike(e.kind, isFm)) continue;
		int st = ScStaffVisibleLaneStaffTop(grid, u, e.ch);
		if (st < 0) continue;
		int x0 = ScStaffTickToX(e.tick, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		int x1 = ScStaffTickToX(ScStaffEvEndTick(e), u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		int midi = (e.kind == SC_EV_REST || e.kind == SC_EV_FM_REST) ? 60
			: ScStaffEvWrittenMidi(ev, evCount, e, isFm, e.ch);
		int y = (e.kind == SC_EV_REST || e.kind == SC_EV_FM_REST)
			? (st + 8) : ScStaffMidiNoteYTrack(u, e.ch, st, midi, e.tick, ev, evCount);
		CRect nr(min(x0, x1), y - 4, max(x0 + 2, x1), y + 8);
		CRect inter;
		if (!inter.IntersectRect(&nr, &r)) continue;
		ScStaffSelAdd(u, i);
	}
	if (expandFull)
		ScStaffExpandSelToFullNotes((ScEvent*)ev, evCount, u, isFm, &t0, &t1, -1);
	/* sync marquee visual to expanded tick range if possible */
	if (u->selRangeValid) {
		int xA = ScStaffTickToX(u->selRangeT0, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		int xB = ScStaffTickToX(u->selRangeT1, u->scrollX, gridLeft, pxBeat, u, ev, evCount);
		u->marqueeX0 = min(xA, xB);
		u->marqueeX1 = max(xA, xB);
		/* keep Y from original rect */
		u->marqueeY0 = r.top;
		u->marqueeY1 = r.bottom;
	}
	return u->nSel;
}

static void ScStaffBreakTiesAroundSelection(ScEvent* ev, int evCount, ScStaffUi* u, int isFm)
{
	if (!ev || !u) return;
	for (int i = 0; i < evCount; i++) {
		if (!ScStaffSelHas(u, i)) continue;
		if (ev[i].kind != SC_EV_TIE) continue;
		int prev = ScStaffFindPrevNoteSamePitch(ev, evCount, i, isFm);
		if (prev >= 0 && !ScStaffSelHas(u, prev)) {
			/* convert this TIE to a normal note (keep pitch from prev sounding) */
			const int ch = (int)ev[i].ch;
			const int pitch = ScStaffNotePitchKey(ev[prev], isFm, ch);
			if (isFm) {
				ev[i].kind = SC_EV_FM_NOTE;
				/* encode from sounding midi */
				int oct = pitch / 12 - 1; if (oct < 0) oct = 0; if (oct > 9) oct = 9;
				int sc = pitch % 12; if (sc < 0) sc = 0;
				ev[i].a = (uint8_t)(((oct & 0x0F) << 4) | (sc & 0x0F));
			} else {
				ev[i].kind = SC_EV_NOTE;
				ev[i].a = (uint8_t)(pitch < 0 ? 60 : pitch);
			}
		}
	}
	/* next after selection that is TIE → convert to note */
	for (int i = 0; i < evCount; i++) {
		if (ScStaffSelHas(u, i)) continue;
		if (ev[i].kind != SC_EV_TIE) continue;
		int prev = ScStaffFindPrevNoteSamePitch(ev, evCount, i, isFm);
		if (prev >= 0 && ScStaffSelHas(u, prev)) {
			const int ch = (int)ev[i].ch;
			const int pitch = ScStaffNotePitchKey(ev[prev], isFm, ch);
			if (isFm) {
				ev[i].kind = SC_EV_FM_NOTE;
				int oct = pitch / 12 - 1; if (oct < 0) oct = 0; if (oct > 9) oct = 9;
				int sc = pitch % 12; if (sc < 0) sc = 0;
				ev[i].a = (uint8_t)(((oct & 0x0F) << 4) | (sc & 0x0F));
			} else {
				ev[i].kind = SC_EV_NOTE;
				ev[i].a = (uint8_t)(pitch < 0 ? 60 : pitch);
			}
		}
	}
	/* Soft-delete slurs that cross the selection boundary (keep indices stable). */
	for (int i = 0; i < evCount; i++) {
		if (ev[i].kind != SC_EV_SLUR_START) continue;
		const int ch = (int)ev[i].ch;
		int end = -1;
		for (int j = 0; j < evCount; j++) {
			if ((int)ev[j].ch != ch) continue;
			if (ev[j].tick < ev[i].tick) continue;
			if (ev[j].kind == SC_EV_SLUR_END) { end = j; break; }
		}
		if (end < 0) continue;
		int midSel = 0, midOut = 0;
		for (int k = 0; k < evCount; k++) {
			if ((int)ev[k].ch != ch) continue;
			if (!ScStaffIsNoteLike(ev[k].kind, isFm)) continue;
			if (ev[k].tick < ev[i].tick || ev[k].tick > ev[end].tick) continue;
			if (ScStaffSelHas(u, k)) midSel = 1; else midOut = 1;
		}
		const int inS = ScStaffSelHas(u, i);
		const int inE = ScStaffSelHas(u, end);
		if ((midSel || inS || inE) && midOut) {
			ev[i].kind = SC_EV_CC; ev[i].a = 255; ev[i].b = 0;
			ev[end].kind = SC_EV_CC; ev[end].a = 255; ev[end].b = 0;
		}
	}
}

static int ScStaffApplyPitchDelta(ScEvent& e, int dSemi, int isFm, int tr)
{
	if (e.kind == SC_EV_REST || e.kind == SC_EV_FM_REST) return 0;
	if (isFm && tr == 6) {
		/* rhythm pads: clamp pad index */
		int pad = (int)(e.a & 0x0F);
		if (pad > 5) pad = pad % 6;
		pad += dSemi;
		while (pad < 0) pad += 6;
		pad %= 6;
		e.a = (uint8_t)((e.a & 0xF0) | (pad & 0x0F));
		if (e.kind == SC_EV_TIE) e.kind = SC_EV_FM_NOTE;
		return 1;
	}
	int midi = ScStaffNotePitchKey(e, isFm, tr);
	if (midi < 0) return 0;
	midi += dSemi;
	if (midi < 0) midi = 0;
	if (midi > 127) midi = 127;
	if (isFm || e.kind == SC_EV_FM_NOTE) {
		int oct = midi / 12 - 1; if (oct < 0) oct = 0; if (oct > 9) oct = 9;
		int sc = midi % 12;
		e.a = (uint8_t)(((oct & 0x0F) << 4) | (sc & 0x0F));
		if (e.kind == SC_EV_TIE) e.kind = SC_EV_FM_NOTE;
		else if (e.kind != SC_EV_FM_NOTE) e.kind = SC_EV_FM_NOTE;
	} else {
		e.a = (uint8_t)midi;
		if (e.kind == SC_EV_TIE) e.kind = SC_EV_NOTE;
	}
	return 1;
}

static void ScStaffRetieSelectedChains(ScEvent* ev, int evCount, ScStaffUi* u, int isFm)
{
	if (!ev || !u || u->nSel < 2) return;
	/* sort selected indices by tick */
	int idx[SC_SEL_MAX];
	int n = 0;
	for (int i = 0; i < u->nSel && n < SC_SEL_MAX; i++) {
		int ix = u->selList[i];
		if (ix >= 0 && ix < evCount) idx[n++] = ix;
	}
	for (int a = 0; a < n; a++)
		for (int b = a + 1; b < n; b++)
			if (ev[idx[b]].tick < ev[idx[a]].tick ||
				(ev[idx[b]].tick == ev[idx[a]].tick && idx[b] < idx[a])) {
				int t = idx[a]; idx[a] = idx[b]; idx[b] = t;
			}
	for (int i = 0; i + 1 < n; i++) {
		ScEvent& a = ev[idx[i]];
		ScEvent& b = ev[idx[i + 1]];
		if ((int)a.ch != (int)b.ch) continue;
		if (a.kind == SC_EV_REST || a.kind == SC_EV_FM_REST) continue;
		if (b.kind == SC_EV_REST || b.kind == SC_EV_FM_REST) continue;
		const int pa = ScStaffNotePitchKey(a, isFm, a.ch);
		const int pb = ScStaffNotePitchKey(b, isFm, b.ch);
		if (pa < 0 || pa != pb) continue;
		if (ScStaffEvEndTick(a) != b.tick && ScStaffEvEndTick(a) + 1 != b.tick) continue;
		/* follower becomes TIE */
		b.kind = SC_EV_TIE;
		b.a = a.a;
	}
}

int ScStaffSelMoveBy(ScEvent* ev, int* evCount, ScStaffUi* u, int dTick, int dSemi, int isFm)
{
	if (!ev || !evCount || !u || (dTick == 0 && dSemi == 0)) return 0;
	const int n = *evCount;
	if (u->nSel < 1 && u->selEv < 0) return 0;
	if (u->nSel < 1 && u->selEv >= 0)
		ScStaffSelAdd(u, u->selEv);

	ScStaffBreakTiesAroundSelection(ev, n, u, isFm);

	int moved = 0;
	for (int s = 0; s < u->nSel; s++) {
		int i = u->selList[s];
		if (i < 0 || i >= n) continue;
		ScEvent& e = ev[i];
		if (!ScStaffIsNoteLike(e.kind, isFm) && e.kind != SC_EV_SLUR_START && e.kind != SC_EV_SLUR_END)
			continue;
		if (dTick) {
			int64_t t = (int64_t)e.tick + dTick;
			if (t < 0) t = 0;
			e.tick = (uint32_t)t;
		}
		if (dSemi && ScStaffIsNoteLike(e.kind, isFm))
			ScStaffApplyPitchDelta(e, dSemi, isFm, e.ch);
		moved++;
	}
	ScStaffRetieSelectedChains(ev, *evCount, u, isFm);
	return moved;
}

int ScStaffSelCopyEx(const ScEvent* ev, int evCount, const ScStaffUi* u, int isFm,
	ScEvent* out, int outMax, uint32_t* outBaseTick, uint32_t* outSpan)
{
	if (!ev || !u || !out || outMax < 1) return 0;
	uint32_t t0 = 0xFFFFFFFFu, t1 = 0;
	char take[SC_EV_MAX];
	memset(take, 0, sizeof(take));
	auto mark = [&](int i) {
		if (i < 0 || i >= evCount) return;
		take[i] = 1;
		uint32_t n0 = ev[i].tick, n1 = ScStaffEvEndTick(ev[i]);
		if (n0 < t0) t0 = n0;
		if (n1 > t1) t1 = n1;
	};
	if (u->nSel > 0) {
		for (int i = 0; i < u->nSel; i++) mark(u->selList[i]);
	} else if (u->selEv >= 0) {
		mark(u->selEv);
	}
	if (t0 == 0xFFFFFFFFu) return 0;
	/* attach controllers in span on same channels as selected notes */
	char chUsed[32];
	memset(chUsed, 0, sizeof(chUsed));
	for (int i = 0; i < evCount; i++)
		if (take[i] && (int)ev[i].ch < 32) chUsed[ev[i].ch] = 1;
	for (int i = 0; i < evCount; i++) {
		if (take[i]) continue;
		if ((int)ev[i].ch >= 32 || !chUsed[ev[i].ch]) continue;
		if (!ScStaffIsCtrlAttachKind(ev[i].kind, isFm)) continue;
		if (ev[i].tick < t0 || ev[i].tick >= t1) continue;
		take[i] = 1;
	}
	int n = 0;
	for (int i = 0; i < evCount && n < outMax; i++) {
		if (!take[i]) continue;
		out[n++] = ev[i];
	}
	if (outBaseTick) *outBaseTick = t0;
	if (outSpan) *outSpan = (t1 > t0) ? (t1 - t0) : 0;
	return n;
}

int ScStaffSelPasteEx(ScEvent* ev, int* evCount, int evMax, ScStaffUi* u, int isFm,
	const ScEvent* clip, int clipN, uint32_t baseTick, uint32_t clipSpan, int insertMode)
{
	if (!ev || !evCount || !clip || clipN < 1) return 0;
	uint32_t minT = 0xFFFFFFFFu, maxEnd = 0;
	char chUsed[32];
	memset(chUsed, 0, sizeof(chUsed));
	for (int i = 0; i < clipN; i++) {
		if (clip[i].tick < minT) minT = clip[i].tick;
		uint32_t end = ScStaffEvEndTick(clip[i]);
		if (end > maxEnd) maxEnd = end;
		if ((int)clip[i].ch < 32) chUsed[clip[i].ch] = 1;
	}
	if (minT == 0xFFFFFFFFu) minT = 0;
	uint32_t span = clipSpan;
	if (span == 0) span = (maxEnd > minT) ? (maxEnd - minT) : 0;

	if (insertMode && span > 0) {
		for (int i = 0; i < *evCount; i++) {
			if ((int)ev[i].ch >= 32 || !chUsed[ev[i].ch]) continue;
			if (ev[i].tick >= baseTick)
				ev[i].tick += span;
		}
	} else if (!insertMode && span > 0) {
		/* overwrite: delete overlapping notes/ctrls on target channels */
		char drop[SC_EV_MAX];
		memset(drop, 0, sizeof(drop));
		for (int i = 0; i < *evCount; i++) {
			if ((int)ev[i].ch >= 32 || !chUsed[ev[i].ch]) continue;
			const uint32_t n0 = ev[i].tick, n1 = ScStaffEvEndTick(ev[i]);
			if (ScStaffIsNoteLike(ev[i].kind, isFm)) {
				if (n1 <= baseTick || n0 >= baseTick + span) continue;
				drop[i] = 1;
			} else if (ScStaffIsCtrlAttachKind(ev[i].kind, isFm)) {
				if (ev[i].tick >= baseTick && ev[i].tick < baseTick + span)
					drop[i] = 1;
			}
		}
		int w = 0;
		for (int i = 0; i < *evCount; i++)
			if (!drop[i]) ev[w++] = ev[i];
		*evCount = w;
	}

	int added = 0;
	for (int i = 0; i < clipN; i++) {
		if (*evCount >= evMax) break;
		ScEvent e = clip[i];
		e.tick = baseTick + (e.tick - minT);
		ev[(*evCount)++] = e;
		added++;
	}
	if (u) {
		uint32_t need = baseTick + span;
		if (need > (uint32_t)u->contentTicks)
			u->contentTicks = (int)need + ScStaffTicksPerMeasure(u);
	}
	return added;
}

int ScStaffInsertBlankRange(ScEvent* ev, int evCount, ScStaffUi* u)
{
	if (!ev || !u || evCount < 1) return 0;
	uint32_t t0 = 0, t1 = 0;
	if (u->selRangeValid && u->selRangeT1 > u->selRangeT0) {
		t0 = u->selRangeT0;
		t1 = u->selRangeT1;
	} else {
		t0 = 0xFFFFFFFFu;
		t1 = 0;
		auto consider = [&](int i) {
			if (i < 0 || i >= evCount) return;
			const uint32_t n0 = ev[i].tick;
			const uint32_t n1 = ScStaffEvEndTick(ev[i]);
			if (n0 < t0) t0 = n0;
			if (n1 > t1) t1 = n1;
		};
		if (u->nSel > 0) {
			for (int s = 0; s < u->nSel; s++)
				consider(u->selList[s]);
		} else if (u->selEv >= 0) {
			consider(u->selEv);
		} else {
			return 0;
		}
		if (t0 == 0xFFFFFFFFu || t1 <= t0) return 0;
		u->selRangeValid = 1;
		u->selRangeT0 = t0;
		u->selRangeT1 = t1;
	}
	const uint32_t span = t1 - t0;
	if (span < 1) return 0;

	char chUsed[32];
	memset(chUsed, 0, sizeof(chUsed));
	if (!u->selRangeAllParts) {
		int any = 0;
		if (u->nSel > 0) {
			for (int s = 0; s < u->nSel; s++) {
				int i = u->selList[s];
				if (i < 0 || i >= evCount) continue;
				if ((int)ev[i].ch < 32) { chUsed[ev[i].ch] = 1; any = 1; }
			}
		} else if (u->selEv >= 0 && u->selEv < evCount && (int)ev[u->selEv].ch < 32) {
			chUsed[ev[u->selEv].ch] = 1;
			any = 1;
		}
		if (!any) {
			int tr = u->markerSolidTrack;
			if (tr < 0 || tr >= 32) tr = 0;
			chUsed[tr] = 1;
		}
	}

	for (int i = 0; i < evCount; i++) {
		if (!u->selRangeAllParts) {
			if ((int)ev[i].ch >= 32 || !chUsed[ev[i].ch]) continue;
		}
		if (ev[i].tick >= t0)
			ev[i].tick += span;
	}
	u->contentTicks += (int)span;
	/* Keep the blank hole as the active range (same t0..t1). */
	u->selRangeValid = 1;
	u->selRangeT0 = t0;
	u->selRangeT1 = t0 + span;
	return 1;
}

void ScStaffShrinkContentIfNeeded(ScStaffUi* u, const ScEvent* ev, int evCount)
{
	if (!u) return;
	const int floorTicks = SC_PPQN * SC_MEASURE_BEATS * SC_MEASURES_DEFAULT;
	uint32_t maxEnd = 0;
	for (int i = 0; i < evCount; i++) {
		uint32_t end = ScStaffEvEndTick(ev[i]);
		if (end > maxEnd) maxEnd = end;
	}
	const int tpm = ScStaffTicksPerMeasure(u);
	uint32_t need = maxEnd + (uint32_t)(tpm * 2);
	if ((int)need < floorTicks) need = (uint32_t)floorTicks;
	/* round up to measure */
	if (tpm > 0)
		need = ((need + (uint32_t)tpm - 1) / (uint32_t)tpm) * (uint32_t)tpm;
	u->contentTicks = (int)need;
}

int ScStaffNoteHasFx(const ScEvent* ev, int evCount, int noteIdx, int isFm,
	int* outPitch, int* outPan, int* outVol, int* outExpr)
{
	if (outPitch) *outPitch = 0;
	if (outPan) *outPan = 0;
	if (outVol) *outVol = 0;
	if (outExpr) *outExpr = 0;
	if (!ev || noteIdx < 0 || noteIdx >= evCount) return 0;
	const ScEvent& n = ev[noteIdx];
	if (!ScStaffIsNoteLike(n.kind, isFm)) return 0;
	const uint32_t n0 = n.tick, n1 = ScStaffEvEndTick(n);
	const int ch = (int)n.ch;
	int any = 0;
	for (int i = 0; i < evCount; i++) {
		if ((int)ev[i].ch != ch) continue;
		if (ev[i].tick < n0 || ev[i].tick >= n1) continue;
		const uint8_t k = ev[i].kind;
		if (k == SC_EV_PITCH || k == SC_EV_FM_PITCH) {
			if (ev[i].a != 64) { if (outPitch) *outPitch = 1; any = 1; }
		} else if (k == SC_EV_PAN) {
			if (ev[i].a != 64) { if (outPan) *outPan = 1; any = 1; }
		} else if (k == SC_EV_VOL || k == SC_EV_FM_VOL) {
			if (outVol) *outVol = 1; any = 1;
		} else if (k == SC_EV_VELO || (k == SC_EV_CC && ev[i].a == 11)) {
			if (outExpr) *outExpr = 1; any = 1;
		}
	}
	return any;
}
