#include "StdAfx.h"
#include "cemu_driver_pc88.h"
#include "../machine/cemu_hard_pc88.h"
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
	VRTC_MILLIHZ = 56400
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
			   still before the poll (lizard88/gineiden/gallforc decrypt). */
			if (nowPoll >= 0 && cpu->r.pc < 0x80) {
				if (jrEntry) {
					if (mem[0x7800] == 0xC3)
						continue; /* full settle */
					if (mem[0x0100] == 0x31 && step < 24)
						continue;
					if ((int)cpu->r.pc < nowPoll)
						continue;
					break;
				}
				if (cpu->r.iff1)
					break;
			}
		}
	}
	hw_->FixupIm2AfterBoot();
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

int CDriverPc88::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	Ay_Cpu* cpu = hw_->Cpu();
	CChip* chip = hw_->SoundChip();
	if (!cpu || !chip) return 0;
	CEmuHardPc88SetActive(hw_);
	if (hw_->NeedsN88RtcGuard())
		hw_->GuardN88RtcVector();
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
			uint8_t* mem = hw_->Mem();
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
			hw_->FixupIm2AfterPlay();
			if (hw_->NeedsDeferredRtc()) {
				RunUntil((uint64_t)cpu->time() + (uint64_t)cpuHz_ / 8);
				hw_->EnableDeferredRtc();
			}
		}
		triggered_ = 1;
	}
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
	}
	/* FmMon AddSamples/Flush は readcemu 側のみ（二重だと curSample が 2 倍進み同期が崩れる） */
	return frames;
}

int CDriverPc88::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
