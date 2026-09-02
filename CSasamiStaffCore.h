#pragma once
/* Shared staff-score geometry / paint / hit-test for SASAMI MIDI & FM score dialogs. */
#include "SasamiComposerDoc.h"

enum {
	SC_STAFF_LINE_GAP0 = 6,
	SC_STAFF_PAD0 = 16,       /* inner staff block below bottom line (ledger uses ScStaffLedgerPad*) */
	SC_STAFF_LEDGER_PAD = 8,  /* minimum margin beyond extreme note head */
	SC_RHYTHM_PAD_LANE = 16,  /* OPNA RHY: px per BD..RIM lane (dedicated tall staff) */
	SC_RHYTHM_STAFF_PAD = 12, /* top/bottom pad around rhythm lanes */
	SC_NOTE_MIDI_MIN = 12,    /* O0C */
	SC_NOTE_MIDI_MAX = 120,   /* O9C */
	SC_PART_GAP = 12,         /* space between parts */
	SC_GRAND_STAFF_GAP = 20,  /* treble ↔ bass on grand staff */
	SC_TRACK_COL_W = 220,
	SC_NAME_BAND_H = 16, /* track title band (left + score spacer) */
	SC_CTRL_LANE_H = 22, /* one SSW-style Tone or Exc/RPN lane */
	SC_PART_GAUGE_H = 60, /* name + Tone + Exc/RPN */
	SC_PART_DISABLED_H = 24, /* compact track-row when part disabled on score */
	SC_PART_ENABLE_W = 18,  /* left ● enable hit zone in track column */
	SC_CLEF_MARGIN = 58, /* space for clef before measure 1 */
	SC_LAYOUT_ZONE_W = 48, /* mid-score meter/clef/key strip */
	SC_SB_ZOOM_BTN = 22,    /* +/- zoom buttons beside scroll tracks */
	SC_STRIP_LANE_H = 72, /* default / 基本 */
	SC_STRIP_H_WIDE = 120,
	SC_STRIP_H_NARROW = 48,
	SC_GLOBAL_TEMPO_H = 36, /* compact global tempo band */
	SC_BAND_LABEL_W = 52,   /* left scale column — avoids title overlap */
	SC_PROP_H = 32,
	SC_TOOL_H = 28,
	SC_RULER_H = 24,
	SC_PX_BEAT_DEFAULT = 28,
	SC_PX_BEAT_MIN = 8,
	SC_PX_BEAT_MAX = 120,
	SC_STAFF_SCALE_MIN = 70,
	SC_STAFF_SCALE_MAX = 220,
	SC_MEASURE_BEATS = 4,
	SC_STRIP_LANES_MAX = 3,
	SC_STRIP_COLS_MAX = 4096, /* 1/64 × ~64 bars of 4/4 @ PPQN=48 */
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
	SC_TOOL_TIE = 4,
	SC_TOOL_METER = 5
};

enum ScStripKind : int {
	SC_STRIP_EXPR = 0, /* Expression CC11 */
	SC_STRIP_VOL = 1,  /* Volume CC7 / FM TL */
	SC_STRIP_PITCH = 2, /* pitch bend / detune, center=64 */
	SC_STRIP_GATE = 3,
	SC_STRIP_PAN = 4,  /* Pan CC10 */
	SC_STRIP_MOD = 5,  /* Modulation CC1 */
	SC_STRIP_PORTA_T = 6, /* Portamento time CC5 */
	SC_STRIP_HOLD = 7, /* Hold pedal CC64 */
	SC_STRIP_PORTA = 8, /* Portamento CC65 */
	SC_STRIP_SOST = 9, /* Sostenuto CC66 */
	SC_STRIP_SOFT = 10, /* Soft pedal CC67 */
	SC_STRIP_REVERB = 11, /* Reverb CC91 */
	SC_STRIP_CHORUS = 12, /* Chorus CC93 */
	SC_STRIP_DELAY = 13, /* Delay CC94 */
	SC_STRIP_VEL = 14, /* Note velocity */
	SC_STRIP_KIND_COUNT = 15
};

