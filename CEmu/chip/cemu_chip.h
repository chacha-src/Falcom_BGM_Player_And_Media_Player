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
	virtual unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const { (void)buf; (void)cap; return 0; }
	/* allowTimerA: 0 = mucom (B only), 1 = typical PC88 (A+B). */
	virtual void SetTimerIrqPolicy(int allowTimerA) { (void)allowTimerA; }
	/* Scale externally supplied master clocks for timer/IRQ scheduling only.
	   Audio remains tied to the physical input clock. */
	virtual void SetTimerClockScale(unsigned scale) { (void)scale; }
	/* Divide output chipRate (pitch only). 1 = native. */
	virtual void SetPitchRateDiv(unsigned div) { (void)div; }
	/* Shift synthesized FM F-number blocks without changing timers,
	   envelopes, LFO, or the register values reported to FmMon. */
	virtual void SetPitchOctaveShift(int octaves) { (void)octaves; }
	/* Clamp recurring carrier-TL drift for drivers with a broken
	   cumulative fade counter. */
	virtual void SetCarrierFadeClamp(int enable) { (void)enable; }
	virtual uint8_t ReadStatus() = 0;
	virtual uint8_t ReadData() = 0;
	virtual uint8_t ReadStatusHi() = 0;
	virtual uint8_t ReadDataHi() = 0;
};
