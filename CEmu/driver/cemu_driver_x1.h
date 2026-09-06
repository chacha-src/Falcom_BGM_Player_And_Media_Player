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

	void RunUntil(uint64_t endCycle);
	void TickChips(uint64_t cpuCycles);
	void DeliverIrqs(uint64_t now);
	void SyncTimerPeriodFromCtc();
	void TriggerSong();
};
