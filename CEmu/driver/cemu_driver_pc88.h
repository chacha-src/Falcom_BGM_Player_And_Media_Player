#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_pc88.h"

/* Stall-watchdog replay counter, for probes that need to tell a driver's own
   loop apart from one the watchdog forced. Global because only one title
   renders at a time in those probes. */
unsigned CEmuPc88WatchdogReplays();
void CEmuPc88WatchdogResetCount();
void CEmuPc88WatchdogSetEnabled(int on);

class CDriverPc88 : public CDriver {
public:
	CDriverPc88();
	~CDriverPc88() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;

private:
	CHardPc88* hw_;
	int sampleRate_;
	int hostRate_;
	int cpuHz_;
	int opnHz_;
	int booted_;
	int triggered_;
	int forceEiBoot_; /* 1 during Wing MCM1/DRIVER1 init pulse */
	uint64_t nextRtc_;
	uint64_t nextVrtc_;
	uint64_t rtcPeriod_;
	uint64_t vrtcPeriod_;
	uint64_t opnResidual_;
	int64_t cpuAcc_; /* fractional cpuHz/hostRate accumulator */
	int64_t cpuCycleBudget_; /* leftover after last insn; prevents +~6% tempo */
	/* Stall watchdog: keeps a rip playing and looping (see WatchdogTick). */
	uint64_t wdSamples_;
	uint64_t wdLastActive_;
	unsigned wdMotion_;
	unsigned wdTimerFires_;
	unsigned wdReplays_;
	int wdEverActive_;
	int wdArmedTick_;
	int replayPending_;

	void RunUntil(uint64_t endCycle);
	void DeliverIrqs(uint64_t now);
	void TickOpn(uint64_t cpuCycles);
	int FindPollLoop() const;
	void Unwedge();
	void TriggerPlay();
	void WatchdogTick();
};
