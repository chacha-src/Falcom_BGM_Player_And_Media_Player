#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

class CHardNeo : public CHard {
public:
	CHardNeo();
	~CHardNeo() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int SampleRate() const { return sampleRate_; }
	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);

	Ay_Cpu* Cpu() override { return cpu_; }
	uint8_t* Mem() override { return mem_; }
	CChip* SoundChip() override { return chip_; }

	uint8_t PortIn(uint16_t port) override;
	void PortOut(uint16_t port, uint8_t data) override;
	void MemWrite(uint16_t addr, uint8_t data);
	uint8_t MemRead(uint16_t addr);

	void SetSoundCommand(uint8_t cmd);
	uint8_t SoundCommand() const { return soundCmd_; }
	int SoundCmdPending() const { return soundCmdPending_; }
	int TakeNmiPulse() {
		/* Deliver only while NMI is enabled (OUT $08); keep pulse pending
		   across OUT $18 so OUT $08 can raise it later. */
		if (!nmiEnabled_ || !nmiPulse_)
			return 0;
		nmiPulse_ = 0;
		nmiDelivered_++;
		return 1;
	}
	int NmiEnabled() const { return nmiEnabled_; }
	unsigned NmiDelivered() const { return nmiDelivered_; }
	uint8_t PeekRam(uint16_t a) const { return mem_[a]; }
	uint64_t CpuCycles() const { return cpuCycles_; }
	void AddCpuCycles(uint64_t n) { cpuCycles_ += n; }

	int cpuHz_;
	int ymHz_;

private:
	void SetBankWindow(int window, uint8_t bank);
	uint8_t ReadM1(uint32_t off) const;

	uint8_t mem_[0x10000]; /* Z80 view: fixed+windows+RAM */
	uint8_t* m1Rom_;
	unsigned m1Size_;
	uint8_t bank_[4]; /* windows 0..3 ($F000 / $E000 / $C000 / $8000) */
	Ay_Cpu* cpu_;
	CChip* chip_;
	int sampleRate_;
	uint64_t cpuCycles_;
	uint8_t soundCmd_;
	int soundCmdPending_;
	int nmiPulse_;
	int nmiEnabled_;
	unsigned nmiDelivered_;
	uint8_t* adpcmA_;
	unsigned adpcmASize_;
	uint8_t* adpcmB_;
	unsigned adpcmBSize_;
};

void CEmuHardNeoSetActive(CHardNeo* hw);
