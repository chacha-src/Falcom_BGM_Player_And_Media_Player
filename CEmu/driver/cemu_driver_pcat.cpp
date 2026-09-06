#include "StdAfx.h"
#include "cemu_driver_pcat.h"
#include "../machine/cemu_hard_pcat.h"
#include "../chip/cemu_chip_opl.h"
#include "../vendor/np2/np2ffi.h"

CDriverPcat::CDriverPcat()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(8000000)
	, oplHz_(3579545)
	, booted_(0)
	, triggered_(0)
	, titleCode_(0)
	, oplResidual_(0)
	, cpuAcc_(0)
{
}

CDriverPcat::~CDriverPcat()
{
	Close();
}

unsigned CDriverPcat::OplWrites() const
{
	return hw_ ? hw_->oplWriteCount_ : 0;
}

int CDriverPcat::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardPcat*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 8000000;
	oplHz_ = hw_->oplHz_ > 0 ? hw_->oplHz_ : 3579545;
	oplResidual_ = 0;
	cpuAcc_ = 0;
	booted_ = 0;
	triggered_ = 0;
	titleCode_ = titleCode;

	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;

	CEmuHardPcatSetActive(hw_);
	booted_ = 1;
	return 1;
}

void CDriverPcat::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
}

int CDriverPcat::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0 || !booted_) return 0;
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	CEmuHardPcatSetActive(hw_);
	if (!triggered_) {
		hw_->TriggerPlay(titleCode_);
		triggered_ = 1;
	}
	const int rate = hostRate_ > 0 ? hostRate_ : 44100;
	if (cpuHz_ < 1 || rate < 1) return 0;
	/* Pump+mix in chunks — per-sample PumpCycles dwarfed realtime for silp/AIL. */
	const int chunk = 512;
	for (int i = 0; i < frames; ) {
		const int n = (frames - i > chunk) ? chunk : (frames - i);
		cpuAcc_ += (int64_t)cpuHz_ * (int64_t)n;
		int cycles = (int)(cpuAcc_ / (int64_t)rate);
		cpuAcc_ %= (int64_t)rate;
		if (cycles < n) cycles = n;
		hw_->PumpCycles(hw_->cpuCycles_ + (uint64_t)cycles);
		chip->Render(stereo + i * 2, n);
		hw_->MixExtra(stereo + i * 2, n);
		i += n;
	}
	hw_->oplWriteCount_ = CEmuChipYm3812WriteCount(chip);
	hw_->oplKeyOnCount_ = CEmuChipYm3812KeyOnCount(chip);
	return frames;
}

int CDriverPcat::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
