#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_x1.h"

/* Sharp X1: Z80 + YM2151/YM2203 + AY。割り込みは CTC 一本（CHardX1::CtcRun）が IM2 を起こす */
class CDriverX1 : public CDriver {
public:
	CDriverX1();
	~CDriverX1() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;
	int OverlayTitle(unsigned titleCode) override;

	unsigned OpmWrites() const;
	unsigned AyWrites() const;

	/* 受理した IRQ 回数（プローブが tick レート確認に使う）。Vsync = ch3、Timer = ch0-2 */
	unsigned TimerIrqs() const { return timerIrqs_; }
	unsigned VsyncIrqs() const { return vsyncIrqs_; }
	uint64_t TimerPeriod() const { return hw_ ? (uint64_t)hw_->CtcTimerPeriodCycles(0) : 0; }

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
	/* サンプル期限を RunUntil が超過したサイクル */
	int64_t cpuDebt_;
	unsigned timerIrqs_;
	unsigned vsyncIrqs_;

	void RunUntil(uint64_t endCycle);
	void TickChips(uint64_t cpuCycles);
	/* CTC を now まで進め、保留中の INT を Zilog デイジー優先（ch0 > ch1 > ch2 > ch3）で 1 本届ける */
	void DeliverIrqs(uint64_t now);
	/* C010/C011 メールボックスへ曲を載せる */
	void TriggerSong();
};
