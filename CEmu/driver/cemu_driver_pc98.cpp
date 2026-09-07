#include "StdAfx.h"
#include "cemu_driver_pc98.h"
#include "../machine/cemu_hard_pc98.h"
#include "../chip/cemu_chip_opna.h"
#include "../vendor/np2/np2ffi.h"

enum {
	/* Matches the Z80 PC-88 watchdog: 2s of total register stillness is never
	   part of a live song. */
	PC98_WD_IDLE_MS = 2000
};

CDriverPc98::CDriverPc98()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(8000000)
	, opnHz_(3993600)
	, booted_(0)
	, triggered_(0)
	, opnResidual_(0)
	, cpuAcc_(0)
	, cpuDebt_(0)
	, titleCode_(0)
	, wdSamples_(0)
	, wdLastActive_(0)
	, wdMotion_(0)
	, wdReplays_(0)
	, wdEverActive_(0)
{
}

CDriverPc98::~CDriverPc98()
{
	Close();
}

int CDriverPc98::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardPc98*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 8000000;
	opnHz_ = hw_->opnHz_ > 0 ? hw_->opnHz_ : 3993600;
	opnResidual_ = 0;
	cpuAcc_ = 0;
	cpuDebt_ = 0;
	booted_ = 0;
	triggered_ = 0;
	titleCode_ = titleCode;
	wdSamples_ = 0;
	wdLastActive_ = 0;
	wdMotion_ = 0;
	wdReplays_ = 0;
	wdEverActive_ = 0;

	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;

	CEmuHardPc98SetActive(hw_);
	if (!hw_->isDos_) {
		/* Bootcs: settle ~1s */
		const uint64_t bootCycles = (uint64_t)cpuHz_;
		RunUntil(hw_->cpuCycles_ + bootCycles);
	}

	booted_ = 1;
	return 1;
}

void CDriverPc98::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
}

void CDriverPc98::TickOpn(uint64_t cpuCycles)
{
	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0 || cpuHz_ <= 0) return;
	opnResidual_ += cpuCycles * (uint64_t)opnHz_;
	const uint64_t opnTicks = opnResidual_ / (uint64_t)cpuHz_;
	opnResidual_ %= (uint64_t)cpuHz_;
	if (opnTicks)
		hw_->SoundChip()->AdvanceClocks(opnTicks);
}

void CDriverPc98::RunUntil(uint64_t endCycle)
{
	if (!hw_) return;
	if (hw_->isDos_) {
		hw_->PumpCycles(endCycle);
		return;
	}
	CEmuHardPc98SetActive(hw_);
	while (hw_->cpuCycles_ < endCycle) {
		const int32_t cyc = np2_step();
		const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
		hw_->cpuCycles_ += u;
		hw_->TickSide(u);
		TickOpn(u);
		hw_->DeliverIrqs();
	}
}

/* Same stall watchdog as the Z80 PC-88 driver, minus the interrupt-source
   repairs: on V30 the play trigger is a single BIOS-style call, so replaying
   the track is the only cure a stalled rip needs. Note motion (key-ons,
   F-num, SSG period) is the liveness signal — idle polling is not playing. */
void CDriverPc98::WatchdogTick()
{
	CChip* chip = hw_ ? hw_->SoundChip() : NULL;
	const int rate = hostRate_ > 0 ? hostRate_ : 44100;
	if (!chip) return;
	unsigned w = 0, k = 0, f = 0, s = 0, m = 0;
	CEmuChipYm2608GetPlayMetrics(chip, &w, &k, &f, &s, &m);
	const unsigned motion = k + f + s;
	if (motion != wdMotion_) {
		wdMotion_ = motion;
		wdLastActive_ = wdSamples_;
		wdEverActive_ = 1;
		return;
	}
	if (wdSamples_ - wdLastActive_ < (uint64_t)rate * PC98_WD_IDLE_MS / 1000u)
		return;
	wdLastActive_ = wdSamples_;
	/* Only while nothing has ever sounded — see CDriverPc88::WatchdogTick.
	   Re-kicking a player that is already running restarts it from whatever
	   state its RAM happens to hold, which is audibly worse than the silence
	   it was trying to cure. */
	if (wdEverActive_ || wdReplays_ >= 4)
		return;
	wdReplays_++;
	hw_->TriggerPlay(titleCode_);
}

int CDriverPc98::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0 || !booted_) return 0;
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	CEmuHardPc98SetActive(hw_);
	if (!triggered_) {
		hw_->TriggerPlay(titleCode_);
		triggered_ = 1;
	}
	const int rate = hostRate_ > 0 ? hostRate_ : 44100;
	if (cpuHz_ < 1 || rate < 1) return 0;
	for (int i = 0; i < frames; i++) {
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)rate);
		cpuAcc_ %= (int64_t)rate;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		cpuDebt_ += cyclesPerSample;
		if (cpuDebt_ > 0) {
			const uint64_t start = hw_->cpuCycles_;
			const uint64_t end = start + (uint64_t)cpuDebt_;
			if (hw_->isDos_) {
				hw_->PumpCycles(end);
			} else {
				while (hw_->cpuCycles_ < end) {
					const int32_t cyc = np2_step();
					const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
					hw_->cpuCycles_ += u;
					hw_->TickSide(u);
					TickOpn(u);
					hw_->DeliverIrqs();
				}
			}
			cpuDebt_ -= (int64_t)(hw_->cpuCycles_ - start);
		}
		chip->Render(stereo + i * 2, 1);
		if ((++wdSamples_ & 511) == 0)
			WatchdogTick();
	}
	return frames;
}

int CDriverPc98::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
