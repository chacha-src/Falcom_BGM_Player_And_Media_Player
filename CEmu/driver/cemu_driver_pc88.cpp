#include "StdAfx.h"
#include "cemu_driver_pc88.h"
#include "../machine/cemu_hard_pc88.h"
#include "../chip/cemu_chip_opna.h"
#include "../z80/Ay_Cpu.h"
#include <string.h>

enum {
	PC88_CPU_HZ = 4000000,
	PC88_OPN_CLOCK_HZ = 3993600,
	PC88_OPNA_CLOCK_HZ = 7987200,
	VEC_VRTC = 0x02,
	VEC_RTC = 0x04,
	VEC_SOUND = 0x08,
	RTC_HZ = 600,
	VRTC_MILLIHZ = 56400,
	/* Longest silence a real PC-88 track holds mid-song is well under this;
	   past it the rip has stopped rather than resting. Players rewrite F-num
	   for vibrato every few frames, so 2s of total register stillness is
	   never part of a live song. */
	WD_IDLE_MS = 2000
};

CDriverPc88::CDriverPc88()
	: hw_(NULL)
	, sampleRate_(44100)
	, hostRate_(44100)
	, cpuHz_(PC88_CPU_HZ)
	, opnHz_(PC88_OPN_CLOCK_HZ)
	, booted_(0)
	, triggered_(0)
	, forceEiBoot_(0)
	, nextRtc_(0)
	, nextVrtc_(0)
	, rtcPeriod_(0)
	, vrtcPeriod_(0)
	, opnResidual_(0)
	, cpuAcc_(0)
	, cpuCycleBudget_(0)
	, wdSamples_(0)
	, wdLastActive_(0)
	, wdMotion_(0)
	, wdTimerFires_(0)
	, wdReplays_(0)
	, wdEverActive_(0)
	, wdArmedTick_(0)
	, replayPending_(0)
{
}

CDriverPc88::~CDriverPc88()
{
	Close();
}

