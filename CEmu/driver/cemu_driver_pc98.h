#pragma once
#include "cemu_driver.h"

class CHardPc98;

/* PC-98: NP2 i286/V30 + OPN/OPNA。DOS 経路は PumpCycles */
class CDriverPc98 : public CDriver {
public:
	CDriverPc98();
	~CDriverPc98() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;
	int OverlayTitle(unsigned titleCode) override;

private:
	/* i286 を endCycle まで進める */
	void RunUntil(uint64_t endCycle);
	/* OPN クロックを CPU 比で進める */
	void TickOpn(uint64_t cpuCycles);
	/* 無音が続くリップを再キックする */
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
	/* 停滞ウォッチドッグ: シーケンサが黙ると曲を再キックしてループさせる */
	uint64_t wdSamples_;
	uint64_t wdLastActive_;
	unsigned wdMotion_;
	unsigned wdReplays_;
	int wdEverActive_;
};
