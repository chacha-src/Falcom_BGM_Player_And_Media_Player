#include "StdAfx.h"
#include "cemu_chip_rf5c68.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>
#include <stdlib.h>

/* MAME src/devices/sound/rf5c68.cpp の 8ch PCM（BSD-3-Clause）。
   レジスタ 0-6 は選択中チャネル、7=enable/cbank/wbank、8=チャネル on/off。
   波形は 64KB RAM。A12=1 の 4KB 窓が wbank 経由。 */
enum { kRf5cChannels = 8, kRf5cShift = 11, kRf5cRam = 0x10000 };

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
		ram_ = (uint8_t*)malloc(kRf5cRam);
		Reset();
	}
	~CChipRf5c68() override
	{
		free(ram_);
	}

	void Reset() override
	{
		memset(reg_, 0, sizeof(reg_));
		memset(ch_, 0, sizeof(ch_));
		enable_ = 0;
		cbank_ = 0;
		wbank_ = 0;
		if (ram_)
			memset(ram_, 0, kRf5cRam);
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		const uint8_t a = (uint8_t)(addr & 0x0f);
		const uint8_t v = (uint8_t)(data & 0xff);
		FmMonShadowApplyRf5cReg(a, v);
		FmMonShadowSetCompanionRegs(reg_, (unsigned)sizeof(reg_));
		Channel& c = ch_[cbank_ & 7];
		switch (a) {
		case 0:
			c.env = v;
			reg_[(cbank_ & 7) * 8] = v;
			break;
		case 1:
			c.pan = v;
			reg_[(cbank_ & 7) * 8 + 1] = v;
			break;
		case 2:
			c.step = (uint16_t)((c.step & 0xff00) | v);
			reg_[(cbank_ & 7) * 8 + 2] = v;
			break;
		case 3:
			c.step = (uint16_t)((c.step & 0x00ff) | (v << 8));
			reg_[(cbank_ & 7) * 8 + 3] = v;
			break;
		case 4:
			c.loop = (uint16_t)((c.loop & 0xff00) | v);
			reg_[(cbank_ & 7) * 8 + 4] = v;
			break;
		case 5:
			c.loop = (uint16_t)((c.loop & 0x00ff) | (v << 8));
			reg_[(cbank_ & 7) * 8 + 5] = v;
			break;
		case 6:
			c.start = v;
			reg_[(cbank_ & 7) * 8 + 6] = v;
			if (!c.on)
				c.addr = (uint32_t)c.start << (8 + kRf5cShift);
			break;
		case 7:
			/* bit7=チップ enable。bit6=1 なら ch 選択、0 なら波形窓バンク。 */
			enable_ = (uint8_t)((v >> 7) & 1);
			if (v & 0x40)
				cbank_ = (uint8_t)(v & 7);
			else
				wbank_ = (uint16_t)((v & 0x0f) << 12);
			reg_[7] = v;
			break;
		case 8:
			/* ビット=0 でチャネル ON（MAME ~data）。 */
			for (int i = 0; i < kRf5cChannels; i++) {
				const int on = ((~v >> i) & 1);
				ch_[i].on = (uint8_t)on;
				if (!on)
					ch_[i].addr = (uint32_t)ch_[i].start << (8 + kRf5cShift);
			}
			reg_[8] = v;
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
		if (!stereo || frames <= 0 || !enable_) return;
		for (int i = 0; i < frames; i++) {
			int l = 0, r = 0;
			for (int ci = 0; ci < kRf5cChannels; ci++) {
				Channel& c = ch_[ci];
				if (!c.on || c.env == 0) continue;
				const uint8_t b = ReadMem((c.addr >> kRf5cShift) & 0xffffu);
				int sample = (int)b;
				if (sample == 0xff) {
					c.addr = (uint32_t)c.loop << kRf5cShift;
					sample = (int)ReadMem((c.addr >> kRf5cShift) & 0xffffu);
					if (sample == 0xff)
						continue;
				}
				const int lv = (c.pan & 0x0f) * (int)c.env;
				const int rv = ((c.pan >> 4) & 0x0f) * (int)c.env;
				if (sample & 0x80) {
					sample &= 0x7f;
					l += (sample * lv) >> 5;
					r += (sample * rv) >> 5;
				} else {
					l -= (sample * lv) >> 5;
					r -= (sample * rv) >> 5;
				}
				uint32_t step = (uint32_t)((uint64_t)c.step * clockHz_ / ((uint64_t)sampleRate_ * 384u));
				if (!step) step = 1;
				c.addr += step;
			}
			stereo[i * 2] = (int16_t)CEmuRf5cClamp16((int)stereo[i * 2] + l * gain / 256);
			stereo[i * 2 + 1] = (int16_t)CEmuRf5cClamp16((int)stereo[i * 2 + 1] + r * gain / 256);
		}
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		rom_ = data;
		romSize_ = size;
		if (ram_ && data && size) {
			const unsigned n = size < kRf5cRam ? size : (unsigned)kRf5cRam;
			memcpy(ram_, data, n);
		}
	}
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

	uint8_t MemR(unsigned addr) const
	{
		const unsigned a = (wbank_ | (addr & 0xfffu)) & 0xffffu;
		return ReadMem(a);
	}
	void MemW(unsigned addr, uint8_t v)
	{
		if (!ram_) return;
		const unsigned a = (wbank_ | (addr & 0xfffu)) & 0xffffu;
		ram_[a] = v;
	}

private:
	struct Channel {
		uint8_t env, pan, on, start;
		uint16_t step, loop;
		uint32_t addr;
	};

	uint8_t ReadMem(uint32_t off) const
	{
		off &= 0xffffu;
		if (ram_)
			return ram_[off];
		if (rom_ && romSize_)
			return rom_[off % romSize_];
		return 0xff;
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint8_t* ram_;
	uint8_t reg_[kRf5cChannels * 8 + 8];
	uint8_t enable_;
	uint8_t cbank_;
	uint16_t wbank_;
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

uint8_t CEmuChipRf5c68MemR(CChip* c, unsigned addr)
{
	return c ? static_cast<CChipRf5c68*>(c)->MemR(addr) : 0xff;
}

void CEmuChipRf5c68MemW(CChip* c, unsigned addr, uint8_t v)
{
	if (c)
		static_cast<CChipRf5c68*>(c)->MemW(addr, v);
}