/* Fixed per-part CC bands (inline above staff, SSW-style). */
enum ScPartBandKind : int {
	SC_PBAND_EXPR = 0,
	SC_PBAND_VOL = 1,
	SC_PBAND_PAN = 2,
	SC_PBAND_REVERB = 3,
	SC_PBAND_CHORUS = 4,
	SC_PBAND_DELAY = 5,
	SC_PBAND_MOD = 6,
	SC_PBAND_VEL = 7,
	SC_PBAND_KIND_COUNT = 8
};
#define SC_PBAND_DEFAULT_MASK ((1 << SC_PBAND_EXPR) | (1 << SC_PBAND_VOL) | (1 << SC_PBAND_REVERB) | (1 << SC_PBAND_CHORUS))

enum ScStripHeight : int {
	SC_STRIP_HGT_WIDE = 0,
	SC_STRIP_HGT_NORMAL = 1,
	SC_STRIP_HGT_NARROW = 2,
	SC_STRIP_HGT_COUNT = 3
};

/* Contextual help-bar topics (palette / tool / hover). */
enum ScHelpTopic : int {
	SC_HELP_DEFAULT = 0,
	SC_HELP_PENCIL,
	SC_HELP_ERASER,
	SC_HELP_SELECT,
	SC_HELP_TEMPO,
	SC_HELP_MARK,
	SC_HELP_LOOP_START,
	SC_HELP_LOOP_END,
	SC_HELP_PED_ON,
	SC_HELP_PED_OFF,
	SC_HELP_OTTAVA,
	SC_HELP_LOCO,
	SC_HELP_TIE,
	SC_HELP_FIT,
	SC_HELP_STRIP,
	SC_HELP_PLAY,
	SC_HELP_SPACE,
	SC_HELP_CH_PART,
	SC_HELP_REST,
	SC_HELP_TUPLET,
	SC_HELP_ACCIDENTAL,
	SC_HELP_NEST,
	SC_HELP_REPLACE,
	SC_HELP_PAL_NOTE,
	SC_HELP_METER,
	SC_HELP_COUNT
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
	int stripCount; /* 0..SC_STRIP_LANES_MAX (0 = no strip) */
	int stripKind[SC_STRIP_LANES_MAX];
	int stripLaneHgt[SC_STRIP_LANES_MAX]; /* ScStripHeight */
	int stripDraw; /* pencil / line / curve */
	int stripStepTicks; /* column = 1 beat fraction: PPQN..PPQN/16 (1/4..1/64) */
	uint8_t strip[SC_STRIP_LANES_MAX][SC_STRIP_COLS_MAX];
	int stripLineAnchorCol;
	int stripLineAnchorVal;
	int stripLineLane;
	/* Per-part remembered strip layout (loaded when channel changes). */
	int partStripCount[32];
	int partStripKind[32][SC_STRIP_LANES_MAX];
	int partStripLaneHgt[32][SC_STRIP_LANES_MAX];
	/* Global tempo band (top of score grid) + per-part CC bands. */
	int globalTempoBandOn;
	int globalTempoBandHgt; /* ScStripHeight */
	uint8_t globalTempoStrip[SC_STRIP_COLS_MAX]; /* BPM 20..255 per column */
	int partBandMask[32]; /* bitmask ScPartBandKind */
	int partBandHgt[32][SC_PBAND_KIND_COUNT];
	int bandEditTrack; /* -2=global tempo, -1=none */
	int bandEditKind; /* ScPartBandKind */
	int bandEditCol;
	uint8_t bandEditBuf[SC_STRIP_COLS_MAX];
	int helpTopic; /* ScHelpTopic */
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
	uint32_t previewOriginTick; /* wav-from-marker: playhead = origin + heard */
	int previewWavMode;        /* 1 = score preview via wavout (ignore MIDI seek) */
	int followViewW;       /* score grid width for playhead auto-scroll (0=off) */
	int clef[32];          /* 0=treble 1=bass 2=grand (G+F) 3=drum/kit */
	wchar_t vstLabel[32][40]; /* Tone row: VST basename / FM voice name */
	wchar_t progLabel[32][40]; /* Prog row under Tone: "Prog 68" / "ALG3 FB5" */
	int dragMode;          /* 0=none 1=resize-dur 2=move-pitch/tick */
	int meterNumer;        /* time signature numerator (default 4) */
	int meterDenom;        /* time signature denominator (default 4) */
	int isFmScore;         /* 1 = FM/OPNA score dialog */
	/* Per-track written note range for ledger pad (updated in ScStaffUpdateContentExtent). */
	int writtenLo[32];
	int writtenHi[32];
	int writtenValid[32];
	/* DAW upgrade */
	int followMode; /* 0=off 1=center 2=page */
	int keySig; /* -7..+7 (flats..sharps) */
	int showRollSplit; /* embed piano roll */
	int chordMode; /* sticky: pencil places full chord until unchecked */
	int chordType; /* ScChordType */
	int chordVoices; /* 2..5 */
	int patternMode; /* sticky: pencil places rhythm pattern until unchecked */
	int patternId; /* ScPatternId */
	int gridEmph; /* emphasize beat/bar lines */
};

