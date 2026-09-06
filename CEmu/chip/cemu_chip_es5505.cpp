/* license:BSD-3-Clause
 * copyright-holders:Aaron Giles
 * Ensoniq ES5505 (OTIS) core — adapted from MAME src/devices/sound/es5506.cpp
 * for CEmu (MSVC / no MAME device framework).
 */
#include "StdAfx.h"
#include "cemu_chip_es5505.h"
#include "cemu_chip.h"
#include <string.h>
#include <stdlib.h>

enum {
	kEs5505Voices = 32,
	kVolumeBit = 8,
	kAddrIntBit = 20,
	kAddrFracBitEs = 9,
	kAddrFracBit = 11,
	kFilterBit = 12,
	kFilterShift = 4,
	kVolumeAccBit = 20,
	kControlStop0 = 0x0001,
	kControlStop1 = 0x0002,
	kControlStopMask = 0x0003,
	kControlLei = 0x0004,
	kControlLpe = 0x0008,
	kControlBle = 0x0010,
	kControlLoopMask = 0x0018,
	kControlIrqe = 0x0020,
	kControlDir = 0x0040,
	kControlIrq = 0x0080,
	kLp3 = 1,
	kLp4 = 2
};

static int CEmuEsClamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

static int64_t CEmuEsLshift(int64_t val, int shift)
{
	return (shift >= 0) ? (val << shift) : (val >> (-shift));
}

static uint64_t CEmuEsLshiftU(uint64_t val, int shift)
{
	return (shift >= 0) ? (val << shift) : (val >> (-shift));
}

static uint64_t CEmuEsRshiftU(uint64_t val, int shift)
{
	return (shift >= 0) ? (val >> shift) : (val << (-shift));
}

static int32_t CEmuEsApplyLowpass(int32_t out, int32_t cutoff, int32_t in)
{
	return ((int32_t)(cutoff >> kFilterShift) * (out - in) / (1 << kFilterBit)) + in;
}

static int32_t CEmuEsApplyHighpass(int32_t out, int32_t cutoff, int32_t in, int32_t prev)
{
	return out - prev + ((int32_t)(cutoff >> kFilterShift) * in) / (1 << (kFilterBit + 1)) + in / 2;
}

class CChipEs5505 : public CChip {
public:
	CChipEs5505(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 15238100u)
		, hostRate_(sampleRate > 0 ? sampleRate : 44100)
		, chipRate_(29761)
		, rom_(NULL)
		, romWords_(0)
		, page_(0)
		, activeVoices_(0x1f)
		, mode_(0)
		, irqv_(0x80)
		, voiceIndex_(0)
		, volumeShift_(0)
		, volumeAccShift_(0)
		, addrAccShift_(0)
		, addrAccMask_(0)
		, phase_(0)
		, lastL_(0)
		, lastR_(0)
	{
		memset(voice_, 0, sizeof(voice_));
		memset(voiceBank_, 0, sizeof(voiceBank_));
		memset(volLut_, 0, sizeof(volLut_));
		ComputeTables();
		GetAccumMask();
		for (int j = 0; j < kEs5505Voices; j++) {
			voice_[j].index = (uint8_t)j;
			voice_[j].control = kControlStopMask;
			voice_[j].lvol = 1 << (kVolumeBit - 1);
			voice_[j].rvol = 1 << (kVolumeBit - 1);
		}
		UpdateChipRate();
	}

	void Reset() override
	{
		page_ = 0;
		activeVoices_ = 0x1f;
		mode_ = 0;
		irqv_ = 0x80;
		voiceIndex_ = 0;
		phase_ = 0;
		memset(voiceBank_, 0, sizeof(voiceBank_));
		for (int j = 0; j < kEs5505Voices; j++) {
			memset(&voice_[j], 0, sizeof(voice_[j]));
			voice_[j].index = (uint8_t)j;
			voice_[j].control = kControlStopMask;
			voice_[j].lvol = 1 << (kVolumeBit - 1);
			voice_[j].rvol = 1 << (kVolumeBit - 1);
		}
		UpdateChipRate();
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		/* Word offset 0..0xf into page registers (CPU maps 0x200000 + offset*2). */
		const unsigned offset = (unsigned)(addr & 0x0f);
		const uint16_t d = (uint16_t)(data & 0xffff);
		Voice* voice = &voice_[page_ & 0x1f];
		if (page_ < 0x20)
			RegWriteLow(voice, offset, d);
		else if (page_ < 0x40)
			RegWriteHigh(voice, offset, d);
		else
			RegWriteTest(offset, d);
	}

