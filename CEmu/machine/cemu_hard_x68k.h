#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

/* 薄い X68k 音源ハード（hoot x68k.cpp）: 68000 + YM2151 @ e90001 + 曲メールボックス @ e00000。
   trap_f.bin は 64K IPL/IOCS。BOOT は 1F0000→FF0000 をコピーして JSR FF0B86。
   romlist に trap_f があれば LoadRoms がその IPL を一度走らせる（PlantDos ではない実コード）。
   メイン RAM は 4MB ($000000..$3FFFFF)。$E00000..$E7FFFF は ADPCM 用拡張 RAM。
   MFP Timer C/D @ $E88000 がベクタ付き IRQ2（VR=$40 なら $110/$114）。 */
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
	/* MSM6258V ADPCM をステレオへ混成（hoot x68k.cpp 経路） */
	void MixAdpcm(int16_t* stereo, int frames);

	/* MC68901 Timer C/D → IRQ2（ベクタ）。CPU 量子ごとに 1 回 */
	void TickMfp(int cpuCycles);
	int MfpIrqPending() const { return mfpIrqPending_; }
	int AckMfpIrq();
	/* IERB+IMRB で Timer D 武装済み — ドライバは $110 をソフトパルスしない */
	int MfpTimerDIrqArmed() const;

	/* Human68k OPEN/READ 用に XML が置いたファイル（ZMUSIC 等） */
	enum { kDosFiles = 64, kDosHandles = 8 };
	struct DosFile {
		char name[32];
		unsigned addr;
		unsigned size;
	};
	struct DosHandle {
		int file; /* dosFiles_ の添字。無ければ -1 */
		unsigned pos;
	};
	int DosFileCount() const { return dosFileCount_; }
	const DosFile* DosFileAt(int i) const {
		return (i >= 0 && i < dosFileCount_) ? &dosFiles_[i] : NULL;
	}
	/* ホスト側 DOS: fn=0x3d OPEN / 0x3f READ / 0x3e CLOSE / 0x4e NAMECK。
	   Human68k 風 d0 を返す。READ はゲストバッファへもコピー。 */
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

	enum {
		/* メイン RAM 4MB: XML の code/x が $180000+（bonnou OPM、Wolfteam、Dempa）。
		   1MB + $10xxxx 512KB 窓のときはスキップされていた。 */
		kRomBytes = 0x400000, kRamBytes = 0x10000,
		kHighBytes = 0x10000, kMfpBytes = 0x1000,
		kHeapBase = 0xA00000, kHeapBytes = 0x40000,
		kExtBase = 0xE00000, kExtBytes = 0x80000
	};
	uint8_t rom_[kRomBytes];
	uint8_t ram_[kRamBytes];
	uint8_t high_[kHighBytes];
	uint8_t heap_[kHeapBytes];
	uint8_t ext_[kExtBytes]; /* $E00000..$E7FFFF ADPCM／拡張 */
	uint8_t mfp_[kMfpBytes];
	int softMfp_; /* catalog mfp=1: GPIP bit4 クリア（arcus 待ちループ） */
	int64_t mfpTdAcc_;
	int64_t mfpTcAcc_;
	int mfpIrqPending_;
	uint8_t mfpIrqVec_;
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
	int adpcmPaused_;
	/* HD63450 ch3（MSM6258V）: ゲストが組んだ転送数とメモリアドレス、配列チェイン基点 */
	uint16_t dmacMtc_;
	uint32_t dmacMar_;
	uint8_t dmacOcr_;
	uint16_t dmacBtc_;
	uint32_t dmacBar_;
	/* 配列チェイン: 再生中の次以降の残りディスクリプタ */
	unsigned adpcmChainPtr_;
	unsigned adpcmChainLeft_;
	int AdpcmLoadChainEntry();
	void AdpcmStartBlock(unsigned addr, unsigned bytes);
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
	/* $E00018..$E0001F DOS ファイル操作メールボックス（ゲスト LINE-F stub） */
	unsigned dosMbA1_;
	unsigned dosMbD0_;
	unsigned dosMbD1_;
	unsigned dosMbResult_;
};

void CEmuHardX68kSetActive(CHardX68k* hw);
CHardX68k* CEmuHardX68kGetActive();
