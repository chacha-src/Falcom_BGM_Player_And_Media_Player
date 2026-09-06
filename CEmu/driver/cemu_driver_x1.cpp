#include "StdAfx.h"
#include "cemu_driver_x1.h"
#include "../chip/cemu_chip_opm.h"
#include "../chip/cemu_chip_ay.h"
#include "../z80/Ay_Cpu.h"
#include <string.h>

enum {
	/* hoot mucomx1: TIMER = 256*18 CPU-timeline clocks. The prescaler is
	   already represented by that constant; applying CPU/2 again here
	   halves Sorcerian's interrupt and music cadence.
	   IM2 vectors come from CTC (ch0/ch3) or XML ctcN — not fixed here. */
	X1_TIMER_CYCLES = 256 * 18
};

/* Resolve IM2 target. NCS gaia/hayato park JP <handler> opcodes at I*256+N
   (code, not a vector table). Word-fetch would read C3 xx as address xxc3. */
static uint16_t X1Im2Target(Ay_Cpu* cpu, uint8_t vector)
{
	if (!cpu) return 0;
	const uint8_t* mem = cpu->get_mem();
	if (!mem) return 0;
	const uint16_t slot = (uint16_t)(((uint16_t)cpu->r.i << 8) | vector);
	if (mem[slot] == 0xC3)
		return (uint16_t)mem[(uint16_t)(slot + 1)]
			| ((uint16_t)mem[(uint16_t)(slot + 2)] << 8);
	return Ay_CpuIm2Target(cpu, vector);
}

CDriverX1::CDriverX1()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(4000000)
	, opmHz_(4000000)
	, ayHz_(2000000)
	, booted_(0)
	, triggered_(0)
	, songCode_(0)
	, titleCode_(0)
	, opmResidual_(0)
	, ayResidual_(0)
	, cpuAcc_(0)
	, nextTimer_(0)
	, nextVsync_(0)
	, timerPeriod_(X1_TIMER_CYCLES)
	, vsyncPeriod_(4000000 / 60)
{
}

CDriverX1::~CDriverX1()
{
	Close();
}

unsigned CDriverX1::OpmWrites() const
{
	return hw_ ? hw_->OpmWrites() : 0;
}

unsigned CDriverX1::AyWrites() const
{
	return hw_ ? hw_->AyWrites() : 0;
}

void CDriverX1::TickChips(uint64_t cpuCycles)
{
	if (!hw_ || cpuCycles == 0) return;
	if (hw_->SoundChip()) {
		opmResidual_ += cpuCycles * (uint64_t)opmHz_;
		const uint64_t ticks = opmResidual_ / (uint64_t)cpuHz_;
		opmResidual_ %= (uint64_t)cpuHz_;
		if (ticks)
			hw_->SoundChip()->AdvanceClocks(ticks);
	}
	if (hw_->ChipAy()) {
		ayResidual_ += cpuCycles * (uint64_t)ayHz_;
		const uint64_t ticks = ayResidual_ / (uint64_t)cpuHz_;
		ayResidual_ %= (uint64_t)cpuHz_;
		if (ticks)
			hw_->ChipAy()->AdvanceClocks(ticks);
	}
}

void CDriverX1::SyncTimerPeriodFromCtc()
{
	if (!hw_) return;
	const unsigned p = hw_->CtcTimerPeriodCycles(0);
	if (p > 0)
		timerPeriod_ = (uint64_t)p;
}

