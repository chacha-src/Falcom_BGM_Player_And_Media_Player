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
	SC_EV_PEDAL_OFF = 27      /* MIDI sustain CC64=0 (MPY cmd 21) */
};

struct ScEvent {
	uint32_t tick;
	uint32_t seq; /* emission order — required so :||: keeps END before next START */
	uint8_t ch;
	uint8_t kind;
	uint8_t a, b, c;
	uint16_t dur; /* note length in ticks */
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
};

struct ScFmDoc {
	ScEvent ev[SC_EV_MAX];
	int evCount;
	int tempoT;
	int opna10;
	char titleSjis[65];
	uint8_t voices[SC_VOICE_MAX][25];
	int voiceCount;
	/* Misao PCM: relative sample path per Misao track (empty = classic number/midisynth) */
	wchar_t pcmRelPath[SC_FM_MISAO][260];
	uint8_t pcmSlot[SC_FM_MISAO];
};

struct ScTextBuf {
	wchar_t text[SC_TEXT_MAX / sizeof(wchar_t)];
	int len;
};

void ScMidiDocClear(ScMidiDoc* d);
void ScFmDocClear(ScFmDoc* d);

/* Score helpers */
int ScMidiAddNote(ScMidiDoc* d, uint32_t tick, int ch, int note, int dur, int vel);
int ScMidiAddRest(ScMidiDoc* d, uint32_t tick, int ch, int dur);
/* Score marks: Q (jump land), J (jump), |:n (loop start), :| (loop end). */
int ScMidiAddJumpMark(ScMidiDoc* d, uint32_t tick, int ch);     /* Q */
int ScMidiAddJump(ScMidiDoc* d, uint32_t tick, int ch);         /* J */
int ScMidiAddLoopStart(ScMidiDoc* d, uint32_t tick, int ch, int repeatN, int stack);
int ScMidiAddLoopEnd(ScMidiDoc* d, uint32_t tick, int ch, int stack);
int ScMidiAddPedalOn(ScMidiDoc* d, uint32_t tick, int ch, int stack);
int ScMidiAddPedalOff(ScMidiDoc* d, uint32_t tick, int ch, int stack);
int ScMidiAddRpn(ScMidiDoc* d, uint32_t tick, int ch, int msb, int lsb, int data);
int ScMidiAddNrpn(ScMidiDoc* d, uint32_t tick, int ch, int msb, int lsb, int data);
int ScMidiAddSysex(ScMidiDoc* d, uint32_t tick, int ch, const uint8_t* bytes, int len);
int ScFmAddNote(ScFmDoc* d, uint32_t tick, int ch, uint8_t noteByte, int dur);
int ScFmAddRest(ScFmDoc* d, uint32_t tick, int ch, int dur);
int ScFmAddJumpMark(ScFmDoc* d, uint32_t tick, int ch);
int ScFmAddJump(ScFmDoc* d, uint32_t tick, int ch);
int ScFmAddLoopStart(ScFmDoc* d, uint32_t tick, int ch, int repeatN, int stack);
int ScFmAddLoopEnd(ScFmDoc* d, uint32_t tick, int ch, int stack);
int ScFmAllocVoice(ScFmDoc* d, const uint8_t voice25[25]); /* returns index or -1 */
int ScFmAddVoiceSelect(ScFmDoc* d, uint32_t tick, int ch, int voiceIdx, int isCustom);
int ScFmAddVolTl(ScFmDoc* d, uint32_t tick, int ch, int tl);
int ScFmAddPcmSample(ScFmDoc* d, uint32_t tick, int ch, int slot);

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
void ScSessionClearLast(void);

/* Compile FM DAT/MML-like text into fm doc */
int ScCompileFmText(const wchar_t* text, ScFmDoc* out, int* errLine, wchar_t* errMsg, int errMsgCch);

uint32_t ScFmDocMaxTick(const ScFmDoc* d);
int ScFmDocNoteCount(const ScFmDoc* d);

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
