#pragma once
/* Shared SASAMI Composer document: score events + text MML compile (fixed arrays, no std). */
#include <stdint.h>
#include <windows.h>
#include "kb_sasami/source/sasami_write.h"

class CWnd;

enum { SC_MIDI_CH = 32 };
enum { SC_FM_CH = 10 };
enum { SC_FM_MISAO = 16 };
enum { SC_FM_TOTAL = SC_FM_CH + SC_FM_MISAO };
enum { SC_EV_MAX = 65536 };
enum { SC_TEXT_MAX = 1024 * 1024 };
enum { SC_VOICE_MAX = 64 };
enum { SC_PPQN = 48 };
/* Native |:…:| nest depth (ASM was 1; player supports up to 16 / FPY2). */
enum { SC_LOOP_NEST_MAX = 16 };

enum ScEvKind : uint8_t {
	SC_EV_NOTE = 1,
	SC_EV_REST = 2,
	SC_EV_TEMPO = 3,
	SC_EV_PROG = 4,
	SC_EV_BANK = 5,
	SC_EV_VOL = 6,
	SC_EV_PAN = 7,
	SC_EV_VELO = 8,
	SC_EV_TIE = 9,
	SC_EV_FM_NOTE = 10,
	SC_EV_FM_REST = 11,
	SC_EV_FM_VOICE = 12,
	SC_EV_FM_VOL = 13,
	SC_EV_FM_TEMPO = 14,
	SC_EV_COMMENT = 15,
	SC_EV_FM_LOOP_START = 16, /* a = repeat count (PMD |:n) */
	SC_EV_FM_LOOP_END = 17,   /* native FPY cmd 14 */
	SC_EV_FM_JUMP = 18,       /* FJUMP / J — a/b = placeholder */
	SC_EV_JUMP_MARK = 19,     /* Q: soft-J land (before body); no MPY opcode */
	SC_EV_PITCH = 20,         /* MIDI pitch bend; a=0..127 center 64 */
	SC_EV_FM_PITCH = 21,      /* FM/Misao FPICH/PMPICH; a=0..127 center 64 */
	SC_EV_PCM_SAMPLE = 22,    /* Misao: a=slot, path in ScFmDoc.pcmRelPath[ch-SC_FM_CH] */
	SC_EV_RPN = 23,           /* MIDI RPN: a=MSB b=LSB c=data */
	SC_EV_NRPN = 24,          /* MIDI NRPN: a=MSB b=LSB c=data */
	SC_EV_SYSEX = 25,         /* MIDI SysEx: a=ScMidiDoc sysex pool index */
	SC_EV_PEDAL_ON = 26,      /* MIDI sustain CC64=127 (MPY cmd 20) */
	SC_EV_PEDAL_OFF = 27,     /* MIDI sustain CC64=0 (MPY cmd 21) */
	SC_EV_OTTAVA = 28,        /* score ottava: a=(int8_t)octaves +1=8va … +3=32va / -1=8vb … */
	SC_EV_OTTAVA_END = 29,    /* loco — cancel ottava (score display / place only) */
	SC_EV_SLUR_START = 30,    /* expression layer */
	SC_EV_SLUR_END = 31,
	SC_EV_CRESC = 32,         /* hairpin start; dur = span ticks */
	SC_EV_DIM = 33,
	/* --- mpsmv / fpy2 Wave3 --- */
	SC_EV_CC = 34,            /* MIDI CC: a=cc#, b=value (cmd 41) → mpsmv */
	SC_EV_FM_EX = 35,         /* FM EX1–4: a=1..4, b=data → FPY cmd via EX map */
	SC_EV_FM_LFO = 36,        /* a=AMS/PMS (cmd21), b=LFO enable (cmd22) */
	SC_EV_FM_DETUNE = 37,     /* a/b = little-endian raw (0x8000 center) → cmd18 */
	SC_EV_FM_LEGATO = 38,     /* note without key-off: a=noteByte, dur=wait → cmd24 */
	SC_EV_FM_FSLR = 39,       /* wait without key-off: dur=wait → cmd10 */
	SC_EV_FM_FLR = 40,        /* stereo B4 high bits: a=LR mask → cmd16 */
	SC_EV_SOFT_VIB = 41,      /* a=mode(0=vib,1=trem), b=delayLen, c=depth */
	SC_EV_SOFT_PORTA = 42,    /* a=semitone delta+64, b=delayLen, c=glideLen */
	SC_EV_METER = 43,         /* time signature change: a=numer, b=denom (global, ch=0) */
	SC_EV_CLEF = 44,          /* clef change (display): ch=part, a=0G 1F 2grand 3drum */
	SC_EV_KEY = 45            /* key signature (display): a=signed sharps -7..+7, ch=0 */
};

