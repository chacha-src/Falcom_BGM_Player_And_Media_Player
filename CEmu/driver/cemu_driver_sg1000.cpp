#include "StdAfx.h"
#include "cemu_driver_sg1000.h"
#include "../chip/cemu_chip_sn76489.h"
#include "../z80/Ay_Cpu.h"
#include <string.h>

CDriverSg1000::CDriverSg1000()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(3579545)
	, psgHz_(3579545)
	, booted_(0)
	, triggered_(0)
	, songCmd_(0x8d)
	, psgResidual_(0)
	, cpuAcc_(0)
	, nextTickAt_(0)
	, toneFallback_(0)
	, knownTick_(0)
{
}

CDriverSg1000::~CDriverSg1000()
{
	Close();
}

unsigned CDriverSg1000::PsgWrites() const
{
	return hw_ ? hw_->psgWrites_ : 0;
}

void CDriverSg1000::TickPsg(uint64_t cpuCycles)
{
	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0) return;
	psgResidual_ += cpuCycles * (uint64_t)psgHz_;
	const uint64_t ticks = psgResidual_ / (uint64_t)cpuHz_;
	psgResidual_ %= (uint64_t)cpuHz_;
	if (ticks)
		hw_->SoundChip()->AdvanceClocks(ticks);
}

void CDriverSg1000::RunUntil(uint64_t endCycle)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CEmuHardSg1000SetActive(hw_);
	int guard = 0;
	while ((uint64_t)cpu->time() < endCycle && guard++ < 2000000) {
		const int cycles = Ay_CpuRunOne(cpu);
		if (cycles <= 0) break;
		hw_->AddCpuCycles((uint64_t)cycles);
		TickPsg((uint64_t)cycles);
		/* HALT: bump time so we can escape idle loops. */
		if (cpu->get_mem() && cpu->get_mem()[cpu->r.pc] == 0x76) {
			cpu->adjust_time(4);
			hw_->AddCpuCycles(4);
			TickPsg(4);
			break;
		}
	}
}

void CDriverSg1000::CallZ80(uint16_t targetPc)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	uint8_t* mem = hw_->Mem();
	if (!mem) return;
	CEmuHardSg1000SetActive(hw_);

	const uint16_t ret = 0xFF80;
	mem[ret] = 0x76; /* HALT sentinel */
	uint16_t sp = cpu->r.sp;
	sp -= 2;
	mem[sp] = (uint8_t)(ret & 0xff);
	mem[(uint16_t)(sp + 1)] = (uint8_t)(ret >> 8);
	cpu->r.sp = sp;
	cpu->r.pc = targetPc;

	const uint64_t start = (uint64_t)cpu->time();
	const uint64_t limit = start + (uint64_t)cpuHz_ / 2; /* 0.5s cap */
	int guard = 0;
	while (guard++ < 2000000) {
		if (cpu->r.pc == ret)
			break;
		if (mem[cpu->r.pc] == 0x76 && cpu->r.pc == ret)
			break;
		if ((uint64_t)cpu->time() >= limit)
			break;
		const int cycles = Ay_CpuRunOne(cpu);
		if (cycles <= 0) break;
		hw_->AddCpuCycles((uint64_t)cycles);
		TickPsg((uint64_t)cycles);
		if (mem[cpu->r.pc] == 0x76) {
			cpu->r.pc = ret;
			break;
		}
	}
	cpu->r.pc = ret;
}

void CDriverSg1000::ForceToneTest()
{
	if (!hw_ || !hw_->SoundChip()) return;
	CChip* chip = hw_->SoundChip();
	/* Tone0 period ~0x100, volume loud; mute others. */
	chip->Write(0, 0x80 | 0x00 | 0x00); /* latch tone0 fine */
	chip->Write(0, 0x10);               /* tone0 coarse */
	chip->Write(0, 0x90 | 0x00);         /* tone0 vol = 0 (loud) */
	chip->Write(0, 0xBF);               /* tone1 mute */
	chip->Write(0, 0xDF);               /* tone2 mute */
	chip->Write(0, 0xFF);               /* noise mute */
	toneFallback_ = 1;
	hw_->psgWrites_ = CEmuChipSn76489WriteCount(chip);
}

