#include "StdAfx.h"
#include "cemu_chip_x1_010.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>

/* Seta X1-010, modelled on MAME x1_010.cpp.

   The chip is an 8 KB RAM window. $0000-$007F is 16 channels x 8 control
   bytes; everything above that is table space that the control bytes index
   in 128-byte pages:

     reg 0  status   bit0 = key on, bit1 = 0:PCM 1:wavetable, bit2 = env one-shot
     reg 1  PCM: volume, low nibble = left, high nibble = right
            wave: waveform page number
     reg 2  PCM: frequency (low 5 bits)      wave: pitch low
     reg 3  wave: pitch high
     reg 4  PCM: sample start, in 4 KB units  wave: envelope period
     reg 5  PCM: sample end as 0x100-end   wave: envelope page number
*/
enum {
	kX1010Channels = 16,
	kX1010RamSize = 0x2000,
	kX1010FreqBits = 14,
	/* An 8-bit sample times a 4-bit volume only reaches +-1920, so scale to
	   full range the way MAME's VOL_BASE does. */
	kX1010VolScale = 16
};

static int CEmuX1010Clamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipX1010 : public CChip {
public:
	CChipX1010(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 16000000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
	{
		Reset();
	}

	void Reset() override
	{
		memset(ram_, 0, sizeof(ram_));
		memset(smpOffs_, 0, sizeof(smpOffs_));
		memset(envOffs_, 0, sizeof(envOffs_));
		memset(playing_, 0, sizeof(playing_));
	}

	uint8_t ReadRam(unsigned offset) const
	{
		return ram_[offset & (kX1010RamSize - 1)];
	}

	void WriteRam(unsigned offset, uint8_t data)
	{
		const unsigned a = offset & (kX1010RamSize - 1);
		if (a < kX1010Channels * 8u) {
			const int ch = (int)(a >> 3);
			const unsigned reg = a & 7u;
			if (reg == 0) {
				const int wasOn = ram_[a] & 1;
				const int nowOn = data & 1;
				/* A fresh key-on restarts the sample and the envelope; the
				   driver rewrites reg0 every frame while a note is held, so
				   only an off->on edge may rewind. */
				if (nowOn && !wasOn) {
					smpOffs_[ch] = 0;
					envOffs_[ch] = 0;
					SetPlaying(ch, 1);
				} else if (!nowOn && wasOn) {
					SetPlaying(ch, 0);
				}
			}
		}
		ram_[a] = data;
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		WriteRam((unsigned)addr, (uint8_t)(data & 0xff));
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
		for (int ch = 0; ch < kX1010Channels; ch++) {
			uint8_t* reg = &ram_[ch * 8];
			if (!(reg[0] & 1)) continue;
			if (reg[0] & 2)
				MixWave(ch, reg, stereo, frames, gain);
			else
				MixPcm(ch, reg, stereo, frames, gain);
		}
	}

	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		rom_ = data;
		romSize_ = size;
	}

	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		/* Only the control block is meaningful to the register panel; the
		   table pages behind it would just scroll waveform bytes past. */
		if (!buf || cap == 0) return 0;
		const unsigned n = cap < kX1010Channels * 8u ? cap : kX1010Channels * 8u;
		memcpy(buf, ram_, n);
		return n;
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}

