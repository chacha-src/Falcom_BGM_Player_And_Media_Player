#pragma once
/* Shared staff-score geometry / paint / hit-test for SASAMI MIDI & FM score dialogs. */
#include "SasamiComposerDoc.h"

enum {
	SC_STAFF_LINE_GAP0 = 6,
	SC_STAFF_PAD0 = 16,       /* inner staff block below bottom line (ledger uses ScStaffLedgerPad*) */
	SC_STAFF_LEDGER_PAD = 8,  /* minimum margin beyond extreme note head */
	SC_NOTE_MIDI_MIN = 12,    /* O0C */
	SC_NOTE_MIDI_MAX = 120,   /* O9C */
	SC_PART_GAP = 12,         /* space between parts */
	SC_GRAND_STAFF_GAP = 20,  /* treble ↔ bass on grand staff */
	SC_TRACK_COL_W = 220,
	SC_NAME_BAND_H = 16, /* track title band (left + score spacer) */
	SC_CTRL_LANE_H = 22, /* one SSW-style Tone or Exc/RPN lane */
	SC_PART_GAUGE_H = 60, /* name + Tone + Exc/RPN */
	SC_CLEF_MARGIN = 58, /* space for clef before measure 1 */
	SC_STRIP_LANE_H = 72,
	SC_PROP_H = 32,
	SC_TOOL_H = 28,
	SC_RULER_H = 24,
	SC_PX_BEAT_DEFAULT = 28,
	SC_PX_BEAT_MIN = 8,
	SC_PX_BEAT_MAX = 120,
	SC_STAFF_SCALE_MIN = 70,
	SC_STAFF_SCALE_MAX = 220,
	SC_MEASURE_BEATS = 4,
	SC_STRIP_LANES_MAX = 2,
	SC_MEASURES_DEFAULT = 64,
	SC_MEASURES_MAX = 2048,
	SC_SEL_MAX = 512,
	SC_CLIP_MAX = 4096
};

/* Left-gauge hit zones (ScStaffHitGauge). */
enum ScGaugeZone : int {
	SC_GAUGE_NONE = 0,
	SC_GAUGE_TONE = 1, /* VST / FM voice name */
	SC_GAUGE_PROG = 2, /* Prog / Exc row */
	SC_GAUGE_CLEF = 3  /* click [G]/[F]/[G+F] to cycle clef */
};

enum ScScoreTool : int {
	SC_TOOL_PENCIL = 0,
	SC_TOOL_ERASER = 1,
	SC_TOOL_SELECT = 2,
	SC_TOOL_TEMPO = 3,
	SC_TOOL_TIE = 4
};

enum ScStripKind : int {
	SC_STRIP_EXPR = 0, /* Expression / Vel */
	SC_STRIP_VOL = 1,
	SC_STRIP_PITCH = 2, /* pitch bend / detune, center=64 → display ±100 */
	SC_STRIP_GATE = 3,
	SC_STRIP_PAN = 4,
	SC_STRIP_KIND_COUNT = 5
};

enum ScStripDraw : int {
	SC_STRIP_DRAW_PENCIL = 0,
	SC_STRIP_DRAW_LINE = 1,
	SC_STRIP_DRAW_CURVE = 2
};

struct ScStaffUi {
	int scrollX;
	int scrollY;
	int pxBeat;
	int staffScale; /* 100 = default; scales line gap / staff height */
	int tool;
	int placeDur;
	int baseDur;
	int tuplet; /* 0=none, 3/5/6/8 */
	int triplet; /* legacy alias: non-zero when tuplet==3 */
	int dotted;
	int placeRest; /* 1 = placing rest of placeDur */
	int placeAccidental; /* -1 flat, 0 natural, +1 sharp — ghost/place UI */
	int selEv; /* primary selection; also first of selList when nSel>0 */
	int nSel;
	int selList[SC_SEL_MAX];
	int trackCount;
	int visible[32];
	int mute[32];
	int solo[32];
	wchar_t names[32][32];
	int stripCount; /* 0..2 (0 = no strip) */
	int stripKind[SC_STRIP_LANES_MAX];
	int stripDraw; /* pencil / line / curve */
	uint8_t strip[SC_STRIP_LANES_MAX][256];
	int stripLineAnchorCol;
	int stripLineAnchorVal;
	int stripLineLane;
	int dragEv;
	int dragOriginX;
	int marqueeOn;
	int marqueeX0, marqueeY0, marqueeX1, marqueeY1;
	int eraseDrag;
	int hoverValid;
	int hoverX, hoverY;
	int hoverTrack;
	int hoverNote;
	uint32_t hoverTick;
	int snapFit;
	int markStack; /* 0=replace same tick+kind (1-deep UX), 1=nest/stack */
	int contentTicks; /* for hscroll range */
	int contentTracks; /* for vscroll */
	/* transport / preview (SSW/Cubase-like) */
	uint32_t markerTick;   /* play-from position */
	int loopATick;         /* -1 = unset */
	int loopBTick;         /* -1 = unset */
	uint32_t playheadTick; /* live playhead while previewing */
	int previewActive;
	int transportMode;     /* 0=none 1=set marker 2=set A 3=set B */
	int markerSeekArmed;   /* seek to marker once after preview starts */
	int followViewW;       /* score grid width for playhead auto-scroll (0=off) */
	int clef[32];          /* 0=treble 1=bass 2=grand (G+F) 3=drum/kit */
	wchar_t vstLabel[32][40]; /* Tone row: VST basename / FM voice name */
	wchar_t progLabel[32][40]; /* Prog row under Tone: "Prog 68" / "ALG3 FB5" */
	int dragMode;          /* 0=none 1=resize-dur 2=move-pitch/tick */
	int meterNumer;        /* time signature numerator (default 4) */
	int meterDenom;        /* time signature denominator (default 4) */
	int isFmScore;         /* 1 = FM/OPNA score dialog */
};

