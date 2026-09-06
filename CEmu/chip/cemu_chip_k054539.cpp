#include "StdAfx.h"
#include "cemu_chip_k054539.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <math.h>
#include <string.h>

/* Simplified from hoot ss054539.cpp / MAME k054539.c. */
enum { kK054539Channels = 8, kK054539Regs = 0x230 };

static int CEmuK054Clamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipK054539 : public CChip {
public:
	CChipK054539(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 18432000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
		, romMask_(0)
		, irq_(0)
		, timerLeft_(-1)
		, timerState_(0)
		, timerFires_(0)
		, keyOns_(0)
		, fmmonBase_(0)
	{
		/* The PCM stream runs at master clock / 384 (MAME k054539).
		   Omitting the divider advances every voice about 384x too fast. */
		freqRatio_ = (double)clockHz_ / (384.0 * (double)sampleRate_);
		for (int i = 0; i < 256; i++)
			volTab_[i] = pow(10.0, (-36.0 * (double)i / 0x40) / 20.0) / 2.0;
		for (int i = 0; i < 15; i++)
			panTab_[i] = sqrt((double)(0x0e - i)) / sqrt(14.0);
		Reset();
	}

	void Reset() override
	{
		memset(reg_, 0, sizeof(reg_));
		memset(ch_, 0, sizeof(ch_));
		memset(ram_, 0, sizeof(ram_));
		curPtr_ = 0;
		curLimit_ = sizeof(ram_);
		irq_ = 0;
		timerLeft_ = -1;
		timerState_ = 0;
		timerFires_ = 0;
		keyOns_ = 0;
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		const uint32_t o = addr & 0x3ff;
		if (o >= kK054539Regs) return;
		const uint8_t v = (uint8_t)(data & 0xff);
		reg_[o] = v;
		FmMonShadowApplyK054539Reg(o, v);
		if (o == 0x214) {
			for (int c = 0; c < kK054539Channels; c++)
				if (v & (1 << c)) KeyOn(c);
		} else if (o == 0x215) {
			for (int c = 0; c < kK054539Channels; c++)
				if (v & (1 << c)) KeyOff(c);
		} else if (o == 0x22d && reg_[0x22e] == 0x80) {
			ram_[curPtr_++] = v;
			if (curPtr_ >= curLimit_) curPtr_ = 0;
		} else if (o == 0x22e) {
			curPtr_ = 0;
			curLimit_ = (v == 0x80) ? sizeof(ram_) : 0x20000u;
		} else if (o == 0x227 || o == 0x22f) {
			if (o == 0x227)
				timerState_ = irq_ = 0;
			if (o == 0x22f && !(v & 0x20))
				timerState_ = irq_ = 0;
			ReloadTimer();
		}
	}

	void AdvanceClocks(uint64_t chipCycles) override
	{
		if (timerLeft_ < 0 || chipCycles == 0) return;
		timerLeft_ -= (int64_t)chipCycles;
		while (timerLeft_ <= 0) {
			/* Hardware timer output is a square wave. MAME toggles the
			   callback state each period; treating every callback as a new
			   asserted edge doubled System GX's sequencer IRQ rate. */
			timerState_ ^= 1;
			irq_ = (reg_[0x22f] & 0x20) ? timerState_ : 0;
			if (irq_) timerFires_++;
			const int64_t over = -timerLeft_;
			ReloadTimer();
			if (timerLeft_ < 0) break;
			timerLeft_ -= over;
		}
	}

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		memset(stereo, 0, (size_t)frames * 2 * sizeof(int16_t));
		MixAdd(stereo, frames, 256);
	}

	void MixAdd(int16_t* stereo, int frames, int gain) override
	{
		if (!stereo || frames <= 0 || !(reg_[0x22f] & 1)) return;
		for (int c = 0; c < kK054539Channels; c++) {
			if (!(reg_[0x22c] & (1 << c))) continue;
			uint8_t* base1 = reg_ + c * 0x20;
			uint8_t* base2 = reg_ + 0x200 + c * 2;
			Channel& vc = ch_[c];
			uint32_t cur = (base1[0x0c] | (base1[0x0d] << 8) | (base1[0x0e] << 16)) & romMask_;
			int frac = (cur == vc.pos) ? vc.frac : 0;
			int val = (cur == vc.pos) ? vc.val : 0;
			const int delta = (int)((base1[0] | (base1[1] << 8) | (base1[2] << 16)) * freqRatio_);
			const int stepBytes = (base2[0] & 0x20) ? -1 : 1;
			const int format = base2[0] & 0x0c;
			int pan = (base1[5] >= 0x11 && base1[5] <= 0x1f) ? (base1[5] - 0x11) : 7;
			if (pan < 0) pan = 0;
			if (pan > 14) pan = 14;
			const double lv = volTab_[base1[3] & 0x7f] * panTab_[pan];
			const double rv = volTab_[base1[3] & 0x7f] * panTab_[0x0e - pan];
			for (int i = 0; i < frames; i++) {
				frac += delta;
				while (frac >= 0x10000) {
					frac -= 0x10000;
					if (format == 0x04) {
						cur = (uint32_t)((int32_t)cur + stepBytes * 2) & romMask_;
						val = (int16_t)(Read8(cur) | (Read8(cur + 1) << 8));
					} else if (format == 0x08) {
						/* 4-bit DPCM: address bit 0 selects low/high nibble. */
						static const int16_t dpcm[16] = {
							0, 0x100, 0x400, 0x900, 0x1000, 0x1900, 0x2400, 0x3100,
							-0x4000, -0x3100, -0x2400, -0x1900,
							-0x1000, -0x900, -0x400, -0x100
						};
						uint32_t nib = (cur << 1) | (uint32_t)(vc.nibble & 1);
						nib = (uint32_t)((int32_t)nib + stepBytes);
						cur = (nib >> 1) & romMask_;
						vc.nibble = (int)(nib & 1);
						const uint8_t b = Read8(cur);
						const int code = vc.nibble ? (b >> 4) : (b & 0x0f);
						val = CEmuK054Clamp16(val + dpcm[code]);
					} else {
						cur = (uint32_t)((int32_t)cur + stepBytes) & romMask_;
						/* 8-bit PCM is signed. Keeping this as a positive int
						   turned every negative sample into a full-scale DC
						   transient and made dual-GX playback sound like noise. */
						val = (int16_t)(Read8(cur) << 8);
					}
					const bool endMarker = (format == 0x08)
						? (Read8(cur) == 0x88) : ((int16_t)val == (int16_t)0x8000);
					if (endMarker) {
						if (base2[1] & 1) {
							cur = (base1[8] | (base1[9] << 8) | (base1[10] << 16)) & romMask_;
							vc.nibble = 0;
							if (format == 0x04)
								val = (int16_t)(Read8(cur) | (Read8(cur + 1) << 8));
							else if (format == 0x08)
								val = 0;
							else
								val = (int16_t)(Read8(cur) << 8);
							continue;
						}
						KeyOff(c);
						goto done_channel;
					}
				}
				stereo[i * 2] = (int16_t)CEmuK054Clamp16((int)stereo[i * 2] + (int)(val * lv) * gain / 256);
				stereo[i * 2 + 1] = (int16_t)CEmuK054Clamp16((int)stereo[i * 2 + 1] + (int)(val * rv) * gain / 256);
			}
done_channel:
			vc.pos = cur;
			vc.frac = frac;
			vc.val = val;
			if (!(reg_[0x22f] & 0x80)) {
				base1[0x0c] = (uint8_t)cur;
				base1[0x0d] = (uint8_t)(cur >> 8);
				base1[0x0e] = (uint8_t)(cur >> 16);
			}
		}
	}

	uint8_t ReadStatus() override { return reg_[0x22c]; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		rom_ = data;
		romSize_ = size;
		romMask_ = 0;
		for (int i = 0; i < 31; i++) {
			if ((1u << i) >= romSize_) {
				romMask_ = (1u << i) - 1;
				break;
			}
		}
		if (!romMask_) romMask_ = romSize_ ? (romSize_ - 1) : 0;
	}

	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap == 0) return 0;
		const unsigned n = cap < sizeof(reg_) ? cap : (unsigned)sizeof(reg_);
		memcpy(buf, reg_, n);
		return n;
	}

	bool Irq() const override { return irq_ != 0; }
	void AckIrq() override { irq_ = 0; }

	/* Direct register peek for 68K memory maps (System GX dual chip). */
	uint8_t PeekReg(unsigned off) const
	{
		return (off < kK054539Regs) ? reg_[off] : 0;
	}
	unsigned TimerFires() const { return timerFires_; }
	unsigned KeyOns() const { return keyOns_; }

	void SetFmMonBase(int base) { fmmonBase_ = base; }

