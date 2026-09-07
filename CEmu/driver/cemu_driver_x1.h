#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_x1.h"

class CDriverX1 : public CDriver {
public:
	CDriverX1();
	~CDriverX1() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;

	unsigned OpmWrites() const;
	unsigned AyWrites() const;

	/* Delivered IRQ counts by source; probes use these to check tick rate. */
	unsigned TimerIrqs() const { return timerIrqs_; }
	unsigned VsyncIrqs() const { return vsyncIrqs_; }
	uint64_t TimerPeriod() const { return timerPeriod_; }

private:
	CHardX1* hw_;
	int hostRate_;
	int cpuHz_;
	int opmHz_;
	int ayHz_;
	int booted_;
	int triggered_;
	uint8_t songCode_;
	unsigned titleCode_;
	uint64_t opmResidual_;
	uint64_t ayResidual_;
	int64_t cpuAcc_;
	uint64_t nextTimer_;
	uint64_t nextVsync_;
	uint64_t timerPeriod_;
	uint64_t vsyncPeriod_;
	/* ZC0 pulses counted toward the ch3 counter-mode time constant, plus the
	   latched ch3 INT that ch0 outranks on the daisy chain. */
	uint64_t ctc3Div_;
	int ctc3Pending_;
	/* Cycles RunUntil overshot the per-sample deadline by. */
	int64_t cpuDebt_;
	unsigned timerIrqs_;
	unsigned vsyncIrqs_;

	void RunUntil(uint64_t endCycle);
	void TickChips(uint64_t cpuCycles);
	void DeliverIrqs(uint64_t now);
	void SyncTimerPeriodFromCtc();
	void TriggerSong();
};
