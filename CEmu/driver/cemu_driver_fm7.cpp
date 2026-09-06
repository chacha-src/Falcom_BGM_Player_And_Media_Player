#include "StdAfx.h"
#include "cemu_driver_fm7.h"
#include "../chip/cemu_chip_opna.h"
#include "../chip/cemu_chip_ay.h"
#include <string.h>

CDriverFm7::CDriverFm7()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(2000000)
	, opnHz_(1228800)
	, ayHz_(1228800)
	, booted_(0)
	, triggered_(0)
	, songCode_(0)
	, titleCode_(0)
	, opnResidual_(0)
	, opnTimerMul_(1)
	, opnTimerDiv_(1)
	, ayResidual_(0)
	, cpuAcc_(0)
	, nextVsync_(0)
	, vsyncPeriod_(2000000 / 60)
	, irqPulses_(0)
	, prevChipIrq_(0)
	, lastFd03IrqVec_(0xFFFF)
{
}

CDriverFm7::~CDriverFm7()
{
	Close();
}

unsigned CDriverFm7::OpnWrites() const
{
	return hw_ ? hw_->OpnWrites() : 0;
}

unsigned CDriverFm7::AyWrites() const
{
	return hw_ ? hw_->AyWrites() : 0;
}

void CDriverFm7::TickChips(uint64_t cpuCycles)
{
	if (!hw_ || cpuCycles == 0) return;
	if (hw_->ChipOpn()) {
		const uint64_t den = (uint64_t)cpuHz_ * (uint64_t)opnTimerDiv_;
		opnResidual_ += cpuCycles * (uint64_t)opnHz_ * (uint64_t)opnTimerMul_;
		const uint64_t ticks = opnResidual_ / den;
		opnResidual_ %= den;
		if (ticks)
			hw_->ChipOpn()->AdvanceClocks(ticks);
	}
	if (hw_->ChipAy()) {
		ayResidual_ += cpuCycles * (uint64_t)ayHz_;
		const uint64_t ticks = ayResidual_ / (uint64_t)cpuHz_;
		ayResidual_ %= (uint64_t)cpuHz_;
		if (ticks)
			hw_->ChipAy()->AdvanceClocks(ticks);
	}
}

