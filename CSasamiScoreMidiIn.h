#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>

enum ScScoreMidiMode : int {
	SC_MIDIIN_OFF = 0,
	SC_MIDIIN_STEP = 1,
	SC_MIDIIN_REALTIME = 2
};

enum ScScoreVelCurve : int {
	SC_VEL_LINEAR = 0,
	SC_VEL_SOFT = 1,   /* console-ish: expand low end */
	SC_VEL_FIXED = 2
};

enum ScScoreQuant : int {
	SC_QUANT_OFF = 0,
	SC_QUANT_WEAK = 1,
	SC_QUANT_STRONG = 2,
	SC_QUANT_SWING = 3
};

/* Posted to HWND: wParam=shortMsg, lParam unused. */
#ifndef WM_SASAMI_SCORE_MIDI
#define WM_SASAMI_SCORE_MIDI (WM_APP + 72)
#endif

struct ScScoreMidiIn {
	HMIDIIN hIn;
	HWND notify;
	int deviceIndex; /* -1 = none */
	int chFilter;    /* 0=all, 1..16 */
	int mode;
	int velCurve;
	int velFixed;    /* used when FIXED */
	int velSense;    /* 50=normal, 1..200 */
	int quant;
	int overdub;
	int metroOn;
	int recording;   /* realtime armed */
	uint32_t recOriginTick;
	uint32_t recLoopEnd; /* 0 = no loop end */
	DWORD recStartMs;
	double tempoBpm;
	int heldNote[128];
	uint32_t heldOnTick[128];
	int pedDown;
};

void ScScoreMidiInInit(ScScoreMidiIn* s);
void ScScoreMidiInShutdown(ScScoreMidiIn* s);
void ScScoreMidiInFillDeviceCombo(HWND combo /* CComboBox* cast via GetSafeHwnd */);
int ScScoreMidiInOpen(ScScoreMidiIn* s, HWND notify, int deviceIndex);
void ScScoreMidiInClose(ScScoreMidiIn* s);
void ScScoreMidiInSetMode(ScScoreMidiIn* s, int mode);
int ScScoreMidiInMapVelocity(const ScScoreMidiIn* s, int velIn);
uint32_t ScScoreMidiInQuantizeTick(const ScScoreMidiIn* s, uint32_t tick, int ppqn, int swing);
uint32_t ScScoreMidiInNowTick(const ScScoreMidiIn* s, int ppqn);
void ScScoreMidiInStartRealtime(ScScoreMidiIn* s, uint32_t originTick, uint32_t loopEndTick, double bpm);
void ScScoreMidiInStopRealtime(ScScoreMidiIn* s);
void ScScoreMidiInMetroTick(ScScoreMidiIn* s, int beatInMeasure); /* click beep */
