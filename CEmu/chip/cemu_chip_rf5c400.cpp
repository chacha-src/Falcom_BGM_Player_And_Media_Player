#include "StdAfx.h"
#include "cemu_chip_rf5c400.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* Ricoh RF5C400: 32-channel PCM with per-channel AR/DR/RR envelopes, used on
   Konami Hornet/Firebeat. Modelled on MAME sound/rf5c400.cpp (Ville Linde and
   the hoot team).

   Register layout is offset-based, not byte-based: offsets below 0x400 are the
   global/command file, above that a channel is selected by (offset>>5)&0x1f
   with the register in the low 5 bits.

   The sample ROM is a 25-bit little-endian word space; a channel address is a
   word index, so a fetch is read_word((pos>>16)<<1). */

namespace {

const int kRf5c400Channels = 32;
const int kRf5c400EnvSteps = 0x9f;

enum {
	kTypeMask = 0x00c0,
	kType16 = 0x0000,
	kType8Low = 0x0040,
	kType8High = 0x0080
};

enum {
	kPhaseNone = 0,
	kPhaseAttack,
	kPhaseDecay,
	kPhaseRelease
};

/* val>=0x80 selects the upper, coarser half of the envelope curve. */
inline uint8_t Decode80(uint8_t val)
{
	return (uint8_t)((val & 0x80) ? ((val & 0x7f) + 0x1f) : val);
}

} /* namespace */

class CChipRf5c400 : public CChip {
public:
	CChipRf5c400(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 16934400u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
		, status_(0)
		, extAddr_(0)
		, extData_(0)
		, reqChannel_(0)
	{
		BuildTables();
		Reset();
	}

	void Reset() override
	{
		memset(chan_, 0, sizeof(chan_));
		status_ = 0;
		extAddr_ = 0;
		extData_ = 0;
		reqChannel_ = 0;
		memset(monOn_, 0, sizeof(monOn_));
		memset(monMidi_, 0xff, sizeof(monMidi_));
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		const unsigned offset = addr & 0xffffu;
		const uint16_t d = (uint16_t)(data & 0xffffu);
		if (offset < 0x400u)
			WriteGlobal(offset, d);
		else
			WriteChannel((int)((offset >> 5) & 0x1fu), offset & 0x1fu, d);
	}

	uint16_t ReadReg(unsigned offset)
	{
		if (offset >= 0x400u) return 0;
		switch (offset) {
		case 0x00: return status_;
		case 0x09: {
			/* Streaming BGM polls how far the channel has advanced so it can
			   DMA into the half of the buffer that is not playing. */
			const Channel* c = &chan_[reqChannel_ & 0x1fu];
			if (c->envPhase == kPhaseNone) return 0;
			const uint32_t start =
				((uint32_t)(c->startH & 0xff00u) << 8) | c->startL;
			return (uint16_t)(((c->pos >> 16) - start) >> 6);
		}
		case 0x13: return ReadRomWord(extAddr_ << 1);
		default: return 0;
		}
	}

	void AdvanceClocks(uint64_t) override {}

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		memset(stereo, 0, (size_t)frames * 2 * sizeof(int16_t));
		MixAdd(stereo, frames, 256);
	}

