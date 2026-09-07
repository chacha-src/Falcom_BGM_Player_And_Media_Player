#pragma once
#include "cemu_driver.h"

class CHardPc98;

class CDriverPc98 : public CDriver {
public:
	CDriverPc98();
	~CDriverPc98() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;

private:
	void RunUntil(uint64_t endCycle);
	void TickOpn(uint64_t cpuCycles);
	void WatchdogTick();

	CHardPc98* hw_;
	int hostRate_;
	int cpuHz_;
	int opnHz_;
	int booted_;
	int triggered_;
	uint64_t opnResidual_;
	int64_t cpuAcc_;
	int64_t cpuDebt_;
	unsigned titleCode_;
	/* Stall watchdog: replays the track when the sequencer goes quiet, so a
	   rip that does not loop on its own still loops (see WatchdogTick). */
	uint64_t wdSamples_;
	uint64_t wdLastActive_;
	unsigned wdMotion_;
	unsigned wdReplays_;
	int wdEverActive_;
};
