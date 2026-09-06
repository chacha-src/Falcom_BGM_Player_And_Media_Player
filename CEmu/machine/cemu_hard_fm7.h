#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "../vendor/mc6809/mc6809.h"
#ifdef __cplusplus
}
#endif
#undef MSB
#undef LSB
#undef A
#undef B
#undef X
#undef Y
#undef U
#undef S

/* Fujitsu FM-7 / FM77AV: M6809 + AY ($FD0D/$FD0E) and/or YM2203 ($FD15/$FD16).
   Play mailbox: $FD58=cmd (1=play), $FD59=song; BGM staged at mdata_addr. */
class CHardFm7 : public CHard {
public:
	CHardFm7();
	~CHardFm7() override;

	int Init(const CEmuGameEntry* ge, int sampleRate);
	void Shutdown();
	int SampleRate() const { return sampleRate_; }
	int LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode);

	Ay_Cpu* Cpu() override { return NULL; }
	mc6809__t* Mc6809() { return &cpu_; }
	uint8_t* Mem() override { return mem_; }
	CChip* SoundChip() override { return chipOpn_ ? chipOpn_ : chipAy_; }
	CChip* ChipOpn() { return chipOpn_; }
	CChip* ChipAy() { return chipAy_; }

	uint8_t PortIn(uint16_t port) override;
	void PortOut(uint16_t port, uint8_t data) override;

	uint8_t MemRead(uint16_t addr);
	void MemWrite(uint16_t addr, uint8_t data);

	uint64_t CpuCycles() const { return cpuCycles_; }
	void AddCpuCycles(uint64_t n) { cpuCycles_ += n; }
	/* Run mc6809_step until at least `cycles` elapsed; returns cycles consumed. */
	uint64_t RunCpu(uint64_t cycles);
	/* JSR helper for Falcom table init vectors (no live sub-CPU). */
	void RunSubroutine(uint16_t addr);

	void TriggerPlay(unsigned titleCode);
	unsigned OpnWrites() const;
	unsigned AyWrites() const;

	static void UnpackTitle(unsigned titleCode, uint8_t* songOut, uint8_t* bankOut);

	int cpuHz_;
	int opnHz_;
	int ayHz_;
	int useOpn_; /* fm77av / OPN path */
	uint16_t initPc_;
	/* Falcom PATCH 8-byte table base (often FED0). Survives boot PC skip past table. */
	uint16_t patchTableBase_;
	uint16_t mdataAddr_;
	unsigned mdataSize_;
	unsigned titleCode_;
	uint8_t playCmdLatch_; /* $FD58 */
	uint8_t playSongLatch_; /* $FD59 */
	uint8_t playParamA_; /* $FD5A */
	uint8_t playParamB_; /* $FD5B */
	uint8_t playParamC_; /* $FD5C */
	int playCmdHold_; /* keep $FD58 asserted for N polls (X1-style) */
	/* Falcom specialty (xana/ys): mailbox at $FD80/$FD82, not $FD58. */
	uint8_t falcomCmdLatch_; /* $FD80 */
	uint8_t falcomSongLatch_; /* $FD82 */
	int falcomCmdHold_;
	uint8_t fd02_;
	uint8_t fd03_;
	uint8_t fd05_; /* sub-CPU interface: bit7 busy */
	uint8_t fd05HaltSticky_; /* keep busy until main clears halt req */
	/* Vsync IRQ status polarity (PATCH ISRs disagree on bit0/2/3). */
	uint8_t fd03VsyncSet_;
	uint8_t fd03VsyncClr_;
	unsigned fd03VsyncPhase_;
	void RefreshFd03Polarity();
	void ApplyFd03Vsync();
	/* FM-7 PSG/OPN bus: FD0E/FD16=data latch, FD0D/FD15=BDIR/BC1 cmd. */
	uint8_t opnDataLatch_;
	uint8_t psgDataLatch_;
	uint8_t opnCmd_;
	uint8_t psgCmd_;
	int falcomMode_; /* prog banks / FD80 mailbox */
	int vdataAddr_;
	int vdataSize_;
	/* Highest exclusive address of loaded code (not irom). StageBgm must
	   not expand past mdata_size into this — laydock DRIVER voice tables
	   live in $4200..$42B8 while mdata window is only $4000+$200. */
	uint16_t codeHighWater_;

	enum { BGM_SIZE = 64 * 1024, PROG_BANKS = 32 };

private:
	void FreeBanks();
	void StageBgm(uint8_t index);
	void StageProg(uint8_t index);
	void StageVoice(uint8_t index);
	void BindCpuCallbacks();
	int HasProgBanks() const;

	uint8_t mem_[0x10000];
	mc6809__t cpu_;
	CChip* chipOpn_;
	CChip* chipAy_;
	int sampleRate_;
	uint64_t cpuCycles_;

	unsigned char* bgmBank_[128];
	unsigned bgmBankSize_[128];
	int bgmPresent_[128];

	unsigned char* progBank_[PROG_BANKS];
	unsigned progBankSize_[PROG_BANKS];
	int progPresent_[PROG_BANKS];

	unsigned char* voiceBank_[128];
	unsigned voiceBankSize_[128];
	int voicePresent_[128];
};

void CEmuHardFm7SetActive(CHardFm7* hw);
CHardFm7* CEmuHardFm7GetActive();
