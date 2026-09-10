#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

/* Sharp X1 mucom88 (hoot mucomx1): Z80 + YM2151 + AY-3-8910.
   Mailbox C010/C011/C012; BGM bank OUT(0); OPM @0700; AY @1B00/1C00. */
class CHardX1 : public CHard {
public:
	CHardX1();
	~CHardX1() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int SampleRate() const { return sampleRate_; }
	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);

	Ay_Cpu* Cpu() override { return cpu_; }
	uint8_t* Mem() override { return mem_; }
	CChip* SoundChip() override { return chipOpm_; }
	CChip* ChipAy() { return chipAy_; }

	uint8_t PortIn(uint16_t port) override;
	void PortOut(uint16_t port, uint8_t data) override;

	void MemWrite(uint16_t addr, uint8_t data);
	uint8_t MemRead(uint16_t addr);

	uint64_t CpuCycles() const { return cpuCycles_; }
	void AddCpuCycles(uint64_t n) { cpuCycles_ += n; }

	/* titleCode: hoot packed 0xSS0000BB → song=SS, bank=BB; plain 0xNN → both.
	   ydos: KOEI 0x010000SS uses hi as a loop flag, song in lo. */
	void TriggerPlay(unsigned titleCode);
	/* Stage BGM into mdata/IO without arming the play mailbox (DRIVER boot). */
	void PrestageBgm(unsigned titleCode);
	unsigned OpmWrites() const;
	unsigned AyWrites() const;

	static void UnpackTitle(unsigned titleCode, uint8_t* songOut, uint8_t* bankOut,
		int ydos = 0);

	int cpuHz_;
	int opmHz_;
	int ayHz_;
	int psgOnly_; /* subtype=psg / x1psg */
	uint16_t initPc_;
	uint16_t mdataAddr_; /* BGM stage in RAM; default 0x4000 */
	unsigned mdataSize_; /* bytes to stage; default BGM_SIZE */
	unsigned titleCode_;
	/* Falcom PATCH polls IN 0 / IN 1 for cmd/song (hoot also pokes C010/C011). */
	uint8_t playCmdLatch_;
	uint8_t playSongLatch_;
	uint8_t playSongLatchF_; /* jesus: port 0F is OPMTBL index, port 1 is 0-8 */
	int playCmdHoldIrqs_; /* keep cmd level-high for N IRQs, then clear */
	/* YDOS: set when PortIn(0) returned a pending cmd from PATCH wait PC. */
	uint8_t ydosCmdSeen_;
	/* YDOS: after accidental OUT0 pointer build, hide cmd from wait-loop re-entry. */
	uint8_t ydosInhibitReentry_;
	/* Catalog has YDOS*.SYS — gen1 may lack OVL-1 until after PATCH decrypt. */
	uint8_t ydosRom_;
	/* Telenet OPMDRV: PATCH writes BGM ptr at drv+0x0A but never arms the
	   ISR play-enable at drv+0x0F (luxsor play XOR-clears it). 0 = n/a. */
	uint16_t opmPlayGate_;
	uint16_t opmPlayTempo_;
	void ArmTelenetPlayGate();
	/* Tecnosoft OPMDRV: PATCH does `IN A,(1); … CP FF; AND 0F; CALL drv`.
	   Port 1 is the 0x80/0x81/0xFF command, not the bank index. */
	uint8_t tecnoCmdHi_;
	/* Enix JESUS: PATCH `CP 09` on port 1 (copy descriptor 0-8) then
	   `CP 72` on port 0F (OPMTBL index). Global ids >= 9 must not share
	   the port 1 latch or play is skipped. */
	uint8_t jesusSplitPorts_;
	/* Herzog `LD DE,2802` / revo2 `LD A,(F5F8); CP 03`: each BGM file is
	   its own song. Title 0xSS0000BB must latch SS (0 when hi=0), not lo.
	   Using lo as the track made HZ-BG3/MUS103 index off the end of the file. */
	uint8_t songIdFromHi_;
	/* produce: BGM banks overlay PROG* at mdata_addr after boot. xanaopm:
	   PR.NO0 sits inside the mdata window and must survive PATCH's LDIR
	   of the player to $F000. Skip RAM StageBgm during Prestage only. */
	uint8_t skipPrestageRam_;
	/* produce: the code at mdata_addr IS the player (PROG1). Staging a
	   different PROG* bank on TriggerPlay replaces JP $0285 with game
	   text. Song id still selects the track inside PROG1/OPMMUS5. */
	uint8_t skipTriggerStage_;
	/* Port 1A01 busy/ready toggle (mars JP P / JP M vs Laplace BIT 2). */
	uint8_t psgStatToggle_;
	/* ys2 PATCH `LDIR C000→4000` for songs >= 0x20; mode 0 still reads $4000. */
	uint8_t ys2Mirror4000_;
	/* Laplace PATCH `IN E,(0F)` feeds CTC ch3 time constant, not a song id. */
	uint8_t laplaceCtcF_;
	/* wibarm: port F is the in-file track / $FF overlay, port 1 a play cmd. */
	uint8_t wibarmPortF_;
	/* Falcom xana2: `IN A,(0F); SUB 2` indexes PR.NO2/3/4/5. Port F is hi
	   (family 2/3/4/5), not the staged file id in lo. */
	uint8_t falcomPortF_;
	/* ametruck: `CP 03` on port 1 (file 0-2); port F is the in-file variant. */
	uint8_t ametruckPortF_;
	/* mars PROG `$420A JP P` handshake. Boot EI's before play; the ISR never
	   RETI's so the wait loop never sees the mailbox. Hold IRQs until the
	   PATCH play CALL has returned from PROG. */
	uint8_t marsHoldIrq_;
	uint8_t marsSeenProg_;
	uint8_t marsPlayReady_;

	/* Z80 CTC @1FA0-1FA3 (MAME X1; mirror 1FA8). Guest programs vector base
	   and per-channel IE; driver delivers IM2 as base+2*ch. */
	uint8_t CtcVector(int channel) const;
	int CtcVectorProgrammed() const { return ctcVectorProgrammed_; }
	int CtcIe(int channel) const {
		return (channel >= 0 && channel < 4) ? ctcIe_[channel] : 0;
	}
	/* Timer-mode ch0 period in CPU clocks (0 = not programmed / use default). */
	unsigned CtcTimerPeriodCycles(int channel) const;
	/* Counter-mode divider (0 = channel is not a counter). The X1 wires CTC
	   ZC0 to TRG3, so ch3 in counter mode divides ch0's timer output. */
	unsigned CtcCounterTc(int channel) const;

	enum {
		PLAY_FLAG = 0xC010,
		PLAY_CODE = 0xC011,
		LOAD_FLAG = 0xC012,
		/* hoot default 8K; some titles set mfile_size up to ~0x8000. */
		BGM_SIZE = 32 * 1024
	};