	void MixAdd(int16_t* stereo, int frames, int gain) override
	{
		if (!stereo || frames <= 0) return;
		/* The chip streams at clock/384 (44.1 kHz on Hornet's 16.9344 MHz);
		   scale the phase step when the host runs at another rate. */
		const double nativeRate = (double)clockHz_ / 384.0;
		const double rateScale = nativeRate / (double)sampleRate_;

		for (int ch = 0; ch < kRf5c400Channels; ch++) {
			Channel* c = &chan_[ch];
			if (c->envPhase == kPhaseNone) continue;

			const uint32_t start =
				((uint32_t)(c->startH & 0xff00u) << 8) | c->startL;
			const uint32_t end =
				((uint32_t)(c->endHloopH & 0x00ffu) << 16) | c->endL;
			const uint32_t loop =
				((uint32_t)(c->endHloopH & 0xff00u) << 8) | c->loopL;
			if (start == end) continue;

			const unsigned vol = c->volume & 0xffu;
			const unsigned lvol = c->pan & 0xffu;
			const unsigned rvol = (c->pan >> 8) & 0xffu;
			const unsigned elvol = c->effect & 0xffu;
			const unsigned ervol = (c->effect >> 8) & 0xffu;
			const unsigned type = (unsigned)((c->volume >> 8) & kTypeMask);
			const uint64_t step = (uint64_t)((double)c->step * rateScale);

			uint64_t pos = c->pos;
			int phase = c->envPhase;
			double level = c->envLevel;
			double estep = c->envStep;

			for (int i = 0; i < frames; i++) {
				if (phase == kPhaseNone) break;

				const int16_t raw = (int16_t)ReadRomWord((uint32_t)((pos >> 16) << 1));
				int32_t sample;
				switch (type) {
				case kType16: sample = raw; break;
				case kType8Low: sample = (int16_t)(raw << 8); break;
				case kType8High: sample = (int16_t)(raw & 0xff00); break;
				default: sample = 0; break;
				}
				if (sample & 0x8000) sample ^= 0x7fff;

				level += estep;
				if (phase == kPhaseAttack) {
					if (level >= 1.0) {
						phase = kPhaseDecay;
						level = 1.0;
						estep = ((c->decay & 0x0080u) || c->decay == 0x100u)
							? 0.0 : envDr_[Decode80((uint8_t)(c->decay >> 8))];
					}
				} else if (level <= 0.0) {
					phase = kPhaseNone;
					level = 0.0;
					estep = 0.0;
				}

				sample *= volumeTable_[vol];
				const double v = (double)(sample >> 9) * level;
				/* Channels 2/3 are the effect sends; the external effect board
				   is not modelled, so fold them back in at half level rather
				   than dropping music that is routed through them. */
				const double l = v * (panTable_[lvol] + panTable_[elvol] * 0.5);
				const double r = v * (panTable_[rvol] + panTable_[ervol] * 0.5);
				int16_t* p = stereo + (size_t)i * 2;
				AddClamped(&p[0], (int)(l * gain) / 256);
				AddClamped(&p[1], (int)(r * gain) / 256);

				pos += step;
				if ((pos >> 16) > end) {
					pos -= (uint64_t)loop << 16;
					pos &= 0xffffff0000ull;
					if (pos < ((uint64_t)start << 16))
						pos = (uint64_t)start << 16;
				}
			}

			c->pos = pos;
			c->envPhase = (uint8_t)phase;
			c->envLevel = level;
			c->envStep = estep;
			if (phase == kPhaseNone) UpdateMon(ch);
		}
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		rom_ = data;
		romSize_ = size;
	}
	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return (uint8_t)status_; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return (uint8_t)(status_ >> 8); }
	uint8_t ReadDataHi() override { return 0; }

