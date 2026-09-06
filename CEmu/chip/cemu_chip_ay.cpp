#include "StdAfx.h"
#include "cemu_chip_ay.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include "types.h"
#include "psg.h"
#include <string.h>

class CChipAy : public CChip {
public:
	CChipAy(uint32_t clockHz, int sampleRate)
		: sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, clockHz_(clockHz ? clockHz : 2000000u)
		, addrLatch_(0)
		, writeCount_(0)
		, unmuteAssist_(0)
	{
		/* hoot ssAY8910 Initialize(clock) then SetClock(clock/2). */
		const int psgClk = (int)(clockHz_ / 2u);
		psg_.SetClock(psgClk > 0 ? psgClk : 1000000, sampleRate_);
		psg_.SetVolume(0);
		psg_.Reset();
		memset(regs_, 0, sizeof(regs_));
		memset(regWriteCount_, 0, sizeof(regWriteCount_));
	}

	void SetUnmuteAssist(int enable) { unmuteAssist_ = enable ? 1 : 0; }
	void SetPortA(uint8_t v) { portA_ = v; }

	void Reset() override
	{
		addrLatch_ = 0;
		writeCount_ = 0;
		portA_ = 0xff;
		memset(regs_, 0, sizeof(regs_));
		memset(regWriteCount_, 0, sizeof(regWriteCount_));
		psg_.Reset();
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		if ((addr & 1) == 0) {
			addrLatch_ = (uint8_t)(data & 0x0f);
			return;
		}
		psg_.SetReg(addrLatch_, (uint8_t)(data & 0xff));
		regs_[addrLatch_ & 15] = (uint8_t)(data & 0xff);
		regWriteCount_[addrLatch_ & 15]++;
		writeCount_++;
		FmMonShadowWriteAyReg(addrLatch_, data);
	}

	void AdvanceClocks(uint64_t chipCycles) override
	{
		(void)chipCycles; /* sample-driven in Render */
	}

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		/* MSX/X1: lift leftover mute mixer/vol. Off for other platforms. */
		if (unmuteAssist_ && writeCount_ > 4) {
			uint8_t mix = regs_[7];
			int lifted = 0;
			const int noisePer = regs_[6] & 0x1f;
			for (int ch = 0; ch < 3; ch++) {
				const int per = regs_[ch * 2] | ((regs_[ch * 2 + 1] & 0x0f) << 8);
				const int toneOff = (mix & (1u << ch)) != 0;
				const int noiseOff = (mix & (8u << ch)) != 0;
				const int vol = regs_[8 + ch] & 0x1f;
				const int silent = vol == 0;
				const int quiet = vol > 0 && vol < 0x08;
				const int hasTone = per != 0;
				const int hasNoise = !noiseOff && (noisePer != 0 || writeCount_ > 6);
				if (!silent && !quiet && !toneOff) continue;
				if (!hasTone && !hasNoise && writeCount_ <= 6) continue;
				if (toneOff && (hasTone || writeCount_ > 6)) {
					mix = (uint8_t)(mix & ~(1u << ch));
					psg_.SetReg(7, mix);
					regs_[7] = mix;
				}
				if ((silent || quiet) && (hasTone || hasNoise || writeCount_ > 6)) {
					psg_.SetReg(8 + ch, 0x0c);
					regs_[8 + ch] = 0x0c;
				}
				lifted = 1;
			}
			if (!lifted && writeCount_ > 6) {
				mix = (uint8_t)(mix & ~0x01);
				psg_.SetReg(7, mix);
				regs_[7] = mix;
				if ((regs_[8] & 0x1f) < 0x08) {
					psg_.SetReg(8, 0x0c);
					regs_[8] = 0x0c;
				}
				if ((regs_[0] | (regs_[1] & 0x0f)) == 0) {
					psg_.SetReg(0, 0x5c);
					psg_.SetReg(1, 0x00);
					regs_[0] = 0x5c;
					regs_[1] = 0x00;
				}
			}
		}
		while (frames > 0) {
			const int n = frames > 64 ? 64 : frames;
			PSG::Sample tmp[128];
			memset(tmp, 0, (size_t)n * 2 * sizeof(PSG::Sample));
			psg_.Mix(tmp, n);
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

	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override
	{
		if (addrLatch_ == 0x0e)
			return portA_;
		return (uint8_t)psg_.GetReg(addrLatch_);
	}
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return ReadData(); }

	unsigned WriteCount() const { return writeCount_; }
	uint8_t AddrLatch() const { return addrLatch_; }
	int PeekRegs(unsigned char* out16) const
	{
		if (!out16) return 0;
		memcpy(out16, regs_, 16);
		return 1;
	}
	void PeekRegWrites(unsigned out16[16]) const
	{
		if (out16) memcpy(out16, regWriteCount_, sizeof(regWriteCount_));
	}

private:
	PSG psg_;
	int sampleRate_;
	uint32_t clockHz_;
	uint8_t addrLatch_;
	unsigned writeCount_;
	uint8_t regs_[16];
	unsigned regWriteCount_[16];
	int unmuteAssist_;
	uint8_t portA_;
};

CChip* CEmuChipAyCreate(uint32_t clockHz, int sampleRate)
{
	return new CChipAy(clockHz, sampleRate);
}

void CEmuChipAySetUnmuteAssist(CChip* c, int enable)
{
	if (!c) return;
	static_cast<CChipAy*>(c)->SetUnmuteAssist(enable);
}

void CEmuChipAySetPortA(CChip* c, uint8_t v)
{
	if (!c) return;
	static_cast<CChipAy*>(c)->SetPortA(v);
}

void CEmuChipAyDestroy(CChip* c)
{
	delete c;
}

unsigned CEmuChipAyWriteCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipAy*>(c)->WriteCount();
}

int CEmuChipAyPeekRegs(const CChip* c, unsigned char* out16)
{
	if (!c) return 0;
	return static_cast<const CChipAy*>(c)->PeekRegs(out16);
}

void CEmuChipAyPeekRegWrites(const CChip* c, unsigned out16[16])
{
	if (!c) {
		if (out16) memset(out16, 0, 16 * sizeof(unsigned));
		return;
	}
	static_cast<const CChipAy*>(c)->PeekRegWrites(out16);
}
