#include "StdAfx.h"
#include "cemu_chip_ymz280b.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <math.h>
#include <string.h>

/* Yamaha YMZ280B, modelled on MAME ymz280b.cpp.

   Register map (voice = (reg >> 2) & 7, field = reg & 0xe3):
     $00+4v  pitch low 8 bits
     $01+4v  bit0 pitch high, bits1-2 mode (1=ADPCM 2=8bit 3=16bit),
             bit4 loop, bit7 key on
     $02+4v  total level
     $03+4v  pan (0=hard left, 8=centre, 15=hard right)
     $20+4v / $40+4v / $60+4v   start address  high / mid / low
     $21/$41/$61                loop start
     $22/$42/$62                loop end
     $23/$43/$63                stop address
     $FF     bit7 enables key-on

   Addresses count ADPCM nibbles, so a 16-bit PCM voice consumes four
   address steps per sample and an 8-bit voice two. */
enum {
	kYmzVoices = 8,
	kYmzFracBits = 14
};

static int CEmuYmzClamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipYmz280b : public CChip {
public:
	CChipYmz280b(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 16934400u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
	{
		Reset();
	}

	void Reset() override
	{
		memset(reg_, 0, sizeof(reg_));
		memset(v_, 0, sizeof(v_));
		regSel_ = 0;
		keyEnable_ = 0;
		status_ = 0;
	}

	void WritePort(unsigned port, uint8_t data)
	{
		if (!(port & 1)) {
			regSel_ = data;
			return;
		}
		WriteReg(regSel_, data);
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		WritePort((unsigned)addr, (uint8_t)(data & 0xff));
	}

	uint8_t ReadStatus() override
	{
		const uint8_t s = status_;
		status_ = 0; /* status bits clear on read */
		return s;
	}
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

	void AdvanceClocks(uint64_t chipCycles) override { (void)chipCycles; }

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		memset(stereo, 0, (size_t)frames * 2 * sizeof(int16_t));
		MixAdd(stereo, frames, 256);
	}

	void MixAdd(int16_t* stereo, int frames, int gain) override
	{
		if (!stereo || frames <= 0 || !rom_ || !romSize_) return;
		for (int i = 0; i < kYmzVoices; i++) {
			Voice& vc = v_[i];
			if (!vc.playing) continue;
			int volL, volR;
			PanVolume(vc, &volL, &volR);
			for (int f = 0; f < frames; f++) {
				while (vc.frac >= (1u << kYmzFracBits)) {
					vc.frac -= (1u << kYmzFracBits);
					if (!NextSample(i, vc)) break;
				}
				if (!vc.playing) break;
				const int s = vc.sample;
				const int l = (s * volL) >> 8;
				const int r = (s * volR) >> 8;
				const int al = (int)stereo[f * 2] + (l * gain >> 8);
				const int ar = (int)stereo[f * 2 + 1] + (r * gain >> 8);
				stereo[f * 2] = (int16_t)CEmuYmzClamp16(al);
				stereo[f * 2 + 1] = (int16_t)CEmuYmzClamp16(ar);
				vc.frac += vc.step;
			}
		}
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		rom_ = data;
		romSize_ = size;
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

private:
	struct Voice {
		uint32_t start, loopStart, loopEnd, stop;
		uint32_t pos;      /* current nibble address */
		uint32_t frac, step;
		uint16_t fnum;
		uint8_t mode, loop, keyon, level, pan, playing;
		int sample;        /* last decoded sample, 16-bit signed */
		int signal, stepIndex;  /* ADPCM state */
		int loopSignal, loopStepIndex;
		uint8_t loopLatched;
	};

	void WriteReg(uint8_t reg, uint8_t data)
	{
		reg_[reg] = data;
		if (reg == 0xff) {
			const int en = (data & 0x80) ? 1 : 0;
			if (!en) {
				/* Dropping the enable silences every voice. */
				for (int i = 0; i < kYmzVoices; i++)
					SetPlaying(i, 0);
			}
			keyEnable_ = (uint8_t)en;
			return;
		}
		if (reg >= 0x80) return;
		const int i = (reg >> 2) & 7;
		Voice& vc = v_[i];
		switch (reg & 0xe3) {
		case 0x00:
			vc.fnum = (uint16_t)((vc.fnum & 0x100u) | data);
			UpdateStep(vc);
			break;
		case 0x01: {
			vc.fnum = (uint16_t)((vc.fnum & 0x0ffu) | ((data & 1u) << 8));
			vc.mode = (uint8_t)((data >> 1) & 3);
			vc.loop = (uint8_t)((data >> 4) & 1);
			const int on = (data & 0x80) ? 1 : 0;
			UpdateStep(vc);
			if (on && keyEnable_ && !vc.playing) {
				vc.pos = vc.start;
				vc.frac = 1u << kYmzFracBits; /* fetch on first sample */
				vc.signal = 0;
				vc.stepIndex = 127;
				vc.sample = 0;
				vc.loopLatched = 0;
				SetPlaying(i, 1);
			} else if (!on && vc.playing) {
				SetPlaying(i, 0);
			}
			vc.keyon = (uint8_t)on;
			break;
		}
		case 0x02: vc.level = data; break;
		case 0x03: vc.pan = (uint8_t)(data & 0x0f); break;
		case 0x20: vc.start = (vc.start & 0x00ffffu) | ((uint32_t)data << 16); break;
		case 0x21: vc.loopStart = (vc.loopStart & 0x00ffffu) | ((uint32_t)data << 16); break;
		case 0x22: vc.loopEnd = (vc.loopEnd & 0x00ffffu) | ((uint32_t)data << 16); break;
		case 0x23: vc.stop = (vc.stop & 0x00ffffu) | ((uint32_t)data << 16); break;
		case 0x40: vc.start = (vc.start & 0xff00ffu) | ((uint32_t)data << 8); break;
		case 0x41: vc.loopStart = (vc.loopStart & 0xff00ffu) | ((uint32_t)data << 8); break;
		case 0x42: vc.loopEnd = (vc.loopEnd & 0xff00ffu) | ((uint32_t)data << 8); break;
		case 0x43: vc.stop = (vc.stop & 0xff00ffu) | ((uint32_t)data << 8); break;
		case 0x60: vc.start = (vc.start & 0xffff00u) | data; break;
		case 0x61: vc.loopStart = (vc.loopStart & 0xffff00u) | data; break;
		case 0x62: vc.loopEnd = (vc.loopEnd & 0xffff00u) | data; break;
		case 0x63: vc.stop = (vc.stop & 0xffff00u) | data; break;
		default: break;
		}
	}

	void UpdateStep(Voice& vc)
	{
		/* ADPCM ignores the 9th pitch bit. */
		const unsigned n = (vc.mode == 1) ? (vc.fnum & 0x0ffu) : (vc.fnum & 0x1ffu);
		const double masterHz = (double)clockHz_ / 384.0;
		const double rate = masterHz * (double)(n + 1) / 256.0;
		uint32_t s = (uint32_t)(rate * (double)(1u << kYmzFracBits)
			/ (double)sampleRate_);
		vc.step = s ? s : 1;
	}

	void SetPlaying(int i, int on)
	{
		Voice& vc = v_[i];
		if (vc.playing == (uint8_t)on) return;
		vc.playing = (uint8_t)on;
		if (!on) status_ = (uint8_t)(status_ | (1u << i));
		FmMonShadowPcmNote(i, 60 + i, on);
	}

	uint8_t RomByte(uint32_t byteAddr) const
	{
		return (byteAddr < romSize_) ? rom_[byteAddr] : 0;
	}

	/* Decode one output sample; returns 0 once the voice has stopped. */
	int NextSample(int i, Voice& vc)
	{
		if (vc.pos >= vc.stop) {
			if (vc.loop) {
				vc.pos = vc.loopStart;
				if (vc.loopLatched) {
					vc.signal = vc.loopSignal;
					vc.stepIndex = vc.loopStepIndex;
				}
			} else {
				SetPlaying(i, 0);
				return 0;
			}
		}
		if (vc.loop && !vc.loopLatched && vc.pos >= vc.loopStart) {
			/* Snapshot the ADPCM predictor at the loop point so repeats do
			   not drift away from the first pass. */
			vc.loopSignal = vc.signal;
			vc.loopStepIndex = vc.stepIndex;
			vc.loopLatched = 1;
		}
		switch (vc.mode) {
		case 1: { /* 4-bit ADPCM, two samples per byte */
			const uint8_t b = RomByte(vc.pos >> 1);
			const int nib = (vc.pos & 1u) ? (b & 0x0f) : (b >> 4);
			const int delta = vc.stepIndex * kNibbleStep[nib & 7] / 8;
			vc.signal += (nib & 8) ? -delta : delta;
			if (vc.signal > 32767) vc.signal = 32767;
			if (vc.signal < -32768) vc.signal = -32768;
			vc.stepIndex = (vc.stepIndex * kIndexScale[nib & 7]) >> 8;
			if (vc.stepIndex > 0x6000) vc.stepIndex = 0x6000;
			if (vc.stepIndex < 0x7f) vc.stepIndex = 0x7f;
			vc.sample = vc.signal;
			vc.pos += 1;
			break;
		}
		case 2: /* 8-bit signed PCM: one sample per two nibble steps */
			vc.sample = (int)(int8_t)RomByte(vc.pos >> 1) * 256;
			vc.pos += 2;
			break;
		case 3: { /* 16-bit signed PCM, big endian */
			const uint32_t a = vc.pos >> 1;
			vc.sample = (int)(int16_t)(((uint16_t)RomByte(a) << 8)
				| RomByte(a + 1));
			vc.pos += 4;
			break;
		}
		default:
			SetPlaying(i, 0);
			return 0;
		}
		return 1;
	}

	void PanVolume(const Voice& vc, int* l, int* r) const
	{
		const int vol = vc.level;
		if (vc.pan == 8) {
			*l = vol;
			*r = vol;
		} else if (vc.pan < 8) {
			*l = vol;
			*r = vol * (int)vc.pan / 8;
		} else {
			*l = vol * (15 - (int)vc.pan) / 7;
			*r = vol;
		}
	}

	static const int kIndexScale[8];
	static const int kNibbleStep[8];

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint8_t reg_[0x100];
	uint8_t regSel_;
	uint8_t keyEnable_;
	uint8_t status_;
	Voice v_[kYmzVoices];
};

/* The chip carries its ADPCM step as a live 0x7F..0x6000 value scaled by the
   nibble magnitude, rather than an index into a precomputed step table. */
const int CChipYmz280b::kIndexScale[8] = {
	0x0e6, 0x0e6, 0x0e6, 0x0e6, 0x133, 0x199, 0x200, 0x266
};

const int CChipYmz280b::kNibbleStep[8] = { 1, 3, 5, 7, 9, 11, 13, 15 };

CChip* CEmuChipYmz280bCreate(uint32_t clockHz, int sampleRate)
{
	return new CChipYmz280b(clockHz, sampleRate);
}

void CEmuChipYmz280bDestroy(CChip* c)
{
	delete c;
}

void CEmuChipYmz280bWritePort(CChip* c, unsigned port, uint8_t data)
{
	if (c) static_cast<CChipYmz280b*>(c)->WritePort(port, data);
}

uint8_t CEmuChipYmz280bReadStatus(CChip* c)
{
	return c ? static_cast<CChipYmz280b*>(c)->ReadStatus() : 0;
}
