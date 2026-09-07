#pragma once

#include "cemu_hard.h"

#include "../chip/cemu_chip.h"

#include "../cemu_zipfs.h"



class CHardPc88 : public CHard {

public:

	CHardPc88();

	~CHardPc88() override;



	int Init(const CEmuGameEntry* ge, int sampleRate);

	void Shutdown();

	int SampleRate() const { return sampleRate_; }

	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);

	/* Stage selected bgm/voice into mdata/vdata (safe to re-call).
	   titleCode: low 8 = bank index; bits 8..23 = byte offset into that bank
	   (KOEI packed MMLDATA / mfile_size sets). Copies clip at 64K. */
	void LoadSongData(unsigned titleCode);

	/* Full title code from last LoadRoms (for song reload). */
	unsigned titleCode_;

	/* Restage on play is unsafe when mdata overlaps PATCH/stack (SP=0x100). */
	int ShouldRestageSong() const;

	/* KOEI FMDRV.SYS family: packed CIM at default 0x4000, play with E=0. */
	int PackedKoei() const { return packedKoei_; }

	/* Port-80/01 play index for the current titleCode_ (herzog sub-song etc.). */
	uint8_t PlaySongIndex() const;

	/* Port-01 param: usually PlaySongIndex(), but song<<8 PATCHes keep the
	   title low byte (duel CP 30 on param while port80 carries the page). */
	uint8_t PlayParamIndex() const;

	/* After PATCH boot: fill missing FE19 IM2 vectors when I!=0 page is empty. */
	void FixupIm2AfterBoot();

	/* Catalog offers RTC+VRTC: keep only the ones the IM2 table really uses. */
	void PruneDeadTickSources();

	/* gineiden: play plants sound ISR but leaves RTC slot empty — mirror. */
	void FixupIm2AfterPlay();

	/* gineiden: PATCH 4E2F clears OPN Timer B; re-arm like AMAIN 4E00. */
	void ArmGineidenOpnTimer();
	/* lizard88: re-arm Timer B + unmask after play CALL 9F0F. */
	void ArmLizardOpnTimer();
	void ArmFallbackOpnTimer();
	/* navitune-class: title bits 8..23 select song inside code@mdata.
	   Rewrite PATCH's LD BC,mdata for cmd07 before retarget play (no host stubs). */
	void ApplyNavituneTitleSong();
	/* navitune: PATCH DI→EI once after code load (hoot patch never EI's). */
	void PrepareNavitunePatch();
	/* PC of PATCH LD A,07 / LD BC,… / CALL 4D00 (0 if not found). */
	unsigned NavituneRetargetPc() const;
	/* navitune: after PATCH play — unmask sound IRQ / EI only. */
	void FinishNavitunePlay();
	/* yakyufan: re-assert play enable flags after mute@0C5D. */
	int NeedsYakyufanArm() const;
	void ArmYakyufanPlay();
	int NeedsGineidenArm() const { return armGineidenTimer_; }
	int NeedsLizardArm() const { return armLizardTimer_; }
	int NeedsLongPlayDrain() const { return longPlayDrain_; }
	int NeedsNavituneArm() const { return armNavituneTimer_; }
	int NeedsDeferredRtc() const { return deferRtcAfterPlay_; }
	void EnableDeferredRtc()
	{
		if (!deferRtcAfterPlay_) return;
		ArmN88RtcPlayer();
		useRtc = 1;
		deferRtcAfterPlay_ = 0;
	}
	/* N88+DEMOM/MUSIC: force IM2 RTC vector to the known player ISR. */
	void ArmN88RtcPlayer();
	int NeedsN88RtcGuard() const { return n88RtcIsr_ != 0; }
	void GuardN88RtcVector()
	{
		if (!n88RtcIsr_ || !mem_ || !cpu_ || cpu_->r.i != 0xF3)
			return;
		const unsigned cur = (unsigned)mem_[0xF304] | ((unsigned)mem_[0xF305] << 8);
		if (cur != n88RtcIsr_ && mem_[n88RtcIsr_] == 0xF5) {
			mem_[0xF304] = (uint8_t)(n88RtcIsr_ & 0xff);
			mem_[0xF305] = (uint8_t)(n88RtcIsr_ >> 8);
		}
		if (n88RtcThrottleAddr_ && n88RtcThrottleAddr_ < 0x10000
			&& mem_[n88RtcThrottleAddr_] == 0)
			mem_[n88RtcThrottleAddr_] = 0x01;
		if (cpu_->r.sp >= 0xF000 || cpu_->r.sp < 0x0100)
			cpu_->r.sp = 0x0200;
	}

	/* schwarz PATCH DI's around play and never EI — keep IRQs alive after cmd. */
	int NeedsFe19PlayEi() const;

	/* Falcom E000 / FE19: DI around play with IM2 sound vector installed. */
	int NeedsPlayEi() const;

	/* PATCH IM2 + CALL init under DI (hadou/gra88): need EI during boot. */
	int NeedsBootEiPulse() const;

	/* BOTHTEC The Scheme OPNA (MUS2+ADR_+INT2): PATCH lives at 0x9000. */
	int IsSchemeOpna() const { return schemeMode_; }

	/* hoot scheme Play(): flags at 0x9010/11/13 (title low + bank high). */
	void SchemePlayTrigger(unsigned titleCode);

	/* Falcom masks port-32 sound IRQ around JP into type=prog; the prog still
	   needs OPN timer IRQs or it never returns to the PATCH unmask. */
	int IgnoreSoundIrqMask() const;

	/* Game Arts / castle: port cmd arms IRQ-driven play, but ISR lives on
	   RTC vector 04 (or castle needs PROG2 entry). Host CALL init then base. */
	unsigned PlayKickBase() const { return playKickBase_; }
	unsigned PlayKickInitOff() const { return playKickInitOff_; }
	int PlayKickEi() const { return playKickEi_; }
	void SetPlayKick(unsigned base, unsigned initOff, int ei)
	{
		playKickBase_ = base;
		playKickInitOff_ = initOff;
		playKickEi_ = ei ? 1 : 0;
	}
	void SetForcePlayEi(int v) { forcePlayEi_ = v ? 1 : 0; }
	void DirectPlayKick(unsigned addr, int ei);

	Ay_Cpu* Cpu() override { return cpu_; }

	uint8_t* Mem() override { return mem_; }

	CChip* SoundChip() override { return chip_; }



	uint8_t PortIn(uint16_t port) override;

	void PortOut(uint16_t port, uint8_t data) override;



	uint64_t CpuCycles() const { return cpuCycles_; }

	void AddCpuCycles(uint64_t n) { cpuCycles_ += n; }



	uint8_t cmd;

	uint8_t param;

	uint8_t song;

	int soundIrqMasked;

	int useRtc;

	int useVrtc;

	int opnaMode;

	/* Effective Z80 Hz: 4000000 * max(1, clockmul). */
	int cpuHz_;



