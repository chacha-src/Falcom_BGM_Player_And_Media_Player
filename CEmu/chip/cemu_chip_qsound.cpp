#include "StdAfx.h"
#include "cemu_chip_qsound.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <math.h>
#include <string.h>

/* Adapted from hoot ssQSound.cpp / MAME qsound.c; signed char ROM samples. */
enum { kQSoundChannels = 16, kQSoundClockDiv = 166, kQSoundLengthDiv = 1 };

static int CEmuQClamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipQSound : public CChip {
public:
	CChipQSound(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 4000000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
		, dataLatch_(0)
	{
		frqRatio_ = ((double)clockHz_ / (double)kQSoundClockDiv) / (double)sampleRate_ * 16.0;
		for (int i = 0; i < 33; i++)
			panTable_[i] = (int)((256.0 / sqrt(32.0)) * sqrt((double)i));
		Reset();
	}

	void Reset() override
	{
		memset(reg_, 0, sizeof(reg_));
		memset(ch_, 0, sizeof(ch_));
		dataLatch_ = 0;
		for (int i = 0; i < kQSoundChannels; i++) {
			ch_[i].lvol = panTable_[16];
			ch_[i].rvol = panTable_[16];
		}
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		const uint8_t v = (uint8_t)(data & 0xff);
		switch (addr & 3) {
		case 0:
			dataLatch_ = (uint16_t)((dataLatch_ & 0x00ff) | (v << 8));
			break;
		case 1:
			dataLatch_ = (uint16_t)((dataLatch_ & 0xff00) | v);
			break;
		case 2:
			WriteCommand(v);
			break;
		default:
			break;
		}
	}

	void WriteCommand(uint8_t cmd)
	{
		int ch = 0;
		int r = 0;
		if (cmd < 0x80) {
			ch = cmd >> 3;
			r = cmd & 7;
		} else if (cmd < 0x90) {
			ch = cmd - 0x80;
			r = 8;
		} else if (cmd >= 0xba && cmd < 0xca) {
			ch = cmd - 0xba;
			r = 9;
		} else {
			return;
		}
		if (ch < 0 || ch >= kQSoundChannels) return;
		const uint16_t value = dataLatch_;
		reg_[ch * 32 + r * 2 + 0] = (uint8_t)(value >> 8);
		reg_[ch * 32 + r * 2 + 1] = (uint8_t)value;
		/* Shadow API uses the native command packing: 8 words per voice.
		   The old *16 stride made channels 8..15 look like offsets >=0x80,
		   so half of QSound's keyboard never reached a PCM row. */
		FmMonShadowApplyQSoundReg((unsigned)(ch * 8 + r), value);

		Channel& c = ch_[ch];
		switch (r) {
		case 0:
			ch_[(ch + 1) & 0x0f].bank = ((uint32_t)value & 0x7f) << 16;
			break;
		case 1:
			c.address = value / kQSoundLengthDiv;
			break;
		case 2:
			c.pitch = (uint32_t)((double)value * frqRatio_) / kQSoundLengthDiv;
			if (!value) c.key = 0;
			break;
		case 4:
			c.loop = value / kQSoundLengthDiv;
			break;
		case 5:
			c.end = value / kQSoundLengthDiv;
			break;
		case 6:
			c.vol = value;
			if (value == 0) c.key = 0;
			else if (!c.key) {
				c.key = 1;
				c.offset = 0;
				c.last = 0;
			}
			break;
		case 8: {
			int pan = (value - 0x10) & 0x3f;
			if (pan > 32) pan = 32;
			c.rvol = panTable_[pan];
			c.lvol = panTable_[32 - pan];
			c.pan = value;
			break;
		}
		case 3:
			c.reg3 = value;
			break;
		case 9:
			c.reg9 = value;
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
		if (!stereo || frames <= 0 || !rom_) return;
		for (int ch = 0; ch < kQSoundChannels; ch++) {
			Channel& c = ch_[ch];
			if (!c.key) continue;
			const int rvol = (c.rvol * (int)c.vol) >> 8;
			const int lvol = (c.lvol * (int)c.vol) >> 8;
			uint32_t address = c.address;
			uint32_t offset = c.offset;
			int last = c.last;
			for (int i = 0; i < frames; i++) {
				const uint32_t count = offset >> 16;
				offset &= 0xffff;
				if (count) {
					address += count;
					if (address >= c.end) {
						if (!c.loop) {
							c.key = 0;
							break;
						}
						address = (c.end - c.loop) & 0xffff;
					}
					const uint32_t romAdr = c.bank + address;
					last = (romAdr < romSize_) ? (int)((int8_t)rom_[romAdr]) : 0;
				}
				const int l = ((last * lvol) >> 6) * gain / 256;
				const int r = ((last * rvol) >> 6) * gain / 256;
				stereo[i * 2] = (int16_t)CEmuQClamp16((int)stereo[i * 2] + l);
				stereo[i * 2 + 1] = (int16_t)CEmuQClamp16((int)stereo[i * 2 + 1] + r);
				offset += c.pitch;
			}
			c.address = address;
			c.offset = offset;
			c.last = last;
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
	uint8_t ReadStatus() override { return 0x80; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return ReadStatus(); }
	uint8_t ReadDataHi() override { return 0; }

private:
	struct Channel {
		uint32_t bank;
		uint32_t address;
		uint32_t pitch;
		uint32_t loop;
		uint32_t end;
		uint32_t offset;
		uint32_t vol;
		uint32_t pan;
		uint16_t reg3;
		uint16_t reg9;
		int rvol;
		int lvol;
		int last;
		uint8_t key;
	};

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	double frqRatio_;
	int panTable_[33];
	uint16_t dataLatch_;
	uint8_t reg_[kQSoundChannels * 32];
	Channel ch_[kQSoundChannels];
};

CChip* CEmuChipQSoundCreate(uint32_t clockHz, int sampleRate)
{
	return new CChipQSound(clockHz, sampleRate);
}

void CEmuChipQSoundDestroy(CChip* c)
{
	delete c;
}

void CEmuChipQSoundWriteCommand(CChip* c, uint8_t data)
{
	if (!c) return;
	static_cast<CChipQSound*>(c)->WriteCommand(data);
}