private:
	void SetPlaying(int ch, int on)
	{
		if (playing_[ch] == (uint8_t)on) return;
		playing_[ch] = (uint8_t)on;
		FmMonShadowPcmNote(ch, 60 + (ch & 15), on);
	}

	/* PCM: 8-bit signed samples straight out of the sample ROM. reg2's low 5
	   bits scale a fixed 1/8192 divider of the master clock; Meta Fox leaves
	   the register at zero and relies on that default rate. */
	void MixPcm(int ch, const uint8_t* reg, int16_t* stereo, int frames, int gain)
	{
		if (!rom_ || !romSize_) return;
		/* reg5 encodes the sample length as 0x100-pages, counted from the
		   start page in reg4 - it is not an absolute end address. */
		const uint32_t base = (uint32_t)reg[4] * 0x1000u;
		const uint32_t len = (uint32_t)(0x100u - reg[5]) * 0x1000u;
		if (!len) return;
		const unsigned freq = reg[2] & 0x1fu;
		const double rate = (double)clockHz_ / 8192.0
			* (double)(freq ? freq : 4u);
		uint32_t step = (uint32_t)(rate * (double)(1u << kX1010FreqBits)
			/ (double)sampleRate_);
		if (!step) step = 1;
		const int volL = (reg[1] & 0x0f) * kX1010VolScale;
		const int volR = ((reg[1] >> 4) & 0x0f) * kX1010VolScale;
		uint32_t offs = smpOffs_[ch];
		for (int i = 0; i < frames; i++) {
			const uint32_t pos = offs >> kX1010FreqBits;
			if (pos >= len) {
				/* Sample ran out: the chip clears its own key-on bit. */
				ram_[ch * 8] &= (uint8_t)~1u;
				SetPlaying(ch, 0);
				break;
			}
			const uint32_t adr = base + pos;
			const int8_t s = (adr < romSize_) ? (int8_t)rom_[adr] : 0;
			AddSample(stereo, i, s * volL, s * volR, gain);
			offs += step;
		}
		smpOffs_[ch] = offs;
	}

	/* Wavetable: a 128-byte signed waveform page selected by reg1, amplitude
	   shaped by a 128-byte stereo envelope page selected by reg5. */
	void MixWave(int ch, const uint8_t* reg, int16_t* stereo, int frames, int gain)
	{
		const int8_t* wave = (const int8_t*)&ram_[((unsigned)reg[1] * 0x80u)
			& (kX1010RamSize - 1)];
		const uint8_t* env = &ram_[((unsigned)reg[5] * 0x80u)
			& (kX1010RamSize - 1)];
		/* One base step drives both the waveform and the envelope; reg2 is a
		   plain multiplier on the waveform side and reg4 divides the
		   envelope, so a note's pitch and its decay scale independently. */
		const double ebase = (double)clockHz_ / 128.0 / 1024.0 / 4.0;
		const uint32_t unit = (uint32_t)(ebase * (double)(1u << kX1010FreqBits)
			/ (double)sampleRate_);
		if (!reg[2]) return;
		uint32_t step = unit * reg[2];
		if (!step) step = 1;
		uint32_t estep = unit / (reg[4] ? reg[4] : 1u);
		if (!estep) estep = 1;
		uint32_t offs = smpOffs_[ch];
		uint32_t eoffs = envOffs_[ch];
		for (int i = 0; i < frames; i++) {
			const unsigned ei = (eoffs >> kX1010FreqBits);
			if ((reg[0] & 4) && ei >= 0x80u) {
				/* One-shot envelope finished. */
				ram_[ch * 8] &= (uint8_t)~1u;
				SetPlaying(ch, 0);
				break;
			}
			const int8_t s = wave[(offs >> kX1010FreqBits) & 0x7f];
			const uint8_t e = env[ei & 0x7f];
			const int volL = ((e >> 4) & 0x0f) * kX1010VolScale;
			const int volR = (e & 0x0f) * kX1010VolScale;
			AddSample(stereo, i, s * volL, s * volR, gain);
			offs += step;
			eoffs += estep;
		}
		smpOffs_[ch] = offs;
		envOffs_[ch] = eoffs;
	}

	void AddSample(int16_t* stereo, int i, int l, int r, int gain)
	{
		const int sl = (int)stereo[i * 2] + (l * gain >> 8);
		const int sr = (int)stereo[i * 2 + 1] + (r * gain >> 8);
		stereo[i * 2] = (int16_t)CEmuX1010Clamp16(sl);
		stereo[i * 2 + 1] = (int16_t)CEmuX1010Clamp16(sr);
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint8_t ram_[kX1010RamSize];
	uint32_t smpOffs_[kX1010Channels];
	uint32_t envOffs_[kX1010Channels];
	uint8_t playing_[kX1010Channels];
};

CChip* CEmuChipX1010Create(uint32_t clockHz, int sampleRate)
{
	return new CChipX1010(clockHz, sampleRate);
}

void CEmuChipX1010Destroy(CChip* c)
{
	delete c;
}

uint8_t CEmuChipX1010Read(CChip* c, unsigned offset)
{
	return c ? static_cast<CChipX1010*>(c)->ReadRam(offset) : 0;
}

void CEmuChipX1010Write(CChip* c, unsigned offset, uint8_t data)
{
	if (c) static_cast<CChipX1010*>(c)->WriteRam(offset, data);
}
