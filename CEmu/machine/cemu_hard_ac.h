#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

struct V35Cpu;
struct H6280Cpu;
struct M6502Cpu;
struct H8Cpu;
struct M37702Cpu;
struct HD63701Cpu;
struct mc6809;
struct m6800; /* opaque ? do not include mc6809.h here (MSB/LSB macros) */

enum CEmuAcBoard {
	CEMU_AC_BOARD_UNKNOWN = 0,
	CEMU_AC_BOARD_SYS16A = 1, /* Z80 + YM2151 I/O 0/1, latch IN C0, NMI */
	CEMU_AC_BOARD_SYS16B = 2,
	CEMU_AC_BOARD_CPS1 = 3,   /* Z80 + YM2151 (I/O or mem F000), latch, IRQ */
	CEMU_AC_BOARD_GNG = 4,    /* Capcom GNG: Z80 + dual YM2203 mem-map, latch C800 */
	CEMU_AC_BOARD_OUTRUN = 5, /* Sega OutRun: Z80 + YM2151 I/O 0/1, latch IN 40, cmd @F800, IM1 */
	CEMU_AC_BOARD_ABURNER = 6, /* After Burner: same I/O map as OutRun, but latch via NMI */
	CEMU_AC_BOARD_CPS_QS = 7,
	CEMU_AC_BOARD_SYS18 = 8,  /* YM3438x2 (YM2612 stand-in) + RF5C68 */
	CEMU_AC_BOARD_SYS32 = 9,
	CEMU_AC_BOARD_KONAMI_PCM = 10,
	CEMU_AC_BOARD_NAMCO_C352 = 11,
	CEMU_AC_BOARD_HANGON = 12, /* Hang-On / Space Harrier: YM2203@D000 + discrete SegaPCM@E000, latch IN40, NMI */
	CEMU_AC_BOARD_SYS24 = 13,  /* System24: dual 68000 + YM2151 (disk; no Z80) */
	CEMU_AC_BOARD_VSYSTEM = 14, /* Video System aerofgt: YM2610 I/O, latch NMI */
	CEMU_AC_BOARD_TAITO_YM2610 = 15, /* F2/B/dual68: YM2610 @E000, TC0140SYT @E200, bank F200 */
	CEMU_AC_BOARD_IREM_M72 = 16,     /* M72/M84: YM2151 I/O 00/01, latch 02/80, sample DAC */
	CEMU_AC_BOARD_IREM_M92 = 17,     /* M92: encrypted NEC V35 + YM2151 + IremGA20 */
	CEMU_AC_BOARD_SEGA_SYS1 = 18,    /* System1/2: dual SN76489 @A000/C000, latch @E000 */
	CEMU_AC_BOARD_DECO = 19,         /* Data East: HuC6280 or M6502 sound + FM (+ OKI) */
	CEMU_AC_BOARD_MEGASYSTEM1 = 20,  /* Jaleco MS1: second 68000 (Musashi) + YM2151 + 2x OKI6295 */
	CEMU_AC_BOARD_TAITO_SJ = 21,     /* Taito SJ: triple AY-3-8910 @4800/4802/4804 */
	CEMU_AC_BOARD_TAITO_OPM = 22,    /* Rastan/Asuka: YM2151 @9000, PC060HA @A000, bank */
	CEMU_AC_BOARD_KONAMI_GX = 23,    /* System GX: 68000 + dual K054539 + K056800 */
	CEMU_AC_BOARD_NAMCO_SYS2 = 24,   /* Namco System 2: M6809 + YM2151 + C140 */
	CEMU_AC_BOARD_NAMCO_SYS1 = 25,   /* Namco System 1: M6809 + YM2151 + CUS30 */
	CEMU_AC_BOARD_NAMCO_SYS86 = 26,  /* Namco System 86: HD63701 MCU + YM2151 + CUS30 */
	CEMU_AC_BOARD_NAMCO_WSG = 27,    /* Namco WSG/15XX: CUS30 MAPPY (+ M6809/Z80 when wired) */
	CEMU_AC_BOARD_TOAPLAN1 = 28,     /* Toaplan1: Z80 + YM3812, cmd via shared RAM 8000 */
	CEMU_AC_BOARD_KONAMI_SCRAMBLE = 29, /* Scramble/scobra: Z80 + dual AY I/O 10/20/40/80 */
	CEMU_AC_BOARD_KONAMI_TIMEPLT = 30,  /* Time Pilot/jungler: Z80 + dual AY mem 4/5/6/7000 */
	CEMU_AC_BOARD_KONAMI_GX400 = 31,    /* GX400/nemesis: Z80 + dual AY @E0xx (+K005289 stub) */
	CEMU_AC_BOARD_TECHNOS_DDRAGON2 = 32, /* DD2/chinagat: Z80 + YM2151@8800 + OKI@9800 */
	CEMU_AC_BOARD_IREM_M62 = 33,        /* M62: M6803 + dual AY (CPU missing → silent) */
	CEMU_AC_BOARD_SEGA_SCSP = 34,       /* Model 2/3: SCSP (no 68K host → silent) */
	CEMU_AC_BOARD_KONAMI_RF5C400 = 35,  /* Hornet/GTI Club: RF5C400 (no 68K → silent) */
	CEMU_AC_BOARD_SNK_OPL = 36,         /* SNK68: Z80 + YM3812 I/O 00/20, latch@F800 NMI */
	CEMU_AC_BOARD_SEIBU_OPL = 37,       /* Seibu raiden: YM3812+OKI (SEI80BU decrypt) */
	CEMU_AC_BOARD_KONAMI_K7232 = 38,    /* Z80+YM2151+K007232 stub (scontra/crimfght/twin16) */
	CEMU_AC_BOARD_KONAMI_HCASTLE = 39,  /* Z80+YM3812@A000 + K007232 stub (hcastle) */
	CEMU_AC_BOARD_TECMO16 = 40,         /* Z80+YM2151@FC04 + OKI@FC00 + latch@FC08 NMI */
	CEMU_AC_BOARD_FLSTORY = 41,         /* Taito flstory: Z80+AY@C800 + MSM5232@CA00 + latch@D800 NMI */
	CEMU_AC_BOARD_TERRACRE = 42,        /* Nichibutsu terracre YM3526 OR armedf/terraf YM3812 */
	CEMU_AC_BOARD_ROBOKID = 43,         /* UPL robokid: Z80+dual YM2203 I/O 00/80, latch@E000 */
	CEMU_AC_BOARD_BATTLANTIS = 44,      /* Konami battlantis: Z80+dual YM3812@A000/C000, latch@E000 */
	CEMU_AC_BOARD_ALPHA68K2 = 45,       /* Alpha 68K-II: Z80 + YM2203 + YM2413 + DAC (skyadvnt/gangwars) */
	CEMU_AC_BOARD_ATARI_SYS1 = 46       /* Atari System1: M6502 + YM2151 (+POKEY stub) JSA */
};

