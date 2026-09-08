#pragma once
#include "cemu_chip.h"
#include <stdint.h>

/* Ricoh RF5C400 (Konami Hornet/Firebeat): 32-channel PCM with per-channel
   AR/DR/RR envelopes, modelled on MAME sound/rf5c400.cpp. Write() takes the
   register offset, not a byte address — offsets below 0x400 are the global
   file, above that the channel is (offset>>5)&0x1f. */
CChip* CEmuChipRf5c400Create(uint32_t clockHz, int sampleRate);
void CEmuChipRf5c400Destroy(CChip* c);

/* Global register file read (status, stream position, external memory). */
unsigned CEmuChipRf5c400ReadReg(CChip* c, unsigned offset);
