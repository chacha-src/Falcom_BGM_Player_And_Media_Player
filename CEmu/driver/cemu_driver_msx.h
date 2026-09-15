#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_msx.h"

/* MSX: KSS/カートリッジ。VBlank は出力サンプル軸の 60Hz */
class CDriverMsx : public CDriver {
public:
	CDriverMsx();
	~CDriverMsx() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;
	int OverlayTitle(unsigned titleCode) override;
	/* 投入した VBlank IRQ 回数 */
	unsigned IrqPulses() const { return irqPulses_; }

private:
	CHardMsx* hw_;
	int hostRate_;
	int cpuHz_;
	int ayHz_;
	int opllHz_;
	uint64_t ayResidual_;
	uint64_t opllResidual_;
	int64_t cpuAcc_;
	uint64_t cpuTarget_;
	uint64_t nextIrq_;
	uint64_t irqPeriod_;
	uint64_t sampleIndex_;
	uint64_t nextIrqSample_;
	unsigned irqPulses_;
	int playing_;

	/* Z80 を endCycle まで進める */
	void RunUntil(uint64_t endCycle);
	/* AY/OPLL は Render 側でサンプル駆動 */
	void TickChips(uint64_t cpuCycles);
	/* 出力タイムライン上の VBlank（IM1/IM2） */
	void PulseVblankIrq();
};