	uint16_t ReadReg(uint32_t addr)
	{
		const unsigned offset = (unsigned)(addr & 0x0f);
		Voice* voice = &voice_[page_ & 0x1f];
		if (page_ < 0x20)
			return RegReadLow(voice, offset);
		if (page_ < 0x40)
			return RegReadHigh(voice, offset);
		return RegReadTest(offset);
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
		const int chipRate = chipRate_ > 0 ? chipRate_ : 29761;
		const int host = hostRate_ > 0 ? hostRate_ : 44100;
		for (int i = 0; i < frames; i++) {
			phase_ += chipRate;
			while (phase_ >= host) {
				phase_ -= host;
				GenerateOne();
			}
			const int l = CEmuEsClamp16((int)stereo[i * 2] + (lastL_ * gain) / 256);
			const int r = CEmuEsClamp16((int)stereo[i * 2 + 1] + (lastR_ * gain) / 256);
			stereo[i * 2] = (int16_t)l;
			stereo[i * 2 + 1] = (int16_t)r;
		}
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		rom_ = data;
		romWords_ = size / 2u;
	}

	void SetVoiceBank(int voice, uint32_t wordBase)
	{
		if (voice < 0 || voice >= kEs5505Voices) return;
		voiceBank_[voice] = wordBase;
	}