enum { SC_MACRO_MAX = 32 };
enum { SC_MACRO_NAME = 32 };
enum { SC_MACRO_BODY = 2048 };

/* Note event flags (ScEvent.flags). */
enum ScEvFlags : uint8_t {
	SC_EF_STEM_AUTO = 0,
	SC_EF_STEM_UP = 1,
	SC_EF_STEM_DOWN = 2,
	SC_EF_STEM_MASK = 3,
	SC_EF_BEAM_BREAK = 4,
	SC_EF_ACC_SHARP = 8,
	SC_EF_ACC_FLAT = 16,
	SC_EF_ACC_NAT = 24,
	SC_EF_ACC_MASK = 24
};

struct ScEvent {
	uint32_t tick;
	uint32_t seq; /* emission order — required so :||: keeps END before next START */
	uint8_t ch;
	uint8_t kind;
	uint8_t a, b, c;
	uint16_t dur; /* note length in ticks */
	uint8_t flags; /* ScEvFlags — stem/beam/accidental display */
	int8_t pad0;
};

/* VST bind only — do NOT embed SasamiWriteMidi (16MB track streams) in the live doc. */
struct ScMidiVstBind {
	int isMpw3;
	int dualPort;
	wchar_t vstPath[32][260];
	int vstProg[32];
	int vstBankMsb[32];
	int vstBankLsb[32];
	int vstForceCh[32]; /* -1 = none */
	uint8_t* vstComp[32];
	uint32_t vstCompLen[32];
	uint8_t* vstCtrl[32];
	uint32_t vstCtrlLen[32];
};

struct ScMidiFxBind {
	enum { SC_FX_SLOTS = 2 };
	wchar_t fxPath[32][SC_FX_SLOTS][260];
	int fxBypass[32][SC_FX_SLOTS];
	uint8_t* fxState[32][SC_FX_SLOTS];
	uint32_t fxStateLen[32][SC_FX_SLOTS];
};

void ScMidiVstBindClear(ScMidiVstBind* b);
void ScMidiVstBindFreeStates(ScMidiVstBind* b);
void ScMidiVstBindSetState(ScMidiVstBind* b, int ch0to31,
	const uint8_t* comp, uint32_t compLen,
	const uint8_t* ctrl, uint32_t ctrlLen);
void ScMidiFxBindClear(ScMidiFxBind* b);
void ScMidiFxBindFreeStates(ScMidiFxBind* b);
void ScMidiFxBindSetState(ScMidiFxBind* b, int ch0to31, int slot0to1,
	const uint8_t* state, uint32_t stateLen);


struct ScMacroTable {
	wchar_t name[SC_MACRO_MAX][SC_MACRO_NAME];
	wchar_t body[SC_MACRO_MAX][SC_MACRO_BODY];
	int count;
};

struct ScMidiDoc {
	ScEvent ev[SC_EV_MAX];
	int evCount;
	int tempoT; /* SASAMI T, 13000 ≈ 120BPM */
	int numer, denom;
	char titleSjis[65];
	ScMidiVstBind bind; /* VST paths / prog for MPW3 — streams built at write time */
	ScMidiFxBind fxBind; /* per-part insert FX paths/state for score/live preview */
	enum { SC_SYSEX_MAX = 64, SC_SYSEX_BYTES = 256 };
	uint8_t sysex[SC_SYSEX_MAX][SC_SYSEX_BYTES];
	uint16_t sysexLen[SC_SYSEX_MAX];
	int sysexCount;
	/* MICP [midiCh:dataArea]: events use dataArea-1 as track slot; part = midiCh-1 */
	uint8_t trackPart[SC_MIDI_CH];
	/* Wave3: dedicated MML / soft-loop / macros → .mpsmv */
	int needMpsmv;
	int usedSoftLoopNative; /* {: :} emitted as |: :| (not expand) */
	ScMacroTable macros;
};

struct ScFmDoc {
	ScEvent ev[SC_EV_MAX];
	int evCount;
	int tempoT;
	int opna10;
	char titleSjis[65];
	uint8_t voices[SC_VOICE_MAX][25];
	int voiceCount;
	/* Misao PCM: path per Misao track. Absolute until fpy2 save; then relative beside file. */
	wchar_t pcmRelPath[SC_FM_MISAO][260];
	uint8_t pcmSlot[SC_FM_MISAO];
	uint8_t pcmAbsUntilSave[SC_FM_MISAO]; /* 1 = full path, not copied yet */
	int needFpy2; /* dedicated cmds / Misao PCM / soft-loop native / macros */
	int usedSoftLoopNative;
	int numer, denom; /* score display meter (default 4/4) */
	ScMacroTable macros;
};

