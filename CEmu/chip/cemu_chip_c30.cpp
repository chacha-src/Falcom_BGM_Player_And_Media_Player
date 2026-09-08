#include "StdAfx.h"
#include "cemu_chip_c30.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>

/* Namco CUS30 8-voice wavetable PSG.
   Ported from hoot ssC30.cpp / ssC30.h (MAME namco.cpp CUS30 / 15XX register
   behavior — license:BSD-3-Clause, copyright-holders:Nicola Salmoria,
   Aaron Giles; hoot adaptation retains the same algorithm). Adapted to the
   CEmu CChip interface (no ssSoundChip / track-info layer).

   MAME namco_cus30 amap (relative):
     0x000-0x0ff  wave RAM (16 waves x 16 packed nibbles)
     0x100-0x13f  voice registers (8 voices x 8)
     0x140-0x3ff  shared RAM (ignored here)

   System 1 / 86 register layout (per voice, base = ch*8):
     +0 left vol    +1 wave_sel|freq_hi  +2/+3 freq
     +4 right vol; bit7 of +4 keys noise on next channel
   Mappy / 15XX layout (WriteMAPPY):
     +3 volume (mono)  +4/+5/+6 frequency  +6 hi nibble selects wave */

enum { kC30Voices = 8, kC30WaveBytes = 0x100, kC30Regs = 0x40 };