private:
	struct Channel {
		uint16_t startH, startL, freq, endL, endHloopH, loopL;
		uint16_t pan, effect, volume;
		uint16_t attack, decay, release, cutoff;
		uint64_t pos, step;
		uint8_t envPhase;
		double envLevel, envStep;
	};

	static void AddClamped(int16_t* dst, int add)
	{
		int s = (int)*dst + add;
		if (s > 32767) s = 32767;
		if (s < -32768) s = -32768;
		*dst = (int16_t)s;
	}

	uint16_t ReadRomWord(uint32_t byteAddr) const
	{
		if (!rom_ || byteAddr + 1u >= romSize_) return 0;
		return (uint16_t)(rom_[byteAddr] | ((uint16_t)rom_[byteAddr + 1u] << 8));
	}

	void WriteGlobal(unsigned offset, uint16_t d)
	{
		switch (offset) {
		case 0x00:
			status_ = d;
			break;
		case 0x01: {
			/* Channel control: 0x60 key-on, 0x40 release, else hard stop. */
			const int ch = d & 0x1f;
			Channel* c = &chan_[ch];
			if ((d & 0x60u) == 0x60u) {
				c->pos = ((uint64_t)(((uint32_t)(c->startH & 0xff00u) << 8)
					| c->startL)) << 16;
				c->envPhase = kPhaseAttack;
				c->envLevel = 0.0;
				c->envStep = envAr_[Decode80((uint8_t)(c->attack >> 8))];
			} else if ((d & 0x60u) == 0x40u) {
				if (c->envPhase != kPhaseNone) {
					c->envPhase = kPhaseRelease;
					c->envStep = (c->release & 0x0080u)
						? 0.0 : envRr_[Decode80((uint8_t)(c->release >> 8))];
				}
			} else {
				c->envPhase = kPhaseNone;
				c->envLevel = 0.0;
				c->envStep = 0.0;
			}
			UpdateMon(ch);
			break;
		}
		case 0x08:
			reqChannel_ = d & 0x1fu;
			break;
		case 0x11:
			extAddr_ = (extAddr_ & ~0xffffu) | d;
			break;
		case 0x12:
			extAddr_ = (extAddr_ & 0xffffu) | ((uint32_t)d << 16);
			break;
		case 0x13:
			extData_ = d;
			break;
		default:
			/* 0x14 is an external-memory write and 0x20-0x32 are the
			   reverb/chorus macros; both target hardware we do not model. */
			break;
		}
	}

	void WriteChannel(int ch, unsigned reg, uint16_t d)
	{
		Channel* c = &chan_[ch];
		switch (reg) {
		case 0x00: c->startH = d; break;
		case 0x01: c->startL = d; break;
		case 0x02:
			/* 13-bit mantissa with a 3-bit octave shift, in 16.16 word steps. */
			c->step = (uint64_t)(((uint32_t)(d & 0x1fffu) << (d >> 13)) * 4u);
			c->freq = d;
			UpdateMon(ch);
			break;
		case 0x03: c->endL = d; break;
		case 0x04: c->endHloopH = d; break;
		case 0x05: c->loopL = d; break;
		case 0x06: c->pan = d; UpdateMon(ch); break;
		case 0x07: c->effect = d; break;
		case 0x08: c->volume = d; UpdateMon(ch); break;
		case 0x09: c->attack = d; break;
		case 0x0c: c->decay = d; break;
		case 0x0e: c->release = d; break;
		case 0x10: c->cutoff = d; break;
		default: break;
		}
	}

	void UpdateMon(int ch)
	{
		if (ch < 0 || ch >= kRf5c400Channels) return;
		const Channel* c = &chan_[ch];
		const int on = (c->envPhase != kPhaseNone && c->step
			&& ((c->pan & 0xffu) || ((c->pan >> 8) & 0xffu))) ? 1 : 0;
		int midi = 60;
		if (on) {
			/* step 0x10000 replays the sample at its recorded rate, so treat
			   that ratio as middle C. */
			midi = FmMonShadowHzToMidi(261.6255653
				* (double)c->step / 65536.0);
			if (midi < 0) midi = 60;
		}
		if (monOn_[ch] == (uint8_t)on && (!on || monMidi_[ch] == (uint8_t)midi))
			return;
		monOn_[ch] = (uint8_t)on;
		monMidi_[ch] = (uint8_t)midi;
		FmMonShadowPcmNote(ch, midi, on);
	}

	void BuildTables()
	{
		double max = 255.0;
		for (int i = 0; i < 256; i++) {
			volumeTable_[i] = (int)(uint16_t)max;
			max /= pow(10.0, (4.5 / (256.0 / 16.0)) / 20.0);
		}
		for (int i = 0; i < 256; i++) panTable_[i] = 0.0;
		for (int i = 0; i < 0x48; i++)
			panTable_[i] = sqrt((double)(0x47 - i)) / sqrt((double)0x47);

		/* Envelope rates are per native sample; MAME's constants are tuned
		   experimentally against clock/384. */
		const double kArSpeed = 0.1, kDrSpeed = 2.0, kRrSpeed = 0.7;
		const int kMinAr = 0x02, kMaxAr = 0x80;
		const int kMinDr = 0x20, kMaxDr = 0x73;
		const int kMinRr = 0x20, kMaxRr = 0x54;
		const double clk = (double)(clockHz_ / 384u);

		double r = 1.0 / (kArSpeed * clk);
		for (int i = 0; i < kMinAr; i++) envAr_[i] = 1.0;
		for (int i = kMinAr; i < kMaxAr; i++)
			envAr_[i] = r * (kMaxAr - i) / (kMaxAr - kMinAr);
		for (int i = kMaxAr; i < kRf5c400EnvSteps; i++) envAr_[i] = 0.0;

		r = -5.0 / (kDrSpeed * clk);
		for (int i = 0; i < kMinDr; i++) envDr_[i] = r;
		for (int i = kMinDr; i < kMaxDr; i++)
			envDr_[i] = r * (kMaxDr - i) / (kMaxDr - kMinDr);
		for (int i = kMaxDr; i < kRf5c400EnvSteps; i++) envDr_[i] = 0.0;

		r = -5.0 / (kRrSpeed * clk);
		for (int i = 0; i < kMinRr; i++) envRr_[i] = r;
		for (int i = kMinRr; i < kMaxRr; i++)
			envRr_[i] = r * (kMaxRr - i) / (kMaxRr - kMinRr);
		for (int i = kMaxRr; i < kRf5c400EnvSteps; i++) envRr_[i] = 0.0;
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint16_t status_;
	uint32_t extAddr_;
	uint16_t extData_;
	unsigned reqChannel_;
	Channel chan_[kRf5c400Channels];
	int volumeTable_[256];
	double panTable_[256];
	double envAr_[kRf5c400EnvSteps];
	double envDr_[kRf5c400EnvSteps];
	double envRr_[kRf5c400EnvSteps];
	uint8_t monOn_[kRf5c400Channels];
	uint8_t monMidi_[kRf5c400Channels];
};

CChip* CEmuChipRf5c400Create(uint32_t clockHz, int sampleRate)
{
	return new CChipRf5c400(clockHz, sampleRate);
}
void CEmuChipRf5c400Destroy(CChip* c) { delete c; }

unsigned CEmuChipRf5c400ReadReg(CChip* c, unsigned offset)
{
	return c ? static_cast<CChipRf5c400*>(c)->ReadReg(offset) : 0;
}
