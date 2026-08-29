#pragma once

#include "sasami_file.h"
#include <string.h>

static const unsigned kMisaoDefaultT = 13000;
static const uint32_t kMisaoMaxTicks = 200000;

struct MisaoChState {
	uint32_t pc;
	int alive;
	uint8_t wait;
	uint8_t loopCnt;
	int backJumps;
	uint8_t lastNote;
	uint16_t pitchM;
	uint16_t detune;
};

static inline int MisaoNoteKey(uint8_t noteByte)
{
	const int oct = ((noteByte >> 4) & 0x0F) + 1;
	const int n = noteByte & 0x0F;
	int key = oct * 12 + n;
	if (key < 0) key = 0;
	if (key > 127) key = 127;
	return key;
}

static inline int MisaoPitchBend14(uint16_t raw)
{
	const int centered = (int)raw - 0x8000;
	int bend = 8192 + (centered >> 2);
	if (bend < 0) bend = 0;
	if (bend > 16383) bend = 16383;
	return bend;
}

static inline int MisaoPanCc(int dl)
{
	int p = dl;
	if (p < 0) p = 0;
	if (p > 126) p = 126;
	return (p * 127 + 63) / 126;
}

static inline int MisaoCmdValid(uint8_t cmd)
{
	switch (cmd) {
	case 0: case 1: case 2: case 3: case 9: case 10: case 11: case 12:
	case 13: case 14: case 18: case 24: case 25:
		return 1;
	default:
		return (cmd >= 4 && cmd <= 8) || (cmd >= 15 && cmd <= 17) || (cmd >= 19 && cmd <= 23);
	}
}

static inline int MisaoEffectiveChCount(const SasamiSong& song)
{
	int n = 0;
	for (int i = 0; i < SASAMI_MISAO_MAX_CH; i++) {
		if (!song.misaoTracks[i].unused && song.misaoTracks[i].fileOff)
			n = i + 1;
	}
	if (n <= 0) n = song.misaoChCount;
	if (n > SASAMI_MISAO_MAX_CH) n = SASAMI_MISAO_MAX_CH;
	return n;
}

static inline int MisaoCombinedBend(const MisaoChState& ch)
{
	const int pb = MisaoPitchBend14(ch.pitchM);
	const int dt = ((int)ch.detune - 0x8000) >> 4;
	int bend = pb + dt;
	if (bend < 0) bend = 0;
	if (bend > 16383) bend = 16383;
	return bend;
}

static inline void MisaoInitChState(MisaoChState* ch, const SasamiSong& song)
{
	memset(ch, 0, sizeof(MisaoChState) * SASAMI_MISAO_MAX_CH);
	for (int i = 0; i < SASAMI_MISAO_MAX_CH; i++) {
		ch[i].pitchM = 0x8000;
		if (!song.misaoTracks[i].unused && song.misaoTracks[i].fileOff) {
			ch[i].pc = song.misaoTracks[i].fileOff;
			ch[i].alive = 1;
		}
	}
}
