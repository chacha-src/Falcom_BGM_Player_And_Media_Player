#include "StdAfx.h"
#include "cemu_driver_f3.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>
#include <stdlib.h>

CDriverF3::CDriverF3()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(15238100)
	, cpuAcc_(0)
	, booted_(0)
	, songCode_(1)
	, tryCount_(0)
	, cmdIndex_(0)
	, dwellLeft_(0)
	, dwellFrames_(22050)
	, bestPeak_(0)
	, windowPeak_(0)
	, bestSongCode_(1)
	, locked_(0)
	, irqPhase_(0)
{
	memset(tryCodes_, 0, sizeof(tryCodes_));
}

CDriverF3::~CDriverF3()
{
	Close();
}

static void CDriverF3Push(unsigned* dst, int* n, int cap, unsigned code)
{
	if (!dst || !n || *n >= cap || code == 0) return;
	for (int i = 0; i < *n; i++) {
		if (dst[i] == code) return;
	}
	dst[(*n)++] = code;
}

int CDriverF3::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs || hw->hardKind != CHard::KIND_F3) return 0;
	hw_ = (CHardF3*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 15238100;
	cpuAcc_ = 0;
	booted_ = 0;
	locked_ = 0;
	bestPeak_ = 0;
	windowPeak_ = 0;
	tryCount_ = 0;
	cmdIndex_ = 0;
	irqPhase_ = 0;

	songCode_ = titleCode ? titleCode : 1;
	CDriverF3Push(tryCodes_, &tryCount_, (int)_countof(tryCodes_), songCode_);
	for (int i = 0; i < ge->titleCount; i++) {
		if (ge->title[i].code)
			CDriverF3Push(tryCodes_, &tryCount_, (int)_countof(tryCodes_), ge->title[i].code);
	}
	static const unsigned kFallback[] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0x10, 0x11, 0x12, 0x20, 0x21, 0x30,
		0x40, 0x50, 0x80, 0x81, 0x82, 0x90, 0xa0, 0xb0, 0x100, 0x101, 0x200
	};
	for (int i = 0; i < (int)(sizeof(kFallback) / sizeof(kFallback[0])); i++)
		CDriverF3Push(tryCodes_, &tryCount_, (int)_countof(tryCodes_), kFallback[i]);
	if (tryCount_ < 1) {
		tryCodes_[0] = 1;
		tryCount_ = 1;
	}

	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;

	CEmuHardF3SetActive(hw_);
	/* Boot settle: DUART/IVR/idle STOP. Ring ready is host-planted in SetSongCommand. */
	RunCycles(cpuHz_);
	booted_ = 1;
	/* If still IPL-masked in a delay, drop IPL once so DUART can run (no main CPU). */
	{
		const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR);
		if (((sr >> 8) & 7) >= 6)
			m68k_set_reg(M68K_REG_SR, (sr & ~0x0700u) | 0x2000u);
	}
	RunCycles(cpuHz_ / 5);
	songCode_ = tryCodes_[0];
	cmdIndex_ = 1;
	hw_->SetSongCommand(songCode_);
	RunCycles(cpuHz_ / 2);
	dwellFrames_ = hostRate_ > 0 ? hostRate_ / 2 : 22050;
	if (dwellFrames_ < 1) dwellFrames_ = 1;
	dwellLeft_ = dwellFrames_;
	bestSongCode_ = songCode_;
	return 1;
}

void CDriverF3::Close()
{
	hw_ = NULL;
	booted_ = 0;
}

void CDriverF3::RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	CEmuHardF3SetActive(hw_);
	/*
	 * Interleave DUART timer with CPU: timer only advances here, so a single
	 * giant m68k_execute would allow at most one IRQ (IACK clears the line and
	 * the handler clears ISR). Slice so STOP/idle loops keep getting IRQ6.
	 *
	 * Task RTE loads SR=0 (user) from the task block; song start uses A-line to
	 * raise IPL. Repair only after a slice — never mid-handler — so DUART can run.
	 */
	while (cycles > 0) {
		int slice = cycles;
		if (slice > 4000) slice = 4000;
		hw_->TickDuart(slice);
		if (hw_->DuartIrqPending())
			m68k_set_irq(M68K_IRQ_6);
		else
			m68k_set_irq(M68K_IRQ_NONE);
		const int ran = m68k_execute(slice);
		{
			const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR);
			const unsigned ipl = (sr >> 8) & 7u;
			const int stuck = (ran == slice); /* Musashi: stopped → returns num_cycles */
			if (!(sr & 0x2000u) || (stuck && ipl >= 6u))
				m68k_set_reg(M68K_REG_SR, (sr | 0x2000u) & ~0x0700u);
		}
		cycles -= slice;
	}
	if (!hw_->DuartIrqPending())
		m68k_set_irq(M68K_IRQ_NONE);
}

void CDriverF3::TryInjectCommand()
{
	if (!hw_ || locked_) return;
	if (dwellLeft_ > 0) {
		dwellLeft_--;
		return;
	}
	if (windowPeak_ > bestPeak_) {
		bestPeak_ = windowPeak_;
		bestSongCode_ = songCode_;
	}
	windowPeak_ = 0;
	if (bestPeak_ > 800) {
		locked_ = 1;
		if (songCode_ != bestSongCode_) {
			songCode_ = bestSongCode_;
			hw_->SetSongCommand(songCode_);
		}
		return;
	}
	if (cmdIndex_ < tryCount_) {
		songCode_ = tryCodes_[cmdIndex_++];
		hw_->SetSongCommand(songCode_);
		dwellLeft_ = dwellFrames_;
	} else {
		locked_ = 1;
		songCode_ = bestSongCode_ ? bestSongCode_ : 1;
		hw_->SetSongCommand(songCode_);
	}
}

int CDriverF3::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	CChip* chip = hw_->SoundChip();
	if (!chip || hostRate_ < 1 || cpuHz_ < 1) return 0;
	CEmuHardF3SetActive(hw_);

	for (int i = 0; i < frames; i++) {
		if (!locked_)
			TryInjectCommand();
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		RunCycles(cyclesPerSample);
		chip->Render(stereo + i * 2, 1);
		if (!locked_) {
			const int16_t l = stereo[i * 2];
			const int16_t r = stereo[i * 2 + 1];
			int a = l < 0 ? -l : l;
			int b = r < 0 ? -r : r;
			if (b > a) a = b;
			if (a > windowPeak_) windowPeak_ = a;
		}
	}
	return frames;
}

int CDriverF3::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}

CDriver* CDriverF3Create()
{
	return new CDriverF3();
}