/* Ticks in one measure from meter (4/4 → 4*PPQN, 7/8 → 7*(PPQN/2)). */
int ScStaffTicksPerMeasure(const ScStaffUi* u);
int ScStaffBeatTicks(int denom);
int ScStaffMeterTicksPerMeasure(int numer, int denom);
void ScStaffMeterAtTick(const ScEvent* ev, int evCount, uint32_t tick,
	int defNumer, int defDenom, int* outNumer, int* outDenom);
uint32_t ScStaffSnapToBarTick(const ScEvent* ev, int evCount, uint32_t tick,
	int defNumer, int defDenom);
void ScStaffDrawTimeSignature(CDC& dc, int cx, int staffTop, int gap, int numer, int denom, COLORREF col);
void ScStaffSetMeter(ScStaffUi* u, int numer, int denom);

int ScStaffLineGap(const ScStaffUi* u);
int ScStaffH(const ScStaffUi* u); /* one 5-line staff height */
int ScStaffLedgerPad(const ScStaffUi* u, int track); /* max(above,below) — prefer Above/Below */
int ScStaffLedgerPadAbove(const ScStaffUi* u, int track);
int ScStaffLedgerPadBelow(const ScStaffUi* u, int track);
int ScStaffRowStaffTop(const ScStaffUi* u, int track, int rowTop);
/* Y where note ledger/staff begins (below Tone/Exc gauge + part bands). */
int ScStaffRowNoteAreaTop(const ScStaffUi* u, int track, int rowTop);
int ScStaffGlobalTempoBandH(const ScStaffUi* u);
int ScStaffGridHeaderH(const ScStaffUi* u);
int ScStaffGridBodyTop(int gridTop, const ScStaffUi* u);
int ScStaffPartBandHeightPx(const ScStaffUi* u, int track, int band);
int ScStaffPartBandsH(const ScStaffUi* u, int track);
int ScStaffPartBandToStripKind(int band);
const wchar_t* ScStaffPartBandName(int band);
const wchar_t* ScStaffPartBandMmlHint(int band);
int ScStaffRowH(const ScStaffUi* u, int track); /* visible row incl. grand / stub */
int ScStaffStripTotalH(const ScStaffUi* u);
int ScStaffStripLaneHeightPx(const ScStaffUi* u, int lane);
const wchar_t* ScStaffStripHeightName(int hgt);
int ScStaffStripKindBipolar(int kind);
int ScStaffStripKindFromCc(int cc);
void ScStaffLoadStripPrefsFromSave(ScStaffUi* u);
void ScStaffSaveStripPrefsToSave(const ScStaffUi* u);
int ScStaffClampNote(int noteMidi);
int ScStaffHTrack(const ScStaffUi* u, int track); /* taller for OPNA RHY */

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
int ScStaffMidiNoteYTrack(const ScStaffUi* u, int track, int staffTop, int noteMidi,
	uint32_t atTick = 0, const ScEvent* ev = NULL, int evCount = 0);