int CDriverPc88::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardPc88*)hw;
	hostRate_ = hw_->SampleRate();
	sampleRate_ = hostRate_;
	opnHz_ = hw_->opnaMode ? PC88_OPNA_CLOCK_HZ : PC88_OPN_CLOCK_HZ;
	cpuHz_ = (hw_->cpuHz_ > 0) ? hw_->cpuHz_ : PC88_CPU_HZ;
	rtcPeriod_ = (uint64_t)cpuHz_ / RTC_HZ;
	vrtcPeriod_ = (uint64_t)cpuHz_ * 1000 / VRTC_MILLIHZ;
	nextRtc_ = rtcPeriod_;
	nextVrtc_ = vrtcPeriod_;
	opnResidual_ = 0;
	cpuAcc_ = 0;
	cpuCycleBudget_ = 0;
	booted_ = 0;
	triggered_ = 0;
	forceEiBoot_ = 0;
	wdSamples_ = 0;
	wdLastActive_ = 0;
	wdMotion_ = 0;
	wdTimerFires_ = 0;
	wdReplays_ = 0;
	wdEverActive_ = 0;
	wdArmedTick_ = 0;
	replayPending_ = 0;
	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;
	/* Boot: let PATCH reach poll loop. Cap at ~1.0s. feris/gunyu clobber
	   page0 if left in DRIVER — snapshot/restore only for I=01/F3.
	   JR-entry titles (ashe/andrgyns/…) need the full settle; early-exit
	   on poll+iff1 cut their DRIVER init short. */
	{
		Ay_Cpu* cpu = hw_->Cpu();
		uint8_t* mem = hw_->Mem();
		const int jrEntry = mem && mem[0] == 0x18;
		uint8_t page0[0x80];
		int hadPoll = 0, pollAt = -1;
		if (mem) {
			memcpy(page0, mem, sizeof(page0));
			for (int i = 0; i + 4 < 0x70; i++) {
				if (mem[i] == 0xDB && mem[i + 1] == 0x00
					&& mem[i + 2] == 0xB7 && mem[i + 3] == 0x28) {
					hadPoll = 1;
					pollAt = i;
					break;
				}
			}
		}
		const uint64_t chunk = (uint64_t)cpuHz_ / 32;
		for (int step = 0; step < 32 && cpu && mem; step++) {
			RunUntil((uint64_t)cpu->time() + chunk);
			int nowPoll = -1;
			for (int i = 0; i + 4 < 0x70; i++) {
				if (mem[i] == 0xDB && mem[i + 1] == 0x00
					&& mem[i + 2] == 0xB7 && mem[i + 3] == 0x28) {
					nowPoll = i;
					break;
				}
			}
			if (hadPoll && nowPoll < 0) {
				/* Only Wing-class (I=F3 + snd Cxxx + high CALL, or mugen3 I=01).
				   Bare I=F3 restore false-triggered pocky2 and left it at poll
				   with a half-inited sequencer (key-on=0). */
				const uint16_t snd = Ay_CpuIm2Target(cpu, (uint8_t)VEC_SOUND);
				if (cpu->r.pc >= 0x80
					&& ((cpu->r.i == 0xF3 && snd >= 0xC000 && hw_->NeedsBootEiPulse())
						|| (cpu->r.i == 0x01 && cpu->r.pc < 0x1000))) {
					memcpy(mem, page0, sizeof(page0));
					if (pollAt >= 0)
						cpu->r.pc = (uint16_t)pollAt;
					cpu->r.iff1 = 1;
				}
				break;
			}
			/* JR-entry: stop once poll is live, except ashe (DRIVER@7800)
			   which needs the full ~1s settle — early exit keeps peak=0.
			   Also: poll bytes are static in PATCH; do not stop while PC is
			   still before the poll (lizard88/gineiden/gallforc decrypt).
			   iceclimb88: VRTC during settle can enter the cmd handler
			   (pc past FE/CP); stopping there left B816=FF forever. */
			if (nowPoll >= 0 && cpu->r.pc < 0x80) {
				if (jrEntry) {
					if (mem[0x7800] == 0xC3)
						continue; /* full settle */
					if (mem[0x0100] == 0x31 && step < 24)
						continue;
					if ((int)cpu->r.pc < nowPoll)
						continue;
					/* Require PC exactly on IN A,(00) — JR Z disp FB must not
					   be treated as an opcode. */
					if ((int)cpu->r.pc != nowPoll)
						continue;
					break;
				}
				if (cpu->r.iff1)
					break;
			}
		}
		/* If settle ended mid cmd-handler, snap back to the poll wait. */
		if (cpu && mem && pollAt >= 0 && cpu->r.pc < 0x80
			&& (int)cpu->r.pc > pollAt) {
			cpu->r.pc = (uint16_t)pollAt;
			cpu->r.iff1 = 1;
			hw_->cmd = 0;
		}
	}
	hw_->FixupIm2AfterBoot();
	hw_->PruneDeadTickSources();
	/* Wing destge/hadou-class: still DI after settle with I=F3 + sound vec
	   in Cxxx + high CALL under DI (NeedsBootEiPulse). Bare I=F3 (pocky2)
	   must not match. gunyu has FB in PATCH so ends settle with iff1=1.
	   scheme OPNA: PATCH@9000 / I=0x80 — NeedsBootEiPulse or IsSchemeOpna. */
	/* Wing destge/hadou-class: still DI after settle with I=F3 + sound vec
	   in Cxxx + high CALL under DI (NeedsBootEiPulse). Bare I=F3 (pocky2)
	   must not match. gunyu has FB in PATCH so ends settle with iff1=1.
	   scheme OPNA: PATCH@9000 / I=0x80 — NeedsBootEiPulse or IsSchemeOpna. */
	{
		Ay_Cpu* cpu = hw_->Cpu();
		const uint16_t snd = cpu ? Ay_CpuIm2Target(cpu, (uint8_t)VEC_SOUND) : 0;
		const int schemeBootEi = hw_->IsSchemeOpna() && cpu
			&& cpu->r.i == 0x80 && cpu->r.pc >= 0x80;
		int didEiPulse = 0;
		if (cpu && !cpu->r.iff1 && cpu->r.im == 2
			&& ((snd != 0 && cpu->r.i == 0xF3 && snd >= 0xC000
					&& hw_->NeedsBootEiPulse())
				|| (snd != 0 && cpu->r.i == 0x01 && cpu->r.pc >= 0x200 && cpu->r.pc < 0x1000)
				|| schemeBootEi)) {
			/* INT2@8350: if MUS2 never planted I:08, point sound IRQ there. */
			if (schemeBootEi && snd == 0) {
				uint8_t* mem = hw_->Mem();
				if (mem && mem[0x8350] == 0xC3) {
					const uint16_t table = (uint16_t)(((uint16_t)cpu->r.i << 8) | (uint16_t)VEC_SOUND);
					mem[table] = 0x50;
					mem[table + 1] = 0x83;
				}
			}
			forceEiBoot_ = 1;
			didEiPulse = 1;
			const int pulseMax = hw_->IsSchemeOpna() ? 256 : 96;
			for (int pulse = 0; pulse < pulseMax; pulse++) {
				cpu->r.iff1 = 1;
				RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 32);
				if (cpu->r.pc < 0x80)
					break; /* reached PATCH poll @0 */
				if (hw_->IsSchemeOpna()
					&& cpu->r.pc >= 0x9000 && cpu->r.pc < 0x9080)
					break; /* scheme poll @9000 */
			}
			forceEiBoot_ = 0;
		}
		/* Snap: Wing after EI pulse; scheme always park on 9000 poll. */
		if ((didEiPulse && cpu && cpu->r.pc >= 0x80 && !hw_->IsSchemeOpna())
			|| (hw_->IsSchemeOpna() && cpu && cpu->r.pc >= 0xA000)) {
			uint8_t* mem = hw_->Mem();
			if (mem) {
				const int pollLo = hw_->IsSchemeOpna() ? 0x9000 : 0;
				const int pollHi = hw_->IsSchemeOpna() ? 0x9080 : 0x70;
				for (int i = pollLo; i + 4 < pollHi; i++) {
					if (mem[i] == 0xDB && mem[i + 1] == 0x00
						&& mem[i + 2] == 0xB7 && mem[i + 3] == 0x28) {
						cpu->r.pc = (uint16_t)i;
						cpu->r.iff1 = 1;
						if (hw_->IsSchemeOpna())
							cpu->r.sp = 0x0100;
						break;
					}
				}
			}
		}
	}
	/* Scheme: MUS2 OPN port mailbox must stay 32/44/46 after boot. */
	if (hw_->IsSchemeOpna()) {
		uint8_t* mem = hw_->Mem();
		if (mem) {
			mem[0xf0bb] = 0x32;
			mem[0xf0bc] = 0x44;
			mem[0xf0bd] = 0x46;
		}
	}
	/* Final park on poll wait — VRTC during settle/EI-pulse can leave PC
	   in the cmd dispatcher (iceclimb88 B816 stuck at ROM FF). Only the
	   IN A,(00) address is safe; sitting on the JR Z displacement (FB)
	   executes EI as an opcode and skips the mailbox plant. */
	{
		Ay_Cpu* cpu = hw_->Cpu();
		uint8_t* mem = hw_->Mem();
		if (cpu && mem && cpu->r.pc < 0x80) {
			for (int i = 0; i + 4 < 0x70; i++) {
				if (mem[i] == 0xDB && mem[i + 1] == 0x00
					&& mem[i + 2] == 0xB7 && mem[i + 3] == 0x28
					&& (int)cpu->r.pc != i) {
					cpu->r.pc = (uint16_t)i;
					hw_->cmd = 0;
					break;
				}
			}
		}
		/* lizard88's (A572) is deliberately left alone. The RET Z that reads
		   it sits at A3A3, in A376's epilogue after CALL A3B0 has already
		   played the song, so a zero there never blocked the player; what it
		   does gate is the stop routine, where zero is the branch that
		   actually writes OPN 07-0E and silences the chip. See
		   CHardPc88::ArmLizardOpnTimer. */
	}
	booted_ = 1;
	return 1;
}

