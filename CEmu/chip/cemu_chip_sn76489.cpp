#include "StdAfx.h"
#include "cemu_chip_sn76489.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>
#include <math.h>

/* Adapted from MAME/hoot ssSN76496 — self-contained, no ss* deps. */
enum { kSnStep = 0x10000, kSnMaxOut = 0x7fff };
#define FB_WNOISE 0x12000
#define FB_PNOISE 0x08000
#define NG_PRESET 0x0f35

class CChipSn76489 : public CChip {
public:
	CChipSn76489(uint32_t clockHz, int sampleRate)
		: sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, clockHz_(clockHz ? clockHz : 3579545u)
		, writeCount_(0)
		, lastReg_(0)
		, updateStep_(0)
	{
		memset(regs_, 0, sizeof(regs_));
		memset(period_, 0, sizeof(period_));
		memset(count_, 0, sizeof(count_));
		memset(output_, 0, sizeof(output_));
		memset(volume_, 0, sizeof(volume_));
		memset(volTable_, 0, sizeof(volTable_));
		rng_ = NG_PRESET;
		noiseFb_ = FB_WNOISE;
		BuildVolTable();
		Reset();
	}

	void Reset() override
	{
		writeCount_ = 0;
		lastReg_ = 0;
		updateStep_ = ((double)kSnStep * (double)sampleRate_ * 16.0) / (double)clockHz_;
		if (updateStep_ < 1.0) updateStep_ = 1.0;
		for (int i = 0; i < 4; i++) {
			volume_[i] = 0;
			output_[i] = 0;
			period_[i] = count_[i] = (int)updateStep_;
		}
		for (int i = 0; i < 8; i += 2) {
			regs_[i] = 0;
			regs_[i + 1] = 0x0f;
		}
		rng_ = NG_PRESET;
		noiseFb_ = FB_WNOISE;
		output_[3] = rng_ & 1;
		Shadow();
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		(void)addr;
		WriteByte((uint8_t)(data & 0xff));
	}

	void AdvanceClocks(uint64_t chipCycles) override
	{
		(void)chipCycles; /* sample-driven in Render */
	}

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		while (frames > 0) {
			int mix = 0;
			/* Advance one host sample. */
			for (int i = 0; i < 3; i++) {
				int volAcc = 0;
				if (output_[i]) volAcc += count_[i];
				count_[i] -= kSnStep;
				while (count_[i] <= 0) {
					count_[i] += period_[i];
					if (count_[i] > 0) {
						output_[i] ^= 1;
						if (output_[i]) volAcc += period_[i];
						break;
					}
					count_[i] += period_[i];
					volAcc += period_[i];
				}
				if (output_[i]) volAcc -= count_[i];
				mix += volAcc * volume_[i];
			}
			{
				int left = kSnStep;
				int volAcc = 0;
				do {
					int nextevent = (count_[3] < left) ? count_[3] : left;
					if (output_[3]) volAcc += count_[3];
					count_[3] -= nextevent;
					if (count_[3] <= 0) {
						if (rng_ & 1) rng_ ^= noiseFb_;
						rng_ >>= 1;
						output_[3] = rng_ & 1;
						count_[3] += period_[3];
						if (output_[3]) volAcc += period_[3];
					}
					if (output_[3]) volAcc -= count_[3];
					left -= nextevent;
				} while (left > 0);
				mix += volAcc * volume_[3];
			}
			if (mix > kSnMaxOut * kSnStep) mix = kSnMaxOut * kSnStep;
			if (mix < 0) mix = 0;
			const int16_t s = (int16_t)(mix / kSnStep);
			*stereo++ = s;
			*stereo++ = s;
			frames--;
		}
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

	unsigned WriteCount() const { return writeCount_; }

private:
	void BuildVolTable()
	{
		double out = (double)kSnMaxOut / 3.0;
		for (int i = 0; i < 15; i++) {
			volTable_[i] = (out > (double)kSnMaxOut / 3.0)
				? (kSnMaxOut / 3) : (int)out;
			out /= 1.258925412; /* 2 dB */
		}
		volTable_[15] = 0;
	}

	void WriteByte(uint8_t data)
	{
		FmMonShadowWriteSnByte(data);
		writeCount_++;
		if (data & 0x80) {
			const int r = (data & 0x70) >> 4;
			const int c = r / 2;
			lastReg_ = r;
			regs_[r] = (regs_[r] & 0x3f0) | (data & 0x0f);
			ApplyReg(r, c);
		} else {
			const int r = lastReg_;
			const int c = r / 2;
			if (r == 0 || r == 2 || r == 4) {
				regs_[r] = (regs_[r] & 0x0f) | ((data & 0x3f) << 4);
				ApplyReg(r, c);
			}
		}
		Shadow();
	}

	void ApplyReg(int r, int c)
	{
		switch (r) {
		case 0: case 2: case 4: {
			int freq = regs_[r];
			period_[c] = (int)(updateStep_ * (double)freq);
			if (period_[c] == 0) period_[c] = (int)updateStep_;
			if (r == 4 && (regs_[6] & 0x03) == 0x03)
				period_[3] = 2 * period_[2];
			break;
		}
		case 1: case 3: case 5: case 7:
			volume_[c] = volTable_[regs_[r] & 0x0f];
			break;
		case 6: {
			const int n = regs_[6];
			noiseFb_ = (n & 4) ? FB_WNOISE : FB_PNOISE;
			const int mode = n & 3;
			period_[3] = (mode == 3)
				? (2 * period_[2])
				: ((int)updateStep_ << (5 + mode));
			rng_ = NG_PRESET;
			output_[3] = rng_ & 1;
			break;
		}
		default:
			break;
		}
	}

	void Shadow()
	{
		unsigned tone[3] = {
			(unsigned)(regs_[0] & 0x3ff),
			(unsigned)(regs_[2] & 0x3ff),
			(unsigned)(regs_[4] & 0x3ff)
		};
		unsigned vol[4] = {
			(unsigned)(regs_[1] & 0x0f),
			(unsigned)(regs_[3] & 0x0f),
			(unsigned)(regs_[5] & 0x0f),
			(unsigned)(regs_[7] & 0x0f)
		};
		unsigned on = 0;
		for (int i = 0; i < 3; i++)
			if (vol[i] < 0x0f) on |= (1u << i);
		if (vol[3] < 0x0f) on |= 8u;
		unsigned noisePer = 0;
		const int mode = regs_[6] & 3;
		if (mode == 3) noisePer = tone[2];
		else noisePer = 0x10u << mode;
		FmMonShadowApplySn76489(tone, noisePer, vol, on);
	}

	int sampleRate_;
	uint32_t clockHz_;
	unsigned writeCount_;
	int lastReg_;
	double updateStep_;
	int regs_[8];
	int period_[4];
	int count_[4];
	int output_[4];
	int volume_[4];
	int volTable_[16];
	int rng_;
	int noiseFb_;
};

CChip* CEmuChipSn76489Create(uint32_t clockHz, int sampleRate)
{
	return new CChipSn76489(clockHz, sampleRate);
}

void CEmuChipSn76489Destroy(CChip* c)
{
	delete c;
}

unsigned CEmuChipSn76489WriteCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipSn76489*>(c)->WriteCount();
}