	uint32_t VoiceIndex() const { return voiceIndex_; }

	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap < 4) return 0;
		buf[0] = page_;
		buf[1] = activeVoices_;
		/* buf[2]: voices currently not STOP'd (audible candidates). */
		uint8_t live = 0;
		for (int j = 0; j <= activeVoices_ && j < kEs5505Voices; j++) {
			if (!(voice_[j].control & kControlStopMask))
				live++;
		}
		buf[2] = live;
		buf[3] = (uint8_t)mode_;
		return 4;
	}

	bool Irq() const override { return (irqv_ & 0x80) == 0; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return irqv_; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

private:
	struct Voice {
		uint32_t control;
		uint64_t freqcount;
		uint64_t start;
		uint32_t lvol;
		uint64_t end;
		uint64_t accum;
		uint32_t rvol;
		uint32_t k2;
		uint32_t k1;
		int32_t o4n1, o3n1, o3n2, o2n1, o2n2, o1n1;
		uint8_t index;
		uint8_t filtcount;
	};

	void ComputeTables()
	{
		/* ES5505: 4-bit exponent + 4-bit mantissa, total volume bit = 8. */
		volumeShift_ = 0;
		const unsigned volumeLen = 1u << 8;
		const unsigned exponentShift = 16;
		const unsigned mantissaLen = 16;
		const unsigned mantissaShift = exponentShift - 4 - 1;
		for (unsigned i = 0; i < volumeLen; i++) {
			const unsigned exponent = (i >> 4) & 15;
			const unsigned mantissa = (i & 15) | mantissaLen;
			volLut_[i] = (mantissa << mantissaShift) >> (exponentShift - exponent);
		}
		volumeAccShift_ = (16 + 15) - kVolumeAccBit;
	}

	void GetAccumMask()
	{
		addrAccShift_ = kAddrFracBit - kAddrFracBitEs;
		const uint64_t intMask = ((1ull << kAddrIntBit) - 1ull);
		const uint64_t fracMask = ((1ull << kAddrFracBitEs) - 1ull);
		addrAccMask_ = CEmuEsLshiftU((intMask << kAddrFracBitEs) | fracMask, addrAccShift_);
		if (addrAccShift_ > 0)
			addrAccMask_ |= ((1ull << addrAccShift_) - 1ull);
	}

	void UpdateChipRate()
	{
		const unsigned den = 16u * (unsigned)(activeVoices_ + 1);
		chipRate_ = den ? (int)(clockHz_ / den) : 29761;
		if (chipRate_ < 1) chipRate_ = 1;
	}

	uint64_t AccShift(uint64_t val, int bias = 0) const
	{
		return CEmuEsLshiftU(val, addrAccShift_ - bias);
	}

	uint64_t AccRes(uint64_t val, int bias = 0) const
	{
		return CEmuEsRshiftU(val, addrAccShift_ - bias);
	}

	uint64_t IntegerAddr(uint64_t accum, int32_t bias = 0) const
	{
		return ((accum + ((uint64_t)bias << kAddrFracBit)) & addrAccMask_) >> kAddrFracBit;
	}

	uint32_t GetLp(uint32_t control) const { return (control >> 10) & 3; }
	uint32_t GetCa(uint32_t control) const { return (control >> 8) & 3; }
	uint32_t GetBank(uint32_t control) const { return (control >> 2) & 1; }

	uint64_t GetVolume(uint32_t volume) const
	{
		uint32_t idx = (uint32_t)CEmuEsRshiftU(volume, volumeShift_);
		if (idx > 255) idx = 255;
		return volLut_[idx];
	}

	int64_t GetSample(int32_t sample, uint32_t volume) const
	{
		return CEmuEsRshiftU((uint64_t)((int64_t)sample * (int64_t)GetVolume(volume)), (int)volumeAccShift_);
	}

	int32_t Interpolate(int32_t s1, int32_t s2, uint64_t accum) const
	{
		const uint32_t shifted = 1u << kAddrFracBit;
		const uint32_t mask = shifted - 1u;
		accum &= mask & addrAccMask_;
		return (s1 * (int32_t)(shifted - (uint32_t)accum) + s2 * (int32_t)accum) >> kAddrFracBit;
	}

	void ApplyFilters(Voice* voice, int32_t& sample)
	{
		sample = CEmuEsApplyLowpass(sample, (int32_t)voice->k1, voice->o1n1);
		voice->o1n1 = sample;
		sample = CEmuEsApplyLowpass(sample, (int32_t)voice->k1, voice->o2n1);
		voice->o2n2 = voice->o2n1;
		voice->o2n1 = sample;
		switch (GetLp(voice->control)) {
		case 0:
			sample = CEmuEsApplyHighpass(sample, (int32_t)voice->k2, voice->o3n1, voice->o2n2);
			voice->o3n2 = voice->o3n1; voice->o3n1 = sample;
			sample = CEmuEsApplyHighpass(sample, (int32_t)voice->k2, voice->o4n1, voice->o3n2);
			voice->o4n1 = sample;
			break;
		case kLp3:
			sample = CEmuEsApplyLowpass(sample, (int32_t)voice->k1, voice->o3n1);
			voice->o3n2 = voice->o3n1; voice->o3n1 = sample;
			sample = CEmuEsApplyHighpass(sample, (int32_t)voice->k2, voice->o4n1, voice->o3n2);
			voice->o4n1 = sample;
			break;
		case kLp4:
			sample = CEmuEsApplyLowpass(sample, (int32_t)voice->k2, voice->o3n1);
			voice->o3n2 = voice->o3n1; voice->o3n1 = sample;
			sample = CEmuEsApplyLowpass(sample, (int32_t)voice->k2, voice->o4n1);
			voice->o4n1 = sample;
			break;
		default:
			sample = CEmuEsApplyLowpass(sample, (int32_t)voice->k1, voice->o3n1);
			voice->o3n2 = voice->o3n1; voice->o3n1 = sample;
			sample = CEmuEsApplyLowpass(sample, (int32_t)voice->k2, voice->o4n1);
			voice->o4n1 = sample;
			break;
		}
	}

	uint16_t ReadSampleWord(Voice* voice, uint64_t wordAddr)
	{
		voiceIndex_ = voice->index;
		if (!rom_ || romWords_ == 0) return 0;
		uint64_t idx = (voiceBank_[voice->index] + wordAddr) & (romWords_ - 1ull);
		/* Also honor BS bit as second half within a 2-bank window when no otisbank. */
		if (GetBank(voice->control) && voiceBank_[voice->index] == 0 && romWords_ > 0x100000ull)
			idx = (idx + 0x100000ull) & (romWords_ - 1ull);
		const uint8_t* p = rom_ + (size_t)idx * 2u;
		return (uint16_t)((p[0] << 8) | p[1]); /* BE word */
	}

	void CheckEndForward(Voice* voice, uint64_t& accum)
	{
		if (accum <= voice->end) return;
		if (voice->control & kControlIrqe)
			voice->control |= kControlIrq;
		switch (voice->control & kControlLoopMask) {
		case 0:
		case kControlBle:
			voice->control |= kControlStop0;
			break;
		case kControlLpe:
			accum = (voice->start + (accum - voice->end)) & addrAccMask_;
			break;
		case kControlLpe | kControlBle:
			accum = (voice->end - (accum - voice->end)) & addrAccMask_;
			voice->control ^= kControlDir;
			break;
		}
	}

	void CheckEndReverse(Voice* voice, uint64_t& accum)
	{
		if (accum >= voice->start) return;
		if (voice->control & kControlIrqe)
			voice->control |= kControlIrq;
		switch (voice->control & kControlLoopMask) {
		case 0:
		case kControlBle:
			voice->control |= kControlStop0;
			break;
		case kControlLpe:
			accum = (voice->end - (voice->start - accum)) & addrAccMask_;
			break;
		case kControlLpe | kControlBle:
			accum = (voice->start + (voice->start - accum)) & addrAccMask_;
			voice->control ^= kControlDir;
			break;
		}
	}

	void GeneratePcm(Voice* voice, int32_t* dest)
	{
		const uint32_t freqcount = (uint32_t)voice->freqcount;
		uint64_t accum = voice->accum & addrAccMask_;
		if (!(voice->control & kControlStopMask)) {
			/* OTIS per-voice vol often left at 0; board gain is MB87078 (0dB reset). */
			uint32_t lv = voice->lvol ? voice->lvol : 0xffu;
			uint32_t rv = voice->rvol ? voice->rvol : 0xffu;
			if (!(voice->control & kControlDir)) {
				int32_t val1 = (int16_t)ReadSampleWord(voice, IntegerAddr(accum));
				int32_t val2 = (int16_t)ReadSampleWord(voice, IntegerAddr(accum, 1));
				val1 = Interpolate(val1, val2, accum);
				accum = (accum + freqcount) & addrAccMask_;
				ApplyFilters(voice, val1);
				dest[0] += (int32_t)GetSample(val1, lv);
				dest[1] += (int32_t)GetSample(val1, rv);
				CheckEndForward(voice, accum);
			} else {
				int32_t val1 = (int16_t)ReadSampleWord(voice, IntegerAddr(accum));
				int32_t val2 = (int16_t)ReadSampleWord(voice, IntegerAddr(accum, 1));
				val1 = Interpolate(val1, val2, accum);
				accum = (accum - freqcount) & addrAccMask_;
				ApplyFilters(voice, val1);
				dest[0] += (int32_t)GetSample(val1, lv);
				dest[1] += (int32_t)GetSample(val1, rv);
				CheckEndReverse(voice, accum);
			}
		}
		voice->accum = accum;
	}

	void GenerateIrq(Voice* voice, int v)
	{
		if (!(voice->control & kControlIrq)) return;
		if (irqv_ & 0x80) {
			irqv_ = (uint8_t)(v & 0x1f);
			voice->control &= ~kControlIrq;
		}
	}

	void GenerateOne()
	{
		int32_t ch[8];
		memset(ch, 0, sizeof(ch));
		for (int v = 0; v <= activeVoices_; v++) {
			Voice* voice = &voice_[v];
			const int channel = (int)(GetCa(voice->control) % 4);
			const int l = channel << 1;
			GeneratePcm(voice, &ch[l]);
			GenerateIrq(voice, v);
		}
		/* Mix 4 stereo pairs → L/R; samples are ~20-bit — shift to 16. */
		int64_t l = 0, r = 0;
		for (int c = 0; c < 4; c++) {
			l += ch[c * 2];
			r += ch[c * 2 + 1];
		}
		lastL_ = CEmuEsClamp16((int)(l >> 4));
		lastR_ = CEmuEsClamp16((int)(r >> 4));
	}

	void RegWriteLow(Voice* voice, unsigned offset, uint16_t data)
	{
		switch (offset) {
		case 0x00:
			voice->control |= 0xf000;
			voice->control = (voice->control & ~0x0fff) | (data & 0x0fff);
			break;
		case 0x01:
			voice->freqcount = (voice->freqcount & ~AccShift(0x00fe, 1)) | AccShift(data & 0x00fe, 1);
			voice->freqcount = (voice->freqcount & ~AccShift(0xff00, 1)) | AccShift(data & 0xff00, 1);
			break;
		case 0x02:
			voice->start = (voice->start & ~AccShift(0x00ff0000ull)) | AccShift(((uint64_t)(data & 0x00ff)) << 16);
			voice->start = (voice->start & ~AccShift(0x1f000000ull)) | AccShift(((uint64_t)(data & 0x1f00)) << 16);
			break;
		case 0x03:
			voice->start = (voice->start & ~AccShift(0x000000e0ull)) | AccShift(data & 0x00e0);
			voice->start = (voice->start & ~AccShift(0x0000ff00ull)) | AccShift(data & 0xff00);
			break;
		case 0x04:
			voice->end = (voice->end & ~AccShift(0x00ff0000ull)) | AccShift(((uint64_t)(data & 0x00ff)) << 16);
			voice->end = (voice->end & ~AccShift(0x1f000000ull)) | AccShift(((uint64_t)(data & 0x1f00)) << 16);
			break;
		case 0x05:
			voice->end = (voice->end & ~AccShift(0x000000e0ull)) | AccShift(data & 0x00e0);
			voice->end = (voice->end & ~AccShift(0x0000ff00ull)) | AccShift(data & 0xff00);
			break;
		case 0x06:
			voice->k2 = (voice->k2 & ~0x00f0u) | (data & 0x00f0);
			voice->k2 = (voice->k2 & ~0xff00u) | (data & 0xff00);
			break;
		case 0x07:
			voice->k1 = (voice->k1 & ~0x00f0u) | (data & 0x00f0);
			voice->k1 = (voice->k1 & ~0xff00u) | (data & 0xff00);
			break;
		case 0x08:
			voice->lvol = (voice->lvol & ~0xffu) | ((data & 0xff00) >> 8);
			break;
		case 0x09:
			voice->rvol = (voice->rvol & ~0xffu) | ((data & 0xff00) >> 8);
			break;
		case 0x0a:
			voice->accum = (voice->accum & ~AccShift(0x00ff0000ull)) | AccShift(((uint64_t)(data & 0x00ff)) << 16);
			voice->accum = (voice->accum & ~AccShift(0x1f000000ull)) | AccShift(((uint64_t)(data & 0x1f00)) << 16);
			break;
		case 0x0b:
			voice->accum = (voice->accum & ~AccShift(0x000000ffull)) | AccShift(data & 0x00ff);
			voice->accum = (voice->accum & ~AccShift(0x0000ff00ull)) | AccShift(data & 0xff00);
			break;
		case 0x0d:
			activeVoices_ = (uint8_t)(data & 0x1f);
			UpdateChipRate();
			break;
		case 0x0f:
			page_ = (uint8_t)(data & 0x7f);
			break;
		default:
			break;
		}
	}

	void RegWriteHigh(Voice* voice, unsigned offset, uint16_t data)
	{
		switch (offset) {
		case 0x00:
			voice->control |= 0xf000;
			voice->control = (voice->control & ~0x0fff) | (data & 0x0fff);
			break;
		case 0x01:
			voice->o4n1 = (int16_t)((voice->o4n1 & ~0xffff) | data);
			break;
		case 0x02:
			voice->o3n1 = (int16_t)data;
			break;
		case 0x03:
			voice->o3n2 = (int16_t)data;
			break;
		case 0x04:
			voice->o2n1 = (int16_t)data;
			break;
		case 0x05:
			voice->o2n2 = (int16_t)data;
			break;
		case 0x06:
			voice->o1n1 = (int16_t)data;
			break;
		case 0x0d:
			activeVoices_ = (uint8_t)(data & 0x1f);
			UpdateChipRate();
			break;
		case 0x0f:
			page_ = (uint8_t)(data & 0x7f);
			break;
		default:
			break;
		}
	}

	void RegWriteTest(unsigned offset, uint16_t data)
	{
		switch (offset) {
		case 0x08:
			mode_ |= 0x7f8;
			mode_ = (uint16_t)((mode_ & ~0xf800) | (data & 0xf800));
			mode_ = (uint16_t)((mode_ & ~0x0007) | (data & 0x0007));
			break;
		case 0x0d:
			activeVoices_ = (uint8_t)(data & 0x1f);
			UpdateChipRate();
			break;
		case 0x0f:
			page_ = (uint8_t)(data & 0x7f);
			break;
		default:
			break;
		}
	}

	uint16_t RegReadLow(Voice* voice, unsigned offset)
	{
		switch (offset) {
		case 0x00: return (uint16_t)(voice->control | 0xf000);
		case 0x01: return (uint16_t)AccRes(voice->freqcount, 1);
		case 0x02: return (uint16_t)(AccRes(voice->start) >> 16);
		case 0x03: return (uint16_t)AccRes(voice->start);
		case 0x04: return (uint16_t)(AccRes(voice->end) >> 16);
		case 0x05: return (uint16_t)AccRes(voice->end);
		case 0x06: return (uint16_t)voice->k2;
		case 0x07: return (uint16_t)voice->k1;
		case 0x08: return (uint16_t)(voice->lvol << 8);
		case 0x09: return (uint16_t)(voice->rvol << 8);
		case 0x0a: return (uint16_t)(AccRes(voice->accum) >> 16);
		case 0x0b: return (uint16_t)AccRes(voice->accum);
		case 0x0d: return activeVoices_;
		case 0x0e: {
			const uint16_t r = irqv_;
			irqv_ = 0x80;
			return r;
		}
		case 0x0f: return page_;
		default: return 0;
		}
	}

	uint16_t RegReadHigh(Voice* voice, unsigned offset)
	{
		switch (offset) {
		case 0x00: return (uint16_t)(voice->control | 0xf000);
		case 0x01: return (uint16_t)voice->o4n1;
		case 0x02: return (uint16_t)voice->o3n1;
		case 0x03: return (uint16_t)voice->o3n2;
		case 0x04: return (uint16_t)voice->o2n1;
		case 0x05: return (uint16_t)voice->o2n2;
		case 0x06: return (uint16_t)voice->o1n1;
		case 0x0d: return activeVoices_;
		case 0x0e: {
			const uint16_t r = irqv_;
			irqv_ = 0x80;
			return r;
		}
		case 0x0f: return page_;
		default: return 0;
		}
	}

	uint16_t RegReadTest(unsigned offset)
	{
		switch (offset) {
		case 0x08: return (uint16_t)(mode_ | 0x7f8);
		case 0x0d: return activeVoices_;
		case 0x0e: {
			const uint16_t r = irqv_;
			irqv_ = 0x80;
			return r;
		}
		case 0x0f: return page_;
		default: return 0;
		}
	}

	uint32_t clockHz_;
	int hostRate_;
	int chipRate_;
	const uint8_t* rom_;
	uint64_t romWords_;
	uint8_t page_;
	uint8_t activeVoices_;
	uint16_t mode_;
	uint8_t irqv_;
	uint32_t voiceIndex_;
	int volumeShift_;
	int64_t volumeAccShift_;
	int addrAccShift_;
	uint64_t addrAccMask_;
	int64_t phase_;
	int lastL_, lastR_;
	uint32_t volLut_[256];
	uint32_t voiceBank_[kEs5505Voices];
	Voice voice_[kEs5505Voices];
};

/* Expose ReadReg via a thin cast helper used by hard layer. */
uint16_t CEmuChipEs5505Read(CChip* c, uint32_t addr)
{
	if (!c) return 0xffff;
	return ((CChipEs5505*)c)->ReadReg(addr);
}

CChip* CEmuChipEs5505Create(uint32_t clockHz, int sampleRate)
{
	return new CChipEs5505(clockHz, sampleRate);
}

void CEmuChipEs5505Destroy(CChip* c)
{
	delete c;
}

void CEmuChipEs5505SetVoiceBank(CChip* c, int voice, uint32_t wordBase)
{
	if (!c) return;
	((CChipEs5505*)c)->SetVoiceBank(voice, wordBase);
}

uint32_t CEmuChipEs5505GetVoiceIndex(CChip* c)
{
	if (!c) return 0;
	return ((CChipEs5505*)c)->VoiceIndex();
}