void CDriverSg1000::TriggerSong()
{
	if (!hw_ || !hw_->Cpu() || !hw_->Mem()) return;
	uint8_t* mem = hw_->Mem();
	const uint16_t box = hw_->mailboxAddr_;
	const uint16_t upd = hw_->soundUpdatePc_;
	const uint16_t mute = hw_->soundMutePc_;

	/* Clear work RAM used by Sega PSG drivers (C000-C3FF covers Congo
	   C1E6 and Mikie C300 engine mailboxes / channel blocks). */
	memset(mem + 0xC000, 0, 0x400);

	/* BIT7 mailbox glue: mute → poke cmd → update tick. */
	const int haveGlue = (box >= 0xC000 && box <= 0xC3FF
		&& upd > 0 && upd < 0xC000 && mute > 0 && mute < 0xC000
		&& mem[upd] != 0x00);
	if (haveGlue) {
		CallZ80(mute);
		mem[box] = songCmd_;
		if ((songCmd_ & 0x80) == 0)
			mem[box] = (uint8_t)(0x80 | (songCmd_ & 0x7f));
		CallZ80(upd);
		/* Second tick helps Congo/Mikie sequencers leave init. */
		CallZ80(upd);
		knownTick_ = 1;
	}

	triggered_ = 1;
	/* Prefer real PSG traffic; tone fallback only if glue produced nothing. */
	if (hw_->psgWrites_ == 0)
		ForceToneTest();
}

int CDriverSg1000::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardSg1000*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 3579545;
	psgHz_ = hw_->psgHz_ > 0 ? hw_->psgHz_ : 3579545;
	psgResidual_ = 0;
	cpuAcc_ = 0;
	booted_ = 0;
	triggered_ = 0;
	toneFallback_ = 0;
	knownTick_ = 0;

	uint8_t code = 0x0d; /* Star Jacker title BGM */
	if (titleCode && (titleCode & 0xff) != 0)
		code = (uint8_t)(titleCode & 0xff);
	else if (ge->titleCount > 0 && ge->title[0].code)
		code = (uint8_t)(ge->title[0].code & 0xff);
	songCmd_ = (uint8_t)(0x80 | (code & 0x7f));

	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;

	CEmuHardSg1000SetActive(hw_);
	/* Brief settle so reset vectors exist; we don't run the full game. */
	RunUntil((uint64_t)cpuHz_ / 120);
	booted_ = 1;
	TriggerSong();
	nextTickAt_ = (uint64_t)hw_->Cpu()->time() + (uint64_t)cpuHz_ / 60;
	return 1;
}

void CDriverSg1000::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
}

int CDriverSg1000::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	Ay_Cpu* cpu = hw_->Cpu();
	CChip* chip = hw_->SoundChip();
	if (!cpu || !chip) return 0;
	CEmuHardSg1000SetActive(hw_);
	if (hostRate_ < 1 || cpuHz_ < 1) return 0;

	for (int i = 0; i < frames; i++) {
		const uint64_t now = (uint64_t)cpu->time();
		if (!toneFallback_ && knownTick_ && now >= nextTickAt_) {
			if (hw_->soundUpdatePc_)
				CallZ80(hw_->soundUpdatePc_);
			nextTickAt_ = (uint64_t)cpu->time() + (uint64_t)cpuHz_ / 60;
		}
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		/* Tone fallback: no Z80 needed; just advance chip time conceptually. */
		if (toneFallback_) {
			TickPsg((uint64_t)cyclesPerSample);
			cpu->adjust_time(cyclesPerSample);
			hw_->AddCpuCycles((uint64_t)cyclesPerSample);
		} else {
			const uint64_t end = (uint64_t)cpu->time() + (uint64_t)cyclesPerSample;
			/* Idle between ticks — keep PSG clocks moving. */
			const uint64_t cur = (uint64_t)cpu->time();
			if (end > cur) {
				cpu->adjust_time((int)(end - cur));
				hw_->AddCpuCycles(end - cur);
				TickPsg(end - cur);
			}
		}
		chip->Render(stereo + i * 2, 1);
	}
	return frames;
}

int CDriverSg1000::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
