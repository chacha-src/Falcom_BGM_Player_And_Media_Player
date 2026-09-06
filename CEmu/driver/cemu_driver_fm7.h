#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_fm7.h"

class CDriverFm7 : public CDriver {
public:
	CDriverFm7();
	~CDriverFm7() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;

	unsigned OpnWrites() const;
	unsigned AyWrites() const;
	unsigned IrqPulses() const { return irqPulses_; }

private:
	CHardFm7* hw_;
	int hostRate_;
	int cpuHz_;
	int opnHz_;
	int ayHz_;
	int booted_;
	int triggered_;
	uint8_t songCode_;
	unsigned titleCode_;
	uint64_t opnResidual_;
	unsigned opnTimerMul_;
	unsigned opnTimerDiv_;
	uint64_t ayResidual_;
	int64_t cpuAcc_;
	uint64_t nextVsync_;
	uint64_t vsyncPeriod_;
	unsigned irqPulses_;
	int prevChipIrq_;
	uint16_t lastFd03IrqVec_;

	void RunUntil(uint64_t endCycle);
	void TickChips(uint64_t cpuCycles);
	void DeliverIrqs(uint64_t now);
	void TriggerSong();
};