void CDriverPc88::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
}

void CDriverPc88::TickOpn(uint64_t cpuCycles)
{
	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0) return;
	opnResidual_ += cpuCycles * (uint64_t)opnHz_;
	const uint64_t opnTicks = opnResidual_ / (uint64_t)cpuHz_;
	opnResidual_ %= (uint64_t)cpuHz_;
	if (opnTicks)
		hw_->SoundChip()->AdvanceClocks(opnTicks);
}

void CDriverPc88::DeliverIrqs(uint64_t now)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CChip* chip = hw_->SoundChip();
	/* castle/castleex PROG2 song-arm sets I=$1A and vectors at $1A04/$1A08.
	   Do NOT force I=$FF — that orphans the ISR page and mutes OPN. */
	int vrtcDue = hw_->useVrtc && now >= nextVrtc_;
	int rtcDue = hw_->useRtc && now >= nextRtc_;
	int opnDue = chip && chip->Irq()
		&& (!hw_->soundIrqMasked || hw_->IgnoreSoundIrqMask());
	if (cpu->r.iff1 && cpu->r.im == 2) {
		if (vrtcDue && Ay_CpuIm2Target(cpu, VEC_VRTC) == 0) vrtcDue = 0;
		if (rtcDue && Ay_CpuIm2Target(cpu, VEC_RTC) == 0) rtcDue = 0;
		if (opnDue && Ay_CpuIm2Target(cpu, VEC_SOUND) == 0) opnDue = 0;
	}
	if (cpu->r.iff1 && (vrtcDue || rtcDue || opnDue)) {
		/* Prefer FM sound IRQ over VRTC/RTC. KOEI OPN (valis2) runs Timer B
		   near the VRTC rate; always taking VRTC first starved vector 08 and
		   halved tempo. OPNA is less affected because Timer A is faster. */
		const uint8_t vector = opnDue ? (uint8_t)VEC_SOUND
			: vrtcDue ? (uint8_t)VEC_VRTC
			: (uint8_t)VEC_RTC;
		if (Ay_CpuIm2Interrupt(cpu, vector)) {
			if (vector == VEC_VRTC) nextVrtc_ += vrtcPeriod_;
			else if (vector == VEC_RTC) nextRtc_ += rtcPeriod_;
			else if (vector == VEC_SOUND && chip) {
				/* hoot: almost all PC88 drivers raise_IRQ then lower_IRQ
				   immediately (edge). Keeping the line high until OUT E4
				   re-entered the ISR after EI and rushed mucom tempo.
				   YM status flags stay sticky for KOEI; E4 still acks too. */
				chip->AckIrq();
			}
		}
	} else {
		/* While DI (inside ISR), hold the due flags — do NOT slide the
		   schedule forward or IRQs are lost / delayed incorrectly. */
		if (cpu->r.iff1) {
			if (rtcDue) nextRtc_ = now + rtcPeriod_;
			if (vrtcDue) nextVrtc_ = now + vrtcPeriod_;
		}
	}
}

