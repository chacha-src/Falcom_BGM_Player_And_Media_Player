#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_x1.h"

/* Sharp X1: Z80 + YM2151/YM2203 + AY。CTC が IM2 を起こす */
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

	/* ソース別 IRQ 回数（プローブが tick レート確認に使う） */
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
	/* ZC0 パルスは ch3 カウンタ TC へ。INT は ch0-2=タイマ端、ch3=ZC0/VSYNC。
	   DeliverIrqs は 1 チャネルずつ取り、ch0 と同じ tick で ch2/ch1 が飢えない
	   ようにする（sc / crimson）。 */
	uint64_t ctc3Div_;
	int ctcPending_[4];
	/* サンプル期限を RunUntil が超過したサイクル */
	int64_t cpuDebt_;
	unsigned timerIrqs_;
	unsigned vsyncIrqs_;

	void RunUntil(uint64_t endCycle);
	void TickChips(uint64_t cpuCycles);
	/* CTC チャネルを 1 本ずつ届ける */
	void DeliverIrqs(uint64_t now);
	/* CTC プログラム値からタイマ周期を同期 */
	void SyncTimerPeriodFromCtc();
	/* C010/C011 メールボックスへ曲を載せる */
	void TriggerSong();
};
