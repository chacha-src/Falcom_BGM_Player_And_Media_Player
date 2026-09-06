#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

/* Thin X68k sound hard (hoot x68k.cpp): 68000 + YM2151 @ e90001 + song mailbox @ e00000.
   trap_f.bin is the 64K IPL/IOCS image; BOOT copies 1F0000→FF0000 then JSR FF0B86.
   When romlist includes trap_f, LoadRoms runs that IPL sub once (real code, not PlantDos).
   CLEAN: XML-accurate ROM placement + hardware MMIO/IRQ + real IPL call only. */
class CHardX68k : public CHard {
public:
	CHardX68k();
	~CHardX68k() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int SampleRate() const { return sampleRate_; }
	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);

	Ay_Cpu* Cpu() override { return NULL; }
	uint8_t* Mem() override { return rom_; }
	CChip* SoundChip() override { return chip_; }

	uint8_t PortIn(uint16_t port) override { (void)port; return 0xff; }
	void PortOut(uint16_t port, uint8_t data) override { (void)port; (void)data; }

	void SetSongCommand(unsigned code);
	unsigned OpmWrites() const { return opmWrites_; }
	unsigned Pc() const { return pc_ & 0xffffffu; }
	void SetPc(unsigned pc) { pc_ = pc & 0xffffffu; }
	int FetchCount() const { return fetchCount_; }
	void AddFetch() { fetchCount_++; }
	uint8_t SongFlag() const { return songFlag_; }
	/* Mix MSM6258V ADPCM into stereo buffer (hoot x68k.cpp path). */
	void MixAdpcm(int16_t* stereo, int frames);

	/* XML-placed files for Human68k OPEN/READ (ZMUSIC etc.). */
	enum { kDosFiles = 64, kDosHandles = 8 };
	struct DosFile {
		char name[32];
		unsigned addr;
		unsigned size;
	};
	struct DosHandle {
		int file; /* index into dosFiles_ or -1 */
		unsigned pos;
	};
	int DosFileCount() const { return dosFileCount_; }
	const DosFile* DosFileAt(int i) const {
		return (i >= 0 && i < dosFileCount_) ? &dosFiles_[i] : NULL;
	}
	/* Host-side DOS file op: fn=0x3d OPEN / 0x3f READ / 0x3e CLOSE / 0x4e NAMECK.
	   Returns Human68k-style d0; for READ also copies into guest buffer. */
	unsigned DosFileOp(unsigned fn, unsigned a1, unsigned d0, unsigned d1);

	uint8_t Read8(unsigned addr);
	uint16_t Read16(unsigned addr);
	uint32_t Read32(unsigned addr);
	void Write8(unsigned addr, uint8_t data);
	void Write16(unsigned addr, uint16_t data);
	void Write32(unsigned addr, uint32_t data);

	int cpuHz_;
	int opmHz_;
	unsigned opmWrites_;

private:
	uint8_t* HighPtr(unsigned addr24);
	const uint8_t* HighPtr(unsigned addr24) const;
	void BootIplFf0b86();
	void DosRegisterFile(const char* name, unsigned addr, unsigned size);
	int DosFindFile(const char* path) const;

	enum { kRomBytes = 0x100000, kMidBytes = 0x80000, kRamBytes = 0x10000, kHighBytes = 0x10000, kMfpBytes = 0x1000 };
	uint8_t rom_[kRomBytes];
	uint8_t ram_[kRamBytes];
	uint8_t high_[kHighBytes];
	uint8_t mid_[kMidBytes];
	uint8_t mfp_[kMfpBytes];
	int softMfp_; /* catalog mfp=1: GPIP bit4 clear (arcus wait loops) */
	int adpcmPlaying_;
	unsigned adpcmAddr_;
	unsigned adpcmSize_;
	unsigned adpcmPos_;
	int adpcmSignal_;
	int adpcmStep_;
	int adpcmNibble_;
	int adpcmRateHz_;
	int adpcmPan_;
	uint8_t adpcmPpi_;
	int64_t adpcmPhase_;
	CChip* chip_;
	int sampleRate_;
	uint8_t ymAddr_;
	uint8_t songFlag_;
	uint16_t songCode_;
	unsigned pc_;
	int fetchCount_;
	int musashiReady_;
	DosFile dosFiles_[kDosFiles];
	int dosFileCount_;
	DosHandle dosHandles_[kDosHandles];
	/* $E00018..$E0001F DOS file-op mailbox (guest LINE-F stubs). */
	unsigned dosMbA1_;
	unsigned dosMbD0_;
	unsigned dosMbD1_;
	unsigned dosMbResult_;
};

void CEmuHardX68kSetActive(CHardX68k* hw);
CHardX68k* CEmuHardX68kGetActive();
