#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Ensoniq ES5505 (OTIS) — ported from MAME es5506.cpp (BSD-3-Clause, Aaron Giles).
   Register R/W, banked PCM ROM, stereo Render. */

CChip* CEmuChipEs5505Create(uint32_t clockHz, int sampleRate);
void CEmuChipEs5505Destroy(CChip* c);
uint16_t CEmuChipEs5505Read(CChip* c, uint32_t addr);

/* Optional: host sets per-voice bank base (word index; Taito uses (n&mask)<<20). */
void CEmuChipEs5505SetVoiceBank(CChip* c, int voice, uint32_t wordBase);
uint32_t CEmuChipEs5505GetVoiceIndex(CChip* c);
