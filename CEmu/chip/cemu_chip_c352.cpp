#include "StdAfx.h"
#include "cemu_chip_c352.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>

/* Namco C352 ? simplified from MAME devices/sound/c352.cpp (superctr).
   32 voices, 8-bit linear/muLaw, key-on via global register 0x202. */
enum {
	kC352Voices = 32,
	kC352RegsPerVoice = 8,
	kC352Regs = kC352Voices * kC352RegsPerVoice,
	C352_FLG_BUSY = 0x8000,
	C352_FLG_KEYON = 0x4000,
	C352_FLG_KEYOFF = 0x2000,
	C352_FLG_LOOPHIST = 0x0800,
	C352_FLG_PHASERL = 0x0200,
	C352_FLG_PHASEFL = 0x0100,
	C352_FLG_PHASEFR = 0x0080,
	C352_FLG_LDIR = 0x0040,
	C352_FLG_LINK = 0x0020,
	C352_FLG_NOISE = 0x0010,
	C352_FLG_MULAW = 0x0008,
	C352_FLG_FILTER = 0x0004,
	C352_FLG_LOOP = 0x0002,
	C352_FLG_REVERSE = 0x0001
};

static int CEmuC352Clamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipC352 : public CChip {
public:
	CChipC352(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 24576000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
		, control_(0)
		, random_(0x1234)
		, renderAcc_(0)
		, heldL_(0)
		, heldR_(0)
	{
		BuildMulaw();
		FmMonShadowSetC352Clock(clockHz_);
		Reset();
	}

	void Reset() override
	{
		memset(v_, 0, sizeof(v_));
		control_ = 0;
		random_ = 0x1234;
		renderAcc_ = 0;
		heldL_ = heldR_ = 0;
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		const unsigned offset = addr & 0x3ffu;
		const uint16_t d = (uint16_t)(data & 0xffff);
		if (offset < 0x100u) {
			const int ch = (int)(offset / 8u);
			const int r = (int)(offset & 7u);
			uint16_t* p = VoiceReg(ch, r);
			*p = d;
			FmMonShadowApplyC352Reg(offset, d);
			return;
		}
		if (offset == 0x200u) {
			control_ = d;
			return;
		}
		if (offset == 0x202u) {
			/* Global key-on / key-off execute (MAME c352_device::write). */
			for (int i = 0; i < kC352Voices; i++) {
				if (v_[i].flags & C352_FLG_KEYON) {
					/* Match C352 hardware/MAME: execute the programmed voice
					   exactly. Zero volume/frequency is valid driver state and
					   must not be promoted into a permanent audible voice. */
					v_[i].pos = ((uint32_t)v_[i].wave_bank << 16) | v_[i].wave_start;
					v_[i].sample = 0;
					v_[i].last_sample = 0;
					v_[i].counter = 0xffff;
					v_[i].flags |= C352_FLG_BUSY;
					v_[i].flags &= (uint16_t)~(C352_FLG_KEYON | C352_FLG_LOOPHIST);
					v_[i].curr_vol[0] = v_[i].curr_vol[1] = 0;
					v_[i].curr_vol[2] = v_[i].curr_vol[3] = 0;
					if (i < 32) {
						/* Sample root notes are not encoded in C352 registers.
						   Treat the phase increment as relative pitch instead
						   of displaying the sample stepping rate as O10. */
						int midi = FmMonShadowC352PitchToMidi(v_[i].freq);
						if (midi < 0) midi = 60;
						FmMonShadowPcmNote(i, midi, 1);
					}
				}
				else if (v_[i].flags & C352_FLG_KEYOFF) {
					v_[i].flags &= (uint16_t)~(C352_FLG_BUSY | C352_FLG_KEYOFF);
					v_[i].counter = 0xffff;
					v_[i].sample = 0;
					v_[i].last_sample = 0;
					if (i < 32) FmMonShadowPcmNote(i, 0, 0);
				}
			}
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
		const uint64_t tickDen = (uint64_t)sampleRate_ * 288u;
		for (int i = 0; i < frames; i++) {
			renderAcc_ += clockHz_;
			unsigned ticks = (unsigned)(renderAcc_ / tickDen);
			renderAcc_ %= tickDen;
			while (ticks--)
				MixNativeFrame(heldL_, heldR_);
			const int outL = heldL_ * gain / 256;
			const int outR = heldR_ * gain / 256;
			stereo[i * 2] = (int16_t)CEmuC352Clamp16((int)stereo[i * 2] + outL);
			stereo[i * 2 + 1] = (int16_t)CEmuC352Clamp16((int)stereo[i * 2 + 1] + outR);
		}
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override { rom_ = data; romSize_ = size; }
	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap == 0) return 0;
		unsigned need = kC352Regs * 2;
		unsigned n = cap < need ? cap : need;
		n &= ~1u;
		for (unsigned i = 0; i < n / 2; i++) {
			const int ch = (int)(i / 8u);
			const int r = (int)(i & 7u);
			const uint16_t w = *VoiceRegConst(ch, r);
			buf[i * 2] = (uint8_t)(w >> 8);
			buf[i * 2 + 1] = (uint8_t)w;
		}
		return n;
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

private:
	struct Voice {
		uint32_t pos;
		uint16_t counter;
		int16_t sample;
		int16_t last_sample;
		uint16_t vol_f;
		uint16_t vol_r;
		uint8_t curr_vol[4];
		uint16_t freq;
		uint16_t flags;
		uint16_t wave_bank;
		uint16_t wave_start;
		uint16_t wave_end;
		uint16_t wave_loop;
	};

	uint16_t* VoiceReg(int ch, int r)
	{
		Voice& v = v_[ch];
		switch (r) {
		case 0: return &v.vol_f;
		case 1: return &v.vol_r;
		case 2: return &v.freq;
		case 3: return &v.flags;
		case 4: return &v.wave_bank;
		case 5: return &v.wave_start;
		case 6: return &v.wave_end;
		default: return &v.wave_loop;
		}
	}
	const uint16_t* VoiceRegConst(int ch, int r) const
	{
		return const_cast<CChipC352*>(this)->VoiceReg(ch, r);
	}

	void BuildMulaw()
	{
		int j = 0;
		for (int i = 0; i < 128; i++) {
			mulaw_[i] = (int16_t)(j << 5);
			if (i < 16) j += 1;
			else if (i < 24) j += 2;
			else if (i < 48) j += 4;
			else if (i < 100) j += 8;
			else j += 16;
		}
		for (int i = 0; i < 128; i++)
			mulaw_[i + 128] = (int16_t)((~mulaw_[i]) & 0xffe0);
	}

	static void RampVol(Voice& v, int ch, uint8_t val)
	{
		const int delta = (int)v.curr_vol[ch] - (int)val;
		if (delta != 0)
			v.curr_vol[ch] = (uint8_t)(v.curr_vol[ch] + (delta > 0 ? -1 : 1));
	}

	void MixNativeFrame(int& outL, int& outR)
	{
		outL = outR = 0;
		for (int j = 0; j < kC352Voices; j++) {
			Voice& v = v_[j];
			int16_t s = 0;
			if (v.flags & C352_FLG_BUSY) {
				/* Some C76 dumps key before their envelope ISR reaches C352.
				   Keep the programmed registers truthful (zero remains zero),
				   but let the shared renderer bridge that transient state.
				   Later register writes take effect immediately. */
				const uint16_t step = v.freq ? v.freq : (uint16_t)0x2000;
				const uint16_t vf = (v.vol_f || v.vol_r) ? v.vol_f : (uint16_t)0x8080;
				const uint16_t vr = (v.vol_f || v.vol_r) ? v.vol_r : (uint16_t)0x8080;
				const int32_t next = (int32_t)v.counter + (int32_t)step;
				if (next & 0x10000)
					FetchSample(v, j);
				if ((next ^ (int32_t)v.counter) & 0x18000) {
					RampVol(v, 0, (uint8_t)(vf >> 8));
					RampVol(v, 1, (uint8_t)(vf & 0xff));
					RampVol(v, 2, (uint8_t)(vr >> 8));
					RampVol(v, 3, (uint8_t)(vr & 0xff));
				}
				v.counter = (uint16_t)(next & 0xffff);
				s = v.sample;
				if ((v.flags & C352_FLG_FILTER) == 0)
					s = (int16_t)(v.last_sample
						+ ((int32_t)v.counter * (v.sample - v.last_sample) >> 16));
			}
			const int fl = (v.flags & C352_FLG_PHASEFL) ? -s : s;
			const int fr = (v.flags & C352_FLG_PHASEFR) ? -s : s;
			outL += (fl * v.curr_vol[0]) >> 8;
			outR += (fr * v.curr_vol[1]) >> 8;
			/* Rear pair folded into stereo for CEmu's 2ch output. */
			outL += (((v.flags & C352_FLG_PHASERL) ? -s : s) * v.curr_vol[2]) >> 8;
			outR += (fr * v.curr_vol[3]) >> 8;
		}
		outL >>= 3;
		outR >>= 3;
	}

	uint8_t ReadRom(uint32_t pos) const
	{
		if (!rom_ || romSize_ == 0) return 0;
		return rom_[pos % romSize_];
	}

	void FetchSample(Voice& v, int voice)
	{
		v.last_sample = v.sample;
		if (v.flags & C352_FLG_NOISE) {
			random_ = (uint16_t)((random_ >> 1) ^ ((-(random_ & 1)) & 0xfff6));
			v.sample = (int16_t)random_;
			return;
		}
		const int8_t s = (int8_t)ReadRom(v.pos);
		if (v.flags & C352_FLG_MULAW)
			v.sample = mulaw_[s & 0xff];
		else
			v.sample = (int16_t)(s << 8);

		const uint16_t pos = (uint16_t)(v.pos & 0xffff);
		if ((v.flags & C352_FLG_LOOP) && (v.flags & C352_FLG_REVERSE)) {
			if ((v.flags & C352_FLG_LDIR) && pos == v.wave_loop)
				v.flags &= (uint16_t)~C352_FLG_LDIR;
			else if (!(v.flags & C352_FLG_LDIR) && pos == v.wave_end)
				v.flags |= C352_FLG_LDIR;
			v.pos += (v.flags & C352_FLG_LDIR) ? (uint32_t)-1 : 1u;
		} else if (pos == v.wave_end) {
			if ((v.flags & C352_FLG_LINK) && (v.flags & C352_FLG_LOOP)) {
				v.pos = ((uint32_t)v.wave_start << 16) | v.wave_loop;
				v.flags |= C352_FLG_LOOPHIST;
			} else if (v.flags & C352_FLG_LOOP) {
				v.pos = (v.pos & 0xff0000u) | v.wave_loop;
				v.flags |= C352_FLG_LOOPHIST;
			} else {
				v.flags |= C352_FLG_KEYOFF;
				v.flags &= (uint16_t)~C352_FLG_BUSY;
				v.sample = 0;
				v.last_sample = 0;
				FmMonShadowPcmNote(voice, 0, 0);
			}
		} else {
			v.pos += (v.flags & C352_FLG_REVERSE) ? (uint32_t)-1 : 1u;
		}
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint16_t control_;
	uint16_t random_;
	uint64_t renderAcc_;
	int heldL_;
	int heldR_;
	int16_t mulaw_[256];
	Voice v_[kC352Voices];
};

CChip* CEmuChipC352Create(uint32_t clockHz, int sampleRate)
{
	return new CChipC352(clockHz, sampleRate);
}

void CEmuChipC352Destroy(CChip* c)
{
	delete c;
}