void CDriverPc88::RunUntil(uint64_t endCycle)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CEmuHardPc88SetActive(hw_);
	while ((uint64_t)cpu->time() < endCycle) {
		if (forceEiBoot_ && !cpu->r.iff1 && cpu->r.im == 2)
			cpu->r.iff1 = 1;
		const uint64_t now = (uint64_t)cpu->time();
		DeliverIrqs(now);
		/* tf88sr PATCH play HALTs waiting for RTC; without a wake advance
		   Ay_Cpu HALT only burns the remaining time-slice and retries the
		   same PC, so the post-HALT play CALL never runs. */
		uint8_t* mem = hw_->Mem();
		if (mem && mem[cpu->r.pc] == 0x76) {
			uint64_t wake = endCycle;
			if (hw_->useRtc && rtcPeriod_ > 0 && nextRtc_ > now && nextRtc_ < wake)
				wake = nextRtc_;
			if (hw_->useVrtc && vrtcPeriod_ > 0 && nextVrtc_ > now && nextVrtc_ < wake)
				wake = nextVrtc_;
			uint64_t delta = (wake > now) ? (wake - now) : 4;
			if (delta < 4) delta = 4;
			if (delta > 0x7fffffff) delta = 0x7fffffff;
			cpu->adjust_time((int)delta);
			hw_->AddCpuCycles(delta);
			TickOpn(delta);
			continue;
		}
		const int cycles = Ay_CpuRunOne(cpu);
		hw_->AddCpuCycles((uint64_t)cycles);
		TickOpn((uint64_t)cycles);
	}
}

/* PATCH command poll — `IN A,(00) / OR A / JR Z,-` in the page-0 stub. */
int CDriverPc88::FindPollLoop() const
{
	const uint8_t* mem = hw_ ? hw_->Mem() : NULL;
	if (!mem) return -1;
	for (int i = 0; i + 4 < 0x70; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x00
			&& mem[i + 2] == 0xB7 && mem[i + 3] == 0x28)
			return i;
	}
	return -1;
}

/* A replay only lands if the guest is sitting in that poll. When a stalled rip
   has wandered off instead — runaway PC, or a player idle loop it never leaves
   — park it back at the poll with a usable stack first. */
void CDriverPc88::Unwedge()
{
	Ay_Cpu* cpu = hw_ ? hw_->Cpu() : NULL;
	if (!cpu || cpu->r.pc < 0x80) return;
	const int pollAt = FindPollLoop();
	if (pollAt < 0) return;
	cpu->r.pc = (uint16_t)pollAt;
	if (cpu->r.sp < 0x0200 || cpu->r.sp >= 0xF000)
		cpu->r.sp = 0x0200;
	cpu->r.iff1 = 1;
	hw_->cmd = 0;
}

/* Song start. Split out of Render so the stall watchdog can re-kick a rip
   exactly the way it was first started. */
