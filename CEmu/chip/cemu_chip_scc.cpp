#include "StdAfx.h"
#include "cemu_chip_scc.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>
#include <stdlib.h>

/* Konami SCC: 5 tone channels. Period is a 12-bit value in master clocks per
   wave step; the wave is 32 signed bytes. Channel 4 reuses channel 3's wave
   on the original SCC (SCC+ has a separate wave — we keep the original). */

namespace {
const int kSccChannels = 5;
const int kSccWave = 32;
const int kSccRegs = 0xC0; /* SCC-I also uses $A0-$BF (ch5 wave / test) */
} /* namespace */

class CChipScc : public CChip {
public:
	CChipScc(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 3579545u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
	{
		Reset();
	}

	void Reset() override
	{
		memset(regs_, 0, sizeof(regs_));
		memset(phase_, 0, sizeof(phase_));
		memset(acc_, 0, sizeof(acc_));
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		WriteReg((unsigned)(addr & 0xffu), (uint8_t)(data & 0xffu));
	}

	void WriteReg(unsigned reg, uint8_t data)
	{
		if (reg >= (unsigned)kSccRegs) return;
		regs_[reg] = data;
		if (reg >= 0x80u)
			UpdateMon();
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
		/* Advance master clocks per output sample. */
		const uint32_t step = (clockHz_ + (uint32_t)sampleRate_ / 2u)
			/ (uint32_t)sampleRate_;

		for (int i = 0; i < frames; i++) {
			int32_t mix = 0;
			for (int ch = 0; ch < kSccChannels; ch++) {
				const unsigned period = Period(ch);
				const int on = (regs_[0x8f] & (1u << ch)) != 0;
				const int vol = on ? (regs_[0x8a + ch] & 0x0f) : 0;
				if (vol && period > 1) {
					const int8_t* wave = Wave(ch);
					mix += (int32_t)wave[phase_[ch]] * vol;
					acc_[ch] += step;
					while (acc_[ch] >= period) {
						acc_[ch] -= period;
						phase_[ch] = (phase_[ch] + 1) & (kSccWave - 1);
					}
				} else if (period > 1) {
					/* Keep phase moving so mute→unmute doesn't click. */
					acc_[ch] += step;
					while (acc_[ch] >= period) {
						acc_[ch] -= period;
						phase_[ch] = (phase_[ch] + 1) & (kSccWave - 1);
					}
				}
			}
			/* wave±128 * vol15 * 5ch → scale into a comfortable headroom. */
			mix = (mix * gain) / (16 * 4);
			int32_t l = (int32_t)stereo[i * 2] + mix;
			int32_t r = (int32_t)stereo[i * 2 + 1] + mix;
			if (l > 32767) l = 32767;
			if (l < -32768) l = -32768;
			if (r > 32767) r = 32767;
			if (r < -32768) r = -32768;
			stereo[i * 2] = (int16_t)l;
			stereo[i * 2 + 1] = (int16_t)r;
		}
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

private:
	unsigned Period(int ch) const
	{
		return ((unsigned)regs_[0x80 + ch * 2]
			| (((unsigned)regs_[0x81 + ch * 2] & 0x0fu) << 8)) + 1u;
	}

	const int8_t* Wave(int ch) const
	{
		if (ch >= 4)
			return (const int8_t*)(regs_ + 0xA0); /* SCC-I ch5 wave */
		return (const int8_t*)(regs_ + ch * kSccWave);
	}

	void UpdateMon()
	{
		unsigned freq[5], vol[5];
		for (int i = 0; i < 5; i++) {
			freq[i] = Period(i) - 1u;
			vol[i] = (unsigned)regs_[0x8a + i] & 0x0fu;
		}
		FmMonShadowSetMsxDevices(SASAMI_FMMON_DEV_PSG | SASAMI_FMMON_DEV_SCC);
		FmMonShadowApplyScc(freq, vol, (unsigned)regs_[0x8f] & 0x1fu);
	}

	uint32_t clockHz_;
	int sampleRate_;
	uint8_t regs_[kSccRegs];
	int phase_[kSccChannels];
	uint32_t acc_[kSccChannels];
};

CChip* CEmuChipSccCreate(uint32_t clockHz, int sampleRate)
{
	return new CChipScc(clockHz, sampleRate);
}
void CEmuChipSccDestroy(CChip* c) { delete c; }

void CEmuChipSccWriteReg(CChip* c, unsigned reg, uint8_t data)
{
	if (c) static_cast<CChipScc*>(c)->WriteReg(reg, data);
}