class CHardAc : public CHard {
public:
	CHardAc();
	~CHardAc() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int SampleRate() const { return sampleRate_; }
	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);
	int LoadRomsMs1(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsGx(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsM92(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsDeco(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsAtariSys1(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsH8(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsM37702(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsNamcoM6809(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsSys86(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsWsg63701(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsPcmChip(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsSeibu(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsM62(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsSegaM1(CEmuZipFs* fs, const CEmuGameEntry* ge);
	void SeibuRefreshOpcodes();
	void SeibuSetBank(unsigned bank);
	int SeibuActive() const { return board_ == CEMU_AC_BOARD_SEIBU_OPL; }
	int SeibuBank() const { return seibuBank_; }
	int SeibuRst10() const { return seibuRst10_; }
	int SeibuRst18() const { return seibuRst18_; }
	void SeibuSetRst10(int v) { seibuRst10_ = v ? 1 : 0; }
	void SeibuSetRst18(int v) { seibuRst18_ = v ? 1 : 0; }
	int M62Active() const { return m6803_ != NULL; }
	struct m6800* M6803Cpu() { return m6803_; }
	uint8_t M62Port1() const { return m62Port1_; }
	uint8_t M62Port2() const { return m62Port2_; }
	unsigned M62BusMask() const { return m62BusMask_; }
	int M62MsmReset() const { return m62MsmReset_; }
	void M62SetPort1(uint8_t v) { m62Port1_ = v; }
	void M62OnPort2Write(uint8_t val);
	int SegaM1Audio() const { return segaM1Audio_; }
	void SegaMidiInjectSong(uint16_t cmd);
	void SegaMidiPush(uint8_t b);
	uint8_t SegaUartRead(unsigned reg);
	void SegaUartWrite(unsigned reg, uint8_t data);
	unsigned Sega68Read16(unsigned addr);
	unsigned Sega68Read8(unsigned addr);
	void Sega68Write16(unsigned addr, uint16_t v);
	void Sega68Write8(unsigned addr, uint8_t v);
	int SegaMidiIrq() const { return segaMidiIrq_; }

	Ay_Cpu* Cpu() override { return cpu_; }
	uint8_t* Mem() override { return mem_; }
	CChip* SoundChip() override { return chip_; }
	int MainIsYm2203() const { return mainIsYm2203_; }
	CChip* PcmChip() { return pcm_; }
	const CChip* PcmChip() const { return pcm_; }
	int PcmKind() const { return pcmKind_; }

	uint8_t PortIn(uint16_t port) override;
	void PortOut(uint16_t port, uint8_t data) override;

	/* Sound-command latch from "main CPU". */
	void SetSoundCommand(uint8_t cmd);
	void SetSoundCommandWord(uint16_t cmd);
	/* System GX: push a raw 4-byte K056800 host packet + doorbell. */
	void GxHostInject(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3);
	uint8_t SoundCommand() const { return soundCmd_; }
	uint16_t SoundCommandWord() const { return soundCmdWord_; }
	int SoundCmdPending() const { return soundCmdPending_; }
	/* One-shot edge for NMI/IRQ delivery; cleared when interrupt is taken. */
	int IrqPulsePending() const { return irqPulse_; }
	int TakeIrqPulse() { int v = irqPulse_; irqPulse_ = 0; return v; }
	void PulseIrq() { irqPulse_ = 1; }
	/* K053260 SH1→NMI: FA00 arms; DeliverIrqs fires while Z80 is on HALT. */
	int KonamiSh1NmiArm() const { return konamiSh1NmiArm_; }
	void ClearKonamiSh1NmiArm() { konamiSh1NmiArm_ = 0; }
	/* WSG: suppress periodic NMI until first latch (boot handshake). */
	int WsgNmiEnable() const { return wsgNmiEnable_; }
	void SetWsgNmiEnable(int v) { wsgNmiEnable_ = v ? 1 : 0; }
	/* Mappy-era 15XX: M6809 sound CPU (subtype wsg6809), not Z80 Pac-Man WSG. */
	int WsgMappy() const { return wsgMappy_; }
	int Wsg63701() const { return wsg63701_; }
	unsigned Sys16RomBoard() const { return sys16RomBoard_; }
	uint8_t ToaplanYmPort() const { return toaplanYmPort_; }
	int QsZn() const { return qsZn_; }
	/* True once when a deferred ZN second-byte NMI should be pulsed. */
	int ZnTakeDeferredNmi()
	{
		if (!znDeferredNmi_) return 0;
		znDeferredNmi_ = 0;
		return 1;
	}
	void ClearSoundCmdPending() { soundCmdPending_ = 0; }
	void SetToaplanTimerA(int v) { toaplanTimerA_ = v ? 1 : 0; }
	int M72IoAlt() const { return m72IoAlt_; }

	/* CPS1 mem-map mirrors (F000/F001). Synced from PortOut and MemWrite. */
	void MemWrite(uint16_t addr, uint8_t data);
	uint8_t MemRead(uint16_t addr);

	uint64_t CpuCycles() const { return cpuCycles_; }
	void AddCpuCycles(uint64_t n) { cpuCycles_ += n; }

	int board_;
	int cpuHz_;
	int opmHz_;
	unsigned opmWrites_; /* YM2151 writes, or YM2203 writes on GNG */

	/* Dual YM2203 (GNG); NULL on OPM boards. */
	CChip* Chip2() { return chip2_; }
	const CChip* Chip2() const { return chip2_; }
	/* Third voice chip (Taito SJ AY#3). NULL elsewhere. */
	CChip* Chip3() { return chip3_; }
	const CChip* Chip3() const { return chip3_; }
	/* auxKind_: 0=YM2203 (GNG), 1=SN76489 (System1), 2=AY-3-8910 (SJ/scramble/�c) */
	int AuxKind() const { return auxKind_; }
	/* Konami AY timer port (scramble/timeplt port B). */
	uint8_t KonamiAyTimer() const;
	/* MAME nemesis_portA_r: bits0-3 cycle timer, bits4/6/7 high. */
	uint8_t Gx400PortA() const;

	/* Taito TC0140SYT / PC060HA master side - used by SetSoundCommand. */
	int SytNmiEnabled() const { return sytNmiEnabled_; }
	uint8_t SytStatus() const { return sytStatus_; }
	uint8_t SytSubMode() const { return sytSubMode_; }
	int Bank() const { return bank_; }
	unsigned SoundRomSize() const { return soundRomSize_; }
	unsigned PcmRomSize() const { return pcmRomSize_; }

	/* Sega System1 periodic sound IRQ needs a plausible vector at 0x0038,
	   and its latch NMI needs one at 0x0066; expose raw ROM bytes. */
	uint8_t PeekMem(uint16_t addr) const { return mem_[addr]; }

	/* MAME m72_state::fake_nmi - for M72 games whose NMI handler is empty the
	   PCM byte transfer is done by external hardware, so do it here. */
	void M72PumpSample();

	/* ---- Jaleco Mega System 1 / Konami System GX: 68000 sound board ----
	   Driven through the shared Musashi core (cemu_m68k_bus.cpp), not the Z80. */
	int Ms1Active() const { return ms1Rom_ != NULL; }
	unsigned Ms1RomSize() const { return ms1RomSize_; }
	unsigned Ms1Read8(unsigned addr);
	unsigned Ms1Read16(unsigned addr);
	void Ms1Write8(unsigned addr, uint8_t v);
	void Ms1Write16(unsigned addr, uint16_t v);
	/* 4 on System A/B (soundlatch_w), 6 on System C (soundlatch_c_w).
	   System GX: 1 = K056800, 2 = K054539 timer. */
	int Ms1LatchIrqLevel() const { return ms1LatchLevel_; }
	int Ms1IrqLevel() const;
	void Ms1AckIrq();
	CChip* Oki(int idx) { return idx ? pcm2_ : pcm_; }
	const CChip* Oki(int idx) const { return idx ? pcm2_ : pcm_; }
	unsigned Ms1OkiWrites() const { return ms1OkiWrites_; }
	unsigned Ms1LatchReads() const { return ms1LatchReads_; }
	unsigned GxPcmWrites() const { return gxPcmWrites_; }

	/* ---- Data East sound (HuC6280 cninja map, or M6502 karnov/dec0) ----
	   HuC6280 21-bit physical space (MAME cninja.cpp sound_map):
	     000000-00FFFF  ROM
	     100000-100001  YM2203
	     110000-110001  YM2151
	     120000-120001  OKIM6295 #1
	     130000-130001  OKIM6295 #2
	     140000         deco_146 soundlatch read
	     1F0000-1F1FFF  RAM
	   Latch pending �� IRQ1; YM2151 irq �� IRQ2.
	   M6502 maps (decoCpuKind_ 1/2/3): see DecoM6502Read8. */
	int DecoActive() const { return decoRom_ != NULL; }
	int DecoCpuKind() const { return decoCpuKind_; }
	H6280Cpu* DecoCpu() { return h6280_; }
	M6502Cpu* DecoM6502() { return m6502_; }
	uint8_t DecoRead8(uint32_t phys);
	void DecoWrite8(uint32_t phys, uint8_t v);
	uint8_t DecoM6502Read8(uint16_t addr);
	void DecoM6502Write8(uint16_t addr, uint8_t v);
	void DecoSyncIrqs();
	unsigned DecoRomSize() const { return decoRomSize_; }
	unsigned DecoLatchReads() const { return decoLatchReads_; }
	unsigned DecoOkiWrites() const { return decoOkiWrites_; }
	unsigned DecoChanWrites() const { return decoChanWrites_; }
	const uint8_t* DecoRom() const { return decoRom_; }
	const uint8_t* DecoRam() const { return decoRam_; }

	/* ---- Irem M92: encrypted NEC V35 sound CPU (CEmu/vendor/v35) ----
	   MAME irem/m92.cpp sound_map, 20-bit space:
	     00000-1FFFF  ROM
	     A0000-A3FFF  RAM
	     A8000-A803F  IremGA20   (umask16 0x00ff ? even bytes only)
	     A8040-A8043  YM2151     (A8040 = address, A8042 = data)
	     A8044        soundlatch read / acknowledge write
	     A8046        soundlatch2 write (answer to the main CPU)
	     FFFF0-FFFFF  reset-vector mirror of soundcpu+0x1FFF0            */
	int M92Active() const { return m92Rom_ != NULL; }
	V35Cpu* M92Cpu() { return v35_; }
	uint8_t M92Read8(uint32_t addr);
	void M92Write8(uint32_t addr, uint8_t v);
	/* Re-samples the YM2151 IRQ line into INTP0; call before each CPU slice. */
	void M92SyncIrqs();
	unsigned M92RomSize() const { return m92RomSize_; }
	int M92PatchRom(uint32_t off, uint8_t v);
	unsigned M92LatchReads() const { return m92LatchReads_; }
	unsigned M92Ga20Writes() const { return m92Ga20Writes_; }
	uint8_t M92Latch2() const { return m92Latch2_; }
	const uint8_t* M92Ram() const { return m92Ram_; }
	const uint8_t* M92Rom() const { return m92Rom_; }
	uint8_t GxSoundCtrl() const { return gxSoundCtrl_; }
	int GxK056800IntEn() const { return k056800IntEn_; }
	int GxK056800Irq() const { return k056800Irq_; }

private:
	void SytMasterWriteCommand(uint8_t cmd);
	void SytSlavePortW(uint8_t data);
	void SytSlaveCommW(uint8_t data);
	uint8_t SytSlaveCommR();
	void SytUpdateNmi();
	void SetBank(int bank);

	uint8_t mem_[0x10000];
	Ay_Cpu* cpu_;
	CChip* chip_;
	CChip* chip2_; /* second YM2203 for GNG; SN2 on System1; AY#2 on Taito SJ */
	CChip* chip3_; /* AY#3 on Taito SJ */
	CChip* pcm_;
	int sampleRate_;
	uint64_t cpuCycles_;
	uint8_t soundCmd_;
	uint16_t soundCmdWord_;
	int soundCmdPending_;
	int irqPulse_;
	int wsgNmiEnable_; /* Namco WSG: periodic NMI after boot/inject */
	int wsgMappy_;     /* 1 = Mappy/15XX M6809 map (vs Pac-Man Z80) */
	int wsg63701_;     /* 1 = HD63701 + CUS30 MAPPY (pacland/skykid) */
	int qsZn_; /* Capcom ZN: latch+NMI (MAME zn.cpp), not CPS2 shared-RAM mailbox */
	int qsKabuki_; /* CPS1 QSound Kabuki: mem_ holds opcodes; qsKabukiData_ for reads */
	uint8_t* qsKabukiData_; /* 0x8000 data-decrypt plane */
	/* ZN QSound: PSX→Z80 is four latch+NMI bytes FF,00,hi,lo (arcade-projects /
	   zn QSound notes). F015/F016 ring stores pairs; FF 00 resets voices. */
	uint8_t znQueue_[4];
	int znQueueLen_;
	int znQueuePos_;
	int znDeferredNmi_; /* arm next NMI after the current handler returns */
	uint8_t ymAddr_; /* CPS1 mem-map address latch at F000 */
	uint8_t gngYmAddr_[2]; /* GNG YM2203 address latches (E000 / E002) */
	int gngCommandoMap_; /* 1: Commando/ExedExes-style 4000/6000/8000 map */
	int gngGaidenMap_;   /* 1: Tecmo gaiden F000/F810/F820/FC20 (+OKI) map */
	uint8_t hangYmAddr_; /* Hang-On YM2203 address latch at D000 */
	uint64_t abStatusPulseSlot_; /* After Burner: synthetic TimerA pulse slot */
	CChip* pcm2_;  /* Mega System 1 second OKI6295 (0x0C0000) */
	uint8_t* pcmRom_;
	unsigned pcmRomSize_;
	uint8_t* pcmRom2_;
	unsigned pcmRom2Size_;
	int pcmKind_; /* 0=none 1=SegaPCM 2=OKI 3=K053260 4=K054539 5=RF5C68 6=IremDAC 7=GA20 8=C140 9=C30 */
	int auxKind_;  /* chip2_/chip3_ type - see AuxKind() */
	int mainIsYm2203_; /* Hang-On / GNG-style destroy */
	int mainIsYm2610_;
	int mainIsYm2612_;

	/* Banked sound ROM (Taito F2/B/dual68, Rastan, V-System). Full image kept
	   so the window can be repointed by the bank write. Taito banks 0x4000
	   bytes at 0x4000; V-System banks 0x8000 bytes at 0x8000. */
	uint8_t* soundRom_;
	unsigned soundRomSize_;
	int bank_;
	int bankLoaded_;
	unsigned bankBase_;
	unsigned bankSize_;

	/* Konami K053260 boards: catalog opm_addr / pcm_addr (default F800 / FC00).
	   K054539 (moo/bucky): defaults EC00 / E000 + bank F800 + K054321 @F000. */
	unsigned konamiOpmAddr_;
	unsigned konamiPcmAddr_;
	unsigned konamiBankAddr_;
	unsigned konamiPcmWindow_; /* K053260=0x40, K054539=0x230 */
	int konamiSh1NmiArm_; /* FA00 write arms one SH1 NMI (Simpsons/Punk Shot) */

	/* Mega System 1 68000 sound board. */
	uint8_t* ms1Rom_;
	unsigned ms1RomSize_;
	uint8_t* ms1Ram_;      /* 0x20000 mapped at 0x0E0000 */
	int ms1LatchLevel_;
	int ms1LatchIrq_;      /* soundlatch_w asserted the IPL lines */
	uint16_t ms1LatchIn_;  /* main -> sound (soundlatch[0]) */
	uint16_t ms1LatchOut_; /* sound -> main (soundlatch[1]) */
	unsigned ms1OkiWrites_;
	unsigned ms1LatchReads_;

	/* Data East HuC6280 / M6502 sound board. */
	H6280Cpu* h6280_;
	M6502Cpu* m6502_;
	uint8_t* decoRom_;
	unsigned decoRomSize_;
	uint8_t* decoRam_;     /* 0x2000 at physical 1F0000 (H6280) or ZP/page0 (M6502) */
	uint8_t decoYm2203Addr_;
	uint8_t decoYm2151Addr_;
	uint8_t atariJsaIo_; /* Atari Sys1 /WRIO latch (self-test bit7 echo) */
	unsigned decoLatchReads_;
	unsigned decoOkiWrites_;
	unsigned decoChanWrites_;
	uint8_t decoM6502Ram_[0x800]; /* M6502 work RAM */

	/* Irem M92 V35 sound board. */
	V35Cpu* v35_;
	uint8_t* m92Rom_;
	unsigned m92RomSize_;
	uint8_t* m92Ram_;      /* 0x4000 mapped at 0xA0000 */
	uint8_t m92Latch_;     /* generic_latch_8 main -> sound */
	int m92LatchPending_;  /* separate_acknowledge: cleared by A8044 write */
	uint8_t m92Latch2_;    /* sound -> main (upd71059c ir3); nothing listens */
	unsigned m92LatchReads_;
	unsigned m92Ga20Writes_;
	int m92BomberGatePatch_; /* 1 when 11F8 is bomberman's PUSH CX fallthrough */
	uint8_t m92EncryptedRet_; /* encrypted byte that decrypts to C3 RET */
	/* Private copy of the Irem Software Guard opcode table. FINT's second
	   byte is plaintext (MAME fetch(), not fetchop()); do not rewrite
	   table[0x92]. */
	uint8_t m92Decrypt_[256];
	int m92DecryptValid_;
	/* 0x20 when reset vector is encrypted JMP FAR (Rev 3.40+ BGM = 0x20+n);
	   0 for early FA/CLI boots (bmaster/lethalth/gunforce). */
	uint8_t m92SongCmdBase_;
	/* 1 when INTP0 sequencer is channel-BGM (MOV BP,#0 / tick) — rtypeleo /
	   hook / nbbatman / …; 0 for uccops-style note-list BGM. */
	uint8_t m92ChannelBgm_;
	/* 1 when command ring is 16× word @0AF0 (firebarr/nbbatman/…);
	   0 for classic byte ring @08C0 (hook/rtypeleo/uccops). */
	uint8_t m92WordQueue_;
	/* 1 when idle spins on [0C31]==3 (main↔sound ready handshake). */
	uint8_t m92ReadyWait_;
public:
	int M92BomberGatePatch() const { return m92BomberGatePatch_; }
	uint8_t M92EncryptedRet() const { return m92EncryptedRet_; }
	uint8_t M92SongCmdBase() const { return m92SongCmdBase_; }
	uint8_t M92ChannelBgm() const { return m92ChannelBgm_; }
	uint8_t M92WordQueue() const { return m92WordQueue_; }
	uint8_t M92ReadyWait() const { return m92ReadyWait_; }

	/* ---- Namco System 1/2 M6809 sound (vendor/mc6809) ----
	   Sys1 map (MAME namcos1 sound_map):
	     0000-3FFF banked ROM, 4000-4001 YM2151, 5000-53FF CUS30,
	     7000-77FF TRI-RAM (cmd @7100), 8000-9FFF RAM, C000-FFFF ROM,
	     C000 bank write, E000 IRQ ack. YM��FIRQ, VBlank��IRQ.
	   Sys2: same skeleton with C140 @7000 instead of / alongside CUS30. */
	int NamcoM6809Active() const { return namcoM6809_ != NULL && soundRom_ != NULL; }
	struct mc6809* NamcoM6809Cpu() { return namcoM6809_; }
	uint8_t NamcoM6809Read8(uint16_t addr);
	void NamcoM6809Write8(uint16_t addr, uint8_t v);
	void NamcoM6809SyncIrqs();
	void NamcoM6809SetBank(unsigned bank);
	unsigned NamcoM6809Bank() const { return namcoBank_; }
	void SetNamcoMailFlag(uint8_t f);
	uint8_t NamcoMailFlag() const;
	unsigned NamcoMailOff() const { return namcoMailOff_; }
	/* Sys2 song-table record type ($20=BGM gate, $64=alt, $21=variant). */
	int Sys2SongRecType(unsigned songLo) const;
	const uint8_t* NamcoTriRam() const { return namcoTriRam_; }

	/* ---- Namco C352 + H8/3002 (System 12 / ND-1) ----
	   Sys12 map (MAME namcos12 sub_program_map):
	     000000-07FFFF  H8 program (WORD_SWAP flash)
	     080000-08FFFF  shared RAM (mailbox with main)
	     280000-287FFF  C352
	     300000-300003  inputs / wait stubs
	   ND-1 map (namcond1 h8rwmap):
	     000000-07FFFF  ROM
	     200000-20FFFF  shared RAM
	     A00000-A07FFF  C352
	   Classic MAME H8 reset starts at *(u32*)0 (bootstrap = initial SP). */
	int H8Active() const { return h8_ != NULL && h8Rom_ != NULL; }
	int M37702Active() const { return m37702_ != NULL && (m37702IntRom_ != NULL || h8Rom_ != NULL); }
	int M37702Soft() const { return m37702Soft_; }
	int M37702C140() const { return m37702C140_; }
	int SnkMapKind() const { return snkMapKind_; } /* 0=snk68 I/O, 1=classic dual OPL mem */
	int KonamiK7232Map() const { return konamiK7232Map_; }
	int AlphaNmiMask() const { return alphaNmiMask_; }
	void AlphaMixOpll(int16_t* stereo, int frames);
	uint8_t SnkStatus() const { return snkStatus_; }
	void SnkSetYmIrq(int which, int on);
	/* terracreMap_: 0=terracre C000 RAM + I/O latch; 1=armedf/terraf F800 RAM + shifted latch */
	int TerracreMap() const { return terracreMap_; }
	int FlstoryNmiEn() const { return flstoryNmiEn_; }
	int GngGaidenMap() const { return gngGaidenMap_; }
	H8Cpu* H8CpuPtr() { return h8_; }
	M37702Cpu* M37702CpuPtr() { return m37702_; }
	uint8_t H8Read8(uint32_t addr);
	void H8Write8(uint32_t addr, uint8_t v);
	uint8_t M37702Read8(uint32_t addr);
	void M37702Write8(uint32_t addr, uint8_t v);
	void H8InjectSong(uint16_t cmd);
	void M37702InjectSong(uint16_t cmd);
	unsigned H8RomSize() const { return h8RomSize_; }
	unsigned H8C352Writes() const { return h8C352Writes_; }
	int H8MapKind() const { return h8MapKind_; } /* 0=sys12, 1=nd1 */
	int M37702MapKind() const { return m37702MapKind_; } /* 0=sys11, 1=na1/nb, 2=sys22 */
	const uint8_t* H8Shared() const { return h8Shared_; }

	/* ---- Namco System 86 HD63701 + YM2151 + CUS30 ----
	   MAME namcos86 / FBNeo d_namcos86:
	     0000-001F  HD63701 internal regs
	     0080-00FF  internal RAM
	     1000-13FF  CUS30
	     1400-1FFF  work RAM
	     YM2151     @2000 (rthunder) / @2800 (genpeitd) / etc
	     4000-BFFF  external MCU ROM (genpeitd/rthunder)
	     F000-FFFF  CUS60 internal ROM */
	int HD63701Active() const { return hd63701_ != NULL && hd63701Rom_ != NULL; }
	HD63701Cpu* HD63701CpuPtr() { return hd63701_; }
	uint8_t HD63701Read8(uint16_t addr);
	void HD63701Write8(uint16_t addr, uint8_t v);
	uint8_t HD63701PortRead(uint16_t port);
	void HD63701PortWrite(uint16_t port, uint8_t v);
	void HD63701InjectSong(uint8_t cmd);
	int HD63701MapKind() const { return hd63701MapKind_; }
	uint16_t HD63701YmBase() const { return hd63701YmBase_; }
	unsigned HD63701YmWrites() const { return hd63701YmWrites_; }
private:
	H8Cpu* h8_;
	M37702Cpu* m37702_;
	uint8_t* h8Rom_;
	unsigned h8RomSize_;
	uint8_t* m37702IntRom_; /* C69/C74/C76 16KB mask ROM @ 0xC000 */
	unsigned m37702IntRomSize_;
	uint8_t* h8Shared_;
	uint8_t* m37702LocalRam_; /* NA1 0x3000-0xAFFF */
	uint16_t m37702Mailbox_[8];
	int h8MapKind_;
	int m37702MapKind_; /* 0=sys11 C76+C352, 1=na/nb C69+C140, 2=sys22 */
	int h8WordSwap_;
	unsigned h8C352Writes_;
	uint8_t h8C352Hi_;
	int h8C352HiValid_;
	uint16_t h8C352Shadow_[0x400];
	int m37702Soft_; /* 1 while family selected but core failed to attach */
	int m37702C140_; /* NA-1/NB-1: C219/C140 stand-in (not C352) */
	int snkMapKind_; /* 0=snk68 YM3812 I/O+NMI; 1=classic mem-map dual OPL+IRQ */
	uint8_t snkStatus_; /* classic F800 status (ym1|ym2|busy|cmd) */
	int terracreMap_; /* 0=terracre, 1=armedf/terraf */
	int flstoryNmiEn_; /* DA00 enable gate (MAME soundnmi in_set<1>) */

	HD63701Cpu* hd63701_;
	uint8_t* hd63701Rom_;      /* 64K MCU address space image */
	unsigned hd63701RomSize_;
	uint8_t hd63701Ram_[0x1000];
	int hd63701MapKind_;       /* 0=hopmappy, 2=genpeitd, 3=rthunder, 4=wndrmomo */
	uint16_t hd63701YmBase_;
	unsigned hd63701YmWrites_;

	/* Namco Sys1/2 M6809. */
	struct mc6809* namcoM6809_;
	uint8_t namcoTriRam_[0x800];
	uint8_t namcoWorkRam_[0x2000]; /* $8000-$9FFF */
	uint8_t namcoCus30_[0x400];    /* Sys1 $5000 mirror for RAM-test readback */
	unsigned namcoBank_;
	uint8_t namcoYmAddr_;
	int namcoIrqAssert_;
	int namcoFirqAssert_;
	uint64_t namcoNextVblank_;
	/* Sys2 mailbox base in DPRAM ($7100 finallap/assault, $7110 burnforc). */
	unsigned namcoMailOff_;
	/* Data East: 0=HuC6280 (cninja), 1=M6502 karnov map, 2=M6502 dec0,
	   3=M6502 actfancr (ROM @4000). */
	int decoCpuKind_;

	/* Konami System GX: K056800 mailbox + dual K054539 (chip_/pcm_).
	   MAME devices/sound/k056800.cpp + konamigx.cpp gxsndmap. */
	uint8_t k056800Host_[4]; /* host_to_snd_regs ? sound_r offsets 0..3 */
	uint8_t k056800Snd_[2];  /* snd_to_host_regs ? sound_w offsets 0..1 */
	int k056800IntEn_;
	int k056800Pending_;
	int k056800Irq_;         /* IRQ1 to sound 68000 */
	uint8_t gxSoundCtrl_;    /* mirror of $500001 control (bit0 enables IRQ2) */
	int gxSoundIntck_;
	unsigned gxPcmWrites_;
	uint8_t gxTmsStatus_;

	/* Taito TC0140SYT / PC060HA CIU state (MAME shared/taitosnd.cpp). */
	uint8_t sytSlaveData_[4];
	uint8_t sytMasterData_[4];
	uint8_t sytMainMode_;
	uint8_t sytSubMode_;
	uint8_t sytStatus_;
	uint8_t sytNmiEnabled_;

	/* Irem M72 sample pointer (m72_audio_device). */
	uint32_t m72SampleAddr_;
	/* MAME irem_m72 sound_ram_map: on the M72 board proper the sound Z80 has no
	   ROM at all ? all 64K is RAM and the V30 uploads the driver into it. The
	   M81/M82/M84 boards use sound_rom_map instead (0000-EFFF ROM). */
	int m72SoundRam_;
	/* M72 I/O decode: 0 = rtype/rtype2 (YM@00), 1 = poundfor/m99 (YM@40, latch@42). */
	int m72IoAlt_;
	/* Sega Sys16 ROM board id from catalog boardtype (0x5358/5521/5704/5797).
	   Selects uPD7759/sound bank bit mapping (MAME segas16b upd7759_control_w). */
	unsigned sys16RomBoard_;
	/* V-System I/O: 0 = aerofgt (YM@00), 1 = spinlbrk/f1gp (YM@18),
	   2 = fromanc/welltris (YM@08), 3 = Psikyo gunbird (YM@04, latch@08). */
	int vsIoKind_;
	/* Konami K007232-era YM2151 map: 0 = scontra/twin16 (latch A000, YM C000),
	   1 = crimfght/aliens (latch C000, YM A000). */
	int konamiK7232Map_;
	/* Alpha 68K-II: emu2413 OPLL + YM2203 port-A NMI gate. */
	void* alphaOpll_;
	uint8_t alphaYmAddr_;
	uint8_t alphaOpllAddr_;
	uint8_t alphaNmiMask_;
	uint8_t alphaPaLatch_;

	/* Taito SJ soundlatch semaphores + AY#4 port-B NMI mask. */
	uint8_t sjLatchFlag_;
	uint8_t sjSemaphore2_;
	uint8_t sjNmiMask_;
	uint8_t sjNmiMaskSeen_;
	uint8_t toaplanTimerA_; /* soft Timer-A doorbell for ISR music path */
	uint8_t toaplanYmPort_; /* YM3812 base port: 00/60/70/A8 */
	uint8_t ayAddr_[3];

	/* Seibu SEI80BU encrypted Z80 + YM3812 + OKI. */
	int seibuEnc_;
	int seibuBank_;
	int seibuSongOr80_; /* raiden: catalog n → table n|0x80; cupsoc uses 0x8e */
	uint8_t seibuMain2Sub_[2];
	uint8_t seibuSub2Main_[2];
	int seibuMainPending_;
	int seibuSubPending_;
	int seibuRst10_;
	int seibuRst18_;

	/* Irem M62 M6803. */
	struct m6800* m6803_;
	uint8_t m62Port1_;
	uint8_t m62Port2_;
	uint8_t m62AyMAddr_;  /* last AY#0 address latch */
	unsigned m62BusMask_; /* 0xffff M62/M52-large; 0x7fff M52-small */
	int m62MsmReset_;     /* AY#0 portB bit0 — MSM5205 held in reset */

	/* Sega Model1 MultiPCM / Model2 SCSP 68000 host. */
	int segaM1Audio_;
	uint8_t segaMidiFifo_[64];
	int segaMidiHead_;
	int segaMidiTail_;
	int segaMidiIrq_;
	void SoftPcmInjectSong(uint16_t cmd); /* SCSP / RF5C400 latch only */

	friend void CEmuAcDestroyMainChip(const CHardAc* hw, CChip* chip);
	friend void CEmuAcDestroyPcmChip(const CHardAc* hw, CChip* pcm);
	friend void CEmuAcDestroyAuxChip(const CHardAc* hw, CChip* aux);
};

void CEmuHardAcSetActive(CHardAc* hw);

/* Prefer a looping BGM code for System GX when the playlist hands us 0/STOP.
   Bank 0x01 with mid-range indices beats voice banks (0x05xx) and empty 0x0101. */
unsigned CEmuAcPickGxDefaultTitle(const CEmuGameEntry* ge);
int CEmuAcGxTitleIsVoice(const CEmuTitleEntry* t);
