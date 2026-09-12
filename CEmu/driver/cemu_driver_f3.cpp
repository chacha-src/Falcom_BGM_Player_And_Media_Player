#include "StdAfx.h"
#include "cemu_driver_f3.h"
#include "../chip/cemu_chip_es5505.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
	, kickedMail_(0)
	, hitIdle_(0)
	, hitIrq_(0)
	, hitTask0_(0)
	, hitMail_(0)
	, hitPlay_(0)
	, hitDisp_(0)
	, hitTick_(0)
	, seqTickAcc_(0)
	, seqCalls_(0)
	, irq6Vec_(0)
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
	kickedMail_ = 0;
	hitIdle_ = 0;
	hitIrq_ = 0;
	hitTask0_ = 0;
	hitMail_ = 0;
	hitPlay_ = 0;
	hitDisp_ = 0;
	hitTick_ = 0;
	seqTickAcc_ = 0;
	seqCalls_ = 0;
	irq6Vec_ = 0;

	songCode_ = titleCode ? titleCode : 1;
	CDriverF3Push(tryCodes_, &tryCount_, (int)_countof(tryCodes_), songCode_);
	if (tryCount_ < 1) {
		tryCodes_[0] = 1;
		tryCount_ = 1;
	}

	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;

	CEmuHardF3SetActive(hw_);
	/* Boot settle: DUART/IVR, TCB copy, first task slice. */
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
	cmdIndex_ = tryCount_;
	locked_ = 1;
	hw_->SetSongCommand(songCode_);
	/* C15702 (voice-chain setup inside C15538) returns immediately unless
	   D4C0 is set; task0 ST's that flag on the real board. D4F9 is the
	   C15702 "key-on enable" byte tested before bset #4 on the catalog. */
	hw_->Write8(0xD4F9u, 1);
	hw_->Write8(0xD4C0u, 1);
	/* Do not plant $6DFC. C12B8C is stop-if-listed (compact + C12AD0
	   poison of 5DAA+4); C12E70 always BRA's to C12C36 to start. */
	KickMailboxOnce();
	RunCycles(cpuHz_ / 2);
	/* C15538 clears D4B3 and C13B94 runs before the D0F4 chain exists, so
	   C152B0 never sees its key-on gates. Arm them after the chain is live. */
	ArmKeyOnGates();
	irq6Vec_ = hw_->Read32(0x100u);
	{
		/* C14A10 walks D0F4 on A7; reset SSP is $FFFFFFF8 (8 bytes, IRQ only). */
		const unsigned ssp = (unsigned)m68k_get_reg(NULL, M68K_REG_ISP);
		if (ssp < 0x400u || ssp >= 0xFFFF00u || ssp == 0x4C00u)
			m68k_set_reg(M68K_REG_ISP, 0x9E00);
	}
	dwellFrames_ = hostRate_ > 0 ? hostRate_ / 2 : 22050;
	if (dwellFrames_ < 1) dwellFrames_ = 1;
	dwellLeft_ = dwellFrames_;
	/* Do not hunt other song codes (SAMESONG) and do not enqueue again. */
	locked_ = 1;
	bestSongCode_ = songCode_;
	LogState("open");
	return 1;
}

void CDriverF3::Close()
{
	if (hw_)
		LogState("close");
	hw_ = NULL;
	booted_ = 0;
}

int CDriverF3::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	songCode_ = titleCode;
	locked_ = 1;
	hw_->SetSongCommand(songCode_);
	return 1;
}

