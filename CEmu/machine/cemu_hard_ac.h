#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

extern int g_traceM68kRead;
extern int g_m68kReadCount[0x10000];
extern int g_m68kPcmWrites;

struct V35Cpu;
struct H6280Cpu;
struct M6502Cpu;
struct H8Cpu;
struct M37702Cpu;
struct HD63701Cpu;
struct mc6809;
struct m6800; /* 不透明。ここでは mc6809.h を include しない（MSB/LSB マクロ） */

enum CEmuAcBoard {
	CEMU_AC_BOARD_UNKNOWN = 0,
	CEMU_AC_BOARD_SYS16A = 1, /* Z80 + YM2151 I/O 0/1、ラッチ IN C0、NMI */
	CEMU_AC_BOARD_SYS16B = 2,
	CEMU_AC_BOARD_CPS1 = 3,   /* Z80 + YM2151（I/O または mem F000）、ラッチ、IRQ */
	CEMU_AC_BOARD_GNG = 4,    /* Capcom GNG: Z80 + YM2203×2 メモリマップ、ラッチ C800 */
	CEMU_AC_BOARD_OUTRUN = 5, /* Sega OutRun: Z80 + YM2151 I/O 0/1、ラッチ IN 40、cmd @F800、IM1 */
	CEMU_AC_BOARD_ABURNER = 6, /* After Burner: OutRun と同じ I/O マップ。ラッチは NMI */
	CEMU_AC_BOARD_CPS_QS = 7,
	CEMU_AC_BOARD_SYS18 = 8,  /* YM3438×2（YM2612 代用）+ RF5C68 */
	CEMU_AC_BOARD_SYS32 = 9,
	CEMU_AC_BOARD_KONAMI_PCM = 10,
	CEMU_AC_BOARD_NAMCO_C352 = 11,
	CEMU_AC_BOARD_HANGON = 12, /* Hang-On / Space Harrier: YM2203@D000 + ディスクリート SegaPCM@E000、ラッチ IN40、NMI */
	CEMU_AC_BOARD_SYS24 = 13,  /* System24: 68000×2 + YM2151（ディスク。Z80 無し） */
	CEMU_AC_BOARD_VSYSTEM = 14, /* Video System aerofgt: YM2610 I/O、ラッチ NMI */
	CEMU_AC_BOARD_TAITO_YM2610 = 15, /* F2/B/dual68: YM2610 @E000、TC0140SYT @E200、バンク F200 */
	CEMU_AC_BOARD_IREM_M72 = 16,     /* M72/M84: YM2151 I/O 00/01、ラッチ 02/80、サンプル DAC */
	CEMU_AC_BOARD_IREM_M92 = 17,     /* M92: 暗号化 NEC V35 + YM2151 + IremGA20 */
	CEMU_AC_BOARD_SEGA_SYS1 = 18,    /* System1/2: SN76489×2 @A000/C000、ラッチ @E000 */
	CEMU_AC_BOARD_DECO = 19,         /* Data East: HuC6280 または M6502 音源 + FM（+ OKI） */
	CEMU_AC_BOARD_MEGASYSTEM1 = 20,  /* Jaleco MS1: 第 2 68000（Musashi）+ YM2151 + OKI6295×2 */
	CEMU_AC_BOARD_TAITO_SJ = 21,     /* Taito SJ 基板: AY-3-8910×3 @4800/4802/4804 */
	CEMU_AC_BOARD_TAITO_OPM = 22,    /* Rastan/Asuka: YM2151 @9000、PC060HA @A000、バンク */
	CEMU_AC_BOARD_KONAMI_GX = 23,    /* System GX 基板: 68000＋K054539×2＋K056800 */
	CEMU_AC_BOARD_NAMCO_SYS2 = 24,   /* Namco System 2 基板: M6809＋YM2151＋C140 */
	CEMU_AC_BOARD_NAMCO_SYS1 = 25,   /* Namco System 1 基板: M6809＋YM2151＋CUS30 */
	CEMU_AC_BOARD_NAMCO_SYS86 = 26,  /* Namco System 86 基板: HD63701 MCU＋YM2151＋CUS30 */
	CEMU_AC_BOARD_NAMCO_WSG = 27,    /* Namco WSG/15XX: CUS30 MAPPY（配線時は M6809/Z80） */
	CEMU_AC_BOARD_TOAPLAN1 = 28,     /* Toaplan1: Z80 + YM3812、コマンドは共有 RAM 8000 */
	CEMU_AC_BOARD_KONAMI_SCRAMBLE = 29, /* Scramble/scobra 基板: Z80＋AY×2 I/O 10/20/40/80 */
	CEMU_AC_BOARD_KONAMI_TIMEPLT = 30,  /* Time Pilot/jungler 基板: Z80＋AY×2 メモリ 4/5/6/7000 */
	CEMU_AC_BOARD_KONAMI_GX400 = 31,    /* GX400/nemesis 基板: Z80＋AY×2 @E0xx（＋K005289 stub） */
	CEMU_AC_BOARD_TECHNOS_DDRAGON2 = 32, /* DD2/chinagat 基板: Z80＋YM2151@8800＋OKI@9800 */
	CEMU_AC_BOARD_IREM_M62 = 33,        /* M62: M6803 + AY×2（CPU 未実装 → 無音） */
	CEMU_AC_BOARD_SEGA_SCSP = 34,       /* Model 2/3: SCSP（68K ホスト無し → 無音） */
	CEMU_AC_BOARD_KONAMI_RF5C400 = 35,  /* Hornet/GTI Club 基板: 68000＋RF5C400＋K056800 */
	CEMU_AC_BOARD_SNK_OPL = 36,         /* SNK68: Z80 + YM3812 I/O 00/20、ラッチ@F800 NMI */
	CEMU_AC_BOARD_SEIBU_OPL = 37,       /* Seibu raiden: YM3812+OKI（SEI80BU 復号） */
	CEMU_AC_BOARD_KONAMI_K7232 = 38,    /* Z80＋YM2151＋K007232 stub 配線（scontra/crimfght/twin16） */
	CEMU_AC_BOARD_KONAMI_HCASTLE = 39,  /* Z80＋YM3812@A000＋K007232 stub 配線（hcastle） */
	CEMU_AC_BOARD_TECMO16 = 40,         /* Z80+YM2151@FC04 + OKI@FC00 + ラッチ@FC08 NMI */
	CEMU_AC_BOARD_FLSTORY = 41,         /* Taito flstory: Z80+AY@C800 + MSM5232@CA00 + ラッチ@D800 NMI */
	CEMU_AC_BOARD_TERRACRE = 42,        /* Nichibutsu terracre YM3526 または armedf/terraf YM3812 */
	CEMU_AC_BOARD_ROBOKID = 43,         /* UPL robokid: Z80+YM2203×2 I/O 00/80、ラッチ@E000 */
	CEMU_AC_BOARD_BATTLANTIS = 44,      /* Konami battlantis: Z80+YM3812×2 @A000/C000、ラッチ@E000 */
	CEMU_AC_BOARD_ALPHA68K2 = 45,       /* Alpha 68K-II 基板: Z80＋YM2203＋YM2413＋DAC（skyadvnt/gangwars） */
	CEMU_AC_BOARD_ATARI_SYS1 = 46,      /* Atari System1 基板: M6502＋YM2151（＋POKEY stub）JSA */
	/* Seta/Allumer と Cave に音源 CPU は無い。メイン 68000 が PCM を直接叩く。
	   Seta は X1-010 RAM 窓、Cave は 2 ポート YMZ280B。47 は Taito F3 が使用済み。 */
	CEMU_AC_BOARD_M68K_PCM = 48,
	/* Raizing / Eighting は同一 68000+Z80 音源を 4 改訂し、毎回チップ配置が動いた
	   （MAME raizing.cpp / raizing_batrider.cpp）。raizingType_ がマップを選ぶ:
	     1 mahou     Z80 + YM2151 @E000 + OKI @E004、共有 RAM メールボックス
	     2 bgaregga  + バンク Z80 ROM、GAL サンプルバンク、ラッチ IRQ @E01C
	     3 batrider  同じ区画を I/O ポートへ、OKI×2、ラッチ NMI
	     4 bbakraid  YMZ280B ポート 80/81、ラッチ NMI + 周期 IRQ0 */
	CEMU_AC_BOARD_RAIZING = 49
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
	int LoadRomsHornet(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsSeibu(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsM62(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsSegaM1(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadRomsRaizing(CEmuZipFs* fs, const CEmuGameEntry* ge);
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
	void SegaMidiInjectSongMode(uint16_t cmd, int hiFirst);
	/* 68000 がまだ読んでいないホスト MIDI バイト。0 はファームが曲選択を受け取済み。
	   再注入は再生中の曲を止めてやり直すだけ。 */
	int SegaMidiFifoPending() const { return segaMidiHead_ != segaMidiTail_; }
	void SegaMidiPush(uint8_t b);
	uint8_t SegaUartRead(unsigned reg);
	void SegaUartWrite(unsigned reg, uint8_t data);
	unsigned Sega68Read16(unsigned addr);
	unsigned Sega68Read8(unsigned addr);
	void Sega68Write16(unsigned addr, uint16_t v);
	void Sega68Write8(unsigned addr, uint8_t v);
	/* Model 2A/2B/2C/3 音源: 68000 + SCSP（MAME model2_snd） */
	unsigned Sega2ARead16(unsigned addr);
	unsigned Sega2ARead8(unsigned addr);
	void Sega2AWrite16(unsigned addr, uint16_t v);
	void Sega2AWrite8(unsigned addr, uint8_t v);
	unsigned Sega2ASampleOffset(unsigned addr) const;
	int LoadRomsSegaScsp(CEmuZipFs* fs, const CEmuGameEntry* ge);
	void Sega2AInjectSong(uint16_t cmd);
	int SegaScspMidiPending() const;
	/* Hornet / GTI Club 基板: 68000＋RF5C400＋K056800（MAME hornet/gticlub） */
	unsigned HornetRead16(unsigned addr);
	unsigned HornetRead8(unsigned addr);
	void HornetWrite16(unsigned addr, uint16_t v);
	void HornetWrite8(unsigned addr, uint8_t v);
	void HornetInjectSong(unsigned code);
	void HornetTickTimer(int cycles);
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

	/* 「メイン CPU」からのサウンドコマンドラッチ */
	void SetSoundCommand(uint8_t cmd);
	void SetSoundCommandWord(uint16_t cmd);
	/* System GX: 生 4 バイト K056800 ホストパケット + ドアベル */
	void GxHostInject(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3);
	uint8_t SoundCommand() const { return soundCmd_; }
	uint16_t SoundCommandWord() const { return soundCmdWord_; }
	int SoundCmdPending() const { return soundCmdPending_; }
	/* NMI/IRQ 配送のワンショット端。割り込み取得でクリア */
	int IrqPulsePending() const { return irqPulse_; }
	int TakeIrqPulse() { int v = irqPulse_; irqPulse_ = 0; return v; }
	void PulseIrq() { irqPulse_ = 1; }
	/* K053260 SH1→NMI: FA00 で武装。Z80 が HALT 中に DeliverIrqs が撃つ */
	int KonamiSh1NmiArm() const { return konamiSh1NmiArm_; }
	void ClearKonamiSh1NmiArm() { konamiSh1NmiArm_ = 0; }
	uint8_t KonamiSoundCtrl() const { return konamiSoundCtrl_; }
	CChip* KonamiPcm2() { return konamiPcm2Addr_ ? pcm2_ : NULL; }
	/* WSG: 最初のラッチまで周期 NMI を抑止（ブートハンドシェイク） */
	int WsgNmiEnable() const { return wsgNmiEnable_; }
	void SetWsgNmiEnable(int v) { wsgNmiEnable_ = v ? 1 : 0; }
	/* Mappy 期 15XX: M6809 音源 CPU（subtype wsg6809）。Z80 Pac-Man WSG ではない */
	int WsgMappy() const { return wsgMappy_; }
	int Wsg63701() const { return wsg63701_; }
	/* Pengo / Pengo2: メイン Z80 + Namco 3 声 WSG @9000（Galaga NMI ではない）。
	   マーカは WSG 未使用の sys16RomBoard_ — CHardAc にフィールドを増やさない。 */
	int PengoWsg() const { return sys16RomBoard_ == 0x5047u; }
	unsigned Sys16RomBoard() const { return sys16RomBoard_; }
	uint8_t ToaplanYmPort() const { return toaplanYmPort_; }
	int ToaplanKaneko() const { return toaplanKaneko_ == 1; }
	int SlapfghtAy() const { return toaplanKaneko_ == 3; }
	uint16_t ToaplanMail() const { return toaplanKaneko_ == 2 ? 0xc000u : 0x8000u; }
	uint16_t ToaplanReady() const { return toaplanKaneko_ == 2 ? 0xc002u : 0x8001u; }
	int TecmoOpl() const { return tecmoOpl_; }
	/* Sailor Moon / Air Gallet: ラッチ 0 の NMI がメインループ tick */
	void CaveEmptyLatch() { soundCmd_ = 0; soundCmdWord_ = 0; }
	int QsZn() const { return qsZn_; }
	/* 遅延した ZN 第 2 バイト NMI をパルスすべきとき一度 True */
	int ZnTakeDeferredNmi()
	{
		if (!znDeferredNmi_) return 0;
		znDeferredNmi_ = 0;
		return 1;
	}
	void ClearSoundCmdPending() { soundCmdPending_ = 0; }
	void SetToaplanTimerA(int v) { toaplanTimerA_ = v ? 1 : 0; }
	int M72IoAlt() const { return m72IoAlt_; }

	/* CPS1 メモリマップミラー（F000/F001）。PortOut と MemWrite から同期 */
	void MemWrite(uint16_t addr, uint8_t data);
	uint8_t MemRead(uint16_t addr);

	uint64_t CpuCycles() const { return cpuCycles_; }
	void AddCpuCycles(uint64_t n) { cpuCycles_ += n; }

	int board_;
	int cpuHz_;
	int opmHz_;
	unsigned opmWrites_; /* YM2151 書込。GNG では YM2203 書込 */

	/* YM2203×2（GNG）。OPM ボードでは NULL */
	CChip* Chip2() { return chip2_; }
	const CChip* Chip2() const { return chip2_; }
	/* 第 3 ボイス（Taito SJ AY#3）。他では NULL */
	CChip* Chip3() { return chip3_; }
	const CChip* Chip3() const { return chip3_; }
	/* auxKind_: 0=YM2203（GNG）、1=SN76489（System1）、2=AY-3-8910（SJ/scramble 等） */
	int AuxKind() const { return auxKind_; }
	/* Konami AY タイマポート（scramble/timeplt ポート B） */
	uint8_t KonamiAyTimer() const;
	/* MAME nemesis_portA_r: bit0-3 周期タイマ、bit4/6/7 High */
	uint8_t Gx400PortA() const;

	/* Taito TC0140SYT / PC060HA マスタ側 — SetSoundCommand が使う */
	int SytNmiEnabled() const { return sytNmiEnabled_; }
	uint8_t SytStatus() const { return sytStatus_; }
	uint8_t SytSubMode() const { return sytSubMode_; }
	int Bank() const { return bank_; }
	unsigned SoundRomSize() const { return soundRomSize_; }
	unsigned PcmRomSize() const { return pcmRomSize_; }
	const uint8_t* PcmRomData() const { return pcmRom_; }

	/* Sega System1 周期音源 IRQ は 0x0038 にそれらしいベクタが要る。ラッチ NMI は 0x0066。生 ROM バイトを出す。 */
	uint8_t PeekMem(uint16_t addr) const { return mem_[addr]; }

	/* MAME m72_state::fake_nmi — NMI ハンドラが空の M72 は PCM バイト転送が外部ハードなのでここで行う */
	void M72PumpSample();

	/* ---- Jaleco Mega System 1 / Konami System GX: 68000 音源 ----
	   共有 Musashi（cemu_m68k_bus.cpp）。Z80 ではない。 */
	int Ms1Active() const { return ms1Rom_ != NULL; }
	unsigned Ms1RomSize() const { return ms1RomSize_; }
	const uint8_t* Ms1Rom() const { return ms1Rom_; }
	unsigned PcmRom2Size() const { return pcmRom2Size_; }
	unsigned Ms1Read8(unsigned addr);
	unsigned Ms1Read16(unsigned addr);
	void Ms1Write8(unsigned addr, uint8_t v);
	void Ms1Write16(unsigned addr, uint16_t v);
	/* System A/B は 4（soundlatch_w）、System C は 6（soundlatch_c_w）。
	   System GX: 1 = K056800、2 = K054539 タイマ。 */
	int Ms1LatchIrqLevel() const { return ms1LatchLevel_; }
	int Ms1IrqLevel() const;
	void Ms1AckIrq();
	CChip* Oki(int idx) { return idx ? pcm2_ : pcm_; }
	const CChip* Oki(int idx) const { return idx ? pcm2_ : pcm_; }
	unsigned Ms1OkiWrites() const { return ms1OkiWrites_; }
	unsigned Ms1LatchReads() const { return ms1LatchReads_; }
	unsigned GxPcmWrites() const { return gxPcmWrites_; }

	/* ---- Data East 音源（HuC6280 cninja マップ、または M6502 karnov/dec0） ----
	   HuC6280 21bit 物理（MAME cninja.cpp sound_map）:
	     000000-00FFFF  ROM
	     100000-100001  YM2203
	     110000-110001  YM2151
	     120000-120001  OKIM6295 #1
	     130000-130001  OKIM6295 #2
	     140000         deco_146 soundlatch 読
	     1F0000-1F1FFF  RAM
	   ラッチ待ち → IRQ1。YM2151 irq → IRQ2。
	   M6502 マップ（decoCpuKind_ 1/2/3）は DecoM6502Read8。 */
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

	/* ---- Irem M92: 暗号化 NEC V35 音源 CPU（CEmu/vendor/v35） ----
	   MAME irem/m92.cpp sound_map、20bit:
	     00000-1FFFF  ROM
	     A0000-A3FFF  RAM
	     A8000-A803F  IremGA20（umask16 0x00ff、偶数バイトのみ）
	     A8040-A8043  YM2151（A8040=addr、A8042=data）
	     A8044        soundlatch 読 / アクノリッジ書
	     A8046        soundlatch2 書（メイン CPU への応答）
	     FFFF0-FFFFF  soundcpu+0x1FFF0 のリセットベクタミラー */
	int M92Active() const { return m92Rom_ != NULL; }
	V35Cpu* M92Cpu() { return v35_; }
	uint8_t M92Read8(uint32_t addr);
	void M92Write8(uint32_t addr, uint8_t v);
	/* YM2151 IRQ 線を INTP0 へ再サンプル。各 CPU スライス前に呼ぶ */
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

	/* ---- Raizing / Eighting 基板（mahou, bgaregga, batrider, bbakraid） ---- */
	int RaizingType() const { return raizingType_; }
	/* 68000 の 1 回の move.l と同じく両ラッチを同時に。Type 1 にラッチは無く、対は共有 RAM メールボックスへ。 */
	void RaizingPostCommand(uint8_t cmd, uint8_t data);
	/* bgaregga と batrider は 0x55 を見て 0xAA で答えるまで全コマンドを捨てる。その後ワーク RAM に非 0 ready を置き、68000 が聞いていない中で答えが残る唯一の場所。Type 1 と Battle Bakraid はコマンドループへ直入。 */
	int RaizingHandshakeAcked() const
	{
		if (raizingType_ == 2) return mem_[0xc011] != 0;
		if (raizingType_ == 3) return mem_[0xc00a] != 0;
		return 1;
	}
	/* Type 1 メールボックスは Z80 が C000 へ 0xFF を書き戻すと空く */
	int RaizingMailboxIdle() const { return mem_[0xc000] == 0xff; }
	/* シーケンサとチップが全ボイスを落とした — 終端に当たったジングル／スクリプト。ループ BGM は busy のまま。 */
	int RaizingTrackIdle();
	/* 保持ラッチ IRQ0（bgaregga）／ラッチ NMI（batrider, bbakraid） */
	int RaizingLatchPending() const { return raizingLatchPending_; }
	int RaizingNmiPending() const { return raizingNmiPending_; }
	void ClearRaizingNmi() { raizingNmiPending_ = 0; }

private:
	uint8_t RaizingPortIn(uint8_t port);
	void RaizingPortOut(uint8_t port, uint8_t data);
	uint8_t RaizingMemRead(uint16_t addr);
	void RaizingMemWrite(uint16_t addr, uint8_t data);
	void RaizingSetZ80Bank(unsigned entry);
	void RaizingOkiBankW(unsigned offset, uint8_t data);

	void SytMasterWriteCommand(uint8_t cmd);
	void SytSlavePortW(uint8_t data);
	void SytSlaveCommW(uint8_t data);
	uint8_t SytSlaveCommR();
	void SytUpdateNmi();
	void SetBank(int bank);

	uint8_t mem_[0x10000];
	Ay_Cpu* cpu_;
	CChip* chip_;
	CChip* chip2_; /* GNG の第 2 YM2203。System1 は SN2。Taito SJ は AY#2 */
	CChip* chip3_; /* Taito SJ の AY#3 */
	CChip* pcm_;
	int sampleRate_;
	uint64_t cpuCycles_;
	uint8_t soundCmd_;
	uint16_t soundCmdWord_;
	int soundCmdPending_;
	int irqPulse_;
	int wsgNmiEnable_; /* Namco WSG: ブート／注入後の周期 NMI */
	int wsgMappy_;     /* 1 = Mappy/15XX M6809 マップ（対 Pac-Man Z80） */
	int wsg63701_;     /* 1 = HD63701＋CUS30 MAPPY 基板（pacland/skykid） */
	int qsZn_; /* Capcom ZN: ラッチ+NMI（MAME zn.cpp）。CPS2 共有 RAM メールボックスではない */
	int qsKabuki_; /* CPS1 QSound Kabuki: mem_ はオペコード。読込は qsKabukiData_ */
	uint8_t* qsKabukiData_; /* 0x8000 データ復号面 */
	/* ZN QSound: PSX→Z80 はラッチ+NMI 4 バイト FF,00,hi,lo。F015/F016 リングが対を格納。FF 00 はボイスリセット。 */
	uint8_t znQueue_[4];
	int znQueueLen_;
	int znQueuePos_;
	int znDeferredNmi_; /* 現ハンドラ復帰後に次 NMI を武装 */
	uint8_t ymAddr_; /* CPS1 メモリマップのアドレスラッチ @ F000 */
	uint8_t gngYmAddr_[2]; /* GNG YM2203 アドレスラッチ（E000 / E002） */
	int gngCommandoMap_; /* 1: Commando/ExedExes 風 4000/6000/8000 マップ */
	int gngGaidenMap_;   /* 1: Tecmo gaiden F000/F810/F820/FC20（+OKI）マップ */
	uint8_t hangYmAddr_; /* Hang-On YM2203 アドレスラッチ @ D000 */
	uint64_t abStatusPulseSlot_; /* After Burner: 合成 TimerA パルス枠 */
	CChip* pcm2_;  /* Mega System 1 第 2 OKI6295（0x0C0000） */
	uint8_t* pcmRom_;
	unsigned pcmRomSize_;
	uint8_t* pcmRom2_;
	unsigned pcmRom2Size_;
	int pcmKind_; /* 0=なし 1=SegaPCM 2=OKI 3=K053260 4=K054539 5=RF5C68 6=IremDAC 7=GA20 8=C140 9=C30 */
	int auxKind_;  /* chip2_/chip3_ 種別 — AuxKind() 参照 */
	int mainIsYm2203_; /* Hang-On / GNG 風の破棄 */
	int mainIsYm2610_;
	int mainIsYm2612_;

	/* バンク音源 ROM（Taito F2/B/dual68、Rastan、V-System）。全イメージを残し窓をバンク書込で付け替える。Taito は 0x4000 で 0x4000 バイト、V-System は 0x8000 で 0x8000 バイト。 */
	uint8_t* soundRom_;
	unsigned soundRomSize_;
	int bank_;
	int bankLoaded_;
	unsigned bankBase_;
	unsigned bankSize_;

	/* Konami K053260 ボード: カタログ opm_addr / pcm_addr（既定 F800 / FC00）。
	   K054539（moo/bucky）: 既定 EC00 / E000 + バンク F800 + K054321 @F000。 */
	unsigned konamiOpmAddr_;
	unsigned konamiPcmAddr_;
	unsigned konamiBankAddr_;
	unsigned konamiPcmWindow_; /* チップ K053260=0x40、K054539=0x230 */
	/* mystwarr.cpp 系の第 2 K054539 窓（0=1 チップ） */
	unsigned konamiPcm2Addr_;
	/* バンク／コントロールポートへ最後に書いたバイト。mystwarr sound_ctrl_w は bit4 で K054539 タイマを Z80 NMI へゲート。 */
	uint8_t konamiSoundCtrl_;
	int konamiSh1NmiArm_; /* FA00 書込で SH1 NMI を 1 発武装（Simpsons/Punk Shot） */

	/* Mega System 1 68000 音源 */
	uint8_t* ms1Rom_;
	unsigned ms1RomSize_;
	/* Model 2A/3: 0x400000 コントロールラッチがサンプルバンクを選ぶ */
	int scspSampleBank_;
	/* Hornet 対 GTI Club 音源マップ（RAM/RF5C400 基点が違う） */
	int hornetGti_;
	int hornetTimerEn_;   /* soundtimer_en_w: bit0 クリアで IRQ1 許可 */
	int hornetTimerIrq_;  /* 待ちの周期 IRQ1 */
	int hornetTimerAcc_;  /* 次の 344.5Hz 端へ向けた CPU サイクル */
	uint8_t* ms1Ram_;      /* 0x20000 を 0x0E0000 にマップ */
	int ms1LatchLevel_;
	int ms1LatchIrq_;      /* soundlatch_w が IPL 線をアサート */
	uint16_t ms1LatchIn_;  /* メイン → 音源（soundlatch[0]） */
	uint16_t ms1LatchOut_; /* 音源 → メイン（soundlatch[1]） */
	unsigned ms1OkiWrites_;
	unsigned ms1LatchReads_;

	/* Data East HuC6280 / M6502 音源 */
	H6280Cpu* h6280_;
	M6502Cpu* m6502_;
	uint8_t* decoRom_;
	unsigned decoRomSize_;
	uint8_t* decoRam_;     /* 物理 1F0000 の 0x2000（H6280）または ZP/page0（M6502） */
	uint8_t decoYm2203Addr_;
	uint8_t decoYm2151Addr_;
	uint8_t atariJsaIo_; /* Atari Sys1 /WRIO ラッチ（セルフテスト bit7 エコー） */
	unsigned decoLatchReads_;
	unsigned decoOkiWrites_;
	unsigned decoChanWrites_;
	uint8_t decoM6502Ram_[0x800]; /* M6502 ワーク RAM */

	/* Irem M92 V35 音源 */
	V35Cpu* v35_;
	uint8_t* m92Rom_;
	unsigned m92RomSize_;
	uint8_t* m92Ram_;      /* 0x4000 を 0xA0000 にマップ */
	uint8_t m92Latch_;     /* generic_latch_8 メイン → 音源 */
	int m92LatchPending_;  /* separate_acknowledge: A8044 書込でクリア */
	uint8_t m92Latch2_;    /* 音源 → メイン（upd71059c ir3）。聞く側は無い */
	unsigned m92LatchReads_;
	unsigned m92Ga20Writes_;
	int m92BomberGatePatch_; /* 1: 11F8 が bomberman の PUSH CX フォールスルー */
	uint8_t m92EncryptedRet_; /* C3 RET に復号される暗号バイト */
	/* Irem Software Guard オペコード表の私有コピー。FINT 第 2 バイトは平文（MAME fetch()。fetchop() ではない）。table[0x92] を書き換えない。 */
	uint8_t m92Decrypt_[256];
	int m92DecryptValid_;
	/* リセットベクタが暗号 JMP FAR なら 0x20（Rev 3.40+ BGM = 0x20+n）。初期 FA/CLI ブート（bmaster/lethalth/gunforce）は 0。 */
	uint8_t m92SongCmdBase_;
	/* 1: INTP0 シーケンサがチャネル BGM（MOV BP,#0 / tick）— rtypeleo / hook / nbbatman 等。0 は uccops 風ノートリスト BGM。 */
	uint8_t m92ChannelBgm_;
	/* 1: コマンドリングが 16×word @0AF0（firebarr/nbbatman 等）。0 は古典バイトリング @08C0（hook/rtypeleo/uccops）。 */
	uint8_t m92WordQueue_;
	/* 1: アイドルが [0C31]==3 で回る（メイン↔音源 ready ハンドシェイク） */
	uint8_t m92ReadyWait_;
public:
	int M92BomberGatePatch() const { return m92BomberGatePatch_; }
	uint8_t M92EncryptedRet() const { return m92EncryptedRet_; }
	uint8_t M92SongCmdBase() const { return m92SongCmdBase_; }
	uint8_t M92ChannelBgm() const { return m92ChannelBgm_; }
	uint8_t M92WordQueue() const { return m92WordQueue_; }
	uint8_t M92ReadyWait() const { return m92ReadyWait_; }

	/* ---- Namco System 1/2 M6809 音源（vendor/mc6809） ----
	   Sys1 マップ（MAME namcos1 sound_map）:
	     0000-3FFF バンク ROM、4000-4001 YM2151、5000-53FF CUS30、
	     7000-77FF TRI-RAM（cmd @7100）、8000-9FFF RAM、C000-FFFF ROM、
	     C000 バンク書、E000 IRQ ack。YM→FIRQ、VBlank→IRQ。
	   Sys2: 骨格は同じ。CUS30 の代わり／併設で C140 @7000。 */
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
	/* Sys2 曲表レコード種別（$20=BGM ゲート、$64=alt、$21=バリアント）。
	   Sys2SongInfo はレコード番地とヘッダ peek も返す。 */
	int Sys2SongRecType(unsigned songLo) const;
	int Sys2SongInfo(unsigned songLo, unsigned* recAddr, uint8_t* hdr, unsigned hdrCap) const;
	const uint8_t* NamcoTriRam() const { return namcoTriRam_; }

	/* ---- Namco C352 + H8/3002（System 12 / ND-1） ----
	   Sys12 マップ（MAME namcos12 sub_program_map）:
	     000000-07FFFF  H8 プログラム（WORD_SWAP flash）
	     080000-08FFFF  共有 RAM（メインとのメールボックス）
	     280000-287FFF  C352
	     300000-300003  入力／待ち stub
	   ND-1 マップ（namcond1 h8rwmap）:
	     000000-07FFFF  ROM
	     200000-20FFFF  共有 RAM
	     A00000-A07FFF  C352
	   古典 MAME H8 リセットは *(u32*)0 から（ブートストラップ=初期 SP）。 */
	int H8Active() const { return h8_ != NULL && h8Rom_ != NULL; }
	int M37702Active() const { return m37702_ != NULL && (m37702IntRom_ != NULL || h8Rom_ != NULL); }
	int M37702Soft() const { return m37702Soft_; }
	int M37702C140() const { return m37702C140_; }
	int SnkMapKind() const { return snkMapKind_; } /* 0=snk68 I/O、1=古典 dual OPL メモリ */
	int KonamiK7232Map() const { return konamiK7232Map_; }
	/* 0 = Rastan/Asuka（YM @9000、PC060HA @A000）
	   1 = darius（YM2203 @9000 + YM2203 #2 @A000、PC060HA @B000）
	   2 = kikikai（YM2203 @C000、曲バイトは共有 RAM 9FFF、vblank IRQ）
	   3 = tokio（YM2203 @B000、ラッチ @9000、NMI A800/A000）
	   4 = bublbobl（YM2203 @9000、YM3526 @A000、ラッチ @B000、NMI B001/B002）
	   5 = lsasquad（YM2203 @A000 + AY @C000、ラッチ D000、NMI D400/D800）
	   6 = lkage（YM2203×2 @9000/@A000、ラッチ B000、NMI B001/B002）
	   FLSTORY 上: 1 = msisaac AY×2+MSM。3 = nycaptor AY×2+MSM。 */
	int TaitoOpmMap() const { return taitoOpmMap_; }
	int MsisaacMap() const { return (board_ == CEMU_AC_BOARD_FLSTORY && taitoOpmMap_ == 1) ? 1 : 0; }
	int NycaptorMap() const { return (board_ == CEMU_AC_BOARD_FLSTORY && taitoOpmMap_ == 3) ? 1 : 0; }
	int Cop01Ay() const { return (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 4) ? 1 : 0; }
	int VsIoKind() const { return vsIoKind_; }
	int MagmaxAy() const { return (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 5) ? 1 : 0; }
	int BombjackAy() const { return (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 6) ? 1 : 0; }
	int CalorieAy() const { return (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 7) ? 1 : 0; }
	int SolomonAy() const { return (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 8) ? 1 : 0; }
	int HalleysAy() const { return (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 9) ? 1 : 0; }
	int PbactionAy() const { return (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 10) ? 1 : 0; }
	int ChaknpopAy() const { return (board_ == CEMU_AC_BOARD_TAITO_SJ && vsIoKind_ == 11) ? 1 : 0; }
	int NbAyIo() const { return Cop01Ay() || MagmaxAy(); }
	int AlphaNmiMask() const { return alphaNmiMask_; }
	unsigned AlphaOpllWrites() const;
	void AlphaMixOpll(int16_t* stereo, int frames);
	uint8_t SnkStatus() const { return snkStatus_; }
	void SnkSetYmIrq(int which, int on);
	/* マップ terracreMap_: 0=terracre C000 RAM。1=armedf/terraf F800。2=cclimbr2/legion C000-FFFF */
	int TerracreMap() const { return terracreMap_; }
	int M68kPcmKind() const { return m68kPcmKind_; }
	/* 68000 スライスループから呼ぶ。vblank 線をオーディオバッファではなく CPU と同期させる */
	void M68kPcmTickVblank(int cycles);
	unsigned M68kPcmRead8(unsigned addr);
	void M68kPcmWrite8(unsigned addr, uint8_t v);
	uint8_t* GetMs1Ram() { return ms1Ram_; }
	unsigned GetM68kRamSize() const { return m68kRamSize_; }
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
	int H8MapKind() const { return h8MapKind_; } /* 種別 0=sys12、1=nd1 */
	int M37702MapKind() const { return m37702MapKind_; } /* 種別 0=sys11、1=na1/nb、2=sys22 */
	/* 素の System 22: C74 マスク ROM。自分の Timer A0 だけで駆動 */
	int M37702MaskRom() const { return m37702MaskRom_; }
	int M37702McuKind() const { return m37702McuKind_; }
	uint8_t C352ReadLane(unsigned off) const;
	const uint8_t* H8Shared() const { return h8Shared_; }

	/* ---- Namco System 86 HD63701 + YM2151 + CUS30 ----
	   MAME namcos86 / FBNeo d_namcos86:
	     0000-001F  HD63701 内部レジスタ
	     0080-00FF  内部 RAM
	     1000-13FF  CUS30
	     1400-1FFF  ワーク RAM
	     YM2151     @2000（rthunder）/ @2800（genpeitd）等
	     4000-BFFF  外部 MCU ROM（genpeitd/rthunder）
	     F000-FFFF  CUS60 内部 ROM */
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
	uint8_t* m37702IntRom_; /* C69/C74/C76 16KB マスク ROM @ 0xC000 */
	unsigned m37702IntRomSize_;
	uint8_t* h8Shared_;
	uint8_t* m37702LocalRam_; /* NA1 窓 0x3000-0xAFFF */
	uint16_t m37702Mailbox_[8];
	int h8MapKind_;
	int m37702MapKind_; /* 種別 0=sys11 C76＋C352、1=na/nb C69＋C140、2=sys22 */
	int m37702MaskRom_; /* 内部 C74 付き sys22（Super System 22 ではない） */
	int m37702McuKind_; /* Namco MCU ラベル: 0 不明、他は 69/70/74/75/76 */
	int h8WordSwap_;
	unsigned h8C352Writes_;
	uint8_t h8C352Hi_;
	int h8C352HiValid_;
	uint16_t h8C352Shadow_[0x400];
	int m37702Soft_; /* ファミリ選択済みだがコア接続に失敗している間 1 */
	int m37702C140_; /* NA-1/NB-1: C219/C140 代用（C352 ではない） */
	int snkMapKind_; /* 0=snk68 YM3812 I/O+NMI。1=古典メモリマップ dual OPL+IRQ */
	uint8_t snkStatus_; /* 古典 F800 ステータス（ym1|ym2|busy|cmd） */
	int terracreMap_; /* マップ 0=terracre、1=armedf/terraf、2=cclimbr2/legion */
	int tecmoOpl_;    /* 0=YM2151 FC04。1=rygar YM3526@8000 RAM@4000 ラッチ@C000
	                     2=gemini YM3812@A000 RAM@8000 ラッチ@C000
	                     3=spbactn YM3812 FC04。4=tbowl YM3812 D000+D800 RAM@C000 ラッチ@E010
	                     5=Cave agallet/sailormn: I/O YM50 + OKI60/80、ラッチ NMI
	                     6=wc90 YM2608 @F800、RAM F000、ラッチ FC10 NMI */
	/* M68K_PCM: 1 = Seta X1-010 RAM 窓、2 = Cave YMZ280B ポート対。
	   ゲームごとにチップとワーク RAM のデコードが違うので、共有推測ではなくゲーム別仕様（MAME seta.cpp / seta2.cpp / cave.cpp）。 */
	int m68kPcmKind_;
	unsigned m68kPcmAddr_;
	unsigned m68kPcmSpan_;
	unsigned m68kRamAddr_;
	unsigned m68kRamSize_;
	unsigned m68kIrqAddr_;   /* Cave irq_cause 窓。無ければ 0 */
	unsigned ms1RamAlloc_;   /* ms1Ram_ の実 malloc バイト */
	/* X1-010 ワード書込は上位バイトを捨てる。MAME はここに置き、読戻しがプログラムの全ワードを返す */
	uint8_t m68kHiWord_[0x2000];
	uint64_t m68kVblankAcc_;
	int m68kVblankLevel_;
	int m68kVblankPending_;
	int flstoryNmiEn_; /* DA00 許可ゲート（MAME soundnmi in_set<1>） */

	/* Raizing / Eighting。raizingType_ 0 = Raizing ボードではない */
	int raizingType_;
	/* GAL サンプルバンク。64K 窓ごとに 4bit エントリ（CEmuChipOki6295SetBankTable）。チップが読むライブ格納。 */
	unsigned raizingOkiBank_[2][8];
	uint8_t raizingLatch_[2];  /* 68000 → Z80、ポート 48/4A または E01C */
	uint8_t raizingLatchOut_[2]; /* Z80 → 68000、ポート 40/42（診断） */
	int raizingLatchPending_;
	int raizingNmiPending_;
	unsigned raizingLastKeyOns_;
	int raizingIdlePolls_;

	HD63701Cpu* hd63701_;
	uint8_t* hd63701Rom_;      /* 64K MCU アドレス空間イメージ */
	unsigned hd63701RomSize_;
	uint8_t hd63701Ram_[0x1000];
	int hd63701MapKind_;       /* マップ 0=hopmappy、2=genpeitd、3=rthunder、4=wndrmomo */
	uint16_t hd63701YmBase_;
	unsigned hd63701YmWrites_;

	/* Namco Sys1/2 M6809 音源CPU */
	struct mc6809* namcoM6809_;
	uint8_t namcoTriRam_[0x800];
	uint8_t namcoWorkRam_[0x2000]; /* 窓 $8000-$9FFF */
	uint8_t namcoCus30_[0x400];    /* Sys1 $5000 ミラー（RAM テスト読戻し） */
	unsigned namcoBank_;
	uint8_t namcoYmAddr_;
	int namcoIrqAssert_;
	int namcoFirqAssert_;
	uint64_t namcoNextVblank_;
	/* Sys2 DPRAM 内メールボックス基点（$7100 finallap/assault、$7110 burnforc） */
	unsigned namcoMailOff_;
	/* Data East 音源CPU: 0=HuC6280（cninja）、1=M6502 karnov、2=M6502 dec0、3=M6502 actfancr（ROM @4000） */
	int decoCpuKind_;

	/* Konami System GX: K056800 メールボックス + K054539×2（chip_/pcm_）。
	   MAME devices/sound/k056800.cpp + konamigx.cpp gxsndmap。 */
	uint8_t k056800Host_[4]; /* host_to_snd_regs — sound_r オフセット 0..3 */
	uint8_t k056800Snd_[2];  /* snd_to_host_regs — sound_w オフセット 0..1 */
	int k056800IntEn_;
	int k056800Pending_;
	int k056800Irq_;         /* 音源 68000 への IRQ1 */
	uint8_t gxSoundCtrl_;    /* $500001 コントロールのミラー（bit0 で IRQ2 許可） */
	int gxSoundIntck_;
	unsigned gxPcmWrites_;
	uint8_t gxTmsStatus_;

	/* Taito TC0140SYT / PC060HA CIU 状態（MAME shared/taitosnd.cpp） */
	uint8_t sytSlaveData_[4];
	uint8_t sytMasterData_[4];
	uint8_t sytMainMode_;
	uint8_t sytSubMode_;
	uint8_t sytStatus_;
	uint8_t sytNmiEnabled_;

	/* Irem M72 サンプルポインタ（m72_audio_device） */
	uint32_t m72SampleAddr_;
	/* MAME irem_m72 sound_ram_map: M72 本体の音源 Z80 に ROM は無く、64K すべて RAM。V30 がドライバをアップロード。M81/M82/M84 は sound_rom_map（0000-EFFF ROM）。 */
	int m72SoundRam_;
	/* M72 I/O デコード: 0 = rtype/rtype2（YM@00）、1 = poundfor/m99（YM@40、ラッチ@42） */
	int m72IoAlt_;
	/* Sega Sys16 ROM ボード id（カタログ boardtype 0x5358/5521/5704/5797）。
	   uPD7759／音源バンク bit 対応を選ぶ（MAME segas16b upd7759_control_w）。 */
	unsigned sys16RomBoard_;
	/* V-System I/O: 0 = aerofgt（YM@00）、1 = spinlbrk/f1gp（YM@18）、2 = fromanc/welltris（YM@08）、3 = Psikyo gunbird（YM@04、ラッチ@08） */
	int vsIoKind_;
	/* Konami K007232 期 YM2151 マップ: 0 = scontra/twin16（ラッチ A000、YM C000）、1 = crimfght/aliens（ラッチ C000、YM A000）、2 = gradius3（ラッチ F010、YM F030、RAM F800） */
	int konamiK7232Map_;
	int taitoOpmMap_;
	/* Alpha 68K-II: emu2413 OPLL + YM2203 ポート A NMI ゲート */
	void* alphaOpll_;
	uint8_t alphaYmAddr_;
	uint8_t alphaOpllAddr_;
	uint8_t alphaNmiMask_;
	uint8_t alphaPaLatch_;

	/* Taito SJ soundlatch セマフォ + AY#4 ポート B NMI マスク */
	uint8_t sjLatchFlag_;
	uint8_t sjSemaphore2_;
	uint8_t sjNmiMask_;
	uint8_t sjNmiMaskSeen_;
	uint8_t toaplanTimerA_; /* ISR 音楽経路用のソフト Timer-A ドアベル */
	uint8_t toaplanYmPort_; /* YM3812 ベースポート: 00/60/70/A8 */
	uint8_t toaplanKaneko_; /* 1 = snowbros Kaneko I/O。2 = Wardner C000 メールボックス。3 = slapfght AY×2 */
	uint8_t ayAddr_[3];

	/* Seibu SEI80BU 暗号化 Z80 + YM3812 + OKI */
	int seibuEnc_;
	int seibuBank_;
	int seibuSongOr80_; /* raiden: カタログ n → 表 n|0x80。cupsoc は 0x8e */
	uint8_t seibuMain2Sub_[2];
	uint8_t seibuSub2Main_[2];
	int seibuMainPending_;
	int seibuSubPending_;
	int seibuRst10_;
	int seibuRst18_;

	/* Irem M62 M6803 音源CPU */
	struct m6800* m6803_;
	uint8_t m62Port1_;
	uint8_t m62Port2_;
	uint8_t m62AyMAddr_;  /* 直前 AY#0 アドレスラッチ */
	unsigned m62BusMask_; /* 0xffff は M62/M52-large。0x7fff は M52-small */
	int m62MsmReset_;     /* AY#0 ポート B bit0 — MSM5205 をリセット保持 */

	/* Sega Model1 MultiPCM / Model2 SCSP 68000 ホスト */
	int segaM1Audio_;
	uint8_t segaMidiFifo_[64];
	int segaMidiHead_;
	int segaMidiTail_;
	int segaMidiIrq_;
	void SoftPcmInjectSong(uint16_t cmd); /* SCSP / RF5C400 ラッチのみ */

	friend void CEmuAcDestroyMainChip(const CHardAc* hw, CChip* chip);
	friend void CEmuAcDestroyPcmChip(const CHardAc* hw, CChip* pcm);
	friend void CEmuAcDestroyAuxChip(const CHardAc* hw, CChip* aux);
};

void CEmuHardAcSetActive(CHardAc* hw);

/* プレイリストが 0/STOP を渡したとき System GX はループ BGM コードを優先。
   バンク 0x01 の中域添字はボイスバンク（0x05xx）や空の 0x0101 より良い。 */
unsigned CEmuAcPickGxDefaultTitle(const CEmuGameEntry* ge);
int CEmuAcGxTitleIsVoice(const CEmuTitleEntry* t);