struct ScTextBuf {
	wchar_t text[SC_TEXT_MAX / sizeof(wchar_t)];
	int len;
};

void ScMidiDocClear(ScMidiDoc* d);
void ScFmDocClear(ScFmDoc* d);

/* Score helpers */
int ScMidiAddNote(ScMidiDoc* d, uint32_t tick, int ch, int note, int dur, int vel, int gatePct = 100);
int ScMidiAddRest(ScMidiDoc* d, uint32_t tick, int ch, int dur);
/* Prog/Bank at tick; same-tick events ordered before notes (like Q/J marks). */
int ScMidiApplyProgBankAt(ScMidiDoc* d, int ch0, uint32_t tick, int prog, int msb, int lsb);
/* Score marks: Q (jump land), J (jump), |:n (loop start), :| (loop end). */
int ScMidiAddJumpMark(ScMidiDoc* d, uint32_t tick, int ch, int stack); /* Q; stack=0 toggles */
int ScMidiAddJump(ScMidiDoc* d, uint32_t tick, int ch, int stack);     /* J; stack=0 toggles */
int ScMidiAddLoopStart(ScMidiDoc* d, uint32_t tick, int ch, int repeatN, int stack);
int ScMidiAddLoopEnd(ScMidiDoc* d, uint32_t tick, int ch, int stack);
int ScMidiAddPedalOn(ScMidiDoc* d, uint32_t tick, int ch, int stack);
int ScMidiAddPedalOff(ScMidiDoc* d, uint32_t tick, int ch, int stack);
/* octaves: ±1=8va/vb, ±2=16va/vb, ±3=32va/vb. 0 = loco (OTTAVA_END). */
int ScMidiAddOttava(ScMidiDoc* d, uint32_t tick, int ch, int octaves, int stack);
int ScMidiAddOttavaEnd(ScMidiDoc* d, uint32_t tick, int ch, int stack);
/* Delete all events of kind at tick+ch. Returns count removed. */
int ScDeleteMarksAt(ScEvent* ev, int* evCount, uint32_t tick, int ch, uint8_t kind);
/* stack=0: same kind at tick removes (toggle). stack=1: always push. */
int ScMarkKindExists(const ScEvent* ev, int n, uint32_t tick, uint8_t ch, uint8_t kind);
int ScToggleOrAddMark(ScEvent* ev, int* n, uint32_t tick, uint8_t ch, uint8_t kind,
	uint8_t a, uint8_t b, uint8_t c, uint16_t dur, int stack);
int ScMidiAddRpn(ScMidiDoc* d, uint32_t tick, int ch, int msb, int lsb, int data);
int ScMidiAddNrpn(ScMidiDoc* d, uint32_t tick, int ch, int msb, int lsb, int data);
int ScMidiAddSysex(ScMidiDoc* d, uint32_t tick, int ch, const uint8_t* bytes, int len);
int ScFmAddNote(ScFmDoc* d, uint32_t tick, int ch, uint8_t noteByte, int dur);
int ScFmAddRest(ScFmDoc* d, uint32_t tick, int ch, int dur);
int ScFmAddJumpMark(ScFmDoc* d, uint32_t tick, int ch, int stack);
int ScFmAddJump(ScFmDoc* d, uint32_t tick, int ch, int stack);
int ScFmAddLoopStart(ScFmDoc* d, uint32_t tick, int ch, int repeatN, int stack);
int ScFmAddLoopEnd(ScFmDoc* d, uint32_t tick, int ch, int stack);
int ScFmAddOttava(ScFmDoc* d, uint32_t tick, int ch, int octaves, int stack);
int ScFmAddOttavaEnd(ScFmDoc* d, uint32_t tick, int ch, int stack);
int ScFmAllocVoice(ScFmDoc* d, const uint8_t voice25[25]); /* returns index or -1 */
int ScFmAddVoiceSelect(ScFmDoc* d, uint32_t tick, int ch, int voiceIdx, int isCustom);
int ScFmAddVolTl(ScFmDoc* d, uint32_t tick, int ch, int tl);
int ScFmAddPcmSample(ScFmDoc* d, uint32_t tick, int ch, int slot);
int ScMidiAddCc(ScMidiDoc* d, uint32_t tick, int ch, int cc, int val);
int ScMidiAddMeter(ScMidiDoc* d, uint32_t tick, int numer, int denom);
int ScFmAddMeter(ScFmDoc* d, uint32_t tick, int numer, int denom);
int ScMidiAddClef(ScMidiDoc* d, uint32_t tick, int ch, int clef);
int ScFmAddClef(ScFmDoc* d, uint32_t tick, int ch, int clef);
int ScMidiAddKey(ScMidiDoc* d, uint32_t tick, int keySig);
int ScFmAddKey(ScFmDoc* d, uint32_t tick, int keySig);
int ScFmAddEx(ScFmDoc* d, uint32_t tick, int ch, int exN, int data);
int ScFmAddLfo(ScFmDoc* d, uint32_t tick, int ch, int amsPms, int enable);
int ScFmAddDetune(ScFmDoc* d, uint32_t tick, int ch, int rawCentered);
int ScFmAddFlr(ScFmDoc* d, uint32_t tick, int ch, int lrMask);
int ScAddSoftVib(ScEvent* ev, int* n, uint32_t tick, int ch, int mode, int delayLen, int depth);
int ScAddSoftPorta(ScEvent* ev, int* n, uint32_t tick, int ch, int semiDelta, int delayLen, int glideLen);

