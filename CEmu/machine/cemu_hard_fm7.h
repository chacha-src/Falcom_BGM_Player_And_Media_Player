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

/* 富士通 FM-7 / FM77AV: M6809 + AY ($FD0D/$FD0E) および／または YM2203 ($FD15/$FD16)。
   再生メールボックス: $FD58=cmd（1=play）、$FD59=曲。BGM は mdata_addr へ載せる。 */
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
	/* mc6809_step を少なくとも cycles 進める。消費サイクルを返す */
	uint64_t RunCpu(uint64_t cycles);
	/* Falcom 表初期化ベクタ用 JSR（生きたサブ CPU は無い） */
	void RunSubroutine(uint16_t addr, int maxSteps = 200000, int clockChips = 0);

	void TriggerPlay(unsigned titleCode);
	/* PATCH のブート JSR が戻らないとき（albatrss DRIVER / ishtar MUSIC.P）、
	   IRQ マスクを外して $FD58 ポーリングへ戻す。 */
	void UnwindStuckBootJsr();
	void ParkAlbatrssIfStuck();
	void ArmLaydockChannels();
	void FinishDaivaOpPlay();
	void FinishDaivaEdPlay();
	void FinishSharrierPlay();
	void FinishXana2PsgPlay();
	int IsXana2PsgPlayer() const;
	uint16_t Xana2Tick() const { return xana2Tick_; }
	uint16_t Xana2Tempo() const { return xana2Tempo_; }
	int UnwindMissingBios();
	unsigned OpnWrites() const;
	unsigned AyWrites() const;

	static void UnpackTitle(unsigned titleCode, uint8_t* songOut, uint8_t* bankOut);

	int cpuHz_;
	int opnHz_;
	int ayHz_;
	int useOpn_; /* fm77av / OPN 経路 */
	uint16_t initPc_;
	/* Falcom PATCH の 8 バイト表基点（多くは FED0）。ブート PC が表を飛ばしても残す */
	uint16_t patchTableBase_;
	uint16_t mdataAddr_;
	unsigned mdataSize_;
	unsigned titleCode_;
	uint8_t playCmdLatch_; /* ポート $FD58 */
	uint8_t playSongLatch_; /* ポート $FD59 */
	uint8_t playParamA_; /* ポート $FD5A */
	uint8_t playParamB_; /* ポート $FD5B */
	uint8_t playParamC_; /* ポート $FD5C */
	int playCmdHold_; /* $FD58 を N 回ポーリングの間アサート（X1 流） */
	/* Falcom 専用（xana/ys）: メールボックスは $FD80/$FD82。$FD58 ではない */
	uint8_t falcomCmdLatch_; /* ポート $FD80 */
	uint8_t falcomSongLatch_; /* ポート $FD82 */
	int falcomCmdHold_;
	uint8_t fd02_;
	uint8_t fd03_;
	uint8_t fd05_; /* サブ CPU インタフェース: bit7 busy */
	uint8_t fd05HaltSticky_; /* メインが halt 要求を落とすまで busy を維持 */
	uint8_t ymIrqSeen_; /* YM2203 IRQ を $FD17 用に sticky（MAME fmirq_r bit3） */
	/* vsync IRQ ステータス極性（PATCH ISR は bit0/2/3 で食い違う） */
	uint8_t fd03VsyncSet_;
	uint8_t fd03VsyncClr_;
	unsigned fd03VsyncPhase_;
	void RefreshFd03Polarity();
	void ApplyFd03Vsync();
	/* FM-7 PSG/OPN バス: FD0E/FD16=データラッチ、FD0D/FD15=BDIR/BC1 コマンド */
	uint8_t opnDataLatch_;
	uint8_t psgDataLatch_;
	uint8_t opnCmd_;
	uint8_t psgCmd_;
	int falcomMode_; /* prog バンク / FD80 メールボックス */
	int vdataAddr_;
	int vdataSize_;
	/* ロード済みコードの排他的上限（irom 以外） */
	uint16_t codeHighWater_;

	enum { BGM_SIZE = 64 * 1024, PROG_BANKS = 32, MMR_PAGES = 64 };

private:
	void FreeBanks();
	void StageBgm(uint8_t index);
	void StageProg(uint8_t index);
	void StageVoice(uint8_t index);
	void BindCpuCallbacks();
	int HasProgBanks() const;
	void MmrInit();
	void MmrCommit(uint16_t addr, unsigned n);
	uint8_t* MmrPhys(uint16_t addr);
	uint8_t MmrReadReg(uint16_t addr);
	void MmrWriteReg(uint16_t addr, uint8_t data);
	void FinishAlbatrssPlay();
	void FinishAsteka2Play();
	void FinishWibarmPlay();
	void SeedTandeFmVoices();
	int IsAsteka2Patch() const;
	int IsDaivaPatch() const;
	int IsWibarmPatch() const;
	int IsTelenetMusFile() const;
	int IsSharrierPatch() const;
	void ArmSharrierSeq();

	uint8_t mem_[0x10000];
	/* FM77AV 256KB メイン RAM。MMR オフ時は CPU $0000-$FBFF を物理 $30000
	   （ページ $30-$3F）へ。$00-$0F は identity コピーを持ち、低いページ番号で
	   MMR を入れるリップでもコードが見える。 */
	uint8_t mmrRam_[MMR_PAGES * 0x1000];
	uint8_t mmrBank_[8][16];
	uint8_t mmrSeg_;
	uint8_t mmrWin_;
	uint8_t mmrMode_;
	int mmrAvail_;
	int mmrOn_;
	int mmrTouched_;
	int albatrssMode_;
	uint16_t albatrssPoll_;
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
	uint16_t xana2Tick_;
	uint16_t xana2Tempo_;
};

void CEmuHardFm7SetActive(CHardFm7* hw);
CHardFm7* CEmuHardFm7GetActive();
