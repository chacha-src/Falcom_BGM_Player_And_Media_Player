#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_ac.h"

/* アーケード音源: ボード種別ごとに Z80/V35/68K/H8 等へ分岐 */
class CDriverAc : public CDriver {
public:
	CDriverAc();
	~CDriverAc() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;
	int OverlayTitle(unsigned titleCode) override;

	unsigned OpmWrites() const;

private:
	CHardAc* hw_;
	int hostRate_;
	int cpuHz_;
	int opmHz_;
	int booted_;
	int triggered_;
	int pinned_; /* プレイリスト／カタログ曲 — 1 回注入し試行表を探さない */
	uint8_t songCmd_;
	uint16_t songCmdWord_; /* 16bit コマンドボード用の完全 title code */
	unsigned songCmdDword_; /* Hornet/GTI: カタログは 32bit (0x01xx0000) */
	uint64_t opmResidual_;
	uint64_t rzOpmAcc_; /* Raizing / Cave YM2151: まとめて進めるタイマ */
	int64_t cpuAcc_;
	int cmdIndex_;
	uint64_t nextCmdAt_;
	uint64_t nextGngIrq_;
	int irqPaceAcc_;       /* 再生中 250Hz をホストサンプルへロックする端数 */
	int irqPaceDue_;       /* 取ってよい周期 IRQ 残 */
	int irqPaceLive_;      /* Render 中は 1。Open のブートは CPU 時間のまま */
	int alphaNmiBusy_;     /* Alpha 68K-II NMI が RETN するまで 1 */
	/* 直前の K054539 タイマ出力。立ち上がりで NMI を起こす */
	int k054539TimerState_;
	uint64_t k054539Residual_;
	uint64_t nextM72Nmi_;  /* MASTER_CLOCK/8/512 = 7812.5 Hz サンプルポンプ */
	int m72FakeNmi_;       /* NMI ハンドラが空 — ホスト側でポンプ */
	int hasCpu_;           /* CEmu が音源 CPU を回せないボードは 0 */
	int ms1_;              /* Mega System 1 / System GX: Musashi 68000 音源CPU */
	int64_t ms1Acc_;
	int m92_;              /* Irem M92: NEC V35 音源CPU */
	int64_t m92Acc_;
	uint64_t m92OpmRes_;   /* YM2151 は V35 クロック / 4 */
	int deco_;             /* Data East: HuC6280 音源CPU */
	int64_t decoAcc_;
	uint64_t decoChipRes_; /* YM2151 クロック比の端数 */
	uint64_t decoNextYmIrq_; /* HuC6280 IRQ2 のレート制限 */
	int h8Board_;          /* Namco C352: H8/3002 音源CPU */
	int64_t h8Acc_;
	int m37702Board_;      /* Namco Sys11/NA1: M37702 音源CPU */
	int64_t m37702Acc_;
	int namcoM6809_;       /* Namco Sys1/2: mc6809 音源CPU */
	int64_t namcoAcc_;
	int sys86_;             /* Namco Sys86: HD63701 音源CPU */
	int64_t sys86Acc_;
	int sys86OciNeed_;      /* 曲開始アイドル後に EOCI を一度再武装 */
	int m62_;              /* Irem M62: M6803 音源CPU */
	int64_t m62Acc_;
	int sega68_;           /* Model1 MultiPCM / Model2 SCSP: 68000 音源CPU */
	int64_t sega68Acc_;
	int16_t* scratch_;     /* 補助チップ混成バッファ（SN×2 / AY×3）。32byte 境界 */
	int scratchFrames_;
	int heard_;            /* 直前コマンド以降に非ゼロサンプルが出たか */
	uint8_t extReserve_[64]; /* 迷路/モニタ/GPU Mix 拡張用リザーブ */

	/* Z80 ボード用 */
	void RunUntil(uint64_t endCycle);
	void Ms1RunCycles(int cycles);
	int Ms1Render(int16_t* stereo, int frames);
	void M92RunCycles(int cycles);
	int M92Render(int16_t* stereo, int frames);
	void DecoRunCycles(int cycles);
	int DecoRender(int16_t* stereo, int frames);
	void H8RunCycles(int cycles);
	int H8Render(int16_t* stereo, int frames);
	void M37702RunCycles(int cycles);
	int M37702Render(int16_t* stereo, int frames);
	void NamcoM6809RunCycles(int cycles);
	int NamcoM6809Render(int16_t* stereo, int frames);
	void Sys86RunCycles(int cycles);
	int Sys86Render(int16_t* stereo, int frames);
	void M62RunCycles(int cycles);
	int M62Render(int16_t* stereo, int frames);
	void Sega68RunCycles(int cycles);
	int Sega68Render(int16_t* stereo, int frames);
	void TickOpm(uint64_t cpuCycles);
	void DeliverIrqs();
	/* ラッチ／NMI へ曲コマンドを注入 */
	void TryInjectCommand();
	uint16_t m92NoteOffSeen_;
	uint16_t m92ChannelPlayOff_; /* FB/F6/F7 スキップ後のチャネル BGM ポインタ */
	int m92NoteStuck_;
	/* 補助混成バッファを確保して返す */
	int16_t* Scratch(int frames);
};
