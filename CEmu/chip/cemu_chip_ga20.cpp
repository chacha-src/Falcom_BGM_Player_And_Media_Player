#include "StdAfx.h"
#include "cemu_chip_ga20.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>

/* Port of MAME src/devices/sound/iremga20.cpp (BSD-3-Clause,
   Acho A. Tang / R. Belmont / Valley Bell). Native stream rate is clock/4;
   host samples are produced by running that many chip steps per output frame. */
enum { kGa20Voices = 4, kGa20Shift = 12 };

static int CEmuGa20Clamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipGa20 : public CChip {
public:
	CChipGa20(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 3579545u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
		, step_(0)
		, acc_(0)
		, last_(0)
	{
		step_ = (int)(((uint64_t)(clockHz_ / 4u) << kGa20Shift) / (uint64_t)sampleRate_);
		if (step_ <= 0) step_ = 1;
		Reset();
	}

	void Reset() override
	{
		memset(regs_, 0, sizeof(regs_));
		memset(ch_, 0, sizeof(ch_));
		acc_ = 0;
		last_ = 0;
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		const unsigned off = addr & 0x1f;
		const uint8_t v = (uint8_t)(data & 0xff);
		regs_[off] = v;
		FmMonShadowApplyGa20Reg(off, v);
		const int c = (int)(off >> 3);
		switch (off & 7) {
		case 4:
			ch_[c].rate = v;
			break;
		case 5:
			ch_[c].volume = (unsigned)((v * 256) / (v + 10));
			break;
		case 6:
			/* d1: key on/off */
			if (v & 2) {
				ch_[c].play = 1;
				ch_[c].pos = (uint32_t)((regs_[(c << 3) | 0] | (regs_[(c << 3) | 1] << 8)) << 4);
				ch_[c].end = (uint32_t)((regs_[(c << 3) | 2] | (regs_[(c << 3) | 3] << 8)) << 4);
				ch_[c].counter = 0x100;
			} else {
				ch_[c].play = 0;
			}
			break;
		default:
			break;
		}
	}

	void AdvanceClocks(uint64_t chipCycles) override { (void)chipCycles; }

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		memset(stereo, 0, (size_t)frames * 2 * sizeof(int16_t));
		MixAdd(stereo, frames, 256);
	}

	void MixAdd(int16_t* stereo, int frames, int gain) override
	{
		if (!stereo || frames <= 0) return;
		for (int i = 0; i < frames; i++) {
			int32_t out = 0;
			int n = 0;
			acc_ += step_;
			while (acc_ >= (1 << kGa20Shift)) {
				acc_ -= (1 << kGa20Shift);
				out += StepOnce();
				n++;
			}
			if (n > 1) out /= n;
			else if (n == 0) out = last_;
			last_ = out;
			/* MAME normalises by 32768*4; keep the same headroom. */
			const int s = (int)(out / 4) * gain / 256;
			stereo[i * 2] = (int16_t)CEmuGa20Clamp16((int)stereo[i * 2] + s);
			stereo[i * 2 + 1] = (int16_t)CEmuGa20Clamp16((int)stereo[i * 2 + 1] + s);
		}
	}

	uint8_t ReadStatus() override
	{
		uint8_t d = 0;
		for (int c = 0; c < kGa20Voices; c++)
			if (ch_[c].play) d |= (uint8_t)(1u << c);
		return d;
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override { rom_ = data; romSize_ = size; }
	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap == 0) return 0;
		const unsigned n = cap < sizeof(regs_) ? cap : (unsigned)sizeof(regs_);
		memcpy(buf, regs_, n);
		return n;
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return ReadStatus(); }
	uint8_t ReadDataHi() override { return 0; }

private:
	struct Voice {
		uint32_t rate;
		uint32_t pos;
		uint32_t counter;
		uint32_t end;
		uint32_t volume;
		uint32_t play;
	};

	int32_t StepOnce()
	{
		int32_t sampleout = 0;
		for (int c = 0; c < kGa20Voices; c++) {
			Voice& v = ch_[c];
			if (!v.play) continue;
			const int sample = (rom_ && v.pos < romSize_) ? (int)rom_[v.pos] : 0;
			if (sample == 0x00) { /* sample end marker */
				v.play = 0;
				continue;
			}
			sampleout += (sample - 0x80) * (int32_t)v.volume;
			v.counter--;
			if (v.counter <= v.rate) {
				v.pos++;
				v.counter = 0x100;
			}
		}
		return sampleout;
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	int step_;
	int acc_;
	int32_t last_;
	uint8_t regs_[0x20];
	Voice ch_[kGa20Voices];
};

CChip* CEmuChipGa20Create(uint32_t clockHz, int sampleRate)
{
	return new CChipGa20(clockHz, sampleRate);
}

void CEmuChipGa20Destroy(CChip* c)
{
	delete c;
}