/* Format gate: scan text/doc for dedicated features. */
int ScTextNeedsMpsmv(const wchar_t* text);
int ScTextNeedsFpy2(const wchar_t* text);
int ScMidiDocNeedsMpsmv(const ScMidiDoc* d);
int ScFmDocNeedsFpy2(const ScFmDoc* d);
/* On fpy2 save: copy absolute PCM beside outPath and rewrite to relative. */
int ScFmDocCommitPcmBesideFpy2(ScFmDoc* d, const wchar_t* fpy2Path);

/* Compile MML / MML2 / MML3 text (UTF-16) into midi doc. errLine out optional. */
int ScCompileMidiMml(const wchar_t* text, ScMidiDoc* out, int* errLine, wchar_t* errMsg, int errMsgCch);

/* Emit MML text from midi doc (score → text sync). */
int ScMidiDocToMml(const ScMidiDoc* d, wchar_t* out, int outCch);
/* Same with full @VSTSTATEB64 lines (large; for expand/save). */
int ScMidiDocToMmlFull(const ScMidiDoc* d, wchar_t* out, int outCch);
/* Copy VST bind blobs/path from src when dst slot is empty (text↔score sync). */
void ScMidiDocMergeVstBind(ScMidiDoc* dst, const ScMidiDoc* src);

/* Last-session leaf inside oggYSEDbgm_uni_avx2.dat (DatArc). Extra reserved for future. */
int ScSessionSaveLastMidi(const ScMidiDoc* d, const wchar_t* mmlOpt);
int ScSessionLoadLastMidi(ScMidiDoc* d, wchar_t* mmlOut, int mmlCch);
int ScSessionSaveLastFm(const ScFmDoc* d, const wchar_t* mmlOpt);
int ScSessionLoadLastFm(ScFmDoc* d, wchar_t* mmlOut, int mmlCch);
void ScSessionClearLast(void);

/* Compile FM DAT/MML-like text into fm doc */
int ScCompileFmText(const wchar_t* text, ScFmDoc* out, int* errLine, wchar_t* errMsg, int errMsgCch);

uint32_t ScFmDocMaxTick(const ScFmDoc* d);
int ScFmDocNoteCount(const ScFmDoc* d);
/* Emit FM MML text from fm doc (score → text sync). */
int ScFmDocToMml(const ScFmDoc* d, wchar_t* out, int outCch);

/* Emit binary writers from docs */
int ScMidiDocToWrite(const ScMidiDoc* d, SasamiWriteMidi* w);
int ScFmDocToWrite(const ScFmDoc* d, SasamiWriteFm* w);
/* Per-channel |: / :| balance. 1=ok. Optional err buffer. */
int ScValidateLoopBalance(const ScEvent* ev, int n, int chMax, wchar_t* errMsg, int errMsgCch);
/* Max simultaneous |: depth across channels (0 = none). */
int ScDocMaxLoopNest(const ScEvent* ev, int n, int chMax);
const wchar_t* ScGetLastWriteErr(void);

/* Load .F wrapper → extract DAT text into wchar buffer */
int ScExtractOldFToText(const wchar_t* path, wchar_t* out, int outCch);

/* Encoding: load file as UTF-16 (BOM) or CP932 */
int ScLoadTextFileW(const wchar_t* path, wchar_t* out, int outCch);
int ScSaveTextFileW(const wchar_t* path, const wchar_t* text);

/* MIDI text: collapse/expand @VSTSTATEB64 / @VSTCTRLB64 blocks for editor display. */
int ScMmlContainsVstB64(const wchar_t* text);
int ScMmlCollapseVstB64(const wchar_t* in, wchar_t* out, int outCch);

/* ビルド済み曲を TranscodeExport（WAV/MP3/FLAC）へ渡す */
void ScOpenAudioExport(CWnd* owner, const wchar_t* builtPath);