void CDriverFm7::DeliverIrqs(uint64_t now)
{
	if (!hw_) return;
	mc6809__t* cpu = hw_->Mc6809();
	if (!cpu) return;

	int vsyncDue = (triggered_ && vsyncPeriod_ > 0 && now >= nextVsync_) ? 1 : 0;
	int chipIrq = 0;
	if (hw_->ChipOpn() && hw_->ChipOpn()->Irq())
		chipIrq = 1;

	/* ISR-sniffed polarity: laydock needs bit2; reviver bit0+bit3; default clear bit3.
	   Re-sniff when $FFF8 remounts after play (asteka2 PATCH: $20D1 stub → $A1C9). */
	const uint16_t irqNow = (uint16_t)(((uint16_t)hw_->Mem()[0xFFF8] << 8) | hw_->Mem()[0xFFF9]);
	if (irqNow != lastFd03IrqVec_) {
		lastFd03IrqVec_ = irqNow;
		hw_->RefreshFd03Polarity();
	}
	if (vsyncDue)
		hw_->ApplyFd03Vsync();

	/* Pulse music IRQ from one tick source:
	   OPN titles → chip timer edge (vsync+timer double-fired ys2_fmav ultra-fast).
	   PSG titles → vsync only. Always ApplyFd03Vsync above for $FD03 status. */
	int chipIrqEdge = (chipIrq && !prevChipIrq_) ? 1 : 0;
	prevChipIrq_ = chipIrq;

	auto isFd03Stub = [&](uint16_t vec) -> int {
		if (vec == 0 || vec == 0xFFFF) return 0;
		const uint8_t* p = hw_->Mem() + vec;
		if (p[0] == 0xB6 && p[1] == 0xFD && p[2] == 0x03) return 1;
		if (p[0] == 0x96 && p[1] == 0x03) return 1;
		return 0;
	};
	const uint16_t irqVec = (uint16_t)(((uint16_t)hw_->Mem()[0xFFF8] << 8) | hw_->Mem()[0xFFF9]);
	const uint16_t firqVec = (uint16_t)(((uint16_t)hw_->Mem()[0xFFF6] << 8) | hw_->Mem()[0xFFF7]);
	const int irqStub = isFd03Stub(irqVec);
	const int firqStub = isFd03Stub(firqVec);

	int raiseFromChip = 0;
	int raiseFromVsync = 0;
	int ranHostTick = 0;
	if (hw_->useOpn_ && hw_->ChipOpn()) {
		raiseFromChip = chipIrqEdge;
		/* Before Timer B arms, still allow a few vsync ticks so PATCH can run. */
		if (!raiseFromChip && vsyncDue && irqPulses_ < 8)
			raiseFromVsync = 1;
	} else {
		raiseFromVsync = vsyncDue;
		/* Ys' ripped image omits BIOS code reached after the hardware ISR.
		   Dispatch its installed soft vector at the real 60 Hz IRQ cadence,
		   then keep the absent foreground parked. */
		if (vsyncDue && hw_->patchTableBase_ == 0xFED0) {
			const uint16_t tick = (uint16_t)(((uint16_t)hw_->Mem()[0xFFE2] << 8)
				| hw_->Mem()[0xFFE3]);
			if (tick >= 0x0100 && tick < 0xFE00) {
				if (tick == 0x28EA || tick == 0x29EC) {
					/* MANPR1 and MANPR2 use the same work/channel format but
					   MANPR2's code grew independently, so use its real entry
					   addresses rather than assuming one relocation delta.
					   Execute all three native channel parsers and recover the
					   pointer store when a stripped BIOS output helper unwinds
					   before the driver's STU 2,X epilogue. */
					uint8_t* m = hw_->Mem();
					const int man2 = (tick == 0x29EC) ? 1 : 0;
					const uint16_t phase = man2 ? 0x2987 : 0x2885;
					const uint16_t active = man2 ? 0x2983 : 0x2881;
					const uint16_t flush = man2 ? 0x2DB3 : 0x2C4E;
					const uint16_t gate = man2 ? 0x2985 : 0x2883;
					const uint16_t gateReload = man2 ? 0x2984 : 0x2882;
					const uint16_t parser = man2 ? 0x2A98 : 0x293D;
					static const uint16_t kChannel1[3] = { 0x2E9A, 0x2EBF, 0x2EE4 };
					static const uint16_t kChannel2[3] = { 0x3040, 0x3065, 0x308A };
					const uint16_t* channels = man2 ? kChannel2 : kChannel1;
					if (++m[phase] >= 3) {
						m[phase] = 0;
						if (m[active])
							hw_->RunSubroutine(flush);
						m[gate] = 0;
						for (int ch = 0; ch < 3; ++ch) {
							const uint16_t base = channels[ch];
							const uint8_t count = m[base];
							const uint16_t oldPtr = (uint16_t)(((uint16_t)m[base + 2] << 8)
								| m[base + 3]);
							cpu->index[0].w = base;
							hw_->RunSubroutine(parser);
							const uint16_t newPtr = (uint16_t)(((uint16_t)m[base + 2] << 8)
								| m[base + 3]);
							uint16_t parsedPtr = cpu->index[2].w;
							if (parsedPtr < hw_->mdataAddr_
								|| parsedPtr >= hw_->mdataAddr_ + hw_->mdataSize_) {
								unsigned p = oldPtr;
								const unsigned end = (unsigned)hw_->mdataAddr_ + hw_->mdataSize_;
								for (int command = 0; command < 8 && p < end; ++command) {
									if (m[p] == 0xFC) p += 7;
									else if (m[p] == 0xFD || m[p] == 0xFE) p += 3;
									else break;
								}
								if (p + 1 < end && m[p] < 0xF0)
									parsedPtr = (uint16_t)(p + 2);
							}
							if (count == 1 && newPtr == oldPtr
								&& parsedPtr >= hw_->mdataAddr_
								&& parsedPtr < hw_->mdataAddr_ + hw_->mdataSize_) {
								m[base + 2] = (uint8_t)(parsedPtr >> 8);
								m[base + 3] = (uint8_t)parsedPtr;
							}
						}
						m[gate] = m[gateReload];
					}
				} else {
					hw_->RunSubroutine(tick);
				}
				hw_->Mem()[0xFC00] = 0x20;
				hw_->Mem()[0xFC01] = 0xFE;
				cpu->pc.w = 0xFC00;
				cpu->cc.i = false;
				cpu->cc.f = true;
				irqPulses_++;
				ranHostTick = 1;
				raiseFromVsync = 0;
			}
		}
	}

	if (!ranHostTick && (raiseFromChip || raiseFromVsync)) {
		/* A vsync source is wired to one 6809 line, not both.  When exactly
		   one vector is the $FD03 handler, use that line; firing the other
		   vector on Ys jumps into PATCH data at $FF00 and destroys the RTI
		   frame before MANPR can produce its first note. */
		const int routeFd03 = (raiseFromVsync && !raiseFromChip
			&& (irqStub != firqStub)) ? 1 : 0;
		if (!cpu->cc.i && (!routeFd03 || irqStub)
			&& !(raiseFromChip && !raiseFromVsync && irqStub)) {
			cpu->irq = true;
			irqPulses_++;
		}
		if (!cpu->cc.f && (!routeFd03 || firqStub)
			&& !(raiseFromChip && !raiseFromVsync && firqStub)) {
			cpu->firq = true;
			irqPulses_++;
		}
	}

	if (chipIrq && hw_->ChipOpn())
		hw_->ChipOpn()->AckIrq();

	if (vsyncDue && vsyncPeriod_ > 0) {
		while (nextVsync_ + vsyncPeriod_ <= now)
			nextVsync_ += vsyncPeriod_;
		nextVsync_ += vsyncPeriod_;
	}
}

