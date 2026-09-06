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

/* Advance emu by frames @ session rate; queue shorts for Steal. */
int CEmuMidiLivePump(int frames);

int CEmuMidiLiveStealShorts(CEmuMidiLiveShort* out, int maxCount);

/* 1 after first NoteOn seen (for playlist time=-1 / loop hint). */
int CEmuMidiLiveHasNotes(void);