int ScStaffYToMidiNoteTrack(const ScStaffUi* u, int track, int staffTop, int y,
	uint32_t atTick = 0, const ScEvent* ev = NULL, int evCount = 0);
/* Ottava (8va/8vb/16/32): active signed octave shift at tick (+1 = 8va). Score display only. */
int ScStaffOttavaOctaves(const ScEvent* ev, int evCount, int ch, uint32_t tick);
const wchar_t* ScStaffOttavaLabel(int octaves); /* "8va" / "8vb" / "16va" / … / "loco" */
int ScStaffSoundingToWritten(int soundingMidi, int octaves);
int ScStaffWrittenToSounding(int writtenMidi, int octaves);
/* Sounding MIDI of note event; written = sounding − 12*ottava(at note tick). */
int ScStaffEvSoundingMidi(const ScEvent& e, int isFm, int tr);
int ScStaffEvWrittenMidi(const ScEvent* ev, int evCount, const ScEvent& e, int isFm, int tr);
int ScStaffBarHasLayoutZone(const ScEvent* ev, int evCount, uint32_t barTick);
int ScStaffLayoutPrefixPx(const ScEvent* ev, int evCount, uint32_t tick, const ScStaffUi* u);
int ScStaffScoreContentWidthPx(const ScStaffUi* u, const ScEvent* ev, int evCount, uint32_t contentTicks);
int ScStaffClefModeAt(const ScStaffUi* u, int track, uint32_t tick, const ScEvent* ev, int evCount);
int ScStaffKeySigAtTick(const ScEvent* ev, int evCount, uint32_t tick, int defKey);
int ScStaffIsInLayoutZone(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount, CPoint pt);
int ScStaffTickToX(uint32_t tick, int scrollX, int gridLeft, int pxBeat,
	const ScStaffUi* u = NULL, const ScEvent* ev = NULL, int evCount = 0);
uint32_t ScStaffXToTick(int x, int scrollX, int gridLeft, int pxBeat, int quantTicks,
	const ScStaffUi* u = NULL, const ScEvent* ev = NULL, int evCount = 0);
void ScStaffPaintTracks(CDC& dc, const CRect& rc, const ScStaffUi* u, int curTrack,
	const ScEvent* ev, int evCount);
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
/* Part enable (visible[]): auto-detect used MIDI/FM parts, apply saved bitmask, pack mask. */
int ScStaffMidiChInUse(const ScMidiDoc* d, const ScEvent* ev, int evCount, int ch);
int ScStaffFmChInUse(const ScFmDoc* d, const ScEvent* ev, int evCount, int ch);
void ScStaffAutoEnableUsedParts(ScStaffUi* u, const ScMidiDoc* md, const ScFmDoc* fd,
	const ScEvent* ev, int evCount);
void ScStaffApplyPartMask(ScStaffUi* u, unsigned partMask);
unsigned ScStaffPackPartMask(const ScStaffUi* u);
void ScStaffRefreshPartEnabled(ScStaffUi* u, const ScMidiDoc* md, const ScFmDoc* fd,
	const ScEvent* ev, int evCount, unsigned savedMask);
int ScStaffIsExtendedChannelView(const ScStaffUi* u);
void ScStaffSetChannelView16(ScStaffUi* u);
void ScStaffSetChannelViewAll(ScStaffUi* u);
int ScStaffHitPartEnable(const CRect& trackRc, const ScStaffUi* u, CPoint pt, int* outTrack);
/* Hit Tone lane (0) or Exc/SysEx lane (1) on staff grid or left track Tone/Prog gauge bands. */
int ScStaffHitToneExcLane(const CRect& grid, const ScStaffUi* u, CPoint pt, int* outTrack, int* outLane,
	const CRect* trackCol = NULL);
uint32_t ScStaffMarkerTickAtGridX(const CRect& grid, const ScStaffUi* u, const ScEvent* ev, int evCount, int x);
/* Move red-bar marker to click X when in Tone/Exc bands (track column maps X onto grid). */
int ScStaffSnapMarkerToToneExcClick(const CRect& grid, const CRect& trackCol,
	ScStaffUi* u, const ScEvent* ev, int evCount, CPoint pt, int* outTrack);
