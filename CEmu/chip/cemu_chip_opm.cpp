#include "StdAfx.h"
#include "cemu_chip_opm.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include "opm.h"
#include <string.h>

/* Optional diagnostics for itests (zero-cost when unused). */
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
	{
		memset(regs_, 0, sizeof(regs_));
		opm_.Init(clockHz_, (uint)sampleRate_, false);
		opm_.Reset();
		/* fmgen Mix: pan==0 → ibuf[0] discarded. Real YM2151 RL=0 is mute;
		   Reset also leaves TL=127. Default RL=L+R until guest $20-$27 only
		   (never rewrite TL). Mirror into regs_ for honest peeks. */
		for (int i = 0; i < 8; i++) {
			opm_.SetReg(0x20 + i, 0xc0);
			regs_[0x20 + i] = 0xc0;
		}
		opm_.SetVolume(0);
	}

	/* Kept for X68k/X1 API compat — no register rewrite. */
	void SetAudibleAssist(int /*enable*/) {}

	void Reset() override
	{
		addrLatch_ = 0;
		writeCount_ = 0;
		keyOnCount_ = 0;
		timerUsec_ = 0;
		irqLatch_ = 0;
		memset(regs_, 0, sizeof(regs_));
		opm_.Reset();
		/* Same as ctor: default RL=L+R until guest $20-$27. Do not touch TL —
		   Operator::Reset leaves TL=127 (silent) until the guest programs it. */
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
		const uint8_t val = (uint8_t)(data & 0xff);
		opm_.SetReg(reg, val);
		regs_[reg] = val;
		writeCount_++;
		g_opmHist[reg]++;
		g_opmHistTotal++;
		/* Clearing timer enable / flag bits drops status — release edge latch
		   so sequencers that arm handlers after the first tick still get IRQs
		   (X68k OPMDRV soft-waits on $A490 via DOS-registered $10C). */
		if (reg == 0x14 && (opm_.ReadStatus() & 0x03) == 0)
			irqLatch_ = 0;
		if (reg == 0x08 && (val & 0x78) != 0)
			keyOnCount_++;
		/* Pass key strobe only for $08 so multi-channel gates stay latched. */
		FmMonShadowSetOpmRegSnapshotEx(regs_, (reg == 0x08) ? (int)val : -1);
	}

	void AdvanceClocks(uint64_t chipCycles) override
	{
		if (clockHz_ == 0) return;
		timerUsec_ += (__int64)chipCycles * 1000000 / (__int64)clockHz_;
		while (timerUsec_ > 0) {
			const int step = (timerUsec_ > 1000) ? 1000 : (int)timerUsec_;
			if (step <= 0) break;
			/* Count returns true on timer A/B expire, but fmgen only raises a
			   status bit when the matching IRQEN bit of reg 0x14 is set, so
			   both flags are legitimate interrupt sources. Rastan drives its
			   sequencer off Timer A alone (reg 14 = 0x35), so masking A here
			   left it silent. Latch on the rising edge rather than the level
			   to avoid re-entering the ISR immediately after EI.
			   After Burner does not use this latch (status poll only). */
			/* Latch once per timer expire while status/IRQEN is live.
			   Rising-edge-only (st1 & ~st0) stalled forever when the ISR left
			   status set (abtengu Alice Soft) — Assist used to paper over that
			   by rewriting 0x14 every soft-wait, which also stomped ys368's
			   Timer B period to $00 (~half tempo). Count-event latch keeps
			   one IRQ per period without level re-entry after EI. */
			if (opm_.Count(step)) {
				if ((opm_.ReadStatus() & 0x03) != 0) {
					irqLatch_ = 1;
					++g_opmIrqEdges;
				}
			}
			timerUsec_ -= step;
		}
	}

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		while (frames > 0) {
			const int n = frames > 64 ? 64 : frames;
			FM::Sample tmp[128];
			memset(tmp, 0, (size_t)n * 2 * sizeof(FM::Sample));
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

	/* Count-event latch: one IRQ per timer expire while status is live.
	   Level Irq() re-entered after EI (~2x); rising-edge-only stalled when
	   status stayed set (Assist had been papering that by stomping 0x14). */
	bool Irq() const override { return irqLatch_ != 0; }
	void AckIrq() override { irqLatch_ = 0; }

	uint8_t ReadStatus() override
	{
		/* Do not clear irqLatch here — M92 OPM-out busy-waits on status and
		   M92SyncIrqs polls every slice; clearing the edge on those reads
		   dropped Timer IRQs for Rev 3.40 sequencers. AckIrq() clears it. */
		return (uint8_t)(opm_.ReadStatus() & 0x03);
	}
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return ReadStatus(); }
	uint8_t ReadDataHi() override { return 0; }

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
	uint8_t regs_[256];
};

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
