#include "StdAfx.h"
#include "cemu_chip_multipcm.h"
#include "cemu_chip.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

extern "C" {
#define VGM_LITTLE_ENDIAN
#include "../vendor/multipcm/mamedef.h"
#include "../vendor/multipcm/multipcm.h"
}

class CChipMultiPcm : public CChip {
public:
	CChipMultiPcm(uint32_t clockHz, int sampleRate, int chipId)
		: clockHz_(clockHz ? clockHz : 10000000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, chipId_((UINT8)(chipId & 1))
		, nativeRate_(0)
		, frac_(0)
		, lastL_(0)
		, lastR_(0)
		, romSize_(0)
	{
		nativeRate_ = device_start_multipcm(chipId_, (int)clockHz_);
		if (nativeRate_ <= 0) nativeRate_ = 44100;
		device_reset_multipcm(chipId_);
	}
	~CChipMultiPcm() override
	{
		device_stop_multipcm(chipId_);
	}
	void Reset() override
	{
		device_reset_multipcm(chipId_);
		frac_ = 0;
		lastL_ = lastR_ = 0;
	}
	void Write(uint32_t addr, uint32_t data) override
	{
		multipcm_w(chipId_, (offs_t)(addr & 3u), (UINT8)(data & 0xff));
	}
	void AdvanceClocks(uint64_t) override {}
	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		for (int i = 0; i < frames; i++) {
			frac_ += (uint32_t)nativeRate_;
			while (frac_ >= (uint32_t)sampleRate_) {
				frac_ -= (uint32_t)sampleRate_;
				stream_sample_t L = 0, R = 0;
				stream_sample_t* outs[2] = { &L, &R };
				MultiPCM_update(chipId_, outs, 1);
				lastL_ = (int16_t)Clamp(L);
				lastR_ = (int16_t)Clamp(R);
			}
			stereo[i * 2] = lastL_;
			stereo[i * 2 + 1] = lastR_;
		}
		(void)clockHz_;
	}
	void MixAdd(int16_t* stereo, int frames, int gain) override
	{
		if (!stereo || frames <= 0) return;
		enum { kChunk = 64 };
		int16_t tmp[kChunk * 2];
		for (int done = 0; done < frames; ) {
			int n = frames - done;
			if (n > (int)kChunk) n = (int)kChunk;
			Render(tmp, n);
			for (int i = 0; i < n * 2; i++) {
				int s = (int)stereo[done * 2 + i] + ((int)tmp[i] * gain) / 256;
				if (s > 32767) s = 32767;
				if (s < -32768) s = -32768;
				stereo[done * 2 + i] = (int16_t)s;
			}
			done += n;
		}
	}
	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		romSize_ = size;
		if (data && size)
			multipcm_write_rom(chipId_, size, 0, size, data);
	}
	void SetBank(unsigned bankMb)
	{
		const UINT32 off = (UINT32)(bankMb & 3u) * 0x100000u;
		multipcm_set_bank(chipId_, off, off);
	}
	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return multipcm_r(chipId_, 0); }
	uint8_t ReadData() override { return multipcm_r(chipId_, 0); }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }
	UINT8 Id() const { return chipId_; }

private:
	static int Clamp(int v)
	{
		if (v > 32767) return 32767;
		if (v < -32768) return -32768;
		return v;
	}
	uint32_t clockHz_;
	int sampleRate_;
	UINT8 chipId_;
	int nativeRate_;
	uint32_t frac_;
	int16_t lastL_, lastR_;
	unsigned romSize_;
};

CChip* CEmuChipMultiPcmCreate(uint32_t clockHz, int sampleRate, int chipId)
{
	return new CChipMultiPcm(clockHz, sampleRate, chipId);
}
void CEmuChipMultiPcmDestroy(CChip* c) { delete c; }
void CEmuChipMultiPcmSetBank(CChip* c, unsigned bankMb)
{
	if (!c) return;
	static_cast<CChipMultiPcm*>(c)->SetBank(bankMb);
}
