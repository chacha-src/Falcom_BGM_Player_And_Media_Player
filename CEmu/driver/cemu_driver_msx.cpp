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
	/* EI;HALT (yosikon play): HALT is the delayed insn, so accept IRQ. */
	if (cpu->get_mem() && cpu->get_mem()[cpu->r.pc] == 0x76)
		cpu->irqDelay = 0;
	/* hoot kss.cpp Interrupt: raise_IRQ(0xff) under IM2 IPL.
	   The IPL ISR lives at $0038; IM2 only works if the game filled
	   (I<<8)|$FF with a real vector. KSS StartSong fills $0000-$3FFF
	   with $C9, so an unset I register yields a $C9C9 target and the
	   music ISR never runs (judo/replcart/labyr SILENT). */
	if (cpu->r.im == 2) {
		const uint16_t target = Ay_CpuIm2Target(cpu, 0xff);
		uint8_t* mem = cpu->get_mem();
		int useIm2 = (target != 0);
		if (useIm2 && mem) {
			const uint8_t op = mem[target];
			if (op == 0xC9 || op == 0x00 || op == 0xFF)
				useIm2 = 0;
		}
		if (!useIm2 || !Ay_CpuIm2Interrupt(cpu, 0xff))
			Ay_CpuIm1Interrupt(cpu);
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
	while ((uint64_t)cpu->time64() < endCycle && guard++ < 4000000) {
		const uint64_t now = (uint64_t)cpu->time64();
		/* HALT: sleep until this sample's CPU budget ends. VBlank is
		   injected from Render on the hostRate/60 sample grid.
		   DI;HALT (f1sp3d CALL $9003, yosikon CALL $D406) never wakes
		   because PulseVblankIrq requires IFF1 — step past as NOP.
		   EI;HALT: irqDelay would otherwise stick — RunUntil never
		   executes HALT as an insn, so PulseVblankIrq keeps dropping. */
		if (cpu->get_mem() && cpu->get_mem()[cpu->r.pc] == 0x76) {
			cpu->irqDelay = 0;
			if (hw_->GenericMode() && !cpu->r.iff1) {
				cpu->r.pc = (uint16_t)(cpu->r.pc + 1);
				cpu->adjust_time(4);
				hw_->AddCpuCycles(4);
				continue;
			}
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
	/* Pass the whole code, including 0 (fmpac sample 00, ds4 track 0).
	   Forcing 0→1 collapsed two picks onto one song. */
	if (!hw_->StartSong(titleCode))
		return 0;

	Ay_Cpu* cpu = hw_->Cpu();
	cpuTarget_ = cpu ? (uint64_t)cpu->time64() : 0;
	playing_ = 1;
	return 1;
}

void CDriverMsx::Close()
{
	hw_ = NULL;
	playing_ = 0;
}

int CDriverMsx::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	return hw_->StartSong(titleCode) ? 1 : 0;
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
		if (hw_->ChipScc())
			hw_->ChipScc()->MixAdd(ayBuf, 1, 256);
		if (hw_->ChipSng()) {
			int16_t snBuf[2] = { 0, 0 };
			hw_->ChipSng()->Render(snBuf, 1);
			int32_t sl = (int32_t)ayBuf[0] + (int32_t)snBuf[0];
			int32_t sr = (int32_t)ayBuf[1] + (int32_t)snBuf[1];
			if (sl > 32767) sl = 32767;
			if (sl < -32768) sl = -32768;
			if (sr > 32767) sr = 32767;
			if (sr < -32768) sr = -32768;
			ayBuf[0] = (int16_t)sl;
			ayBuf[1] = (int16_t)sr;
		}
		if (hw_->Opll()) {
			OPLL* ochip = (OPLL*)hw_->Opll();
			int32_t o = (int32_t)OPLL_calc(ochip) * 5;
			if (o > 32767) o = 32767;
			if (o < -32768) o = -32768;
			opllS = (int16_t)o;
		}
		if (hw_->ChipOpl()) {
			int16_t oplBuf[2] = { 0, 0 };
			hw_->ChipOpl()->Render(oplBuf, 1);
			int32_t ol = (int32_t)opllS + (int32_t)oplBuf[0];
			if (ol > 32767) ol = 32767;
			if (ol < -32768) ol = -32768;
			opllS = (int16_t)ol;
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