int ScStaffHitRulerTick(const CRect& grid, const ScStaffUi* u, CPoint pt, uint32_t* outTick,
	const ScEvent* ev = NULL, int evCount = 0);
void ScStaffEnsureStripFromDoc(ScStaffUi* u, const ScEvent* ev, int evCount, int track);
void ScStaffEnsureGlobalTempoFromDoc(ScStaffUi* u, const ScEvent* ev, int evCount, int defBpm);
void ScStaffEnsurePartBandFromDoc(const ScStaffUi* u, const ScEvent* ev, int evCount, int track, int band, uint8_t* out);
void ScStaffApplyGlobalTempoToDoc(ScMidiDoc* d, const ScStaffUi* u);
void ScStaffApplyGlobalTempoToDocFm(ScFmDoc* d, const ScStaffUi* u);
void ScStaffApplyPartBandToDoc(ScMidiDoc* d, int track, int band, const uint8_t* strip, const ScStaffUi* u);
void ScStaffApplyPartBandToDocFm(ScFmDoc* d, int track, int band, const uint8_t* strip, const ScStaffUi* u);
void ScStaffLoadBandPrefsFromSave(ScStaffUi* u);
void ScStaffSaveBandPrefsToSave(const ScStaffUi* u);
int ScStaffHasBandEditing(const ScStaffUi* u);
void ScStaffBandFillRange(uint8_t* buf, int colMax, int c0, int v0, int c1, int v1, int vmin, int vmax);
void ScStaffBandFillCurve(uint8_t* buf, int colMax, int c0, int v0, int c1, int v1, int vmin, int vmax);
void ScStaffBandDragStroke(ScStaffUi* u, uint8_t* buf, int anchorCol, int anchorVal, int col, int val, int isTempo);
/* Last column (inclusive) of a flat segment starting at colStart. */
int ScStaffBandSegmentEndCol(const uint8_t* buf, int colMax, int colStart);
/* Replace flat segment at colStart with newVal (until next tempo/CC breakpoint). */
void ScStaffBandFillHoldSegment(uint8_t* buf, int colMax, int colStart, int newVal, int vmin, int vmax);
void ScStaffApplyTempoAtMarker(ScMidiDoc* d, ScStaffUi* u, const ScEvent* ev, int evCount, uint32_t markerTick, int bpm);
void ScStaffApplyTempoAtMarkerFm(ScFmDoc* d, ScStaffUi* u, const ScEvent* ev, int evCount, uint32_t markerTick, int bpm);
int ScStaffHitGlobalTempoBand(const CRect& grid, const ScStaffUi* u, CPoint pt, int* outCol, int* outBpm,
	const ScEvent* ev = NULL, int evCount = 0);
int ScStaffHitPartBand(const CRect& grid, const ScStaffUi* u, CPoint pt, int* outTrack, int* outBand, int* outCol, int* outVal,
	const ScEvent* ev = NULL, int evCount = 0);
int ScStaffBandColValFromPt(const CRect& grid, const ScStaffUi* u, int track, int band, int isTempo, CPoint pt, int* outCol, int* outVal,
	const ScEvent* ev = NULL, int evCount = 0);
int ScStaffHitStrip(const CRect& stripRc, const ScStaffUi* u, CPoint pt, int* outLane, int* outCol, int* outVal);
void ScStaffApplyStripToDocMidi(ScMidiDoc* d, int track, const ScStaffUi* u);
void ScStaffApplyStripToDocFm(ScFmDoc* d, int track, const ScStaffUi* u);
/* Strip column grid: step = 1/4..1/64 note (PPQN .. PPQN/16). */
int ScStaffStripStepTicks(const ScStaffUi* u);
int ScStaffStripColCount(const ScStaffUi* u);
int ScStaffStripTickToCol(const ScStaffUi* u, uint32_t tick);
uint32_t ScStaffStripColToTick(const ScStaffUi* u, int col);
void ScStaffNormalizeStripStep(ScStaffUi* u);
int ScStaffVisibleLaneStaffTop(const CRect& grid, const ScStaffUi* u, int track);
int ScStaffTrackRowTop(const CRect& trackRc, const ScStaffUi* u, int track);
int ScStaffContentHeight(const ScStaffUi* u);

