#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_sg1000.h"

class CDriverSg1000 : public CDriver {
public:
	CDriverSg1000();
	~CDriverSg1000() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;

	unsigned PsgWrites() const;
	int ToneFallback() const { return toneFallback_; }

private:
	CHardSg1000* hw_;
	int hostRate_;
	int cpuHz_;
	int psgHz_;
	int booted_;
	int triggered_;
	uint8_t songCmd_;
	uint64_t psgResidual_;
	int64_t cpuAcc_;
	uint64_t nextTickAt_;
	int toneFallback_;
	int knownTick_;

	void RunUntil(uint64_t endCycle);
	void TickPsg(uint64_t cpuCycles);
	void CallZ80(uint16_t targetPc);
	void TriggerSong();
	void ForceToneTest();
};
