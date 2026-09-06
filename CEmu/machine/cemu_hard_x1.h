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

	/* titleCode: hoot packed 0xSS0000BB → song=SS, bank=BB; plain 0xNN → both. */
	void TriggerPlay(unsigned titleCode);
	/* Stage BGM into mdata/IO without arming the play mailbox (DRIVER boot). */
	void PrestageBgm(unsigned titleCode);
	unsigned OpmWrites() const;
	unsigned AyWrites() const;

	static void UnpackTitle(unsigned titleCode, uint8_t* songOut, uint8_t* bankOut);

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
	int playCmdHoldIrqs_; /* keep cmd level-high for N IRQs, then clear */
	/* YDOS: set when PortIn(0) returned a pending cmd from PATCH wait PC. */
	uint8_t ydosCmdSeen_;
	/* YDOS: after accidental OUT0 pointer build, hide cmd from wait-loop re-entry. */
	uint8_t ydosInhibitReentry_;
	/* Catalog has YDOS*.SYS — gen1 may lack OVL-1 until after PATCH decrypt. */
	uint8_t ydosRom_;

	/* Z80 CTC @1FA0-1FA3 (MAME X1; mirror 1FA8). Guest programs vector base
	   and per-channel IE; driver delivers IM2 as base+2*ch. */
	uint8_t CtcVector(int channel) const;
	int CtcVectorProgrammed() const { return ctcVectorProgrammed_; }
	int CtcIe(int channel) const {
		return (channel >= 0 && channel < 4) ? ctcIe_[channel] : 0;
	}
	/* Timer-mode ch0 period in CPU clocks (0 = not programmed / use default). */
	unsigned CtcTimerPeriodCycles(int channel) const;

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
	uint16_t stageLimit_;

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