void CDriverFm7::RunUntil(uint64_t endCycle)
{
	if (!hw_) return;
	mc6809__t* cpu = hw_->Mc6809();
	if (!cpu) return;
	CEmuHardFm7SetActive(hw_);
	int guard = 0;
	while ((uint64_t)cpu->cycles < endCycle && guard++ < 4000000) {
		const uint64_t now = (uint64_t)cpu->cycles;
		DeliverIrqs(now);
		/* CWAI/SYNC: jump to next vsync/sample so IRQs stay realtime. */
		if (cpu->cwai || cpu->sync) {
			uint64_t wake = endCycle;
			if (vsyncPeriod_ > 0 && nextVsync_ > now && nextVsync_ < wake)
				wake = nextVsync_;
			uint64_t delta = (wake > now) ? (wake - now) : 4;
			if (delta < 4) delta = 4;
			if (delta > 0x7fffffff) delta = 0x7fffffff;
			cpu->cycles += (unsigned long)delta;
			hw_->AddCpuCycles(delta);
			TickChips(delta);
			continue;
		}
		const unsigned long before = cpu->cycles;
		const int rc = mc6809_step(cpu);
		uint64_t ran = (uint64_t)(cpu->cycles - before);
		if (ran == 0) ran = 1;
		hw_->AddCpuCycles(ran);
		TickChips(ran);
		if (rc != 0)
			break;
	}
}

void CDriverFm7::TriggerSong()
{
	if (!hw_) return;
	hw_->TriggerPlay(titleCode_ ? titleCode_ : (unsigned)songCode_);
	triggered_ = 1;
}