void CDriverPc88::TriggerPlay()
{
	Ay_Cpu* cpu = hw_ ? hw_->Cpu() : NULL;
	if (!cpu) return;
	CEmuHardPc88SetActive(hw_);
	if (wdReplays_ > 0)
		Unwedge();
	if (!triggered_) {
		/* Re-stage song at mdata/vdata in case boot clobbered it — but not
		   when mdata sits on the PATCH/stack page. Use full titleCode_ so
		   packed-bank offsets survive reload. */
		if (hw_->ShouldRestageSong())
			hw_->LoadSongData(hw_->titleCode_);
		/* KOEI FMDRV: BGM uses play index 0 (packed CIM @4000). PCM SE
		   titles (valis2 PCM00.. = code>=0xE0) must pass the raw code so
		   PATCH's CP E0 path runs — forcing 0 muted ADPCM and left the
		   guest spinning in status waits under the UI. */
		if (hw_->PackedKoei()) {
			hw_->song = hw_->PlaySongIndex();
			hw_->param = hw_->PlayParamIndex();
		} else {
			/* Match LoadRoms play-index rule (pointer-table banks → high byte). */
			hw_->song = hw_->PlaySongIndex();
			hw_->param = hw_->PlayParamIndex();
		}
		if (hw_->PlayKickBase()) {
			/* Game Arts: PATCH port-play runs CALL +6 init; host then CALL
			   player base (ISR entry). castle/castleex: ~64 host samples of
			   cmd=1 (CALL 1033 arm) then CALL PROG2@1000. */
			const unsigned base = hw_->PlayKickBase();
			/* N88 thexder/bokosuka: cmd=1 before kick lets PATCH port-play
			   CALL stop (E80E) / wander into N88 and clobber F304. Kick only. */
			if (hw_->NeedsDeferredRtc() && !hw_->PlayKickInitOff()) {
				hw_->cmd = 0;
				hw_->DirectPlayKick(base, hw_->PlayKickEi());
				/* Wait until DEMOM/MUSIC play entry RETs to PATCH poll so
				   channel/voice init finishes before the first RTC tick. */
				for (int step = 0; step < 64; step++) {
					RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 64);
					if (cpu->r.pc < 0x80)
						break;
				}
				hw_->EnableDeferredRtc();
				if (hw_->NeedsPlayEi() && !cpu->r.iff1)
					cpu->r.iff1 = 1;
			} else if (base == 0x1000) {
				/* castle/castleex: PROG2@1000 init then PATCH cmd=1 song arm.
				   105D ends in CALL 1374 which LD SP,$FE80 and PUSH-wipes
				   MUSIC@F800 — NOP that CALL, keep the SSG/port bring-up. */
				uint8_t* mem = hw_->Mem();
				if (mem && mem[0x1082] == 0xCD && mem[0x1083] == 0x74
					&& mem[0x1084] == 0x13) {
					mem[0x1082] = 0x00;
					mem[0x1083] = 0x00;
					mem[0x1084] = 0x00;
				}
				hw_->cmd = 0;
				hw_->DirectPlayKick(base, 0);
				for (int step = 0; step < 32; step++) {
					RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 64);
					if (cpu->r.pc < 0x80)
						break;
				}
				hw_->cmd = 1;
				for (int step = 0; step < 128; step++) {
					RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 64);
					if (step >= 8 && cpu->r.pc < 0x80 && hw_->cmd == 0)
						break;
				}
				hw_->cmd = 0;
				if (mem && mem[0x154F] == 0xF5) {
					const uint8_t ip = (cpu->r.i != 0) ? cpu->r.i : 0x1a;
					cpu->r.i = ip;
					const unsigned v4 = ((unsigned)ip << 8) | 0x04u;
					const unsigned v8 = ((unsigned)ip << 8) | 0x08u;
					mem[v4] = 0x4E;
					mem[v4 + 1] = 0x15;
					mem[v8] = 0x4F;
					mem[v8 + 1] = 0x15;
				}
				if (mem && mem[0x1669] == 0 && hw_->param)
					mem[0x1669] = 1;
				if (mem)
					mem[0x14F4] = (uint8_t)(mem[0x14F4] | 0x01);
				cpu->r.iff1 = 1;
			} else if (!hw_->PlayKickInitOff()
				&& base >= 0xb000 && base < 0xe000) {
				/* yokosuka SOUND@B5C3: PATCH cmd=1 (param!=FF) does
				   DI; CALL SOUND+0x1BD and returns to the poll loop.
				   Host DirectPlayKick of the same entry nested inside a
				   short cmd RunUntil and muted FM; let PATCH finish the
				   CALL, then EI for RTC. Effects: param=FF → (E23C). */
				hw_->cmd = 1;
				for (int step = 0; step < 256; step++) {
					RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 64);
					if (step >= 4 && cpu->r.pc < 0x80 && hw_->cmd == 0)
						break;
				}
				hw_->cmd = 0;
				if (hw_->PlayKickEi() && !cpu->r.iff1)
					cpu->r.iff1 = 1;
			} else {
				hw_->cmd = 1;
				if (hw_->PlayKickInitOff()) {
					for (int step = 0; step < 16; step++) {
						RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 64);
						if (step >= 2 && cpu->r.pc < 0x80)
							break;
					}
				} else {
					/* Match probe: 64 host samples at cpuHz/hostRate. */
					RunUntil((uint64_t)cpu->time()
						+ (uint64_t)cpuHz_ * 64u / (uint64_t)(hostRate_ > 0 ? hostRate_ : 44100));
				}
				hw_->cmd = 0;
				hw_->DirectPlayKick(base, hw_->PlayKickEi());
			}
		} else {
			/* DI while PATCH consumes cmd=1. hangon88/iceclimb poll under EI;
			   an RTC/VRTC tick between IN A,(01) and LD (mailbox),A clobbers
			   A (0x8F→0x80) or skips the store (B816 stays ROM FF). Also
			   clear B so ED 49 OUT (C),C with C=0 hits port 0. */
			const int wantEi = cpu->r.iff1 || hw_->NeedsPlayEi();
			uint8_t* mem = hw_->Mem();
			/* yaksa PATCH2 polls under DI + use_vrtc; play CALL needs EI.
			   Do NOT key this off NeedsPlayEi() — forcePlayEi titles
			   (and makai with use_vrtc) must still drain cmd under DI or
			   VRTC nests into the handler and parks mid-ISR. */
			const int keepEiForVrtcLoad = hw_->useVrtc && !cpu->r.iff1
				&& mem && mem[0] == 0x18;
			if (!keepEiForVrtcLoad)
				cpu->r.iff1 = 0;
			cpu->r.b.b = 0;
			hw_->cmd = 1;
			if (hw_->IsSchemeOpna()) {
				hw_->SchemePlayTrigger(hw_->titleCode_);
				Ay_Cpu* cpu2 = hw_->Cpu();
				if (cpu2) {
					cpu2->r.iff1 = 1;
					cpu2->r.sp = 0x0100;
				}
			}
			/* spitfl88 only: let PATCH arm A6A9, then re-assert A824 (boot
			   CALL A826 clears it when mid-RAM 79D7 < 0x34). Must not run for
			   Game Arts kick titles (jikochu*) — A6A9/A824 collide with music. */
			if (mem && hw_->useRtc && mem[0xA826] == 0xF3
				&& mem[0xA830] == 0xFE && mem[0xA831] == 0x34) {
				RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 32);
				if (mem[0xA6A9] == 0x20 && mem[0xA824] == 0)
					mem[0xA824] = 1;
			}
			/* gineiden: let PATCH play plant vec08 / load song, then re-arm
			   Timer B that CALL 4E2F cleared. */
			if (hw_->NeedsGineidenArm()) {
				RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 8);
				hw_->ArmGineidenOpnTimer();
			}
			if (hw_->NeedsLizardArm()) {
				RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 8);
				hw_->ArmLizardOpnTimer();
			}
			if (hw_->NeedsNavituneArm()) {
				/* 1) Port-play with BC=mdata binds phrase banks.
				   2) Rewrite LD BC to title song and run cmd07+cmd10+cmd0E.
				   Raise SP before EI — tick EI's under (4D59) and nests. */
				RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 4);
				hw_->ApplyNavituneTitleSong();
				const unsigned retarget = hw_->NavituneRetargetPc();
				if (retarget) {
					if (cpu->r.sp < 0x4000 || cpu->r.sp >= 0x7700)
						cpu->r.sp = 0x7000;
					hw_->DirectPlayKick(retarget, 0);
					RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 8);
				}
				hw_->FinishNavitunePlay();
			}
			if (hw_->NeedsYakyufanArm()) {
				/* Let PATCH cmd=1 reach CALL play (clears 0118 via 0C5D). */
				RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 8);
				hw_->ArmYakyufanPlay();
			}
			/* Drain cmd under DI before sample loop re-enables IRQs. */
			int pollAt = -1;
			if (mem) {
				for (int i = 0; i + 4 < 0x70; i++) {
					if (mem[i] == 0xDB && mem[i + 1] == 0x00
						&& mem[i + 2] == 0xB7 && mem[i + 3] == 0x28) {
						pollAt = i;
						break;
					}
				}
			}
			int sawDispatch = (pollAt < 0);
			const int drainSteps = hw_->NeedsLongPlayDrain() ? 512 : 64;
			for (int step = 0; step < drainSteps; step++) {
				RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 64);
				if (pollAt >= 0 && cpu->r.pc < 0x80 && (int)cpu->r.pc > pollAt + 4)
					sawDispatch = 1;
				/* 1942 ADEE lives at 0034 (still <0x80). Breaking on any
				   page0 PC aborts mid-LDIR before A343 arms I+Timer. */
				if (hw_->NeedsLongPlayDrain()) {
					if (step >= 8 && hw_->cmd == 0 && pollAt >= 0
						&& (int)cpu->r.pc == pollAt && sawDispatch)
						break;
					continue;
				}
				if (step >= 2 && hw_->cmd == 0 && cpu->r.pc < 0x80
					&& sawDispatch
					&& (pollAt < 0 || (int)cpu->r.pc <= pollAt + 4))
					break;
			}
			hw_->cmd = 0;
			hw_->FixupIm2AfterPlay();
			if (hw_->NeedsLongPlayDrain() && hw_->SoundChip() && cpu) {
				/* 1942 A343 ends with mode 2A; ensure Timer B is live and
				   port32 is unmasked so AD92 can sequence FM. */
				hw_->PortOut(0x44, 0x26);
				hw_->PortOut(0x45, 0xCF);
				hw_->PortOut(0x44, 0x27);
				hw_->PortOut(0x45, 0x2A);
				hw_->PortOut(0x32, (uint8_t)(hw_->PortIn(0x32) & 0x7F));
				cpu->r.iff1 = 1;
			}
			if (hw_->NeedsDeferredRtc()) {
				RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 8);
				hw_->EnableDeferredRtc();
			}
			if (wantEi)
				cpu->r.iff1 = 1;
		}
		triggered_ = 1;
	}
}

