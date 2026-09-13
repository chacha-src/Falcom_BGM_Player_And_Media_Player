#pragma once

/* Realtime MPU UART → growing SMF + short-message stream for VST inject.
   PC/AT (SC-55/MT-32/SC-88 via midiout glue) first; PC98 MIDI zips share Pump/Stop. */

struct CEmuMidiLiveShort {
	DWORD msg;
	int sampleOfs; /* within the last Pump window; -1 = as-soon-as */
};

int CEmuMidiLiveActive(void);

/* Boot PCAT/PC98 midiout, write stub SMF (multi-day length + CC#111 start), return path. */
int CEmuMidiLiveStartPcat(const wchar_t* zipPath, unsigned titleCode,
	wchar_t* outMidPath, int outCap);

void CEmuMidiLiveStop(void);

/* Same-zip SE while live UART BGM is running: inject title, do not restart. */
int CEmuMidiLiveSameZip(const wchar_t* zipPath);
int CEmuMidiLiveOverlayTitle(unsigned titleCode);

/* SMF BGM already on VST: boot live MPU and inject SE without replacing the SMF. */
int CEmuMidiLiveStartOverlayPcat(const wchar_t* zipPath, unsigned titleCode);

/* Advance emu by frames @ session rate; queue shorts for Steal. */
int CEmuMidiLivePump(int frames);

int CEmuMidiLiveStealShorts(CEmuMidiLiveShort* out, int maxCount);

/* Frames pumped since the session started. sampleOfs of a stolen short is
   relative to this, so the caller can stamp it onto the audible timeline. */
__int64 CEmuMidiLiveAudioFrames(void);

/* 1 after first NoteOn seen (for playlist time=-1 / loop hint). */
int CEmuMidiLiveHasNotes(void);

/* What the UART capture held vs what the inject/defer rings could carry.
   hw* are parsed from the machine's capture buffer, so a probe can tell a
   driver that never sent program changes from a chain that dropped them. */
struct CEmuMidiLiveDiag {
	unsigned injDropped;
	unsigned holdDropped;
	unsigned injPeak;
	unsigned holdPeak;
	unsigned hwBytes;
	unsigned hwNoteOn;
	unsigned hwNoteOff;
	unsigned hwProgram;
	unsigned hwControl;
	__int64 midiSample;
	__int64 audioSample;
};
int CEmuMidiLiveGetDiag(struct CEmuMidiLiveDiag* out);
