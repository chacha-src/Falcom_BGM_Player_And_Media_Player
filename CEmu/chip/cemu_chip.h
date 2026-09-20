#pragma once
#include <stdint.h>

/* 音源チップ抽象 — ym2608/2203 等 */
class CChip {
public:
	virtual ~CChip() {}

	virtual void Reset() = 0;
	virtual void Write(uint32_t addr, uint32_t data) = 0;
	virtual void AdvanceClocks(uint64_t chipCycles) = 0;
	virtual void Render(int16_t* stereo, int frames) = 0;
	virtual bool Irq() const = 0;
	virtual void AckIrq() = 0;
	virtual void SetAdpcmRom(const uint8_t* data, unsigned size, unsigned destOffset) { (void)data; (void)size; (void)destOffset; }
	virtual void SetAdpcmB(const uint8_t* data, unsigned size, unsigned destOffset) { (void)data; (void)size; (void)destOffset; }
	virtual unsigned GetAdpcmRomSize() const { return 0; }
	virtual unsigned GetAdpcmBSize() const { return 0; }
	virtual void SetPcmRom(const uint8_t* data, unsigned size) { (void)data; (void)size; }
	virtual void MixAdd(int16_t* stereo, int frames, int gain) { (void)stereo; (void)frames; (void)gain; }
	/* stereo は 32byte 境界が望ましい（AVX2 Mix）。未整列だと遅くなる。 */
	virtual unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const { (void)buf; (void)cap; return 0; }
	/* 前回呼び出し以降のタイマ満了回数を返し、内部カウンタをゼロにする。
	   合体IRQ線だけを見るドライバは満了の区別ができないため、満了1回につき
	   CPU割込を1本だけ上げる用途。 */
	virtual unsigned TakeTimerExpiries() { return 0; }
	/* allowTimerA: 0=mucom(Bのみ)、1=一般的なPC88(A+B)。 */
	virtual void SetTimerIrqPolicy(int allowTimerA) { (void)allowTimerA; }
	/* 外部供給マスタクロックをタイマ/IRQスケジュール専用にスケールする。
	   音声は物理入力クロックに固定。 */
	virtual void SetTimerClockScale(unsigned scale) { (void)scale; }
	virtual void SetTimerClockScaleRatio(unsigned num, unsigned den) { (void)num; (void)den; }
	/* 出力 chipRate を割る（ピッチのみ）。1=ネイティブ。 */
	virtual void SetPitchRateDiv(unsigned div) { (void)div; }
	/* 合成FMのF-numberブロックだけシフト。タイマ・エンベロープ・LFO・
	   FmMonへ報告するレジスタ値は変えない。 */
	virtual void SetPitchOctaveShift(int octaves) { (void)octaves; }
	/* 累積フェードカウンタが壊れたドライバ向けに、キャリアTLドリフトを抑える。 */
	virtual void SetCarrierFadeClamp(int enable) { (void)enable; }
	virtual uint8_t ReadStatus() = 0;
	virtual uint8_t ReadData() = 0;
	virtual uint8_t ReadStatusHi() = 0;
	virtual uint8_t ReadDataHi() = 0;
};