void CDriverF3::ArmKeyOnGates()
{
	if (!hw_) return;
	/* C152B0: cmpi.w #stamp, $d09a / bne skip. Prefer the 6630 key-on gate. */
	unsigned stamp = 0;
	for (unsigned a = 0xC13600u; a + 8u < 0xC15400u; a += 2u) {
		if (hw_->Read16(a) != 0x0C78u || hw_->Read16(a + 4u) != 0xD09Au)
			continue;
		const unsigned imm = hw_->Read16(a + 2u);
		const unsigned br = hw_->Read16(a + 6u);
		if (imm < 0x3000u || imm > 0x36FFu)
			continue;
		stamp = imm;
		if (br == 0x6630u)
			break;
	}
	if (stamp) {
		if (hw_->Read16(0xD09Au) != stamp)
			hw_->Write16(0xD09Au, (uint16_t)stamp);
		if (hw_->Read16(0xD09Eu) != stamp)
			hw_->Write16(0xD09Eu, (uint16_t)stamp);
	}
	if (hw_->Read8(0xD4F9u) == 0)
		hw_->Write8(0xD4F9u, 1);
	/* After C15538, D4B3 is 0 so the "start mode" branch of C152B0 is dead. */
	if (hw_->Read8(0xD4B3u) == 0)
		hw_->Write8(0xD4B3u, 3);
	unsigned n = hw_->Read16(0xD0F4u);
	int hops = 0;
	while (n >= 0xD000u && n < 0xEE00u && hops < 16) {
		const uint8_t f = hw_->Read8(n + 2u);
		if ((f & 0x18u) != 0x18u)
			hw_->Write8(n + 2u, (uint8_t)(f | 0x18u));
		n = hw_->Read16(n);
		hops++;
	}
}

void CDriverF3::KickMailboxOnce()
{
	if (!hw_ || kickedMail_) return;
	kickedMail_ = 1;
	unsigned wp = hw_->RingWp();
	unsigned pkt = (wp >= 6u) ? (wp - 6u) : ((wp + 0x800u - 6u) & 0x7feu);
	pkt &= 0x7feu;
	/* $EE00 sits just before the OS free list at $EE8A. Do not use $EE88
	   (TRAP #3 / list head) or $D200 (OS vars). */
	hw_->Write16(0xEE00u, 0);
	hw_->Write16(0xEE02u, 1);
	hw_->Write16(0xEE04u, pkt);
	hw_->Write16(0xEE06u, 0);
	const unsigned tcb = 0xFB3Eu;
	hw_->Write16(tcb + 0x0Au, 0xEE00u);
	hw_->Write16(tcb + 0x0Eu, 0xEE00u);
	hw_->Write16(tcb + 0x10u, 0xEE00u);
	const uint8_t b2 = hw_->Read8(tcb + 2u);
	const uint8_t b3 = hw_->Read8(tcb + 3u);
	if (b2 == b3)
		hw_->Write8(tcb + 2u, (uint8_t)(b2 ^ 0x80u));
}

void CDriverF3::WakeMailboxIfQueued()
{
	/*
	 * TRAP #6 wait sets TCB+3 bit7; TRAP #9 then BSET TCB+2 bit7 to
	 * deliver a message. When both bits match, the scheduler XOR is 0
	 * and the mailbox sleeps on a queued packet. Flip +2 only if +$10
	 * is a real queue head — waking an empty wait poisons the $0136
	 * free list via TRAP #4 with A5=0.
	 */
	if (!hw_) return;
	const unsigned tcb = 0xFB3Eu;
	if (hw_->Read16(tcb + 0x10u) == 0)
		return;
	const uint8_t b2 = hw_->Read8(tcb + 2u);
	const uint8_t b3 = hw_->Read8(tcb + 3u);
	if (b2 == b3)
		hw_->Write8(tcb + 2u, (uint8_t)(b2 ^ 0x80u));
}

