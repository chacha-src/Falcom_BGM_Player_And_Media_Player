#pragma once
#include <stdint.h>
#include "cemu_chip.h"

struct CEmuChipOpna {
	void* chip;
	int32_t sampleRate;
	int opnaMode;
	int ready;
};

void CEmuChipOpnaInit(CEmuChipOpna* c, uint32_t clockHz, int opnaMode, int sampleRate);
void CEmuChipOpnaReset(CEmuChipOpna* c);
void CEmuChipOpnaWrite(CEmuChipOpna* c, uint32_t addr, uint32_t data);
void CEmuChipOpnaAdvanceClocks(CEmuChipOpna* c, uint64_t chipCycles);
void CEmuChipOpnaRender(CEmuChipOpna* c, int16_t* stereo, int frames);
int CEmuChipOpnaIrq(const CEmuChipOpna* c);
void CEmuChipOpnaAckIrq(CEmuChipOpna* c);
uint8_t CEmuChipOpnaReadStatus(CEmuChipOpna* c);
uint8_t CEmuChipOpnaReadData(CEmuChipOpna* c);
uint8_t CEmuChipOpnaReadStatusHi(CEmuChipOpna* c);
uint8_t CEmuChipOpnaReadDataHi(CEmuChipOpna* c);
void CEmuChipOpnaSetAdpcmRom(CEmuChipOpna* c, const uint8_t* data, unsigned size, unsigned destOffset);
void CEmuChipOpnaSetAdpcmB(CEmuChipOpna* c, const uint8_t* data, unsigned size, unsigned destOffset);
unsigned CEmuChipOpnaGetAdpcmRomSize(const CEmuChipOpna* c);
unsigned CEmuChipOpnaGetAdpcmBSize(const CEmuChipOpna* c);
void CEmuChipOpnaSetTimerIrqPolicy(CEmuChipOpna* c, int allowTimerA);
void CEmuChipOpnaSetTimerClockScale(CEmuChipOpna* c, unsigned scale);
void CEmuChipOpnaSetPitchRateDiv(CEmuChipOpna* c, unsigned div);
void CEmuChipOpnaSetPitchOctaveShift(CEmuChipOpna* c, int octaves);
void CEmuChipOpnaSetCarrierFadeClamp(CEmuChipOpna* c, int enable);
int32_t CEmuChipOpnaGetChipRate(const CEmuChipOpna* c);
void CEmuChipOpnaGetTimerDebug(const CEmuChipOpna* c, unsigned* fireA, unsigned* fireB, unsigned* irqPulse);
void CEmuChipOpnaGetTimerDebugEx(const CEmuChipOpna* c, unsigned* fireA, unsigned* fireB, unsigned* irqPulse,
	int64_t* lastDurB, uint64_t* clockSum);
void CEmuChipOpnaClearTimerDebug(CEmuChipOpna* c);
void CEmuChipOpnaShutdown(CEmuChipOpna* c);

class CChipYm2608;
CChip* CEmuChipYm2608Create(uint32_t clockHz, int opnaMode, int sampleRate);
void CEmuChipYm2608Destroy(CChip* c);
void CEmuChipYm2608GetTimerDebug(CChip* c, unsigned* fireA, unsigned* fireB, unsigned* irqPulse);
void CEmuChipYm2608GetTimerDebugEx(CChip* c, unsigned* fireA, unsigned* fireB, unsigned* irqPulse,
	int64_t* lastDurB, uint64_t* clockSum);
void CEmuChipYm2608ClearTimerDebug(CChip* c);
/* Play probe: data-port writes, FM key-ons (reg28), F-num / SSG period motion. */
void CEmuChipYm2608GetPlayMetrics(CChip* c, unsigned* writes, unsigned* keyOns,
	unsigned* fnumChg, unsigned* ssgChg, unsigned* chMask);
/* SSG A/B/C: bank0 $00-$0F shadow, per-ch period changes, YM2203 sample energy. */
void CEmuChipYm2608GetSsgDebug(CChip* c, uint8_t regsOut[16], unsigned periodChg[3],
	uint64_t energy[3]);
void CEmuChipYm2608GetSsgDebugEx(CChip* c, uint8_t regsOut[16], unsigned periodChg[3],
	uint64_t energy[3], unsigned regWrites[16]);
void CEmuChipYm2608GetSsgWriteHist(CChip* c, uint8_t* volCHist, unsigned* volCHistN,
	uint8_t* mixHist, unsigned* mixHistN);
void CEmuChipYm2608GetSsgVolCStats(CChip* c, unsigned* non0F, unsigned* zero,
	uint8_t seen[32]);
