#include "StdAfx.h"
#include "cemu_driver_msx.h"
#include "../chip/cemu_chip_ay.h"
#include "../z80/Ay_Cpu.h"
#include "../s98/device/emu2413/emu2413.h"
#include <string.h>
#include <stdlib.h>

CDriverMsx::CDriverMsx()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(3579545)
	, ayHz_(3579545 / 2)
	, opllHz_(3579545)
	, ayResidual_(0)
	, opllResidual_(0)
	, cpuAcc_(0)
	, cpuTarget_(0)
	, nextIrq_(0)
	, irqPeriod_(3579545 / 60)
	, sampleIndex_(0)
	, nextIrqSample_(0)
	, irqPulses_(0)
	, playing_(0)
{
}

CDriverMsx::~CDriverMsx()
{
	Close();
}

void CDriverMsx::TickChips(uint64_t cpuCycles)
{
	(void)cpuCycles;
	/* AY/OPLL are sample-driven in Render. */
}

void CDriverMsx::PulseVblankIrq()
{
	if (!hw_ || !hw_->Cpu() || !playing_) return;
	Ay_Cpu* cpu = hw_->Cpu();
	/* Sample-timeline VBlank only (not CPU-cycle DeliverIrq inside RunUntil).
	   Dual scheduling ran Quinpl's play routine twice per frame, blew the
	   Z80 stack into adjacent heap, and crashed on driver destroy. */
	if (!cpu->r.iff1) return;
	/* hoot kss.cpp Interrupt: raise_IRQ(0xff) under IM2 IPL. */
	if (cpu->r.im == 2) {
		if (!Ay_CpuIm2Interrupt(cpu, 0xff))
			Ay_CpuIm1Interrupt(cpu); /* IPL stub @0038 */
	} else {
		Ay_CpuIm1Interrupt(cpu);
	}
	irqPulses_++;
}

void CDriverMsx::RunUntil(uint64_t endCycle)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CEmuHardMsxSetActive(hw_);
	int guard = 0;
	while ((uint64_t)cpu->time() < endCycle && guard++ < 4000000) {
		const uint64_t now = (uint64_t)cpu->time();
		/* HALT: sleep until this sample's CPU budget ends. VBlank is
		   injected from Render on the hostRate/60 sample grid. */
		if (cpu->get_mem() && cpu->get_mem()[cpu->r.pc] == 0x76) {
			uint64_t delta = (endCycle > now) ? (endCycle - now) : 4;
			if (delta < 4) delta = 4;
			if (delta > 0x7fffffff) delta = 0x7fffffff;
			cpu->adjust_time((int)delta);
			hw_->AddCpuCycles(delta);
			continue;
		}
		const int cycles = Ay_CpuRunOne(cpu);
		if (cycles <= 0) break;
		hw_->AddCpuCycles((uint64_t)cycles);
		TickChips((uint64_t)cycles);
	}
}

int CDriverMsx::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardMsx*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 3579545;
	ayHz_ = hw_->ayHz_ > 0 ? hw_->ayHz_ : cpuHz_ / 2;
	opllHz_ = hw_->opllHz_ > 0 ? hw_->opllHz_ : cpuHz_;
	irqPeriod_ = (uint64_t)cpuHz_ / 60;
	ayResidual_ = 0;
	opllResidual_ = 0;
	cpuAcc_ = 0;
	cpuTarget_ = 0;
	sampleIndex_ = 0;
	nextIrqSample_ = (uint64_t)hostRate_ / 60u;
	nextIrq_ = 0;
	irqPulses_ = 0;
	playing_ = 0;

	if (!hw_->LoadKss(fs, ge, titleCode))
		return 0;
	unsigned song = titleCode ? (titleCode & 0xff) : 1u;
	if (!song && ge->titleCount > 0)
		song = ge->title[0].code & 0xff;
	if (!hw_->StartSong(song))
		return 0;

	Ay_Cpu* cpu = hw_->Cpu();
	cpuTarget_ = cpu ? (uint64_t)cpu->time() : 0;
	playing_ = 1;
	return 1;
}

void CDriverMsx::Close()
{
	hw_ = NULL;
	playing_ = 0;
}

int CDriverMsx::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	Ay_Cpu* cpu = hw_->Cpu();
	if (!cpu || hostRate_ < 1 || cpuHz_ < 1) return 0;
	CEmuHardMsxSetActive(hw_);

	for (int i = 0; i < frames; i++) {
		/* Absolute sample→CPU mapping: VBlank is scheduled on the output
		   timeline (hostRate/60), not on whatever instruction overshoot the
		   Z80 accumulated. That removes Quinpl's tempo wobble. */
		sampleIndex_++;
		const uint64_t want = (sampleIndex_ * (uint64_t)cpuHz_) / (uint64_t)hostRate_;
		if (want > cpuTarget_)
			cpuTarget_ = want;
		RunUntil(cpuTarget_);
		if (sampleIndex_ >= nextIrqSample_) {
			/* Advance the 60 Hz grid even under DI so EI never catches up
			   multiple missed edges (Quinpl ~2x). */
			const uint64_t step = (uint64_t)hostRate_ / 60u;
			nextIrqSample_ += step ? step : 1u;
			PulseVblankIrq();
			/* Do not add an out-of-band ISR budget here.  The next output
			   samples naturally execute the handler on the same absolute
			   sample-to-CPU timeline.  A 200 us bonus on every VBlank made
			   the CPU run in periodic bursts, then idle until the timeline
			   caught up, which was audible as Quinpl tempo wobble. */
		}

		int16_t ayBuf[2] = { 0, 0 };
		int16_t opllS = 0;
		if (hw_->ChipAy())
			hw_->ChipAy()->Render(ayBuf, 1);
		if (hw_->Opll()) {
			OPLL* ochip = (OPLL*)hw_->Opll();
			int32_t o = (int32_t)OPLL_calc(ochip) * 5;
			if (o > 32767) o = 32767;
			if (o < -32768) o = -32768;
			opllS = (int16_t)o;
		}
		int32_t l = (int32_t)ayBuf[0] + (int32_t)opllS;
		int32_t r = (int32_t)ayBuf[1] + (int32_t)opllS;
		if (l > 32767) l = 32767;
		if (l < -32768) l = -32768;
		if (r > 32767) r = 32767;
		if (r < -32768) r = -32768;
		stereo[i * 2] = (int16_t)l;
		stereo[i * 2 + 1] = (int16_t)r;
	}
	return frames;
}

int CDriverMsx::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
