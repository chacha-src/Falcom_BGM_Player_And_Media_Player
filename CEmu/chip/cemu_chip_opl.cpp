#include "StdAfx.h"
#include "cemu_chip_opl.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"

extern "C" {
#include "mame/driver.h"
#include "mame/fmopl.h"
}

class CChipOpl2 : public CChip {
public:
	CChipOpl2(uint32_t clockHz, int sampleRate)
		: chip_(NULL)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, clockHz_(clockHz ? clockHz : 3579545u)
		, writeCount_(0)
		, keyOnCount_(0)
		, addrLatch_(0)
	{
		timerLeft_[0] = timerLeft_[1] = -1;
		memset(regHist_, 0, sizeof(regHist_));
		chip_ = YM3812Init((int)clockHz_, sampleRate_);
		if (chip_) {
			YM3812ResetChip(chip_);
			/* Melodic mode (not rhythm). */
			YM3812Write(chip_, 0, 0x01);
			YM3812Write(chip_, 1, 0x00);
			/* KOEI FMDRV AdLib detect (and any OPL timer user) needs
			   TimerHandler + AdvanceClocks → YM3812TimerOver. Without
			   this, detect fails, [0114] stays 0, INT 66 returns FFFF. */
			YM3812SetTimerHandler(chip_, &CChipOpl2::OnTimer, this);
		}
	}

	~CChipOpl2() override
	{
		if (chip_) {
			YM3812SetTimerHandler(chip_, NULL, NULL);
			YM3812Shutdown(chip_);
			chip_ = NULL;
		}
	}

	void Reset() override
	{
		writeCount_ = 0;
		keyOnCount_ = 0;
		addrLatch_ = 0;
		timerLeft_[0] = timerLeft_[1] = -1;
		memset(regHist_, 0, sizeof(regHist_));
		if (chip_) {
			YM3812ResetChip(chip_);
			YM3812Write(chip_, 0, 0x01);
			YM3812Write(chip_, 1, 0x00);
			YM3812SetTimerHandler(chip_, &CChipOpl2::OnTimer, this);
		}
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		if (!chip_) return;
		if ((addr & 1) == 0) {
			addrLatch_ = (uint8_t)(data & 0xff);
			YM3812Write(chip_, 0, (int)(data & 0xff));
			return;
		}
		YM3812Write(chip_, 1, (int)(data & 0xff));
		writeCount_++;
		regHist_[addrLatch_ & 0xff]++;
		/* Key-on: reg Bx bit5. */
		if ((addrLatch_ & 0xf0) == 0xb0 && (data & 0x20) != 0)
			keyOnCount_++;
		/* WriteOplReg enables OPL mode unless EnterKeysOnly suppressed it. */
		FmMonShadowWriteOplReg(addrLatch_, (uint8_t)(data & 0xff));
	}

	void AdvanceClocks(uint64_t chipCycles) override
	{
		if (!chip_ || chipCycles == 0) return;
		for (int t = 0; t < 2; t++) {
			if (timerLeft_[t] < 0) continue;
			timerLeft_[t] -= (int64_t)chipCycles;
			while (timerLeft_[t] <= 0) {
				const int64_t over = -timerLeft_[t];
				timerLeft_[t] = -1;
				YM3812TimerOver(chip_, t);
				/* TimerOver reloads via OnTimer; if still inactive, stop. */
				if (timerLeft_[t] < 0) break;
				timerLeft_[t] -= over;
			}
		}
	}

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		if (!chip_) {
			memset(stereo, 0, (size_t)frames * 2 * sizeof(int16_t));
			return;
		}
		/* Keep timers alive between IRQ pumps (sample-time advance). */
		if (clockHz_ > 0 && sampleRate_ > 0) {
			const uint64_t clocks =
				(uint64_t)frames * (uint64_t)clockHz_ / (uint64_t)sampleRate_;
			if (clocks) AdvanceClocks(clocks);
		}
		for (int i = 0; i < frames; i++) {
			OPLSAMPLE s = 0;
			YM3812UpdateOne(chip_, &s, 1);
			int32_t v = (int32_t)s;
			if (v > 32767) v = 32767;
			if (v < -32768) v = -32768;
			stereo[i * 2] = (int16_t)v;
			stereo[i * 2 + 1] = (int16_t)v;
		}
	}

	bool Irq() const override
	{
		/* Status bit7 = IRQ pending (MAME fmopl). Toaplan1 / AdLib sequencers
		   wire this to Z80 INT; the ISR clears it via reg 04. */
		return chip_ && (YM3812Read(chip_, 0) & 0x80) != 0;
	}
	void AckIrq() override {}
	uint8_t ReadStatus() override
	{
		return chip_ ? (uint8_t)YM3812Read(chip_, 0) : 0;
	}
	uint8_t ReadData() override { return ReadStatus(); }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

	unsigned WriteCount() const { return writeCount_; }
	unsigned KeyOnCount() const { return keyOnCount_; }
	const unsigned* RegHist() const { return regHist_; }

private:
	static void OnTimer(void* param, int timer, double intervalSec)
	{
		CChipOpl2* self = (CChipOpl2*)param;
		if (!self || timer < 0 || timer > 1) return;
		if (intervalSec <= 0.0) {
			self->timerLeft_[timer] = -1;
			return;
		}
		/* Convert seconds → chip clocks. */
		const double clocks = intervalSec * (double)self->clockHz_;
		self->timerLeft_[timer] = (clocks > 1.0) ? (int64_t)clocks : 1;
	}

	void* chip_;
	int sampleRate_;
	uint32_t clockHz_;
	unsigned writeCount_;
	unsigned keyOnCount_;
	uint8_t addrLatch_;
	unsigned regHist_[256];
	int64_t timerLeft_[2];
};

CChip* CEmuChipYm3812Create(uint32_t clockHz, int sampleRate)
{
	return new CChipOpl2(clockHz, sampleRate);
}

void CEmuChipYm3812Destroy(CChip* c)
{
	delete c;
}

unsigned CEmuChipYm3812WriteCount(const CChip* c)
{
	return c ? ((const CChipOpl2*)c)->WriteCount() : 0;
}

unsigned CEmuChipYm3812KeyOnCount(const CChip* c)
{
	return c ? ((const CChipOpl2*)c)->KeyOnCount() : 0;
}

const unsigned* CEmuChipYm3812RegHist(const CChip* c)
{
	return c ? ((const CChipOpl2*)c)->RegHist() : NULL;
}