int ScStaffBpmFromTempoT(int tempoT);
int ScStaffTempoTFromBpm(int bpm);
int ScStaffStripKindEmitMml(const ScEvent& e, wchar_t* out, int outCch);
double ScStaffSecFromTick(uint32_t tick, int tempoT);
uint32_t ScStaffTickFromSec(double sec, int tempoT);
/* Build temp already written: add to playlist and start playback. Returns 1 on start. */
int ScStaffStartHostPreview(LPCTSTR path, const ScStaffUi* u, int tempoT);
/* Stop host preview (Space toggle / dialog close). */
void ScStaffStopHostPreview(ScStaffUi* u);
/* Live host play of baked score (mpw2/mpsmv/fpy). Prefer over wavout when VST-bound. */
int ScStaffPreviewViaHost(LPCTSTR builtPath, ScStaffUi* u, int tempoT);
/* Wavout built path → temp WAV → host play (score-local, from previewOriginTick). */
int ScStaffPreviewViaWavout(LPCTSTR builtPath, ScStaffUi* u, int tempoT);
/* Shift/copy events so marker becomes tick 0 (for from-red-bar wav preview). */
int ScStaffCopyEventsFromMarker(const ScEvent* src, int srcN, uint32_t marker,
	ScEvent* dst, int dstMax, int* outN);
/* Sync playhead from host clock; soft A-B loop. Returns 1 if UI should invalidate. */
int ScStaffSyncPreviewPlayhead(ScStaffUi* u, int tempoT);
/* Append EQ band event into command-roll prompt text and auto-ON (UI need not be open). */
int ScStaffWriteCmdRollEq(double t0Sec, double t1Sec, int bandVal0, int bandVal1);

void ScStaffDrawNoteGlyph(CDC& dc, const CRect& rc, int durTicks, int rest, int selected);
void ScStaffDrawPalCell(CDC& dc, const CRect& rc, int selected);
void ScStaffDrawPalRadioCell(CDC& dc, const CRect& rc, const wchar_t* label, int selected);
void ScStaffDrawPalRadioRow(CDC& dc, const CRect& rc, const wchar_t* label, int selected, int hot);
void ScStaffDrawPalTab(CDC& dc, const CRect& rc, const wchar_t* label, int selected);
const wchar_t* ScStaffKeySigName(int keySig);
const wchar_t* ScStaffKeySigMajorName(int keySig);
const wchar_t* ScStaffKeySigMinorName(int keySig);
int ScStaffKeySigPixelWidth(int keySig);
int ScStaffMarginPixelWidth(int clefMode, int keySig, int drawMeter);
int ScStaffClefMarginPx(const ScStaffUi* u, const ScEvent* ev, int evCount);
int ScStaffGridLeftPx(int gridLeftEdge, const ScStaffUi* u, const ScEvent* ev, int evCount);
void ScStaffPaintKeySignature(CDC& dc, int x0, int staffTop, int gap, int staffH, int clefMode, int keySig);
void ScStaffDrawMarginKeyAndMeter(CDC& dc, int marginLeft, int staffTop, int gap, int staffH,
	int clefMode, int keySig, int meterN, int meterD, COLORREF col);
void ScStaffDrawLayoutPreview(CDC& dc, const CRect& rc, int clefMode, int keySig, int meterN, int meterD);
void ScStaffTimeSigOrigin(int clefMode, int staffTop, int staffH, int gap, int keySig, int gridLeft, int* outX, int* outY);
void ScStaffDrawNoteGlyphPal(CDC& dc, const CRect& rc, int durTicks, int rest, int selected, int accidental);
int ScStaffMeterFromPalCmd(int cmdId, int* numer, int* denom);
/* One call for note or rest: shape follows durTicks (PPQN ticks). */
void ScStaffDrawSymbol(CDC& dc, int x, int y, int durTicks, int rest, COLORREF col, int selected, int stemUp);
/* accidental: -1 flat, 0 none/natural glyph omitted, +1 sharp — drawn left of head/rest. */
void ScStaffDrawSymbolEx(CDC& dc, int x, int y, int durTicks, int rest, COLORREF col,
	int selected, int stemUp, int accidental);
