#include "StdAfx.h"
#include "cemu_chip_saa1099.h"
#include "cemu_chip.h"
#include <string.h>
#include <stdlib.h>

/* Compact SAA1099 core from MAME (BSD-3-Clause; Buchmueller/Abadia). */

enum { SAA_LEFT = 0, SAA_RIGHT = 1, SAA_DIV = 256 };

static const uint16_t kSaaAmp[16] = {
	0, 2047, 4094, 6141, 8188, 10235, 12282, 14329,
	16376, 18423, 20470, 22517, 24564, 26611, 28658, 30705
};

static const uint8_t kSaaEnv[8][64] = {
	{0},
	{15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	 15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	 15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
	 15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15},
	{15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0},
	{15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
	 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
	 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
	 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0},
	{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
	 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0},
	{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
	 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
	 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
	 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0},
	{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
	{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
	 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
	 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
	 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}
};

class CChipSaa1099 : public CChip {
public:
	CChipSaa1099(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 7159090u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, writeCount_(0)
		, toneOnCount_(0)
		, selectedReg_(0)
		/* CMS.DRV never writes reg 0x1C (all-enable); Creative titles rely on
		   sound being live once freq_enable (0x14) is programmed. Default on. */
		, allEnable_(1)
		, clockResidual_(0)
	{
		memset(noiseParams_, 0, sizeof(noiseParams_));
		memset(envEnable_, 0, sizeof(envEnable_));
		memset(envRevR_, 0, sizeof(envRevR_));
		memset(envMode_, 0, sizeof(envMode_));
		memset(envBits_, 0, sizeof(envBits_));
		memset(envClock_, 0, sizeof(envClock_));
		memset(envStep_, 0, sizeof(envStep_));
		memset(ch_, 0, sizeof(ch_));
		for (int i = 0; i < 6; i++) {
			ch_[i].envelope[0] = ch_[i].envelope[1] = 16;
			ch_[i].counter = 1;
		}
		noise_[0].level = noise_[1].level = 0xffffffffu;
		noise_[0].freq = noise_[1].freq = 256;
		noise_[0].counter = noise_[1].counter = 1;
	}

	void Reset() override
	{
		writeCount_ = toneOnCount_ = 0;
		selectedReg_ = 0;
		allEnable_ = 1;
		memset(ch_, 0, sizeof(ch_));
		for (int i = 0; i < 6; i++) {
			ch_[i].envelope[0] = ch_[i].envelope[1] = 16;
			ch_[i].counter = 1;
		}
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		writeCount_++;
		if (addr & 1)
			ControlW((uint8_t)data);
		else
			DataW((uint8_t)data);
	}

	void AdvanceClocks(uint64_t chipCycles) override { (void)chipCycles; }

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		memset(stereo, 0, (size_t)frames * 2 * sizeof(int16_t));
		if (!allEnable_ || clockHz_ == 0 || sampleRate_ == 0) return;
		/* MAME: stream rate = clock/256; each StepOne is one stream tick
		   (counter -= 256). Running at full clock made tones ~8 octaves high. */
		for (int i = 0; i < frames; i++) {
			clockResidual_ += (uint64_t)clockHz_;
			const uint64_t den = (uint64_t)sampleRate_ * (uint64_t)SAA_DIV;
			uint64_t ticks = clockResidual_ / den;
			clockResidual_ %= den;
			int32_t ol = 0, orr = 0;
			while (ticks--)
				StepOne(ol, orr);
			if (ol > 32767) ol = 32767;
			if (ol < -32768) ol = -32768;
			if (orr > 32767) orr = 32767;
			if (orr < -32768) orr = -32768;
			stereo[i * 2] = (int16_t)ol;
			stereo[i * 2 + 1] = (int16_t)orr;
		}
	}

	void MixAdd(int16_t* stereo, int frames, int gain) override
	{
		if (!stereo || frames <= 0) return;
		int16_t tmp[2];
		for (int i = 0; i < frames; i++) {
			Render(tmp, 1);
			int32_t l = (int32_t)stereo[i * 2] + ((int32_t)tmp[0] * gain / 256);
			int32_t r = (int32_t)stereo[i * 2 + 1] + ((int32_t)tmp[1] * gain / 256);
			if (l > 32767) l = 32767; if (l < -32768) l = -32768;
			if (r > 32767) r = 32767; if (r < -32768) r = -32768;
			stereo[i * 2] = (int16_t)l;
			stereo[i * 2 + 1] = (int16_t)r;
		}
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return 0xff; }
	uint8_t ReadData() override { return 0xff; }
	uint8_t ReadStatusHi() override { return 0xff; }
	uint8_t ReadDataHi() override { return 0xff; }

	unsigned WriteCount() const { return writeCount_; }
	unsigned ToneOnCount() const { return toneOnCount_; }

private:
	struct Chan {
		uint8_t frequency;
		uint8_t octave;
		int freq_enable;
		int noise_enable;
		uint16_t amplitude[2];
		uint8_t envelope[2];
		int counter;
		uint8_t level;
		uint32_t Freq() const { return (uint32_t)((511 - frequency) << (8 - octave)); }
	};
	struct Noise {
		int counter;
		int freq;
		uint32_t level;
	};

	void ControlW(uint8_t data)
	{
		selectedReg_ = data & 0x1f;
		if (selectedReg_ == 0x18 || selectedReg_ == 0x19) {
			if (envClock_[0]) EnvelopeW(0);
			if (envClock_[1]) EnvelopeW(1);
		}
	}

	void DataW(uint8_t data)
	{
		const int reg = selectedReg_;
		int ch;
		switch (reg) {
		case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
			ch = reg & 7;
			ch_[ch].amplitude[SAA_LEFT] = kSaaAmp[data & 0x0f];
			ch_[ch].amplitude[SAA_RIGHT] = kSaaAmp[(data >> 4) & 0x0f];
			break;
		case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d:
			ch_[reg & 7].frequency = data;
			break;
		case 0x10: case 0x11: case 0x12:
			ch = (reg - 0x10) << 1;
			ch_[ch].octave = data & 7;
			ch_[ch + 1].octave = (data >> 4) & 7;
			break;
		case 0x14:
			for (ch = 0; ch < 6; ch++) {
				ch_[ch].freq_enable = (data >> ch) & 1;
				if (ch_[ch].freq_enable) toneOnCount_++;
			}
			/* CMS.DRV never writes reg 0x1C; it only arms tones via 0x14.
			   Keep/restore all-enable so Render is not stuck silent. */
			if (data & 0x3f)
				allEnable_ = 1;
			break;
		case 0x15:
			for (ch = 0; ch < 6; ch++)
				ch_[ch].noise_enable = (data >> ch) & 1;
			break;
		case 0x16:
			noiseParams_[0] = data & 3;
			noiseParams_[1] = (data >> 4) & 3;
			break;
		case 0x18: case 0x19:
			ch = reg - 0x18;
			envRevR_[ch] = data & 1;
			envMode_[ch] = (data >> 1) & 7;
			envBits_[ch] = (data >> 4) & 1;
			envClock_[ch] = (data >> 5) & 1;
			envEnable_[ch] = (data >> 7) & 1;
			envStep_[ch] = 0;
			break;
		case 0x1c:
			allEnable_ = data & 1;
			if (data & 2) {
				for (int i = 0; i < 6; i++) {
					ch_[i].level = 0;
					ch_[i].counter = (int)ch_[i].Freq();
				}
			}
			break;
		default: break;
		}
	}

	void EnvelopeW(int eg)
	{
		if (!envEnable_[eg]) {
			for (int i = 0; i < 3; i++) {
				ch_[eg * 3 + i].envelope[0] = 16;
				ch_[eg * 3 + i].envelope[1] = 16;
			}
			return;
		}
		envStep_[eg] = (uint8_t)(((envStep_[eg] + 1) & 0x3f) | (envStep_[eg] & 0x20));
		int mask = envBits_[eg] ? 14 : 15;
		uint8_t v = (uint8_t)(kSaaEnv[envMode_[eg]][envStep_[eg]] & mask);
		for (int i = 0; i < 3; i++) {
			ch_[eg * 3 + i].envelope[0] = v;
			ch_[eg * 3 + i].envelope[1] = envRevR_[eg] ? (uint8_t)((15 - v) & mask) : v;
		}
	}

	void StepOne(int32_t& ol, int32_t& orr)
	{
		for (int ch = 0; ch < 2; ch++) {
			switch (noiseParams_[ch]) {
			case 0: case 1: case 2:
				noise_[ch].freq = 256 << noiseParams_[ch];
				break;
			default:
				noise_[ch].freq = (int)ch_[ch * 3].Freq();
				break;
			}
		}
		for (int ch = 0; ch < 6; ch++) {
			while (ch_[ch].counter <= 0) {
				ch_[ch].counter += (int)ch_[ch].Freq();
				ch_[ch].level ^= 1;
				if (ch == 1 && !envClock_[0]) EnvelopeW(0);
				if (ch == 4 && !envClock_[1]) EnvelopeW(1);
			}
			ch_[ch].counter -= SAA_DIV;
			uint8_t level = 0;
			const uint8_t noiseOut = (uint8_t)(noise_[ch / 3].level & 1);
			const uint8_t toneOut = (uint8_t)(ch_[ch].level & 1);
			if (ch_[ch].noise_enable) {
				if (ch_[ch].freq_enable)
					level = noiseOut ? (uint8_t)(toneOut << 1) : toneOut;
				else
					level = noiseOut;
			} else if (ch_[ch].freq_enable) {
				level = toneOut;
			}
			if (level > 0) {
				ol += (int32_t)ch_[ch].amplitude[SAA_LEFT] * ch_[ch].envelope[SAA_LEFT] / 16 / level;
				orr += (int32_t)ch_[ch].amplitude[SAA_RIGHT] * ch_[ch].envelope[SAA_RIGHT] / 16 / level;
			}
		}
		for (int ch = 0; ch < 2; ch++) {
			while (noise_[ch].counter <= 0) {
				noise_[ch].counter += noise_[ch].freq;
				const int b17 = (noise_[ch].level & 0x20000) == 0;
				const int b10 = (noise_[ch].level & 0x0400) == 0;
				noise_[ch].level = (noise_[ch].level << 1) | (uint32_t)(b17 != b10);
			}
			noise_[ch].counter -= SAA_DIV;
		}
	}

	uint32_t clockHz_;
	int sampleRate_;
	unsigned writeCount_;
	unsigned toneOnCount_;
	uint8_t selectedReg_;
	int allEnable_;
	uint8_t noiseParams_[2];
	int envEnable_[2], envRevR_[2], envBits_[2], envClock_[2];
	uint8_t envMode_[2], envStep_[2];
	Chan ch_[6];
	Noise noise_[2];
	uint64_t clockResidual_;
};

CChip* CEmuChipSaa1099Create(uint32_t clockHz, int sampleRate)
{
	return new CChipSaa1099(clockHz, sampleRate);
}

void CEmuChipSaa1099Destroy(CChip* c)
{
	delete c;
}

unsigned CEmuChipSaa1099WriteCount(const CChip* c)
{
	const CChipSaa1099* s = dynamic_cast<const CChipSaa1099*>(c);
	return s ? s->WriteCount() : 0;
}

unsigned CEmuChipSaa1099ToneOnCount(const CChip* c)
{
	const CChipSaa1099* s = dynamic_cast<const CChipSaa1099*>(c);
	return s ? s->ToneOnCount() : 0;
}