static unsigned s_wdReplayCount = 0;
static int s_wdEnabled = 1;

unsigned CEmuPc88WatchdogReplays() { return s_wdReplayCount; }
void CEmuPc88WatchdogResetCount() { s_wdReplayCount = 0; }
void CEmuPc88WatchdogSetEnabled(int on) { s_wdEnabled = on ? 1 : 0; }

/* Stall watchdog for a boot that left the player with no way to run: the guest
   turned interrupts off, it masked the sound IRQ, or nothing at all is ticking.
   Note motion (key-ons + F-num + SSG period changes) is the liveness signal —
   idle register polling does not count as playing — and the whole thing
   disarms itself the moment notes appear.

   It deliberately will NOT restart a track that has played. Re-running the
   play kick over a live player resumes from whatever state its RAM is in, so
   what came out was a garbled half-restart every couple of seconds, and a
   real defect (yokosuka: the port-70h text window went unemulated, so the
   sequencer read its note lengths from the wrong page and died a second in)
   sounded like a bad loop instead of showing up as the stall it was. A clean
   restart would mean re-reading the roms, and the zip is closed once Open
   returns, so the honest choice is to leave a finished song silent and fix
   whatever stopped it. */
void CDriverPc88::WatchdogTick()
{
	Ay_Cpu* cpu = hw_ ? hw_->Cpu() : NULL;
	CChip* chip = hw_ ? hw_->SoundChip() : NULL;
	if (!cpu || !chip || sampleRate_ < 1 || !s_wdEnabled) return;

	unsigned w = 0, k = 0, f = 0, s = 0, m = 0;
	CEmuChipYm2608GetPlayMetrics(chip, &w, &k, &f, &s, &m);
	const unsigned motion = k + f + s;
	if (motion != wdMotion_) {
		wdMotion_ = motion;
		wdLastActive_ = wdSamples_;
		wdEverActive_ = 1;
		return;
	}
	const uint64_t idleLimit = (uint64_t)sampleRate_ * WD_IDLE_MS / 1000u;
	if (wdSamples_ - wdLastActive_ < idleLimit)
		return;
	wdLastActive_ = wdSamples_;

	/* Cheap and idempotent, so apply both every time rather than spending a
	   timeout each — the gap between loops is audible. */
	const int haveVec = Ay_CpuIm2Target(cpu, VEC_SOUND)
		|| Ay_CpuIm2Target(cpu, VEC_VRTC) || Ay_CpuIm2Target(cpu, VEC_RTC);
	if (!cpu->r.iff1 && cpu->r.im == 2 && haveVec)
		cpu->r.iff1 = 1;
	if (hw_->soundIrqMasked && Ay_CpuIm2Target(cpu, VEC_SOUND))
		hw_->PortOut(0x32, (uint8_t)(hw_->PortIn(0x32) & 0x7F));

	unsigned fa = 0, fb = 0, ip = 0;
	CEmuChipYm2608GetTimerDebug(chip, &fa, &fb, &ip);
	const int ticking = (fa + fb) != wdTimerFires_;
	wdTimerFires_ = fa + fb;
	/* Some rips (mappy88, jikochu*) leave the play call to write the opening
	   notes inline and never program a tick, so the sequencer advances once
	   and stops. Give the installed vector a clock — once only, and only when
	   the boot really left every source dead. */
	if (!wdArmedTick_ && !ticking && !hw_->useVrtc && !hw_->useRtc) {
		wdArmedTick_ = 1;
		if (Ay_CpuIm2Target(cpu, VEC_SOUND)) {
			hw_->ArmFallbackOpnTimer();
			return;
		}
		if (Ay_CpuIm2Target(cpu, VEC_VRTC)) {
			hw_->useVrtc = 1;
			nextVrtc_ = (uint64_t)cpu->time() + vrtcPeriod_;
			return;
		}
		if (Ay_CpuIm2Target(cpu, VEC_RTC)) {
			hw_->useRtc = 1;
			nextRtc_ = (uint64_t)cpu->time() + rtcPeriod_;
			return;
		}
	}
	/* Last resort, and only while the track has never made a note: the kick
	   may have raced the boot. Once anything has sounded, stop interfering. */
	if (wdEverActive_ || wdReplays_ >= 4)
		return;
	wdReplays_++;
	s_wdReplayCount++;
	replayPending_ = 1;
}