/* Ticks in one measure from meter (4/4 → 4*PPQN, 7/8 → 7*(PPQN/2)). */
int ScStaffTicksPerMeasure(const ScStaffUi* u);
void ScStaffSetMeter(ScStaffUi* u, int numer, int denom);

int ScStaffLineGap(const ScStaffUi* u);
int ScStaffH(const ScStaffUi* u); /* one 5-line staff height */
int ScStaffLedgerPad(const ScStaffUi* u, int track); /* max(above,below) — prefer Above/Below */
int ScStaffLedgerPadAbove(const ScStaffUi* u, int track);
int ScStaffLedgerPadBelow(const ScStaffUi* u, int track);
int ScStaffRowStaffTop(const ScStaffUi* u, int track, int rowTop);
/* Y where note ledger/staff begins (below Tone/Exc gauge). */
int ScStaffRowNoteAreaTop(int rowTop);
int ScStaffRowH(const ScStaffUi* u, int track); /* visible row incl. grand / stub */
int ScStaffStripTotalH(const ScStaffUi* u);
int ScStaffClampNote(int noteMidi);

void ScStaffUiInit(ScStaffUi* u, int trackCount, int isFm);
int ScStaffIsOpnaRhythmTrack(int isFm, int track); /* FM ch7 / track index 6 */
int ScStaffIsFmSsgTrack(int isFm, int track);      /* FM ch4-6 / track index 3-5 */
/* Live pointer / hover line for status bar (ch, note, tick). Returns 1 if out filled. */
int ScStaffFormatPointerStatus(const ScStaffUi* u, const ScEvent* ev, int evCount,
	int isFm, const CRect& grid, CPoint pt, wchar_t* out, size_t outCch);
void ScStaffRecomputePlaceDur(ScStaffUi* u);
int ScStaffPlaceQuant(const ScStaffUi* u);
int ScStaffMidiNoteY(int noteMidi, int staffTop, int lineGap);
int ScStaffYToMidiNote(int y, int staffTop, int lineGap);
int ScStaffMidiNoteYClef(int noteMidi, int staffTop, int lineGap, int clefBass);
int ScStaffYToMidiNoteClef(int y, int staffTop, int lineGap, int clefBass);
/* clef[]: 0=G 1=F 2=grand 3=drum — grand uses C4 split (MIDI 60);
   drum uses C1 (36) as staff bottom for kit / Groove Agent range */
int ScStaffMidiNoteYTrack(const ScStaffUi* u, int track, int staffTop, int noteMidi);
int ScStaffYToMidiNoteTrack(const ScStaffUi* u, int track, int staffTop, int y);
int ScStaffTickToX(uint32_t tick, int scrollX, int gridLeft, int pxBeat);
uint32_t ScStaffXToTick(int x, int scrollX, int gridLeft, int pxBeat, int quantTicks);
void ScStaffPaintTracks(CDC& dc, const CRect& rc, const ScStaffUi* u, int curTrack);
void ScStaffPaintStaves(CDC& dc, const CRect& grid, const ScStaffUi* u,
	const ScEvent* ev, int evCount, int isFm, int curTrack, int docTempoT);
void ScStaffPaintStrip(CDC& dc, const CRect& rc, const ScStaffUi* u);
int ScStaffHitTrack(const CRect& trackRc, const ScStaffUi* u, CPoint pt);
/* Returns track index or -1; *outZone = SC_GAUGE_* when click is in Tone/Prog strip. */
int ScStaffHitGauge(const CRect& trackRc, const ScStaffUi* u, CPoint pt, int* outZone);
int ScStaffHitNote(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int isFm, CPoint pt, int* outTrack);
/* SSW-style: hit Prog/Bank/Voice chip in the score strip above staff lines (not left track list). */
int ScStaffHitScoreCtrl(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int isFm, CPoint pt, int* outTrack);
/* Hit |: :| Q J Ped. ＊ drawn through the staff (not only Tone-chip strip). */
int ScStaffHitStaffMark(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int isFm, CPoint pt, int* outTrack);
/* Collect all staff marks under pt (outer→inner). Returns count written to outIdx[]. */
int ScStaffCollectStaffMarksAt(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount,
	int isFm, CPoint pt, int* outIdx, int maxOut);
