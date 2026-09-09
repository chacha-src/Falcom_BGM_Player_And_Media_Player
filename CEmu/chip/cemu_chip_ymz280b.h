#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Yamaha YMZ280B (PCMD8), 8 channels of 4-bit ADPCM / 8-bit / 16-bit PCM
   streamed from an external sample ROM. The host interface is two ports:
   write the register number, then the value. */
CChip* CEmuChipYmz280bCreate(uint32_t clockHz, int sampleRate);
void CEmuChipYmz280bDestroy(CChip* c);

/* Port 0 = register select, port 1 = data. */
void CEmuChipYmz280bWritePort(CChip* c, unsigned port, uint8_t data);
uint8_t CEmuChipYmz280bReadStatus(CChip* c);

/* Boards that tie both chip outputs to one amplifier (Battle Bakraid is
   MAME's add_route(ALL_OUTPUTS, "mono")) hear L+R on both sides, which
   matters because such programs leave voices panned hard to one side. */
void CEmuChipYmz280bSetMono(CChip* c, int mono);

/* Voices currently decoding. Status-port reads clear IRQ bits, so boards
   that only need a "is anything still playing" test use this instead. */
unsigned CEmuChipYmz280bPlayingCount(const CChip* c);
