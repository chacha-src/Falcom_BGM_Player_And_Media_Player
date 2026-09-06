#include "StdAfx.h"
#include "cemu_chip_k053260.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>

/* Simplified from hoot ss053260.cpp / MAME K053260. */
enum { kK053260Channels = 4, kK053260Shift = 16 };

static int CEmuK053Clamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipK053260 : public CChip {
public:
	CChipK053260(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 3579545u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
	{
		BuildDelta();
		Reset();
	}

	void Reset() override
	{
		memset(reg_, 0, sizeof(reg_));
		memset(port_, 0xff, sizeof(port_)); /* idle = 0xFF (not stop-code 0x00) */
		memset(ch_, 0, sizeof(ch_));
		mode_ = 0;
		keyOn_ = 0;
	}

	uint8_t ReadReg(unsigned offset)
	{
		const unsigned r = offset & 0x3fu;
		if (r <= 0x01u) {
			const uint8_t v = port_[r];
			/* Destructive read — main leaves the byte until sound drains it;
			   otherwise every IRQ restarts the same song. */
			port_[r] = 0xff;
			return v;
		}
		if (r == 0x2e) {
			/* ROM readback while mode bit0 set (boot checksum / sample probe). */
			if (!(mode_ & 1) || !rom_)
				return 0;
			Channel& vc = ch_[0];
			const uint32_t offs = vc.start + (vc.pos >> kK053260Shift);
			vc.pos += (1u << kK053260Shift);
			return (offs < romSize_) ? rom_[offs] : 0;
		}
		if (r == 0x29u) {
			uint8_t st = 0;
			for (int i = 0; i < kK053260Channels; i++)
				if (ch_[i].play) st = (uint8_t)(st | (1u << i));
			return st;
		}
		return 0;
	}

	void MainWrite(unsigned offset, uint8_t data)
	{
		port_[offset & 1u] = data; /* main → sound */
	}

	uint8_t MainRead(unsigned offset) const
	{
		return port_[2u + (offset & 1u)]; /* sound → main */
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		const uint8_t r = (uint8_t)(addr & 0x3f);
		if (r > 0x2f) return;
		const uint8_t v = (uint8_t)(data & 0xff);
		/* 0x00/0x01 are read-only on the sound side (main→sub ports). */
		if (r <= 0x01)
			return;
		if (r == 0x02 || r == 0x03) {
			port_[r] = v; /* sound → main */
			return;
		}
		if (r == 0x28) {
			const uint8_t rising = (uint8_t)(v & ~keyOn_);
			keyOn_ = v;
			reg_[r] = v;
			for (int i = 0; i < kK053260Channels; i++) {
				ch_[i].reverse = (v >> (4 + i)) & 1;
				if (rising & (1 << i))
					Start(i);
				else if ((v & (1 << i)) == 0)
					Stop(i);
			}
			return;
		}
		reg_[r] = v;
		if (r >= 8 && r < 0x28) {
			const int c = (r - 8) / 8;
			Channel& vc = ch_[c];
			switch ((r - 8) & 7) {
			case 0: vc.rate = (uint16_t)((vc.rate & 0x0f00) | v); break;
			case 1: vc.rate = (uint16_t)((vc.rate & 0x00ff) | ((v & 0x0f) << 8)); break;
			case 2: vc.size = (uint16_t)((vc.size & 0xff00) | v); break;
			case 3: vc.size = (uint16_t)((vc.size & 0x00ff) | (v << 8)); break;
			case 4: vc.start = (vc.start & 0x1fff00u) | v; break;
			case 5: vc.start = (vc.start & 0x1f00ffu) | ((uint32_t)v << 8); break;
			case 6: vc.start = (vc.start & 0x00ffffu) | (((uint32_t)v & 0x1fu) << 16); break;
			case 7: vc.volume = (uint8_t)(v & 0x7f); break;
			}
		} else if (r == 0x2a) {
			for (int i = 0; i < kK053260Channels; i++) {
				ch_[i].loop = (v >> i) & 1;
				ch_[i].ppcm = (v >> (4 + i)) & 1;
			}
		} else if (r == 0x2c) {
			ch_[0].pan = v & 7;
			ch_[1].pan = (v >> 3) & 7;
		} else if (r == 0x2d) {
			ch_[2].pan = v & 7;
			ch_[3].pan = (v >> 3) & 7;
		} else if (r == 0x2f) {
			mode_ = v & 7;
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
		if (!stereo || frames <= 0 || !rom_ || !(mode_ & 2)) return;
		for (int i = 0; i < frames; i++) {
			int l = 0, r = 0;
			for (int c = 0; c < kK053260Channels; c++) {
				Channel& vc = ch_[c];
				if (!vc.play) continue;
				const uint32_t pos = vc.pos >> kK053260Shift;
				if (pos >= vc.size) {
					if (vc.loop) vc.pos = 0;
					else { Stop(c); continue; }
				}
				const uint32_t base = vc.start;
				const uint32_t off = vc.pos >> kK053260Shift;
				const uint32_t adr = vc.reverse ? (base - off) : (base + off);
				const int8_t s = (adr < romSize_) ? (int8_t)rom_[adr] : 0;
				const int pan = vc.pan & 7;
				/* Approximate MAME pan_mul[8][2] with integer weights. */
				static const int kPanL[8] = { 0, 256, 234, 210, 181, 148, 104, 0 };
				static const int kPanR[8] = { 0, 0, 104, 148, 181, 210, 234, 256 };
				l += (s * (int)vc.volume * kPanL[pan]) >> 8;
				r += (s * (int)vc.volume * kPanR[pan]) >> 8;
				vc.pos += delta_[vc.rate & 0x0fff];
			}
			stereo[i * 2] = (int16_t)CEmuK053Clamp16((int)stereo[i * 2] + l * gain / 256);
			stereo[i * 2 + 1] = (int16_t)CEmuK053Clamp16((int)stereo[i * 2 + 1] + r * gain / 256);
		}
	}

	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

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

private:
	struct Channel {
		uint16_t rate, size;
		uint32_t start;
		uint8_t volume, play, pan, loop, ppcm, reverse;
		uint32_t pos;
		int ppcmData;
	};

	void BuildDelta()
	{
		for (int i = 0; i < 0x1000; i++) {
			double v = (double)(0x1000 - i);
			double target = (double)clockHz_ / v;
			uint32_t d = (target > 0.0) ? (uint32_t)((double)(1 << kK053260Shift) / ((double)sampleRate_ / target)) : 1;
			delta_[i] = d ? d : 1;
		}
	}

	void Start(int c)
	{
		ch_[c].play = 1;
		/* MAME: KADPCM starts at nybble 1 due to preincrement. */
		ch_[c].pos = ch_[c].ppcm ? (1u << kK053260Shift) : 0;
		ch_[c].ppcmData = 0;
		FmMonShadowPcmNote(c, 60 + c, 1);
	}

	void Stop(int c)
	{
		ch_[c].play = 0;
		FmMonShadowPcmNote(c, 60 + c, 0);
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint8_t reg_[0x30];
	uint8_t port_[4]; /* 0/1 main→sound, 2/3 sound→main */
	uint8_t mode_;
	uint8_t keyOn_;
	uint32_t delta_[0x1000];
	Channel ch_[kK053260Channels];
};

CChip* CEmuChipK053260Create(uint32_t clockHz, int sampleRate)
{
	return new CChipK053260(clockHz, sampleRate);
}

void CEmuChipK053260Destroy(CChip* c)
{
	delete c;
}

uint8_t CEmuChipK053260Read(CChip* c, unsigned offset)
{
	return c ? static_cast<CChipK053260*>(c)->ReadReg(offset) : 0;
}

void CEmuChipK053260MainWrite(CChip* c, unsigned offset, uint8_t data)
{
	if (c) static_cast<CChipK053260*>(c)->MainWrite(offset, data);
}

uint8_t CEmuChipK053260MainRead(CChip* c, unsigned offset)
{
	return c ? static_cast<CChipK053260*>(c)->MainRead(offset) : 0;
}