/* Same-tick stack index among same kind+ch (0=first). Used to offset draw/hit. */
int ScStaffMarkStackIndex(const ScEvent* ev, int evCount, int idx);
/* 1 = loop/jump/pedal mark kinds erasable via tool / Delete / context menu. */
int ScStaffIsStaffMarkKind(uint8_t kind, int isFm);
/* 1 = point is in the per-track Prog/control strip above staff (no note pencil there). */
int ScStaffPtInScoreCtrlStrip(const CRect& grid, const ScStaffUi* u, CPoint pt, int* outTrack);
int ScStaffHitRulerTick(const CRect& grid, const ScStaffUi* u, CPoint pt, uint32_t* outTick);
void ScStaffEnsureStripFromDoc(ScStaffUi* u, const ScEvent* ev, int evCount, int track);
int ScStaffHitStrip(const CRect& stripRc, const ScStaffUi* u, CPoint pt, int* outLane, int* outCol, int* outVal);
void ScStaffApplyStripToDocMidi(ScMidiDoc* d, int track, const ScStaffUi* u);
void ScStaffApplyStripToDocFm(ScFmDoc* d, int track, const ScStaffUi* u);
int ScStaffVisibleLaneStaffTop(const CRect& grid, const ScStaffUi* u, int track);
int ScStaffTrackRowTop(const CRect& trackRc, const ScStaffUi* u, int track);
int ScStaffContentHeight(const ScStaffUi* u);

int ScStaffBpmFromTempoT(int tempoT);
double ScStaffSecFromTick(uint32_t tick, int tempoT);
uint32_t ScStaffTickFromSec(double sec, int tempoT);
/* Build temp already written: add to playlist and start playback. Returns 1 on start. */
int ScStaffStartHostPreview(LPCTSTR path, const ScStaffUi* u, int tempoT);
/* Sync playhead from host clock; soft A-B loop. Returns 1 if UI should invalidate. */
int ScStaffSyncPreviewPlayhead(ScStaffUi* u, int tempoT);
/* Append EQ band event into command-roll prompt text and auto-ON (UI need not be open). */
int ScStaffWriteCmdRollEq(double t0Sec, double t1Sec, int bandVal0, int bandVal1);

void ScStaffDrawNoteGlyph(CDC& dc, const CRect& rc, int durTicks, int rest, int selected);
/* One call for note or rest: shape follows durTicks (PPQN ticks). */
void ScStaffDrawSymbol(CDC& dc, int x, int y, int durTicks, int rest, COLORREF col, int selected, int stemUp);
/* accidental: -1 flat, 0 none/natural glyph omitted, +1 sharp — drawn left of head/rest. */
void ScStaffDrawSymbolEx(CDC& dc, int x, int y, int durTicks, int rest, COLORREF col,
	int selected, int stemUp, int accidental);
void ScStaffDrawRestGlyph(CDC& dc, int x, int y, int durTicks, COLORREF col);
HCURSOR ScStaffCreateBlankCursor(void);
const wchar_t* ScStaffStripKindName(int kind);

void ScStaffUpdateContentExtent(ScStaffUi* u, const ScEvent* ev, int evCount);
int ScStaffScrollTrackW(void);
int ScStaffScrollTrackH(void);
/* Draw thick visible H/V scroll thumbs in reserved sb margins (SSW-like). */
void ScStaffPaintScrollThumbs(CDC& dc, const CRect& client, const CRect& body,
	const ScStaffUi* u, int pageW, int pageH);
/* Hit-test custom scroll tracks. Returns 0=none, 1=vert, 2=horz; sets *outPos. */
int ScStaffHitScroll(const CRect& client, const CRect& body, const ScStaffUi* u,
	int pageW, int pageH, CPoint pt, int* outPos);

/* Multi-select / clipboard helpers */
void ScStaffSelClear(ScStaffUi* u);
void ScStaffSelAdd(ScStaffUi* u, int evIdx);
int ScStaffSelHas(const ScStaffUi* u, int evIdx);
void ScStaffSelSetPrimary(ScStaffUi* u, int evIdx);
int ScStaffSelCopy(const ScEvent* ev, int evCount, const ScStaffUi* u, ScEvent* out, int outMax, uint32_t* outBaseTick);
int ScStaffSelPaste(ScEvent* ev, int* evCount, int evMax, const ScEvent* clip, int clipN, uint32_t baseTick, int trackDelta);
int ScStaffSelDelete(ScEvent* ev, int* evCount, ScStaffUi* u);
int ScStaffTieSelected(ScEvent* ev, int evCount, ScStaffUi* u);
int ScStaffCopyMeasure(const ScEvent* ev, int evCount, int track, uint32_t measTicks, int measIndex, int allTracks,
	ScEvent* out, int outMax, uint32_t* outBaseTick);
void ScStaffPaintMarquee(CDC& dc, const ScStaffUi* u);
