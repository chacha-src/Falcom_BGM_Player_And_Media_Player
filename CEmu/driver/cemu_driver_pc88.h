#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_pc88.h"

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

	void RunUntil(uint64_t endCycle);
	void DeliverIrqs(uint64_t now);
	void TickOpn(uint64_t cpuCycles);
};