private:
	void FreeBanks();
	void StageBgm(uint8_t index);
	void CtcReset();
	void CtcWrite(int channel, uint8_t data);

	uint8_t mem_[0x10000];
	uint8_t ioport_[0x10000];
	Ay_Cpu* cpu_;
	CChip* chipOpm_;
	CChip* chipAy_;
	int sampleRate_;
	uint64_t cpuCycles_;
	/* Highest exclusive address StageBgm may write (avoid clobbering code). */
	uint32_t stageLimit_;
	/* Laplace title mid word: byte offset into the staged MUSIC file. */
	unsigned bgmStageOff_;

	unsigned char* bgmBank_[128];
	unsigned bgmBankSize_[128];
	int bgmPresent_[128];

	/* CTC: vector base from ch0 bit0=0 write; IE from control bit7. */
	uint8_t ctcVectorBase_;
	int ctcVectorProgrammed_;
	uint8_t ctcIe_[4];
	uint8_t ctcExpectTc_[4];
	uint8_t ctcControl_[4];
	uint8_t ctcTc_[4];
	int ctcTcValid_[4];
	/* XML ctc0/ctc3: hoot use_ctcN vector when guest has not programmed CTC. */
	int xmlCtcVec_[4];
};

void CEmuHardX1SetActive(CHardX1* hw);
