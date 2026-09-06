#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_f3.h"

class CDriverF3 : public CDriver {
public:
	CDriverF3();
	~CDriverF3() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;

private:
	void RunCycles(int cycles);
	void TryInjectCommand();

	CHardF3* hw_;
	int hostRate_;
	int cpuHz_;
	int64_t cpuAcc_;
	int booted_;
	unsigned songCode_;
	unsigned tryCodes_[64];
	int tryCount_;
	int cmdIndex_;
	int dwellLeft_;
	int dwellFrames_;
	int bestPeak_;
	int windowPeak_;
	unsigned bestSongCode_;
	int locked_;
	int irqPhase_;
};

CDriver* CDriverF3Create();
