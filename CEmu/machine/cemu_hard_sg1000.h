#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

class CHardSg1000 : public CHard {
public:
	CHardSg1000();
	~CHardSg1000() override;

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

	uint64_t CpuCycles() const { return cpuCycles_; }
	void AddCpuCycles(uint64_t n) { cpuCycles_ += n; }

	/* Star Jacker-class: mailbox @C066, update @5AFA. */
	uint16_t mailboxAddr_;
	uint16_t soundUpdatePc_;
	uint16_t soundMutePc_;
	int cpuHz_;
	int psgHz_;
	unsigned psgWrites_;

private:
	uint8_t mem_[0x10000];
	Ay_Cpu* cpu_;
	CChip* chip_;
	int sampleRate_;
	uint64_t cpuCycles_;
	uint8_t vdpStatus_;
	uint8_t vdpAddrLo_;
	uint8_t vdpAddrHi_;
	int vdpAddrLatch_;
};

void CEmuHardSg1000SetActive(CHardSg1000* hw);
