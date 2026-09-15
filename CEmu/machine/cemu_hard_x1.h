#pragma once
#include "cemu_hard.h"
#include "../chip/cemu_chip.h"
#include "../cemu_zipfs.h"

/* Sharp X1 mucom88（hoot mucomx1）: Z80 + YM2151 + AY-3-8910。
   メールボックス C010/C011/C012。BGM バンク OUT(0)。OPM @0700。AY @1B00/1C00。 */
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
	CChip* SoundChip() override { return chipOpn_ ? chipOpn_ : chipOpm_; }
	CChip* ChipAy() { return chipAy_; }

	uint8_t PortIn(uint16_t port) override;
	void PortOut(uint16_t port, uint8_t data) override;

	void MemWrite(uint16_t addr, uint8_t data);
	uint8_t MemRead(uint16_t addr);

	uint64_t CpuCycles() const { return cpuCycles_; }
	void AddCpuCycles(uint64_t n) { cpuCycles_ += n; }

	/* titleCode: hoot パック 0xSS0000BB → 曲=SS、バンク=BB。素の 0xNN は両方。
	   ydos: KOEI 0x010000SS は hi がループフラグ、曲は lo。 */
	void TriggerPlay(unsigned titleCode);
	/* 再生メールボックスを武装せず mdata/IO へ BGM を載せる（DRIVER ブート） */
	void PrestageBgm(unsigned titleCode);
	unsigned OpmWrites() const;
	unsigned AyWrites() const;

	static void UnpackTitle(unsigned titleCode, uint8_t* songOut, uint8_t* bankOut,
		int ydos = 0);

	int cpuHz_;
	int opmHz_;
	int ayHz_;
	int psgOnly_; /* サブタイプ psg / x1psg */
	int opnMode_; /* subtype=opn — YM2203 at E0/E1。CZ-8BS1 OPM ではない */
	uint16_t initPc_;
	uint16_t mdataAddr_; /* RAM 上の BGM 載せ先。既定 0x4000 */
	unsigned mdataSize_; /* 載せるバイト数。既定 BGM_SIZE */
	unsigned titleCode_;
	/* Falcom PATCH は IN 0 / IN 1 で cmd/曲を見る（hoot も C010/C011 を poke） */
	uint8_t playCmdLatch_;
	uint8_t playSongLatch_;
	uint8_t playSongLatchF_; /* jesus: ポート 0F は OPMTBL 添字、ポート 1 は 0-8 */
	int playCmdHoldIrqs_; /* コマンドを N IRQ の間 High に保ち、その後クリア */
	/* YDOS: PortIn(0) が PATCH 待ち PC から未処理コマンドを返したときセット */
	uint8_t ydosCmdSeen_;
	/* YDOS: 誤 OUT0 でポインタを組んだあと、待ちループ再入にコマンドを隠す */
	uint8_t ydosInhibitReentry_;
	/* カタログに YDOS*.SYS — 第 1 世代は PATCH 復号まで OVL-1 が無いことがある */
	uint8_t ydosRom_;
	/* Telenet OPMDRV: PATCH は drv+0x0A に BGM ポインタを書くが、ISR 再生許可 drv+0x0F を
	   武装しない（luxsor の play が XOR で落とす）。0 = 対象外。 */
	uint16_t opmPlayGate_;
	uint16_t opmPlayTempo_;
	void ArmTelenetPlayGate();
	/* sghost: OPMDRV $4595 のホストコピー。PATCH CALL INIT の戻りは $F05A */
	void LoadSghostOpmPatches();
	/* Tecnosoft OPMDRV: PATCH は `IN A,(1); … CP FF; AND 0F; CALL drv`。
	   ポート 1 は 0x80/0x81/0xFF コマンドでありバンク添字ではない。 */
	uint8_t tecnoCmdHi_;
	/* Enix JESUS: PATCH はポート 1 で `CP 09`（コピー記述 0-8）、ポート 0F で `CP 72`
	   （OPMTBL 添字）。グローバル id >= 9 がポート 1 ラッチを共有すると再生が飛ばされる。 */
	uint8_t jesusSplitPorts_;
	/* Herzog `LD DE,2802` / revo2 `LD A,(F5F8); CP 03`: 各 BGM ファイルが 1 曲。
	   Title 0xSS0000BB は SS をラッチ（hi=0 なら 0）。lo をトラックにすると
	   HZ-BG3/MUS103 がファイル末尾を外す。 */
	uint8_t songIdFromHi_;
	/* produce: ブート後 BGM バンクが mdata_addr の PROG* を覆う。xanaopm:
	   PR.NO0 は mdata 窓内にあり、PATCH のプレーヤ LDIR → $F000 を生き延びる必要がある。
	   RAM StageBgm は Prestage 中だけ飛ばす。 */
	uint8_t skipPrestageRam_;
	/* produce: mdata_addr のコードがプレーヤ本体（PROG1）。TriggerPlay で別 PROG* を
	   載せると JP $0285 がゲームテキストに置き換わる。曲 id は PROG1/OPMMUS5 内のトラック。 */
	uint8_t skipTriggerStage_;
	/* ポート 1A01 busy/ready トグル（mars JP P / JP M 対 Laplace BIT 2） */
	uint8_t psgStatToggle_;
	/* ys2 PATCH は曲 >= 0x20 で `LDIR C000→4000`。mode 0 はまだ $4000 を読む */
	uint8_t ys2Mirror4000_;
	/* Laplace PATCH の `IN E,(0F)` は CTC ch3 タイムコンスタント。曲 id ではない */
	uint8_t laplaceCtcF_;
	/* wibarm: ポート F はファイル内トラック / $FF overlay、ポート 1 は再生コマンド */
	uint8_t wibarmPortF_;
	/* Falcom xana2: `IN A,(0F); SUB 2` が PR.NO2/3/4/5 を指す。ポート F は hi
	   （系列 2/3/4/5）。lo の載せたファイル id ではない。 */
	uint8_t falcomPortF_;
	/* ametruck: ポート 1 で `CP 03`（ファイル 0-2）。ポート F はファイル内バリアント */
	uint8_t ametruckPortF_;
	/* mars PROG `$420A JP P` ハンドシェイク。ブートは play 前に EI。ISR が RETI しないので
	   待ちループがメールボックスを見ない。PATCH の play CALL が PROG から戻るまで IRQ を止める。 */
	uint8_t marsHoldIrq_;
	uint8_t marsSeenProg_;
	uint8_t marsPlayReady_;

	/* Z80 CTC @1FA0-1FA3（MAME X1。ミラー 1FA8）。ゲストがベクタ基点とチャネル IE を組む。
	   ドライバは IM2 を base+2*ch で届ける。 */
	uint8_t CtcVector(int channel) const;
	int CtcVectorProgrammed() const { return ctcVectorProgrammed_; }
	int CtcIe(int channel) const {
		return (channel >= 0 && channel < 4) ? ctcIe_[channel] : 0;
	}
	/* タイマモード ch0 周期（CPU クロック）。0 = 未プログラム／既定 */
	unsigned CtcTimerPeriodCycles(int channel) const;
	/* カウンタモード分周（0 = カウンタではない）。X1 は CTC ZC0 を TRG3 へ配線するので、
	   ch3 カウンタは ch0 タイマ出力を分周する。 */
	unsigned CtcCounterTc(int channel) const;

	enum {
		PLAY_FLAG = 0xC010,
		PLAY_CODE = 0xC011,
		LOAD_FLAG = 0xC012,
		/* hoot 既定 8K。タイトルによっては mfile_size が約 0x8000 まで */
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
	CChip* chipOpn_; /* YM2203（ishtar OPN）。OPM/PSG 行では NULL */
	CChip* chipAy_;
	int sampleRate_;
	uint64_t cpuCycles_;
	/* StageBgm が書いてよい排他的上限（コードを潰さない） */
	uint32_t stageLimit_;
	/* Laplace 曲 mid ワード: 載せた MUSIC ファイル内のバイトオフセット */
	unsigned bgmStageOff_;

	unsigned char* bgmBank_[128];
	unsigned bgmBankSize_[128];
	int bgmPresent_[128];

	/* CTC: ベクタ基点は ch0 の bit0=0 書込。IE はコントロール bit7 */
	uint8_t ctcVectorBase_;
	int ctcVectorProgrammed_;
	uint8_t ctcIe_[4];
	uint8_t ctcExpectTc_[4];
	uint8_t ctcControl_[4];
	uint8_t ctcTc_[4];
	int ctcTcValid_[4];
	/* XML ctc0/ctc3: ゲストが CTC を組む前の hoot use_ctcN ベクタ */
	int xmlCtcVec_[4];
};

void CEmuHardX1SetActive(CHardX1* hw);
