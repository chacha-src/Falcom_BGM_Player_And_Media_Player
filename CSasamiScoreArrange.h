#pragma once
#include "SasamiComposerDoc.h"
#include "CSasamiStaffCore.h"

enum ScArrangeScope : int {
	SC_ARR_SEL = 0,
	SC_ARR_PART = 1,
	SC_ARR_ALL = 2
};

enum ScArrangePreset : int {
	SC_ARR_HUMANIZE = 0,
	SC_ARR_PUSH_PULL = 1,
	SC_ARR_EXP_WAVE = 2,
	SC_ARR_DRUM_HUMAN = 3,
	SC_ARR_COUNT
};

enum ScChordType : int {
	SC_CHORD_OCTAVE = 0,
	SC_CHORD_POWER = 1,   /* root+5th */
	SC_CHORD_FIFTHS = 2,  /* root+5+oct */
	SC_CHORD_MAJOR = 3,
	SC_CHORD_MINOR = 4,
	SC_CHORD_SUS4 = 5,
	SC_CHORD_ADD9 = 6,
	SC_CHORD_MAJ7 = 7,
	SC_CHORD_MIN7 = 8,
	SC_CHORD_TYPE_COUNT
};

enum ScPatternId : int {
	SC_PAT_Q_1BAR = 0,
	SC_PAT_E_1BAR,
	SC_PAT_S_1BAR,
	SC_PAT_E_SS,      /* 8 + 16 + 16 per beat */
	SC_PAT_SS_E,      /* 16 + 16 + 8 */
	SC_PAT_TRIPLET_1BAR,
	SC_PAT_Q_REST_Q,  /* quarter rest quarter × bars */
	SC_PAT_DOTTED_E,
	SC_PAT_2BAR_E,
	SC_PAT_4BAR_Q,
	SC_PAT_8BAR_Q,
	SC_PAT_COUNT
};

int ScArrangeApply(ScEvent* ev, int* evCount, const ScStaffUi* u, int part,
	int scope, int preset, int strength /*0..100*/);
int ScChordIntervals(int type, int* outSemis, int maxOut); /* returns voice count 2..5 */
/* Build sounding MIDI pitches for a chord from root. Returns voice count. */
int ScChordBuildPitches(int rootMidi, int chordType, int voices, int* outMidi, int maxOut);
/* Place a chord at tick (MIDI SC_EV_NOTE). Returns notes added. */
int ScChordPlaceAt(ScEvent* ev, int* evCount, int evMax, int part,
	uint32_t tick, int rootMidi, int dur, int vel, int gate, int chordType, int voices,
	uint8_t noteKind /* SC_EV_NOTE or SC_EV_FM_NOTE */, int fmPack /*1=pack FM note byte*/);
int ScChordExpandNotes(ScEvent* ev, int* evCount, int evMax, int part,
	uint32_t t0, uint32_t t1, int chordType, int voices);
int ScChordFromSymbol(ScEvent* ev, int* evCount, int evMax, int part,
	uint32_t tick, int dur, int vel, const wchar_t* symbol, int octave);
int ScPatternPlace(ScEvent* ev, int* evCount, int evMax, int part,
	uint32_t tick, int note, int vel, int gate, int patternId, int bars,
	int meterNumer, int meterDenom, int ppqn);
/* Tick length of one ScPatternPlace call (for marker advance). */
int ScPatternSpanTicks(int patternId, int bars, int meterNumer, int meterDenom, int ppqn);
const wchar_t* ScArrangePresetName(int preset);
const wchar_t* ScChordTypeName(int type);
const wchar_t* ScPatternName(int id);
int ScDeleteNotesInRange(ScEvent* ev, int* evCount, int part, uint32_t t0, uint32_t t1);
int ScTransposeSelected(ScEvent* ev, int evCount, const ScStaffUi* u, int semis);
int ScTransposeChannel(ScEvent* ev, int evCount, int ch, int semis);
int ScTransposeAll(ScEvent* ev, int evCount, int semis);
int ScTransposeFmSelected(ScEvent* ev, int evCount, const ScStaffUi* u, int semis);
int ScTransposeFmChannel(ScEvent* ev, int evCount, int ch, int semis);
int ScTransposeFmAll(ScEvent* ev, int evCount, int semis);
int ScScaleDurSelected(ScEvent* ev, int evCount, const ScStaffUi* u, int mul, int div);
int ScBulkPropsSelected(ScEvent* ev, int evCount, const ScStaffUi* u, int vel, int gate, int applyVel, int applyGate);
