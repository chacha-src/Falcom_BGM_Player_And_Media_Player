#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

/* MSX: KSS (hoot IPL) + generic offset romlists (patch@init_pc + driver + mdata). */
class CHardMsx : public CHard {
public:
	CHardMsx();
	~CHardMsx() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int SampleRate() const { return sampleRate_; }
	int LoadKss(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);
	int StartSong(unsigned titleCode);

	Ay_Cpu* Cpu() override { return cpu_; }
	uint8_t* Mem() override { return mem_; }
	CChip* SoundChip() override { return chipAy_ ? (CChip*)chipAy_ : chipSng_; }
	CChip* ChipAy() { return chipAy_; }
	CChip* ChipScc() { return (sccAccessed_ && chipScc_) ? chipScc_ : NULL; }
	CChip* ChipSng() { return chipSng_; }
	CChip* ChipOpl() { return chipOpl_; }
	void* Opll() { return chipOpll_; }

	uint8_t PortIn(uint16_t port) override;
	void PortOut(uint16_t port, uint8_t data) override;

	void MemWrite(uint16_t addr, uint8_t data);
	uint8_t MemRead(uint16_t addr);

	uint64_t CpuCycles() const { return cpuCycles_; }
	void AddCpuCycles(uint64_t n) { cpuCycles_ += n; }

	void MarkIdle() { idle_ = 1; }
	int Idle() const { return idle_; }
	void ClearIdle() { idle_ = 0; }

	unsigned AyWrites() const;
	unsigned OpllWrites() const;
	int PlayCmdPending() const { return playCmdPending_; }
	uint8_t IoPort(uint8_t p) const { return ioport_[p & 0xff]; }
	int GenericMode() const { return genericMode_; }

	int cpuHz_;
	int ayHz_;
	int opllHz_;
	uint8_t chips_;
	int playing_;

	enum {
		CHIP_FMPAC = (1 << 0),
		CHIP_SNG = (1 << 1),
		CHIP_GGSTEREO = (1 << 2),
		CHIP_MSXAUDIO = (1 << 3),
		CHIP_SCCDISABLE = (1 << 7),
		INIT_ADR = 0x0019,
		INT_ADR = 0x003a,
		PLAY_CODE_PORT = 0x00,
		SKIP_PORT = 0x01,
		BGM_BANKS = 128,
		BGM_SIZE = 0x4000,
		MAP_NONE = 0,
		MAP_ASCII16 = 1,
		MAP_DS4 = 2,
		MAP_ASCII8 = 3
	};

private:
	static int ParseOptHex(const CEmuGameEntry* ge, const char* name, int defVal);
	void ApplyBank(uint8_t bankSel);
	void MapDefault();
	void FreeBanks();
	void EnsureOpll(int force);
	void EnsureSng();
	void EnsureMsxAudio();
	void PlantBiosStubs();
	void StageBgm(unsigned index);
	void MapAscii16(int page, uint8_t bank);
	void MapAscii8(int page, uint8_t bank);
	void MapDs4(int page, uint8_t bank);
	int LoadCartRom(CEmuZipFs* fs, const CEmuGameEntry* ge, const char* type);
	int LoadKssImage(CEmuZipFs* fs, const CEmuGameEntry* ge);
	int LoadGeneric(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);
	int LoadDs4(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);
	int LoadAscii16(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);
	int LoadDq(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);
	int StartSongKss(unsigned titleCode);
	int StartSongGeneric(unsigned titleCode);
	int StartSongDs4(unsigned titleCode);
	int StartSongDq(unsigned titleCode);
	void PlantDqBios();
	int IsKssMagic(const unsigned char* data, unsigned sz) const;

	uint8_t mem_[0x10000];
	uint8_t rom_[0x40000];
	uint8_t* bank_;
	unsigned bankBytes_;
	uint8_t ioport_[0x100];
	Ay_Cpu* cpu_;
	CChip* chipAy_;
	CChip* chipScc_;
	CChip* chipSng_;
	CChip* chipOpl_;
	void* chipOpll_;
	int sampleRate_;
	uint64_t cpuCycles_;
	int idle_;

	uint16_t loadAdr_;
	uint16_t loadSize_;
	uint16_t initAdr_;
	uint16_t intAdr_;
	uint8_t bankOfs_;
	uint8_t bankNum_;
	int bank8k_;
	uint8_t bankShadow_[0x4000];
	int bank8kRam_[2];
	int sccEnable_;
	int sccMapped_; /* Konami mapper: 0x3F→$9000 exposes SCC at $9800 */
	int sccAccessed_; /* MixAdd only after a real SCC register write */
	unsigned ayWriteCount_;
	unsigned opllWriteCount_;
	uint8_t opllLatch_;
	uint8_t opllRegs_[64];

	int genericMode_;
	int dqMode_;
	uint16_t initPc_;
	uint16_t mdataAddr_;
	unsigned mdataSize_;
	unsigned char* bgmBank_[BGM_BANKS];
	unsigned bgmBankSize_[BGM_BANKS];
	uint8_t bgmPresent_[BGM_BANKS];
	unsigned titleCode_;
	const CEmuGameEntry* ge_;
	int playCmdPending_;
	int mapper_;
	uint8_t* cart_;
	unsigned cartBytes_;
	uint8_t ttlPrg_[0x2C00];
	unsigned ttlPrgBytes_;
	uint16_t ttlPrgAddr_;
	uint8_t ascii16Bank_[2];
	uint8_t ascii8Bank_[4];
	uint8_t ds4Bank_[2];
};

void CEmuHardMsxSetActive(CHardMsx* hw);
