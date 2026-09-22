#include "StdAfx.h"
#include "cemu_chip_opm.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include "opm.h"
#include <string.h>

/* 任意診断用（未使用時はコストゼロ）。 */
unsigned g_opmHist[256];
unsigned g_opmHistTotal;
unsigned g_opmIrqEdges;

class CChipYm2151 : public CChip {
public:
	CChipYm2151(uint32_t clockHz, int sampleRate)
		: sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, clockHz_(clockHz ? clockHz : 4000000u)
		, addrLatch_(0)
		, writeCount_(0)
		, keyOnCount_(0)
		, timerUsec_(0)
		, irqLatch_(0)
		, rlZeroAsLr_(0)
	{
		memset(regs_, 0, sizeof(regs_));
		opm_.Init(clockHz_, (uint)sampleRate_, false);
		opm_.Reset();
		/* fmgen Mix: pan==0 → ibuf[0] は捨てられる。実YM2151の RL=0 はミュート。
		   Reset 後は TL=127 のまま。ゲストが $20-$27 を書くまで RL=L+R を既定
		   （TLは書き換えない）。regs_ にもミラーして Peek を正直にする。 */
		for (int i = 0; i < 8; i++) {
			opm_.SetReg(0x20 + i, 0xc0);
			regs_[0x20 + i] = 0xc0;
		}
		opm_.SetVolume(0);
	}

	/* X68k/X1 API互換のため残す — レジスタは書き換えない。 */
	void SetAudibleAssist(int /*enable*/) {}
	void SetRlZeroAsLr(int enable) { rlZeroAsLr_ = enable ? 1 : 0; }

	void Reset() override
	{
		addrLatch_ = 0;
		writeCount_ = 0;
		keyOnCount_ = 0;
		timerUsec_ = 0;
		irqLatch_ = 0;
		memset(regs_, 0, sizeof(regs_));
		opm_.Reset();
		/* コンストラクタと同じ: ゲスト $20-$27 まで RL=L+R。TLは触らない —
		   Operator::Reset はゲストが書くまで TL=127（無音）。 */
		for (int i = 0; i < 8; i++) {
			opm_.SetReg(0x20 + i, 0xc0);
			regs_[0x20 + i] = 0xc0;
		}
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		if ((addr & 1) == 0) {
			addrLatch_ = (uint8_t)(data & 0xff);
			return;
		}
		const uint8_t reg = addrLatch_;
		uint8_t val = (uint8_t)(data & 0xff);
		/* fmgen Mix: pan==0 → ibuf[0] は加算されない。実YM2151の RL=0 はミュート
		   だが、X1 KOEI/KSK MML は FB を RL ビット未設定で書くことが多い。 */
		if (rlZeroAsLr_ && reg >= 0x20 && reg <= 0x27 && (val & 0xC0) == 0)
			val = (uint8_t)(val | 0xC0);
		/* CSM ($14 bit7) だと 0x08 が KeyOff になる。Sys86/X1 は CSM 未使用。 */
		if (rlZeroAsLr_ && reg == 0x14)
			val = (uint8_t)(val & 0x7fu);
		opm_.SetReg(reg, val);
		regs_[reg] = val;
		writeCount_++;
		g_opmHist[reg]++;
		g_opmHistTotal++;
		/* タイマイネーブル/フラグbitを落とすとステータスが落ちる — エッジラッチ
		   を解放し、初回ティック後にハンドラを武装するシーケンサでもIRQを取る
		   （X68k OPMDRV は DOS登録 $10C 経由で $A490 をソフトウェイト）。 */
		if (reg == 0x14 && (opm_.ReadStatus() & 0x03) == 0)
			irqLatch_ = 0;
		if (reg == 0x08 && (val & 0x78) != 0)
			keyOnCount_++;
		/* $08 だけキーストローブを渡し、他chゲートはラッチしたまま。 */
		FmMonShadowSetOpmRegSnapshotEx(regs_, (reg == 0x08) ? (int)val : -1);
		FmMonShadowMarkRegWrite(reg);
	}

