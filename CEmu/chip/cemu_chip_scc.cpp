#include "StdAfx.h"
#include "cemu_chip_scc.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>
#include <stdlib.h>

/* Konami SCC: 5トーンch。周期は波形1ステップあたりのマスタクロック12bit。
   波形は符号付き32バイト。原SCCではch4がch3波形を再利用（SCC+は独立だが
   こちらは原仕様のまま）。 */

namespace {
const int kSccChannels = 5;
const int kSccWave = 32;
const int kSccRegs = 0xC0; /* SCC-I は $A0-$BF も使う（ch5波形 / テスト） */
} /* namespace */

class CChipScc : public CChip {
public:
	CChipScc(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 3579545u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, plusMode_(0)
	{
		Reset();
	}

	void Reset() override
	{
		memset(regs_, 0, sizeof(regs_));
		memset(phase_, 0, sizeof(phase_));
		memset(acc_, 0, sizeof(acc_));
		plusMode_ = 0;
	}

	void SetPlusMode(int plus) { plusMode_ = plus ? 1 : 0; }

	void Write(uint32_t addr, uint32_t data) override
	{
		WriteReg((unsigned)(addr & 0xffu), (uint8_t)(data & 0xffu));
	}

	void WriteReg(unsigned reg, uint8_t data)
	{
		if (reg >= (unsigned)kSccRegs) return;
		regs_[reg] = data;
		if (reg >= 0x80u)
			UpdateMon(); /* 周波数/音量/オンをFMモニタへ */
	}

	uint8_t ReadReg(unsigned reg) const
	{
		if (reg >= (unsigned)kSccRegs) return 0xFFu;
		return regs_[reg];
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
		/* 出力1サンプルあたりマスタクロックを進める。 */
		const uint32_t step = (clockHz_ + (uint32_t)sampleRate_ / 2u)
			/ (uint32_t)sampleRate_;

		for (int i = 0; i < frames; i++) {
			int32_t mix = 0;
			for (int ch = 0; ch < kSccChannels; ch++) {
				const unsigned period = Period(ch);
				const unsigned onMask = plusMode_ ? regs_[0xaf] : regs_[0x8f];
				const unsigned volBase = plusMode_ ? 0xaau : 0x8au;
				const int on = (onMask & (1u << ch)) != 0;
				const int vol = on ? (regs_[volBase + ch] & 0x0f) : 0;
				if (vol && period > 1) {
					const int8_t* wave = Wave(ch);
					mix += (int32_t)wave[phase_[ch]] * vol;
					acc_[ch] += step;
					while (acc_[ch] >= period) {
						acc_[ch] -= period;
						phase_[ch] = (phase_[ch] + 1) & (kSccWave - 1);
					}
				} else if (period > 1) {
					/* ミュート中も位相を動かし、unmute時のクリックを防ぐ。 */
					acc_[ch] += step;
					while (acc_[ch] >= period) {
						acc_[ch] -= period;
						phase_[ch] = (phase_[ch] + 1) & (kSccWave - 1);
					}
				}
			}
			/* 波形±128 * vol15 * 5ch → 余裕あるヘッドルームへスケール。 */
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
		const unsigned base = plusMode_ ? 0xA0u : 0x80u;
		return ((unsigned)regs_[base + (unsigned)ch * 2u]
			| (((unsigned)regs_[base + 1u + (unsigned)ch * 2u] & 0x0fu) << 8)) + 1u;
	}

	const int8_t* Wave(int ch) const
	{
		if (plusMode_)
			return (const int8_t*)(regs_ + ch * kSccWave);
		if (ch >= 4)
			return (const int8_t*)(regs_ + 0xA0); /* SCC-I ch5 波形 */
		return (const int8_t*)(regs_ + ch * kSccWave);
	}

	void UpdateMon()
	{
		unsigned freq[5], vol[5];
		const unsigned volBase = plusMode_ ? 0xaau : 0x8au;
		const unsigned onMask = plusMode_ ? regs_[0xaf] : regs_[0x8f];
		for (int i = 0; i < 5; i++) {
			freq[i] = Period(i) - 1u;
			vol[i] = (unsigned)regs_[volBase + i] & 0x0fu;
		}
		/* SCC周波数/音量/オンマスクと 32 サンプル波形をFMモニタへ。 */
		uint8_t waves[5 * 32];
		for (int i = 0; i < 5; i++)
			memcpy(waves + i * 32, Wave(i), 32);
		FmMonShadowSetMsxDevices(SASAMI_FMMON_DEV_PSG | SASAMI_FMMON_DEV_SCC);
		FmMonShadowApplyScc(freq, vol, onMask & 0x1fu);
		FmMonShadowSetWaves(1, 5, 32, waves);
	}

	uint32_t clockHz_;
	int sampleRate_;
	int plusMode_;
	uint8_t regs_[kSccRegs];
	int phase_[kSccChannels];
	uint32_t acc_[kSccChannels];
};

/* SCC ラッパ生成。 */
CChip* CEmuChipSccCreate(uint32_t clockHz, int sampleRate)
{
	return new CChipScc(clockHz, sampleRate);
}
void CEmuChipSccDestroy(CChip* c) { delete c; }

void CEmuChipSccWriteReg(CChip* c, unsigned reg, uint8_t data)
{
	if (c) static_cast<CChipScc*>(c)->WriteReg(reg, data);
}

uint8_t CEmuChipSccReadReg(CChip* c, unsigned reg)
{
	return c ? static_cast<CChipScc*>(c)->ReadReg(reg) : 0xFFu;
}

void CEmuChipSccSetPlusMode(CChip* c, int plus)
{
	if (c) static_cast<CChipScc*>(c)->SetPlusMode(plus);
}
