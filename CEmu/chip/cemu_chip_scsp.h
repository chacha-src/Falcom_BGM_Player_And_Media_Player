#pragma once
#include "cemu_chip.h"
#include <stdint.h>

/* Model 2A/3 SCSP (YMF292-F), 32 slots — real synthesis via the core vendored
   under CEmu/vendor/scsp. Playback still needs the sound 68000 to be running:
   the chip renders whatever is keyed, but nobody keys it until the host runs. */
CChip* CEmuChipScspCreate(uint32_t clockHz, int sampleRate);
void CEmuChipScspDestroy(CChip* c);

/* The 512KB sound RAM is shared: it is both the sound 68000's low memory and
   the only address space the SCSP fetches waves from, so the host bus writes
   samples straight into it. Words are kept in host order (the vendored core
   follows the byte-swapped-RAM convention), so 8-bit host access must use
   addr^1. Returns NULL until a SCSP has been created. */
uint8_t* CEmuChipScspRam();
unsigned CEmuChipScspRamSize();

/* Register window at 0x100000 on Model 2/3 — word index, not byte offset. */
unsigned CEmuChipScspReadReg(CChip* c, unsigned wordIndex);

/* Song select arrives on the SCSP MIDI input, same as on real hardware. */
void CEmuChipScspMidiIn(CChip* c, uint8_t data);

/* Pending IPL for the sound 68000 (timer A/B/C and MIDI), 0 when idle. */
int CEmuChipScspIrqLevel();