void CDriverX1::DeliverIrqs(uint64_t now)
{
	SyncTimerPeriodFromCtc();
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	int timerDue = (timerPeriod_ > 0 && now >= nextTimer_) ? 1 : 0;
	int vsyncDue = (vsyncPeriod_ > 0 && now >= nextVsync_) ? 1 : 0;

	/* Decay play-cmd hold once per due VSYNC (~60Hz → 90 ≈ 1.5s). */
	if (vsyncDue && hw_->playCmdHoldIrqs_ > 0) {
		hw_->playCmdHoldIrqs_--;
		if (hw_->playCmdHoldIrqs_ == 0) {
			hw_->playCmdLatch_ = 0;
			hw_->ydosCmdSeen_ = 0;
			hw_->ydosInhibitReentry_ = 0;
			if (hw_->Mem() && hw_->Mem()[CHardX1::PLAY_FLAG] == 0x01)
				hw_->Mem()[CHardX1::PLAY_FLAG] = 0x00;
		}
	}

	/* CTC-programmed IM2 vectors (hoot mucomx1: ch0→TIMER, ch3→VSYNC).
	   Once the guest programs the CTC, honor each channel's IE bit. Injecting
	   both host sources regardless of IE double-steps Falcom music drivers. */
	uint8_t timerVec = hw_->CtcVector(0);
	uint8_t vsyncVec = hw_->CtcVector(3);
	const int ctcProgrammed = hw_->CtcVectorProgrammed();

	int timerIrq = timerDue && (!ctcProgrammed || hw_->CtcIe(0));
	int vsyncIrq = vsyncDue && (!ctcProgrammed || hw_->CtcIe(3));
	if (!cpu->r.iff1) {
		timerIrq = 0;
		vsyncIrq = 0;
	} else if (cpu->r.im == 2) {
		if (timerIrq && X1Im2Target(cpu, timerVec) == 0) timerIrq = 0;
		if (vsyncIrq && X1Im2Target(cpu, vsyncVec) == 0) vsyncIrq = 0;
	}

	if (timerIrq) {
		if (cpu->r.im == 2)
			Ay_CpuIm2InterruptTo(cpu, X1Im2Target(cpu, timerVec));
		else
			Ay_CpuIm1Interrupt(cpu);
	} else if (vsyncIrq) {
		if (cpu->r.im == 2)
			Ay_CpuIm2InterruptTo(cpu, X1Im2Target(cpu, vsyncVec));
		else
			Ay_CpuIm1Interrupt(cpu);
	}

	/* Catch up schedules to 'now' so a long DI/halt gap does not multi-fire
	   decay or IRQ storms on the next instructions. */
	if (timerDue && timerPeriod_ > 0) {
		while (nextTimer_ + timerPeriod_ <= now)
			nextTimer_ += timerPeriod_;
		nextTimer_ += timerPeriod_;
	}
	if (vsyncDue && vsyncPeriod_ > 0) {
		while (nextVsync_ + vsyncPeriod_ <= now)
			nextVsync_ += vsyncPeriod_;
		nextVsync_ += vsyncPeriod_;
	}
}

void CDriverX1::RunUntil(uint64_t endCycle)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CEmuHardX1SetActive(hw_);
	int guard = 0;
	while ((uint64_t)cpu->time() < endCycle && guard++ < 4000000) {
		const uint64_t now = (uint64_t)cpu->time();
		DeliverIrqs(now);
		/* HALT: jump time to next timer/vsync/sample so IRQs stay realtime. */
		if (cpu->get_mem() && cpu->get_mem()[cpu->r.pc] == 0x76) {
			uint64_t wake = endCycle;
			if (timerPeriod_ > 0 && nextTimer_ > now && nextTimer_ < wake)
				wake = nextTimer_;
			if (vsyncPeriod_ > 0 && nextVsync_ > now && nextVsync_ < wake)
				wake = nextVsync_;
			uint64_t delta = (wake > now) ? (wake - now) : 4;
			if (delta < 4) delta = 4;
			if (delta > 0x7fffffff) delta = 0x7fffffff;
			cpu->adjust_time((int)delta);
			hw_->AddCpuCycles(delta);
			TickChips(delta);
			continue;
		}
		const int cycles = Ay_CpuRunOne(cpu);
		if (cycles <= 0) break;
		hw_->AddCpuCycles((uint64_t)cycles);
		TickChips((uint64_t)cycles);
	}
}