void CDriverF3::PostTypeE()
{
	/* C12D94 trap#3-allocs a type-$E packet and trap#9-posts it to FB3E.
	   Doing that from the timer IRQ runs C14884 on SSP and hangs. Do the
	   same alloc+wake from the host so the mailbox task runs it on USP.
	   A static $EE20 is fatal: C149E4 trap#4 frees A5 onto $136. */
	if (!hw_) return;
	const unsigned tcb = 0xFB3Eu;
	const uint8_t b2 = hw_->Read8(tcb + 2u);
	const uint8_t b3 = hw_->Read8(tcb + 3u);
	if (b2 != b3)
		return;
	/* 0000 is the scheduler's "currently dispatched" paint, not asleep.
	   Waking it nests RTE into C14884 and smashes USP. 8080/0101 sleep. */
	if (b2 != 0x80u && b2 != 0x01u)
		return;
	const unsigned pc = hw_->Read32(tcb + 4u);
	if (pc < 0xC131C0u || pc >= 0xC13320u)
		return;
	const unsigned msg = hw_->Read16(0x0136u);
	if ((msg & 1u) || msg < 0x200u || msg >= 0xFF00u)
		return;
	if (msg >= 0xFB00u && msg < 0xFC00u)
		return;
	hw_->Write16(0x0136u, hw_->Read16(msg));
	hw_->Write8(0x014Du, (uint8_t)(hw_->Read8(0x014Du) + 1u));
	hw_->Write16(msg, 0);
	hw_->Write16(msg + 2u, 0x000Eu);
	hw_->Write16(msg + 4u, 1);
	hw_->Write16(msg + 6u, 0);
	hw_->Write16(tcb + 0x0Au, (uint16_t)msg);
	hw_->Write8(tcb + 2u, (uint8_t)(b2 ^ 0x80u));
}

