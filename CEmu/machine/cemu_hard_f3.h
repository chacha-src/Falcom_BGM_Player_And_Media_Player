#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

/* Taito F3 Ensoniq sound board (MAME taito_en): MC68000 + ES5505. */
class CHardF3 : public CHard {
public:
	CHardF3();
	~CHardF3() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int SampleRate() const { return sampleRate_; }
	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);

	Ay_Cpu* Cpu() override { return NULL; }
	uint8_t* Mem() override { return osram_; }
	CChip* SoundChip() override { return chip_; }

	uint8_t PortIn(uint16_t port) override { (void)port; return 0xff; }
	void PortOut(uint16_t port, uint8_t data) override { (void)port; (void)data; }

	uint8_t Read8(unsigned addr);
	uint16_t Read16(unsigned addr);
	uint32_t Read32(unsigned addr);
	void Write8(unsigned addr, uint8_t data);
	void Write16(unsigned addr, uint16_t data);
	void Write32(unsigned addr, uint32_t data);

	void SetSongCommand(unsigned code);
	unsigned SongCommand() const { return songCode_; }
	int MusashiReady() const { return musashiReady_; }
	unsigned DpramReadHit(int i) const { return (i >= 0 && i < 32) ? dpramReadHits_[i] : 0; }
	uint8_t DpramByte(unsigned i) const { return (i < kDpramBytes) ? dpram_[i] : 0; }
	/* Advance DUART timer; returns 1 if IRQ6 should be asserted. */
	int TickDuart(int cpuCycles);
	int DuartIrqPending() const { return duartIrqPending_; }
	/* MC68681 IVR — used as 68K IACK vector (MAME fc7_map). */
	uint8_t DuartIvr() const { return duart_[0x0c] ? duart_[0x0c] : 0x40; }
	void UpdateDuartIrq();

	int cpuHz_;
	int esHz_;

private:
	void RebuildOtisBanks();
	void PlaceEvenBytes(uint8_t* dst, unsigned dstBytes, unsigned dstOff, const uint8_t* src, unsigned srcBytes);
	void PlaceInterleaved(uint8_t* dst, unsigned dstBytes, unsigned dstOff,
		const uint8_t* even, unsigned evenBytes, const uint8_t* odd, unsigned oddBytes);
	unsigned DpramMovepRead(unsigned byteOff) const;
	void DpramMovepWrite(unsigned byteOff, unsigned value);
	void DpramRingWriteByte(unsigned byteOff, uint8_t data);
	void EnqueueRingPacket(const uint8_t* bytes, int nbytes);
	void EnsureHostRing();

	enum {
		kOsramBytes = 0x10000,
		kDpramBytes = 0x800,
		kOtisBankWords = 0x20,
		kAudioCpuMax = 0x200000,
		kEnsoniqMax = 0x1000000
	};

	uint8_t osram_[kOsramBytes];
	uint8_t dpram_[kDpramBytes];
	unsigned dpramReadHits_[32];
	unsigned dpramWriteHits_;
	uint16_t otisBank_[kOtisBankWords];
	uint32_t calcOtisBank_[kOtisBankWords];
	uint8_t* audioCpu_;
	unsigned audioCpuSize_;
	uint8_t* ensoniq_;
	unsigned ensoniqSize_;
	uint32_t bankMask_;
	unsigned cpuBankEntry_[3];
	unsigned cpuBankMax_;
	CChip* chip_;
	int sampleRate_;
	int musashiReady_;
	unsigned songCode_;
	uint8_t duart_[0x20];
	int duartIrqPending_;
	uint8_t duartImr_;
	uint8_t duartIsr_;
	uint8_t duartAcr_;
	uint16_t duartCtr_;
	uint8_t esp_[0x200];
	int64_t duartTimerAcc_;
	int ringInited_;
};

void CEmuHardF3SetActive(CHardF3* hw);
CHardF3* CEmuHardF3GetActive();
int CEmuHardF3IntAckCount();
int CEmuHardF3IntAckLast();
void CEmuHardF3IntAckReset();