void ScStaffDrawRestGlyph(CDC& dc, int x, int y, int durTicks, COLORREF col);
HCURSOR ScStaffCreateBlankCursor(void);

typedef struct ScStaffDrawCursors {
	HCURSOR pencil;
	HCURSOR line;
	HCURSOR curve;
} ScStaffDrawCursors;
void ScStaffDrawCursorsInit(ScStaffDrawCursors* c);
void ScStaffDrawCursorsFree(ScStaffDrawCursors* c);
HCURSOR ScStaffDrawCursorPick(const ScStaffDrawCursors* c, int stripDraw);
/* 1=tempo 2=part band 3=bottom strip; for cursor / band-edit priority. */
int ScStaffPtInBandEditZone(const CRect& grid, const CRect& stripRc, const ScStaffUi* u, CPoint pt,
	const ScEvent* ev, int evCount);
const wchar_t* ScStaffStripKindName(int kind);
const wchar_t* ScStaffStripKindNameFm(int kind); /* FM score strip combo labels */
const wchar_t* ScStaffStripKindNameJp(int kind); /* alias → localized name */
const wchar_t* ScStaffStripDrawModeName(int mode); /* pencil/line/curve */
const wchar_t* ScStaffStripLanesLabel(int lanes); /* 0..SC_STRIP_LANES_MAX */
const wchar_t* ScStaffToolName(int tool);
/* Fill bottom help bar from topic + current tool/strip state. */
void ScStaffFormatHelpBar(wchar_t* out, int cch, const ScStaffUi* u, int isFm, int curPart);
void ScStaffSavePartStrip(ScStaffUi* u, int part);
void ScStaffLoadPartStrip(ScStaffUi* u, int part);

void ScStaffUpdateContentExtent(ScStaffUi* u, const ScEvent* ev, int evCount);
int ScStaffScrollTrackW(void);
int ScStaffScrollTrackH(void);
int ScStaffScrollGutterW(void);
int ScStaffScrollGutterH(void);
void ScStaffZoomPxBeat(ScStaffUi* u, int delta);
void ScStaffZoomStaffScale(ScStaffUi* u, int delta);
/* Draw thick visible H/V scroll thumbs in reserved sb margins (SSW-like). */
void ScStaffPaintScrollThumbs(CDC& dc, const CRect& client, const CRect& body, const CRect& view,
	const ScStaffUi* u, int pageW, int pageH,
	const ScEvent* ev = NULL, int evCount = 0);
/* Hit-test scroll/zoom. 0=none 1=vert 2=horz 3=hZoom- 4=hZoom+ 5=vZoom- 6=vZoom+ */
int ScStaffHitScroll(const CRect& client, const CRect& body, const CRect& view, const ScStaffUi* u,
	int pageW, int pageH, CPoint pt, int* outPos,
	const ScEvent* ev = NULL, int evCount = 0);
BOOL ScStaffPtOnVertThumb(const ScStaffUi* u, int pageH, const CRect& client, const CRect& view, CPoint pt);
BOOL ScStaffPtOnHorzThumb(const ScStaffUi* u, int pageW, const CRect& client, const CRect& body,
	CPoint pt, const ScEvent* ev, int evCount);
int ScStaffMapVertScrollJump(const ScStaffUi* u, int pageH, const CRect& client, const CRect& view, int ptY);
int ScStaffMapVertScrollDrag(const ScStaffUi* u, int pageH, const CRect& client, const CRect& view,
	int ptY, int anchorY, int scroll0);
int ScStaffMapHorzScrollJump(const ScStaffUi* u, int pageW, const CRect& client, const CRect& body,
	int ptX, const ScEvent* ev, int evCount);
int ScStaffMapHorzScrollDrag(const ScStaffUi* u, int pageW, const CRect& client, const CRect& body,
	int ptX, int anchorX, int scroll0, const ScEvent* ev, int evCount);

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