void CDriverF3::LogState(const char* tag)
{
	if (!hw_ || !tag) return;
	FILE* log = fopen(".cursor/_f3_last_boot.txt", "a");
	if (!log) return;
	uint8_t snap[4] = { 0, 0, 0, 0 };
	if (hw_->SoundChip())
		hw_->SoundChip()->GetRegSnapshot(snap, 4);
	const unsigned mq = hw_->Read16(0xFB3Eu + 0x0Eu);
	fprintf(log,
		"%s code=%04X PC=%06X SR=%04X USP=%08X SSP=%08X A5=%08X A6=%08X A0=%08X fires=%u hits0=%u hits18=%u wp=%04X rp=%04X esW=%u live=%u d404=%08X v28=%08X v8C=%08X vA4=%08X v100=%08X cur=%04X flist=%04X ack=%d ivr=%02X idle=%u irq=%u t0=%u mail=%u play=%u disp=%u tick=%u t9=%04X ee=%04X %04X %04X mh=%04X %04X %04X cr=%04X %04X %04X %04X seqc=%u\n",
		tag, songCode_,
		(unsigned)m68k_get_reg(NULL, M68K_REG_PC),
		(unsigned)m68k_get_reg(NULL, M68K_REG_SR),
		(unsigned)m68k_get_reg(NULL, M68K_REG_USP),
		(unsigned)m68k_get_reg(NULL, M68K_REG_ISP),
		(unsigned)m68k_get_reg(NULL, M68K_REG_A5),
		(unsigned)m68k_get_reg(NULL, M68K_REG_A6),
		(unsigned)m68k_get_reg(NULL, M68K_REG_A0),
		hw_->DuartFires(), hw_->DpramReadHit(0), hw_->DpramReadHit(18),
		hw_->RingWp(), hw_->RingRp(),
		hw_->EsWrites(), (unsigned)snap[2],
		hw_->Read32(0xd404),
		hw_->Read32(0x28), hw_->Read32(0x8c), hw_->Read32(0xa4), hw_->Read32(0x100),
		hw_->Read16(0x134), hw_->Read16(0x136),
		CEmuHardF3IntAckCount(), hw_->DuartIvr(),
		hitIdle_, hitIrq_, hitTask0_, hitMail_, hitPlay_, hitDisp_, hitTick_,
		hw_->Read16(0xC10C3Eu), hw_->Read16(0xEE00u), hw_->Read16(0xEE02u), hw_->Read16(0xEE04u),
		mq, mq ? hw_->Read16(mq + 2u) : 0u, mq ? hw_->Read16(mq + 4u) : 0u,
		CEmuChipEs5505PeekCr(hw_->SoundChip(), 0),
		CEmuChipEs5505PeekCr(hw_->SoundChip(), 1),
		CEmuChipEs5505PeekCr(hw_->SoundChip(), 2),
		CEmuChipEs5505PeekCr(hw_->SoundChip(), 3),
		seqCalls_);
	{
		const unsigned obj = hw_->Read16(0x6DFCu);
		fprintf(log, "  list 6DFC=%04X %04X %04X %04X ch=%04X obj=%04X w0=%04X w2=%04X w4=%04X w6=%04X w1c=%04X d40e=%04X d4c0=%02X d0f4=%04X d414=%08X d408=%08X loop=%04X %04X hole=%04X %04X\n",
			hw_->Read16(0x6DFCu), hw_->Read16(0x6DFEu), hw_->Read16(0x6E00u), hw_->Read16(0x6E02u),
			0x5E5Cu + (songCode_ & 0xffu) * 0x28u, obj,
			obj ? hw_->Read16(obj) : 0u, obj ? hw_->Read16(obj + 2u) : 0u,
			obj ? hw_->Read16(obj + 4u) : 0u, obj ? hw_->Read16(obj + 6u) : 0u,
			obj ? hw_->Read16(obj + 0x1Cu) : 0u,
			hw_->Read16(0xD40Eu),
			hw_->Read8(0xD4C0u), hw_->Read16(0xD0F4u), hw_->Read32(0xD414u), hw_->Read32(0xD408u),
			hw_->Read16(0xC14A74u), hw_->Read16(0xC14A76u), hw_->Read16(0xC14A78u), hw_->Read16(0xC14A7Cu));
	}
	{
		unsigned best = 0, besta = 0, run = 0, runa = 0;
		for (unsigned a = 0x200u; a < 0xD000u; a += 2u) {
			if (hw_->Read16(a) == 0) {
				if (!run) runa = a;
				run++;
				if (run > best) { best = run; besta = runa; }
			} else {
				run = 0;
			}
		}
		fprintf(log, "  gap @%04X n=%u\n", besta, best);
	}
	fprintf(log, "  otis d84c=%04X d848=%04X d85a=%04X d8a0=%04X d8a6=%04X d4a6=%04X d490=%04X d48e=%04X d0e2=%04X d0e4=%04X d0d8=%04X d0da=%04X d464=%02X d0e8=%08X d50e=%04X d463=%04X d09a=%04X d4f9=%02X d4b3=%02X strm=%08X d098=%08X slot=%04X +4=%04X +24=%04X s38=%04X s38p24=%04X\n",
		hw_->Read16(0xD84Cu), hw_->Read16(0xD848u), hw_->Read16(0xD85Au), hw_->Read16(0xD8A0u), hw_->Read16(0xD8A6u),
		hw_->Read16(0xD4A6u), hw_->Read16(0xD490u),
		hw_->Read16(0xD48Eu), hw_->Read16(0xD0E2u), hw_->Read16(0xD0E4u),
		hw_->Read16(0xD0D8u), hw_->Read16(0xD0DAu), hw_->Read8(0xD464u),
		hw_->Read32(0xD0E8u), hw_->Read16(0xD50Eu),
		hw_->Read16(0xD463u), hw_->Read16(0xD09Au), hw_->Read8(0xD4F9u), hw_->Read8(0xD4B3u),
		hw_->Read16(0xD0F4u) ? hw_->Read32(hw_->Read16(0xD0F4u) + 6u) : 0u,
		hw_->Read32(0xD098u),
		hw_->Read16(0x5DAAu),
		hw_->Read16(0x5DAAu) ? hw_->Read16(hw_->Read16(0x5DAAu) + 4u) : 0u,
		hw_->Read16(0x5DAAu) ? hw_->Read16(hw_->Read16(0x5DAAu) + 0x24u) : 0u,
		hw_->Read16(0x5DAAu + 0x70u),
		hw_->Read16(0x5DAAu + 0x70u) ? hw_->Read16(hw_->Read16(0x5DAAu + 0x70u) + 0x24u) : 0u);
	{
		int n24 = 0;
		fprintf(log, "  cr32");
		for (int v = 0; v < 32; v++)
			fprintf(log, " %04X", CEmuChipEs5505PeekCr(hw_->SoundChip(), v));
		fprintf(log, "\n  p24");
		for (unsigned s = 0; s < 16; s++) {
			const unsigned p = hw_->Read16(0x5DAAu + s * 2u);
			const unsigned w = p ? hw_->Read16(p + 0x24u) : 0;
			if (w) {
				fprintf(log, " %u:%04X=%04X", s, p, w);
				n24++;
			}
		}
		if (!n24) fprintf(log, " none");
		fprintf(log, "\n");
	}
	{
		unsigned n = hw_->Read16(0xD0F4u);
		int hops = 0;
		fprintf(log, "  chain");
		while (n && hops < 8) {
			fprintf(log, " [%04X +2=%04X +4=%04X +6=%04X +8=%04X +A=%04X]",
				n, hw_->Read16(n + 2u), hw_->Read16(n + 4u), hw_->Read16(n + 6u),
				hw_->Read16(n + 8u), hw_->Read16(n + 0xAu));
			n = hw_->Read16(n);
			hops++;
		}
		fprintf(log, " end=%04X hops=%d\n", n, hops);
	}
	for (unsigned tcb = 0xFB12u; tcb < 0xFB96u; tcb += 0x16u) {
		fprintf(log, "  tcb %04X f=%02X%02X pc=%08X sr=%04X usp=%04X a=%04X mq=%04X %04X t=%04X\n",
			tcb,
			hw_->Read8(tcb + 2u), hw_->Read8(tcb + 3u),
			hw_->Read32(tcb + 4u),
			hw_->Read16(tcb + 8u),
			hw_->Read16(tcb + 0xcu),
			hw_->Read16(tcb + 0xau),
			hw_->Read16(tcb + 0xeu),
			hw_->Read16(tcb + 0x10u),
			hw_->Read16(tcb + 0x12u));
	}
	fclose(log);
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
			/* Only unstick a CPU parked in STOP with IPL masking DUART.
			   Do not rewrite user-mode SR: Ensoniq OS RTE's into tasks that
			   way, and clobbering them every slice kills the mailbox reader. */
			const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR);
			const unsigned ipl = (sr >> 8) & 7u;
			const int stuck = (ran == slice);
			/* STOP at IPL 7 returns ~0 cycles (IRQ masked), so "full slice"
			   never trips. Unmask those waits; leave busy C1490A alone. */
			if (ipl >= 6u && (stuck || ran < 256))
				m68k_set_reg(M68K_REG_SR, (sr | 0x2000u) & ~0x0700u);
		}
		{
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC);
			if (pc >= 0xC10A90u && pc < 0xC10AA0u) hitIdle_++;
			else if (pc >= 0xC10E68u && pc < 0xC10F00u) hitIrq_++;
			else if (pc >= 0xC0B5D0u && pc < 0xC0B700u) hitTask0_++;
			else if (pc >= 0xC131C0u && pc < 0xC13320u) hitMail_++;
			else if (pc >= 0xC12B8Cu && pc < 0xC12D70u) hitPlay_++;
			else if (pc >= 0xC12DFEu && pc < 0xC12E80u) hitDisp_++;
			else if (pc >= 0xC14884u && pc < 0xC14910u) hitTick_++;
		}
		WakeMailboxIfQueued();
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
		seqTickAcc_ += 1;
		if (seqTickAcc_ >= (hostRate_ > 60 ? hostRate_ / 60 : 1)) {
			seqTickAcc_ = 0;
			const unsigned head = hw_->Read16(0xD0F4u);
			if (head >= 0xD000u && head < 0xEE00u) {
				hw_->Write16(0xD4A6u, 1);
				seqCalls_++;
				if (seqCalls_ <= 8u)
					ArmKeyOnGates();
				{
					const unsigned ssp = (unsigned)m68k_get_reg(NULL, M68K_REG_ISP);
					if (ssp < 0x400u || ssp >= 0xFFFF00u)
						m68k_set_reg(M68K_REG_ISP, 0x9E00);
				}
				if (irq6Vec_ && hw_->Read32(0x100u) == 0)
					hw_->Write32(0x100u, irq6Vec_);
			}
		}
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
