#pragma once
#include <stdint.h>
#include "cemu_chip.h"

CChip* CEmuChipYm2151Create(uint32_t clockHz, int sampleRate);
void CEmuChipYm2151Destroy(CChip* c);

/* Diagnostics: how many SetReg calls since create/reset. */
unsigned CEmuChipYm2151WriteCount(const CChip* c);
/* Copy 256-byte OPM register snapshot (addr-latch side). Returns 0 if null. */
int CEmuChipYm2151PeekRegs(const CChip* c, unsigned char* out256);
/* Count KeyOn writes (reg 0x08 with any slot bits). */
unsigned CEmuChipYm2151KeyOnCount(const CChip* c);
/* No-op API kept for X68k/X1 compile compat — does not rewrite TL/KeyOn. */
void CEmuChipYm2151SetAudibleAssist(CChip* c, int enable);
