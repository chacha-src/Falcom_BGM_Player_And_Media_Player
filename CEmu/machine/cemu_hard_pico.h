#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

/* Sega Pico: 68000 + VDP SN76489 + 315-5641 FIFO/READY。カートは data\pico。 */
class CHardPico : public CHard {
public:
	CHardPico();
	~CHardPico() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int SampleRate() const { return sampleRate_; }
	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);

	Ay_Cpu* Cpu() override { return NULL; }
	uint8_t* Mem() override { return ram_; }
	CChip* SoundChip() override { return chip_; }

	uint8_t PortIn(uint16_t port) override { (void)port; return 0xff; }
	void PortOut(uint16_t port, uint8_t data) override { (void)port; (void)data; }

	uint8_t Read8(unsigned addr);
	uint16_t Read16(unsigned addr);
	uint32_t Read32(unsigned addr);
	void Write8(unsigned addr, uint8_t data);
	void Write16(unsigned addr, uint16_t data);
	void Write32(unsigned addr, uint32_t data);

	int MusashiReady() const { return musashiReady_; }
	unsigned PsgWrites() const { return psgWrites_; }
	unsigned MailboxAddr() const { return mailboxAddr_; }
	unsigned SmpsRamBase() const { return smpsRamBase_; }
	unsigned CartSize() const { return cartSize_; }
	/* VBlank / IRQ6。IE0 が立つまで線だけ上げ、ゲームがマスクを下げるのを待つ。 */
	void PulseVint();
	void SetVintPending();
	/* 315-5641 FIFO を CPU サイクル分進める。IRQ3 もここで。 */
	void TickPcm(int cycles);

	int cpuHz_;
	int psgHz_;

private:
	enum { kCartMax = 0x400000, kRamBytes = 0x10000 };
	uint8_t* cart_;
	unsigned cartSize_;
	uint8_t ram_[kRamBytes];
	CChip* chip_;
	int sampleRate_;
	int musashiReady_;
	unsigned psgWrites_;
	unsigned mailboxAddr_;
	unsigned smpsRamBase_;
	uint8_t pageReg_;
	uint16_t vdpStatus_;
	uint16_t vdpLatch_;
	int vdpLatchHalf_;
	uint8_t vdpReg_[32];
	uint16_t hvCount_;
	int vintEnable_;
	int vintPending_;
	uint16_t pcmCtrl_;

	void PcmFifoReset();
	void PcmFifoPush(uint8_t b);
	uint16_t IoRead(unsigned off);
	void IoWrite(unsigned off, uint16_t data, uint16_t mask);
	uint16_t VdpRead(unsigned off);
	void VdpWrite(unsigned off, uint16_t data);
	void DetectSoundGlue();
};

void CEmuHardPicoSetActive(CHardPico* hw);
CHardPico* CEmuHardPicoGetActive();