int CDriverPc88::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	Ay_Cpu* cpu = hw_->Cpu();
	CChip* chip = hw_->SoundChip();
	if (!cpu || !chip) return 0;
	CEmuHardPc88SetActive(hw_);
	if (hw_->NeedsN88RtcGuard())
		hw_->GuardN88RtcVector();
	if (replayPending_) {
		replayPending_ = 0;
		triggered_ = 0;
	}
	if (!triggered_)
		TriggerPlay();
	if (hostRate_ < 1 || cpuHz_ < 1) return 0;
	for (int i = 0; i < frames; i++) {
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		/* Carry insn overshoot into the next sample. Without this, each sample
		   runs past the budget by ~half an instruction (~5–6% extra Z80/OPN
		   clocks) and soundtrack tempo runs fast. */
		cpuCycleBudget_ += (int64_t)cyclesPerSample;
		while (cpuCycleBudget_ > 0) {
			const uint64_t now = (uint64_t)cpu->time();
			DeliverIrqs(now);
			uint8_t* mem = hw_->Mem();
			if (mem && mem[cpu->r.pc] == 0x76) {
				uint64_t wake = now + (uint64_t)cpuCycleBudget_;
				if (hw_->useRtc && rtcPeriod_ > 0 && nextRtc_ > now && nextRtc_ < wake)
					wake = nextRtc_;
				if (hw_->useVrtc && vrtcPeriod_ > 0 && nextVrtc_ > now && nextVrtc_ < wake)
					wake = nextVrtc_;
				uint64_t delta = (wake > now) ? (wake - now) : 4;
				if (delta < 4) delta = 4;
				if (delta > (uint64_t)cpuCycleBudget_)
					delta = (uint64_t)cpuCycleBudget_;
				cpu->adjust_time((int)delta);
				cpuCycleBudget_ -= (int64_t)delta;
				hw_->AddCpuCycles(delta);
				TickOpn(delta);
				continue;
			}
			const int cycles = Ay_CpuRunOne(cpu);
			cpuCycleBudget_ -= (int64_t)cycles;
			hw_->AddCpuCycles((uint64_t)cycles);
			TickOpn((uint64_t)cycles);
		}
		/* schwarz FE19 / Falcom E000 PATCH: DI around play, no matching EI. */
		if (hw_->NeedsPlayEi() && !cpu->r.iff1)
			cpu->r.iff1 = 1;
		if (hw_->NeedsN88RtcGuard() && (i & 63) == 0)
			hw_->GuardN88RtcVector();
		chip->Render(stereo + i * 2, 1);
		if ((++wdSamples_ & 511) == 0) {
			WatchdogTick();
			/* Replay in place: deferring to the next Render() call would add
			   that call's whole buffer to the gap between loops. */
			if (replayPending_) {
				replayPending_ = 0;
				triggered_ = 0;
				TriggerPlay();
			}
		}
	}
	/* FmMon AddSamples/Flush は readcemu 側のみ（二重だと curSample が 2 倍進み同期が崩れる） */
	return frames;
}

int CDriverPc88::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
