#include "StdAfx.h"
#include "cemu_chip_rf5c68.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>

/* Simplified MAME-style RF5C68 core: 8 channels, signed 8-bit PCM RAM/ROM. */
enum { kRf5cChannels = 8, kRf5cShift = 11 };

static int CEmuRf5cClamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipRf5c68 : public CChip {
public:
	CChipRf5c68(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 12500000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
	{
		Reset();
	}

	void Reset() override
	{
		memset(reg_, 0, sizeof(reg_));
		memset(ch_, 0, sizeof(ch_));
		enable_ = 0;
		curCh_ = 0;
		bank_ = 0;
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		const uint8_t a = (uint8_t)(addr & 0xff);
		const uint8_t v = (uint8_t)(data & 0xff);
		FmMonShadowApplyRf5cReg(a, v);
		if (a < 7) {
			Channel& c = ch_[curCh_ & 7];
			reg_[(curCh_ & 7) * 8 + a] = v;
			switch (a) {
			case 0: c.env = v; break;
			case 1: c.pan = v; break;
			case 2: c.step = (c.step & 0xff00) | v; break;
			case 3: c.step = (c.step & 0x00ff) | (v << 8); break;
			case 4: c.loop = (c.loop & 0xff00) | v; break;
			case 5: c.loop = (c.loop & 0x00ff) | (v << 8); break;
			case 6: c.start = (uint32_t)v << 8; c.addr = c.start << kRf5cShift; break;
			}
		} else if (a == 0x07 || a == 0xff) {
			enable_ = (v & 0x80) ? 0 : 1;
			curCh_ = v & 7;
		} else if (a == 0x08) {
			bank_ = v & 0x0f;
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
		if (!stereo || frames <= 0 || !enable_) return;
		for (int i = 0; i < frames; i++) {
			int l = 0, r = 0;
			for (int ch = 0; ch < kRf5cChannels; ch++) {
				Channel& c = ch_[ch];
				if (c.env == 0 || c.pan == 0 || c.step == 0) continue;
				const uint32_t off = c.addr >> kRf5cShift;
				const uint8_t b = ReadMem(off);
				if (b == 0xff) {
					c.addr = c.loop << kRf5cShift;
					continue;
				}
				const int s = (int)((int8_t)b) * c.env;
				l += s * ((c.pan >> 4) & 0x0f) / 16;
				r += s * (c.pan & 0x0f) / 16;
				uint32_t step = (uint32_t)((uint64_t)c.step * clockHz_ / ((uint64_t)sampleRate_ * 384u));
				if (!step) step = 1;
				c.addr += step;
			}
			stereo[i * 2] = (int16_t)CEmuRf5cClamp16((int)stereo[i * 2] + l * gain / 256);
			stereo[i * 2 + 1] = (int16_t)CEmuRf5cClamp16((int)stereo[i * 2 + 1] + r * gain / 256);
		}
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override { rom_ = data; romSize_ = size; }
	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap == 0) return 0;
		const unsigned n = cap < sizeof(reg_) ? cap : (unsigned)sizeof(reg_);
		memcpy(buf, reg_, n);
		return n;
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

private:
	struct Channel {
		uint8_t env, pan;
		uint16_t step, loop;
		uint32_t start, addr;
	};

	uint8_t ReadMem(uint32_t off) const
	{
		const uint32_t adr = ((uint32_t)bank_ << 12) + off;
		if (!rom_ || romSize_ == 0) return 0xff;
		return rom_[adr % romSize_];
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint8_t reg_[kRf5cChannels * 8];
	uint8_t enable_;
	uint8_t curCh_;
	uint8_t bank_;
	Channel ch_[kRf5cChannels];
};

CChip* CEmuChipRf5c68Create(uint32_t clockHz, int sampleRate)
{
	return new CChipRf5c68(clockHz, sampleRate);
}

void CEmuChipRf5c68Destroy(CChip* c)
{
	delete c;
}