	void AdvanceClocks(uint64_t chipCycles) override
	{
		if (clockHz_ == 0) return;
		/* 余りを捨てると 3.579545 MHz（CPS1 等）で 4 サイクル命令が 1.117µs → 1µs になり
		   Timer A が約 10% 遅れる。4 MHz は割り切れるので旧経路では CPS1 だけ遅かった。 */
		timerUsec_ += (__int64)chipCycles * 1000000;
		const __int64 us = timerUsec_ / (__int64)clockHz_;
		timerUsec_ %= (__int64)clockHz_;
		__int64 left = us;
		while (left > 0) {
			const int step = (left > 1000) ? 1000 : (int)left;
			if (step <= 0) break;
			/* Count はタイマA/B満了で真を返すが、fmgen がステータスbitを立てるのは
			   reg 0x14 の対応IRQENが立っているときだけ。どちらも正当な割込源。
			   Rastan はタイマAのみ（reg 14 = 0x35）なので、ここでAをマスクすると無音。
			   マスタクロック別カウンタで IRQ を立てると、Count のステータスと位相がずれ、
			   sf2 は ISR が $14 を書くたびに線が再アサートされノイズになった。 */
			if (opm_.Count(step)) {
				if ((opm_.ReadStatus() & 0x03) != 0) {
					irqLatch_ = 1;
					++g_opmIrqEdges;
				}
			}
			left -= step;
		}
	}

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		while (frames > 0) {
			const int n = frames > 64 ? 64 : frames;
			FM::Sample tmp[128];
			memset(tmp, 0, (size_t)n * 2 * sizeof(FM::Sample));
			/* fmgen Mix は既にステレオ。16bitへ飽和。 */
			opm_.Mix(tmp, n);
			for (int i = 0; i < n * 2; i++) {
				int32_t v = (int32_t)tmp[i];
				if (v > 32767) v = 32767;
				if (v < -32768) v = -32768;
				stereo[i] = (int16_t)v;
			}
			stereo += n * 2;
			frames -= n;
		}
	}

	/* Countイベントラッチ: ステータス生存中はタイマ満了ごとにIRQ1本。
	   レベル Irq() は EI後に再入（約2倍）。立ち上がり辺のみだとステータスが
	   残ったまま止まる（Assist は 0x14 を潰して隠していた）。 */
	bool Irq() const override { return irqLatch_ != 0; }
	void AckIrq() override { irqLatch_ = 0; }

	uint8_t ReadStatus() override
	{
		/* ここで irqLatch を落とさない — M92 OPM-out はステータスをビジーウェイトし、
		   M92SyncIrqs がスライス毎に読む。その読みでエッジを消すと Rev 3.40
		   シーケンサのタイマIRQが落ちる。クリアは AckIrq()。 */
		return (uint8_t)(opm_.ReadStatus() & 0x03);
	}
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return ReadStatus(); }
	uint8_t ReadDataHi() override { return 0; }
	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap == 0) return 0;
		const unsigned n = cap < 256u ? cap : 256u;
		memcpy(buf, regs_, n);
		return n;
	}

	unsigned WriteCount() const { return writeCount_; }
	unsigned KeyOnCount() const { return keyOnCount_; }
	int PeekRegs(unsigned char* out256) const
	{
		if (!out256) return 0;
		memcpy(out256, regs_, 256);
		return 1;
	}

private:
	FM::OPM opm_;
	int sampleRate_;
	uint32_t clockHz_;
	uint8_t addrLatch_;
	unsigned writeCount_;
	unsigned keyOnCount_;
	__int64 timerUsec_;
	int irqLatch_;
	int rlZeroAsLr_;
	uint8_t regs_[256];
};

/* YM2151 (OPM) ラッパ生成。 */
CChip* CEmuChipYm2151Create(uint32_t clockHz, int sampleRate)
{
	return new CChipYm2151(clockHz, sampleRate);
}

void CEmuChipYm2151Destroy(CChip* c)
{
	delete c;
}

void CEmuChipYm2151SetAudibleAssist(CChip* c, int enable)
{
	if (!c) return;
	static_cast<CChipYm2151*>(c)->SetAudibleAssist(enable);
}

void CEmuChipYm2151SetRlZeroAsLr(CChip* c, int enable)
{
	if (!c) return;
	static_cast<CChipYm2151*>(c)->SetRlZeroAsLr(enable);
}

unsigned CEmuChipYm2151WriteCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipYm2151*>(c)->WriteCount();
}

int CEmuChipYm2151PeekRegs(const CChip* c, unsigned char* out256)
{
	if (!c) return 0;
	return static_cast<const CChipYm2151*>(c)->PeekRegs(out256);
}

unsigned CEmuChipYm2151KeyOnCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipYm2151*>(c)->KeyOnCount();
}
