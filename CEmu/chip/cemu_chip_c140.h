#pragma once
#include <stdint.h>
#include "cemu_chip.h"

/* Namco C140, 24ch 8/12-bit PCM (System 2 / System 21). Type 2 = C219 (NA-1). */
CChip* CEmuChipC140Create(uint32_t clockHz, int sampleRate);
void CEmuChipC140Destroy(CChip* c);
void CEmuChipC140SetType(CChip* c, int type); /* 0=Sys2, 1=Sys21, 2=C219 */
/* Host-bus read with MAME keyon-status / timer quirks (not raw snapshot). */
uint8_t CEmuChipC140Read(CChip* c, unsigned offset);
int CEmuChipC140KeyedCount(const CChip* c);
unsigned CEmuChipC140WriteCount(const CChip* c);
unsigned CEmuChipC140VoiceWriteCount(const CChip* c);
unsigned CEmuChipC140ModeWriteCount(const CChip* c);
unsigned CEmuChipC140Mode80WriteCount(const CChip* c);
unsigned CEmuChipC140Mode40WriteCount(const CChip* c);
unsigned CEmuChipC140ModeNzWriteCount(const CChip* c);
uint8_t CEmuChipC140LastModeWrite(const CChip* c);
int CEmuChipC140KeyedPeak(const CChip* c);
