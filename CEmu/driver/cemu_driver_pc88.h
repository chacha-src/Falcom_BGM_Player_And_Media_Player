#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_pc88.h"

/* Stall-watchdog replay counter, for probes that need to tell a driver's own
   loop apart from one the watchdog forced. Global because only one title
   renders at a time in those probes. */
unsigned CEmuPc88WatchdogReplays();
void CEmuPc88WatchdogResetCount();
void CEmuPc88WatchdogSetEnabled(int on);

/* Opening audio the last play kick recovered, and how much of its head was
   init silence that got trimmed (frames). Same probe-only contract. */
unsigned CEmuPc88LeadFrames();
unsigned CEmuPc88LeadTrimmedFrames();

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
	/* Lead-in: audio the OPN produced while TriggerPlay was draining the
	   PATCH command, i.e. the opening of the song. See BeginLeadCapture. */
	int16_t* lead_;
	int leadCap_;   /* int16_t slots */
	int leadLen_;   /* int16_t slots in use */
	int leadPos_;   /* int16_t read cursor */
	int capturing_;
	int64_t capAcc_; /* cycles*hostRate accumulator for capture pacing */
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
	void BeginLeadCapture();
	void CaptureLead(uint64_t cpuCycles);
	void EndLeadCapture();
	int DrainLead(int16_t* stereo, int frames);
};