private:
	void ReloadTimer()
	{
		/* MAME period:
		     Hz = 2 * (38 + reg[227]) * ((clock / 384) / 14400)
		   Expressed in master-clock cycles this is 384*14400 /
		   (2*(38+n)).  The old approximation inverted the register's
		   effect and produced the wrong sequencer IRQ rate. */
		if (!(reg_[0x22f] & 0x20) && !(reg_[0x22f] & 0x02)) {
			/* Prefer enable bits seen in GX firmware; if unset, still arm a
			   modest tick so the sequencer has a timebase after key-on. */
		}
		const unsigned n = reg_[0x227];
		timerLeft_ = (int64_t)(384u * 14400u) / (int64_t)(2u * (38u + n));
		if (timerLeft_ < 64) timerLeft_ = 64;
	}

	struct Channel {
		uint32_t pos;
		int frac;
		int val;
		int nibble;
	};

	uint8_t Read8(uint32_t addr) const
	{
		if (reg_[0x22e] == 0x80)
			return ram_[addr & (sizeof(ram_) - 1)];
		if (!rom_ || romSize_ == 0)
			return 0x80;
		return rom_[addr % romSize_];
	}

	void KeyOn(int c)
	{
		reg_[0x22c] |= (uint8_t)(1 << c);
		keyOns_++;
		ch_[c].pos = 0xffffffffu;
		ch_[c].nibble = 0;
		/* Relative pitch: delta 0x10000 ≈ 8 kHz @ 18.432 MHz ≈ unity/C4.
		   Absolute Hz put GX voices up in O8–O10 and the UI looked empty
		   when bind still used an OPNA shell. */
		{
			uint8_t* base1 = reg_ + c * 0x20;
			const unsigned delta = (unsigned)(base1[0] | (base1[1] << 8) | (base1[2] << 16));
			/* Map 24-bit delta into PitchRate's 14-bit 0x1000=unity space. */
			const unsigned rate = delta ? (delta >> 4) : 0;
			int midi = FmMonShadowPitchRateToMidi(rate ? rate : 1u);
			if (midi < 0) midi = 60;
			if (midi < 24) midi = 24;
			if (midi > 108) midi = 108;
			FmMonShadowPcmNote(fmmonBase_ + c, midi, 1);
		}
		if (timerLeft_ < 0) ReloadTimer();
	}

	void KeyOff(int c)
	{
		reg_[0x22c] &= (uint8_t)~(1 << c);
		FmMonShadowPcmNote(fmmonBase_ + c, 0, 0);
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint32_t romMask_;
	double freqRatio_;
	double volTab_[256];
	double panTab_[15];
	uint8_t reg_[kK054539Regs];
	uint8_t ram_[0x4000];
	uint32_t curPtr_;
	uint32_t curLimit_;
	Channel ch_[kK054539Channels];
	int irq_;
	int64_t timerLeft_;
	int timerState_;
	unsigned timerFires_;
	unsigned keyOns_;
	int fmmonBase_;
};

CChip* CEmuChipK054539Create(uint32_t clockHz, int sampleRate)
{
	return new CChipK054539(clockHz, sampleRate);
}

void CEmuChipK054539Destroy(CChip* c)
{
	delete c;
}

void CEmuChipK054539SetFmMonBase(CChip* c, int baseChannel)
{
	CChipK054539* k = dynamic_cast<CChipK054539*>(c);
	if (k) k->SetFmMonBase(baseChannel);
}

uint8_t CEmuChipK054539PeekReg(CChip* c, unsigned off)
{
	CChipK054539* k = dynamic_cast<CChipK054539*>(c);
	return k ? k->PeekReg(off) : 0;
}

void CEmuChipK054539GetMetrics(CChip* c, unsigned* timerFires, unsigned* keyOns)
{
	CChipK054539* k = dynamic_cast<CChipK054539*>(c);
	if (timerFires) *timerFires = k ? k->TimerFires() : 0;
	if (keyOns) *keyOns = k ? k->KeyOns() : 0;
}