int CDriverFm7::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardFm7*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 2000000;
	opnHz_ = hw_->opnHz_ > 0 ? hw_->opnHz_ : 1228800;
	ayHz_ = hw_->ayHz_ > 0 ? hw_->ayHz_ : 2457600;
	vsyncPeriod_ = (uint64_t)cpuHz_ / 60;
	opnResidual_ = 0;
	opnTimerMul_ = 1;
	opnTimerDiv_ = 1;
	/* Ys II FM77AV's staged Timer B cadence measures about 63.8 Hz.
	   Keep this title on the board's 60 Hz cadence without derating
	   unrelated FM-7 OPN drivers. */
	if (ge->archive[0] && _stricmp(ge->archive, "ys2_fmav") == 0) {
		opnTimerMul_ = 15;
		opnTimerDiv_ = 16;
	}
	ayResidual_ = 0;
	cpuAcc_ = 0;
	booted_ = 0;
	triggered_ = 0;
	prevChipIrq_ = 0;
	irqPulses_ = 0;
	lastFd03IrqVec_ = 0xFFFF;

	if (titleCode || ge->titleCount <= 0) {
		titleCode_ = titleCode;
	} else {
		/* Auto-pick only when caller passed 0 AND the set has no real title 0
		   (jikochu uses 0 as a playable song index). */
		int hasTitleZero = 0;
		for (int i = 0; i < ge->titleCount; i++) {
			if (ge->title[i].code == 0) { hasTitleZero = 1; break; }
		}
		if (hasTitleZero) {
			titleCode_ = 0;
		} else {
			titleCode_ = ge->title[0].code;
			for (int i = 0; i < ge->titleCount; i++) {
				if (ge->title[i].code != 0) {
					titleCode_ = ge->title[i].code;
					break;
				}
			}
		}
	}
	{
		uint8_t song = 0, bank = 0;
		CHardFm7::UnpackTitle(titleCode_, &song, &bank);
		(void)bank;
		songCode_ = song;
	}

	if (!hw_->LoadRoms(fs, ge, titleCode_))
		return 0;

	CEmuHardFm7SetActive(hw_);
	{
		mc6809__t* cpu = hw_->Mc6809();
		if (cpu) {
			nextVsync_ = (uint64_t)cpu->cycles + vsyncPeriod_;
			/* ~1.0s boot so PATCH installs vectors and reaches FD58 poll. */
			RunUntil((uint64_t)cpu->cycles + (uint64_t)cpuHz_);
		}
	}
	booted_ = 1;
	hw_->RefreshFd03Polarity();
	TriggerSong();
	/* Let PATCH consume $FD58/$FD80 play and remount IRQ vectors. */
	{
		mc6809__t* cpu = hw_->Mc6809();
		if (cpu) {
			uint64_t settle = (uint64_t)cpuHz_ / 10;
			/* Laydock: IRQ $3502 + $5E6F arm needs extra settle so OPN
			   voice init finishes before the first vsync music ticks. */
			const uint16_t irq = (uint16_t)(((uint16_t)hw_->Mem()[0xFFF8] << 8) | hw_->Mem()[0xFFF9]);
			if (irq == 0x3502)
				settle = (uint64_t)cpuHz_ / 2;
			/* PATCH now sees TTLPRG row zero immediately; the former
			   five-second settle consumed its embedded title before render. */
			RunUntil((uint64_t)cpu->cycles + settle);
			if (!hw_->useOpn_ && hw_->patchTableBase_ == 0xFED0
				&& titleCode_ == 0x1000
				&& hw_->Mem()[0xFFE2] == 0xFF && hw_->Mem()[0xFFE3] == 0xFF) {
				/* The resident handoff initialized TTLPRG but the ripped BIOS
				   cannot return to PATCH's final STD $FFE2.  Complete that one
				   vector write from row zero and park the missing foreground. */
				hw_->Mem()[0xFFE2] = 0x11;
				hw_->Mem()[0xFFE3] = 0xB7;
				hw_->Mem()[0xFC00] = 0x20;
				hw_->Mem()[0xFC01] = 0xFE;
				cpu->pc.w = 0xFC00;
				cpu->cc.i = false;
				cpu->cc.f = true;
			}
		}
	}
	/* Vectors / DRIVER may remount after play — refresh once more. */
	hw_->RefreshFd03Polarity();
	/* jikochu: PATCH clears $0614 after consuming $FD58; re-arm PSG gate.
	   Also re-apply song entry — play does JSR $C006 before reading $FD59,
	   so a short settle can leave boot init as the only active song. */
	if (!hw_->useOpn_ && hw_->ChipAy() && hw_->mdataAddr_ == 0xC000) {
		uint8_t* m = hw_->Mem();
		if (m && m[0xC19D] == 0x7D && m[0xC19E] == 0x06 && m[0xC19F] == 0x14) {
			m[0x0614] = 0x80;
			const uint8_t song = hw_->playSongLatch_;
			uint16_t entry = 0xC000;
			if (song == 1) {
				m[0xC114] = 0xBD;
				m[0xC115] = 0xC2;
				m[0xC116] = 0x42;
				entry = 0xC00A;
			} else if (song == 2) {
				m[0xC114] = 0x7E;
				m[0xC115] = 0xC0;
				m[0xC116] = 0xE6;
				entry = 0xC010;
			} else {
				m[0xC114] = 0x7E;
				m[0xC115] = 0xC0;
				m[0xC116] = 0xE6;
				m[0xC01D] = 0x09;
				entry = 0xC000;
			}
			hw_->RunSubroutine(entry);
			m[0x0614] = 0x80;
		}
	}
	return 1;
}

void CDriverFm7::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
}

int CDriverFm7::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	mc6809__t* cpu = hw_->Mc6809();
	if (!cpu || hostRate_ < 1 || cpuHz_ < 1) return 0;
	CEmuHardFm7SetActive(hw_);

	for (int i = 0; i < frames; i++) {
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		const uint64_t end = (uint64_t)cpu->cycles + (uint64_t)cyclesPerSample;
		RunUntil(end);
		const uint64_t now = (uint64_t)cpu->cycles;
		if (now >= nextVsync_ + vsyncPeriod_ * 4)
			nextVsync_ = now + vsyncPeriod_;

		int16_t opnBuf[2] = { 0, 0 };
		int16_t ayBuf[2] = { 0, 0 };
		if (hw_->ChipOpn())
			hw_->ChipOpn()->Render(opnBuf, 1);
		if (hw_->ChipAy())
			hw_->ChipAy()->Render(ayBuf, 1);
		int32_t l = (int32_t)opnBuf[0] + (int32_t)ayBuf[0];
		int32_t r = (int32_t)opnBuf[1] + (int32_t)ayBuf[1];
		if (l > 32767) l = 32767;
		if (l < -32768) l = -32768;
		if (r > 32767) r = 32767;
		if (r < -32768) r = -32768;
		stereo[i * 2] = (int16_t)l;
		stereo[i * 2 + 1] = (int16_t)r;
	}
	return frames;
}

int CDriverFm7::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