static int CEmuC30Clamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipC30 : public CChip {
public:
	CChipC30(uint32_t clockHz, int sampleRate, int mode)
		: clockHz_(clockHz ? clockHz : 24000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, mode_(mode == CEMU_C30_MAPPY ? CEMU_C30_MAPPY
			: (mode == CEMU_C30_PACMAN ? CEMU_C30_PACMAN : CEMU_C30_STEREO))
		, stereo_(mode_ == CEMU_C30_STEREO ? 1 : 0)
		, enable_(1)
	{
		Reset();
	}

	void Reset() override
	{
		memset(reg_, 0, sizeof(reg_));
		memset(shared_, 0, sizeof(shared_));
		memset(wave_, 0, sizeof(wave_));
		memset(ch_, 0, sizeof(ch_));
		memset(pacRegs_, 0, sizeof(pacRegs_));
		memset(monOn_, 0, sizeof(monOn_));
		memset(monMidi_, 0, sizeof(monMidi_));
		for (int i = 0; i < kC30Voices; i++) {
			ch_[i].wave = wave_;
			ch_[i].noise_seed = 1;
		}
	}

	void SetEnable(int enable) { enable_ = enable ? 1 : 0; }

	void Write(uint32_t addr, uint32_t data) override
	{
		const unsigned a = addr & 0x3ffu;
		const uint8_t d = (uint8_t)(data & 0xff);
		if (mode_ == CEMU_C30_PACMAN) {
			WritePacman(a & 0x1fu, (uint8_t)(d & 0x0fu));
			return;
		}
		/* MAME namco_15xx amap: 000-03F regs, 040-3FF shared RAM (no wave RAM). */
		if (mode_ == CEMU_C30_MAPPY) {
			if (a < 0x40u)
				WriteRegMappy((int)a, d);
			else
				shared_[a] = d;
			return;
		}
		if (a < kC30WaveBytes) {
			wave_[a] = d;
			return;
		}
		if (a >= 0x100u && a < 0x140u) {
			WriteReg((int)(a - 0x100u), d);
			return;
		}
		/* Direct 0x00-0x3f register poke (audition / stripped maps). */
		if (a < kC30Regs)
			WriteReg((int)a, d);
	}

	uint8_t ReadMappy(uint32_t addr) const
	{
		const unsigned a = addr & 0x3ffu;
		if (a < 0x40u)
			return reg_[a];
		return shared_[a];
	}

	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }
	uint8_t ReadByte(uint32_t addr) { return ReadMappy(addr); }

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
		if (gain <= 0) return;
		for (int i = 0; i < kC30Voices; i++) {
			Channel& ch = ch_[i];
			const int vl = ch.voll;
			const int vr = ch.volr;
			if (!ch.noise) {
				if (!(vl || vr) || !ch.freq) continue;
				for (int f = 0; f < frames; f++) {
					const int p = (ch.offset >> 16) & 0x1f;
					int8_t s;
					if (p & 1)
						s = (int8_t)((ch.wave[p >> 1] & 0x0f) - 8);
					else
						s = (int8_t)((ch.wave[p >> 1] >> 4) - 8);
					s = (int8_t)(s << 4);
					const int l = (int)s * vl;
					const int r = (int)s * vr;
					stereo[f * 2] = (int16_t)CEmuC30Clamp16(
						(int)stereo[f * 2] + l * gain / 256);
					stereo[f * 2 + 1] = (int16_t)CEmuC30Clamp16(
						(int)stereo[f * 2 + 1] + r * gain / 256);
					ch.offset += ch.incr;
				}
			} else {
				if (!(vl || vr) || !(ch.freq & 0xff)) continue;
				int delta = ((ch.freq & 0xff) << 4) * sampleRate_ / (int)clockHz_;
				if (delta < 1) delta = 1;
				int c = ch.noise_counter;
				for (int f = 0; f < frames; f++) {
					const int s = ch.noise_state ? (0x07 * 16) : (-0x08 * 16);
					const int l = s * vl;
					const int r = s * vr;
					stereo[f * 2] = (int16_t)CEmuC30Clamp16(
						(int)stereo[f * 2] + l * gain / 256);
					stereo[f * 2 + 1] = (int16_t)CEmuC30Clamp16(
						(int)stereo[f * 2 + 1] + r * gain / 256);
					c += delta;
					int cnt = c >> 12;
					c &= (1 << 12) - 1;
					for (; cnt > 0; cnt--) {
						if ((ch.noise_seed + 1) & 2) ch.noise_state ^= 1;
						if (ch.noise_seed & 1) ch.noise_seed ^= 0x28000;
						ch.noise_seed >>= 1;
					}
				}
				ch.noise_counter = c;
			}
		}
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		/* PROM / dumped wave tables for older WSG boards. CUS30 normally
		   gets wave RAM written by the sound CPU into 0x000-0x0ff. */
		if (!data || !size) return;
		const unsigned n = size < kC30WaveBytes ? size : (unsigned)kC30WaveBytes;
		memcpy(wave_, data, n);
		if (n < kC30WaveBytes)
			memset(wave_ + n, 0, kC30WaveBytes - n);
		for (int i = 0; i < kC30Voices; i++) {
			if (!ch_[i].wave) ch_[i].wave = wave_;
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

private:
	struct Channel {
		int freq;
		int voll;
		int volr;
		const uint8_t* wave;
		int offset;
		int incr;
		uint8_t noise;
		uint8_t noise_state;
		int noise_counter;
		int noise_seed;
	};

	/* Report the voice to the FM monitor. CUS30 has no key-on strobe: a voice
	   sounds whenever it has volume and a non-zero divisor, which is exactly
	   the condition MixAdd uses. The bind table already lists CUS30 as 8 PCM
	   rows, so without this the monitor stayed blank on every WSG board while
	   audio played (rally-x, wsg6809, nd1, wsg63701). */
	void UpdateMon(int channel)
	{
		if (channel < 0 || channel >= kC30Voices) return;
		const Channel& ch = ch_[channel];
		const int hasVol = (ch.voll || ch.volr) ? 1 : 0;
		const int on = ch.noise ? (hasVol && (ch.freq & 0xff))
			: (hasVol && ch.freq);
		int midi = 36; /* noise has no pitch; park it at the low end */
		if (on && !ch.noise) {
			/* MixAdd walks 32 wave steps per 2^21 of offset accumulator and
			   incr = freq*clock*2/rate, so the tone is freq*clock/2^20 Hz. */
			midi = FmMonShadowHzToMidi((double)ch.freq * (double)clockHz_
				/ 1048576.0);
			if (midi < 0) midi = 36;
		}
		if (monOn_[channel] == (uint8_t)on
			&& (!on || monMidi_[channel] == (uint8_t)midi))
			return;
		monOn_[channel] = (uint8_t)on;
		monMidi_[channel] = (uint8_t)midi;
		FmMonShadowPcmNote(channel, midi, on);
	}

	void RecomputeIncr(Channel& ch)
	{
		/* ssC30: incr = freq * basefreq * 2 / sample_rate (truncated to int). */
		if (ch.freq <= 0 || sampleRate_ <= 0) {
			ch.incr = 0;
			return;
		}
		ch.incr = (int)((double)ch.freq * (double)clockHz_ * 2.0 / (double)sampleRate_);
	}

	void WriteRegStereo(int adr, uint8_t data)
	{
		if (adr < 0 || adr >= kC30Regs) return;
		reg_[adr] = data;
		const int channel = adr >> 3;
		Channel& ch = ch_[channel];
		const int reg = adr & 7;
		int alsoMon = -1;
		switch (reg) {
		case 4:
			ch_[(channel + 1) % kC30Voices].noise = (uint8_t)(data & 0x80);
			alsoMon = (channel + 1) % kC30Voices;
			/* fall through */
		case 0: {
			ch.voll = (reg_[channel * 8 + 0] & 0x0f);
			if (stereo_)
				ch.volr = (reg_[channel * 8 + 4] & 0x0f);
			else
				ch.volr = (reg_[channel * 8 + 0] & 0x0f);
			break;
		}
		case 1:
			ch.wave = wave_ + ((data >> 4) & 0x0f) * 16;
			/* fall through */
		case 2:
		case 3:
			ch.freq = ((reg_[channel * 8 + 1] & 0x0f) << 16)
				+ (reg_[channel * 8 + 2] << 8)
				+ reg_[channel * 8 + 3];
			RecomputeIncr(ch);
			break;
		default:
			break;
		}
		UpdateMon(channel);
		if (alsoMon >= 0) UpdateMon(alsoMon);
	}

	void WriteRegMappy(int adr, uint8_t data)
	{
		if (adr < 0 || adr >= kC30Regs) return;
		reg_[adr] = data;
		const int channel = adr >> 3;
		Channel& ch = ch_[channel];
		const int reg = adr & 7;
		switch (reg) {
		case 3: {
			const int v = data & 0x0f;
			ch.voll = v;
			ch.volr = v;
			break;
		}
		case 6:
			ch.wave = wave_ + ((data >> 4) & 7) * 16;
			/* fall through */
		case 4:
		case 5:
			ch.freq = ((reg_[channel * 8 + 6] & 0x0f) << 16)
				+ (reg_[channel * 8 + 5] << 8)
				+ reg_[channel * 8 + 4];
			RecomputeIncr(ch);
			break;
		default:
			break;
		}
		UpdateMon(channel);
	}

	void WriteReg(int adr, uint8_t data)
	{
		if (mode_ == CEMU_C30_MAPPY)
			WriteRegMappy(adr, data);
		else
			WriteRegStereo(adr, data);
	}

	/* MAME namco_wsg_device::pacman_sound_w — 3 voices, nibble regs @0x00-0x1F. */
	void WritePacman(unsigned offset, uint8_t data)
	{
		if (offset > 0x1fu) return;
		pacRegs_[offset] = (uint8_t)(data & 0x0fu);
		int ch;
		if (offset < 0x10u)
			ch = ((int)offset - 5) / 5;
		else if (offset == 0x10u)
			ch = 0;
		else
			ch = ((int)offset - 0x11) / 5;
		if (ch < 0 || ch > 2) return;
		Channel& voice = ch_[ch];
		const int rel = (int)offset - ch * 5;
		switch (rel) {
		case 0x05:
			voice.wave = wave_ + (pacRegs_[offset] & 7) * 16;
			break;
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14: {
			unsigned freq = (ch == 0) ? pacRegs_[0x10] : 0u;
			freq += (unsigned)pacRegs_[ch * 5 + 0x11] << 4;
			freq += (unsigned)pacRegs_[ch * 5 + 0x12] << 8;
			freq += (unsigned)pacRegs_[ch * 5 + 0x13] << 12;
			freq += (unsigned)pacRegs_[ch * 5 + 0x14] << 16;
			voice.freq = (int)freq;
			RecomputeIncr(voice);
			break;
		}
		case 0x15: {
			const int v = pacRegs_[offset] & 0x0f;
			voice.voll = v;
			voice.volr = v;
			break;
		}
		default:
			break;
		}
		UpdateMon(ch);
	}

	uint32_t clockHz_;
	int sampleRate_;
	int mode_;
	int stereo_;
	int enable_;
	uint8_t reg_[kC30Regs];
	uint8_t shared_[0x400];
	uint8_t pacRegs_[0x20];
	uint8_t wave_[kC30WaveBytes];
	Channel ch_[kC30Voices];
	uint8_t monOn_[kC30Voices];
	uint8_t monMidi_[kC30Voices];
};

CChip* CEmuChipC30Create(uint32_t clockHz, int sampleRate, int mode)
{
	return new CChipC30(clockHz, sampleRate, mode);
}

void CEmuChipC30Destroy(CChip* c)
{
	delete c;
}

void CEmuChipC30SetEnable(CChip* c, int enable)
{
	if (c) static_cast<CChipC30*>(c)->SetEnable(enable);
}

uint8_t CEmuChipC30Read(CChip* c, uint32_t addr)
{
	return c ? static_cast<CChipC30*>(c)->ReadByte(addr) : 0xff;
}