private:

	void FreeBanks();

	void StageBanks(CEmuZipFs* fs, const CEmuGameEntry* ge);

	void BankCopyBgm(uint8_t songIndex);

	void SetSoundIrqPort(uint8_t data);

	/* PC-8801 text window (I/O 70h / 78h): move the 1KB view at 8000-83FF. */
	void SetTextWindow(uint8_t hi);



	uint8_t mem_[0x10000];

	Ay_Cpu* cpu_;

	CChip* chip_;

	int sampleRate_;

	uint64_t cpuCycles_;

	/* hoot-style ioport latch (default 0). Unknown ports RMW here —
	   open-bus 0xFF on IN would stick bit7 of 0x32 forever (arcus2 silence).
	   Ports 0x32 and 0xAA share the byte at ioPorts_[0x32]. */
	uint8_t ioPorts_[256];

	/* Text-window base page (0x80 = window closed, i.e. plain main RAM), and
	   the real 8000-83FF bytes hidden while it is open. */
	uint8_t textWinHi_;
	uint8_t textWinShadow_[0x400];

	int mdataAddr_;

	int mdataSize_;

	int mfileSize_;

	int vdataAddr_;

	int vfileSize_;

	int wolfteamMode_;

	/* Port-0 bank-copy is mucom88-only. KOEI PATCH keeps IRQ opcodes at
	   0x5C/0x5D (POP AF / EI); treating them as a dst pointer corrupts RAM. */
	int mucomBankCopy_;

	int mdataAddrDefaulted_;

	int packedKoei_;

	int initPc_;

	/* f_crisis-class: PATCH never EI's; arm once at LoadRoms. */
	int forcePlayEi_;

	/* gineiden: mirror IM2 sound vector into empty RTC slot after play. */
	int mirrorSoundToRtc_;

	/* gineiden: host re-arm OPN Timer B after PATCH clears it. */
	int armGineidenTimer_;
	/* lizard88: host re-arm OPN Timer B after play. */
	int armLizardTimer_;
	/* 1942_88: ADEE LDIR needs a longer cmd=1 drain. */
	int longPlayDrain_;
	/* yaksa PATCH2: catalog rom name — play index must stay nonzero. */
	int yaksaPatch2_;
	/* navitune-class: code@mdata + bgm same image; title fileOff → song. */
	int armNavituneTimer_;
	uint16_t naviSongAddr_; /* absolute song header (mdata+fileOff) */

	/* rogueal: enable RTC only after play (boot LDIR vs RTC race). */
	int deferRtcAfterPlay_;

	/* N88 thexder/bokosuka: RTC ISR address DEMOM/MUSIC would LD (F304),HL. */
	unsigned n88RtcIsr_;
	unsigned n88RtcThrottleAddr_;

	/* Scheme OPNA specialty (PATCH@9000, BGM via port0 → C000). */
	int schemeMode_;

	/* Direct CALL play: base address (PLAY88/C000/PROG2), optional +init
	   offset (Game Arts +6), and whether to EI after the base CALL. */
	unsigned playKickBase_;
	unsigned playKickInitOff_;
	int playKickEi_;

	unsigned char* bgmBank_[256];

	unsigned bgmBankSize_[256];

	/* Falcom type=prog (separate from bgm — xana2 uses both). */
	unsigned char* progBank_[256];

	unsigned progBankSize_[256];

	unsigned char* voiceBank_[256];

	unsigned voiceBankSize_[256];

};



void CEmuHardPc88SetActive(CHardPc88* hw);