void CDriverX1::TriggerSong()
{
	if (!hw_) return;
	hw_->TriggerPlay(titleCode_ ? titleCode_ : (unsigned)songCode_);
	triggered_ = 1;
}

int CDriverX1::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardX1*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 4000000;
	opmHz_ = hw_->opmHz_ > 0 ? hw_->opmHz_ : 4000000;
	ayHz_ = hw_->ayHz_ > 0 ? hw_->ayHz_ : 2000000;
	timerPeriod_ = (uint64_t)X1_TIMER_CYCLES;
	vsyncPeriod_ = (uint64_t)cpuHz_ / 60;
	opmResidual_ = 0;
	ayResidual_ = 0;
	cpuAcc_ = 0;
	booted_ = 0;
	triggered_ = 0;

	/* titleCode 0 is a valid hoot "main theme" — do not treat as missing. */
	titleCode_ = titleCode;
	{
		uint8_t song = 0, bank = 0;
		CHardX1::UnpackTitle(titleCode_, &song, &bank);
		(void)bank;
		songCode_ = song;
	}
	if (ge->titleCount == 0 && titleCode_ == 0)
		titleCode_ = 0; /* catalog may use song 0 — never invent 0x1b */

	if (!hw_->LoadRoms(fs, ge, titleCode_))
		return 0;

	CEmuHardX1SetActive(hw_);
	/* Pre-stage BGM before boot so DRIVER init (gaia/hayato CALL DRV)
	   can walk music headers at mdata_addr instead of jumping through
	   nulls into empty high RAM. Play mailbox stays clear until TriggerSong. */
	hw_->PrestageBgm(titleCode_);
	/* Boot until PATCH poll (after CALL 040D CTC/IM2 setup).
	   Some drivers need ~0.5s of timer/vsync before accepting Play. */
	{
		Ay_Cpu* cpu = hw_->Cpu();
		if (cpu) {
			nextTimer_ = (uint64_t)cpu->time() + timerPeriod_;
			nextVsync_ = (uint64_t)cpu->time() + vsyncPeriod_;
			RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_); /* ~1.0s boot */
		}
	}
	booted_ = 1;
	TriggerSong();
	return 1;
}

void CDriverX1::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
}

int CDriverX1::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	Ay_Cpu* cpu = hw_->Cpu();
	if (!cpu || hostRate_ < 1 || cpuHz_ < 1) return 0;
	CEmuHardX1SetActive(hw_);

	for (int i = 0; i < frames; i++) {
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		const uint64_t end = (uint64_t)cpu->time() + (uint64_t)cyclesPerSample;
		RunUntil(end);
		/* Keep schedule moving if we stalled under DI. */
		const uint64_t now = (uint64_t)cpu->time();
		if (now >= nextTimer_ + timerPeriod_ * 4)
			nextTimer_ = now + timerPeriod_;
		if (now >= nextVsync_ + vsyncPeriod_ * 4)
			nextVsync_ = now + vsyncPeriod_;

		int16_t opmBuf[2] = { 0, 0 };
		int16_t ayBuf[2] = { 0, 0 };
		if (hw_->SoundChip())
			hw_->SoundChip()->Render(opmBuf, 1);
		if (hw_->ChipAy())
			hw_->ChipAy()->Render(ayBuf, 1);
		int32_t l = (int32_t)opmBuf[0] + (int32_t)ayBuf[0];
		int32_t r = (int32_t)opmBuf[1] + (int32_t)ayBuf[1];
		if (l > 32767) l = 32767;
		if (l < -32768) l = -32768;
		if (r > 32767) r = 32767;
		if (r < -32768) r = -32768;
		stereo[i * 2] = (int16_t)l;
		stereo[i * 2 + 1] = (int16_t)r;
	}
	return frames;
}

int CDriverX1::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
