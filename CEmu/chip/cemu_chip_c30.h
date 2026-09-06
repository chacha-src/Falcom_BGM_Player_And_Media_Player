#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Namco CUS30 / 15XX / Pac-Man WSG wavetable.
   mode: 0 = System 1 / System 86 stereo register map (ssC30::Write)
         1 = Mappy / 15XX mono register map (ssC30::WriteMAPPY)
         2 = Pac-Man / Galaga / Dig Dug 3-voice PROM WSG (pacman_sound_w) */
enum {
	CEMU_C30_STEREO = 0,
	CEMU_C30_MAPPY = 1,
	CEMU_C30_PACMAN = 2
};

CChip* CEmuChipC30Create(uint32_t clockHz, int sampleRate, int mode);
void CEmuChipC30Destroy(CChip* c);
void CEmuChipC30SetEnable(CChip* c, int enable);
uint8_t CEmuChipC30Read(CChip* c, uint32_t addr);
