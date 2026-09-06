#include "StdAfx.h"
#include "cemu_chip_irem_dac.h"
#include "cemu_chip.h"
#include <string.h>

/* MAME irem/m72_a.cpp drives DAC_8BIT_R2R from the sound CPU: sample_w()
   writes one unsigned byte and bumps the sample pointer. 0x80 is silence. */
static int CEmuIremDacClamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipIremDac : public CChip {
public:
	explicit CChipIremDac(int sampleRate)
		: sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, level_(0x80)
		, writes_(0)
	{
	}

	void Reset() override
	{
		level_ = 0x80;
		writes_ = 0;
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		(void)addr;
		level_ = (uint8_t)(data & 0xff);
		writes_++;
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
		if (!writes_) return; /* never driven — stay silent, no DC step */
		const int s = ((int)level_ - 0x80) * 96 * gain / 256;
		for (int i = 0; i < frames; i++) {
			stereo[i * 2] = (int16_t)CEmuIremDacClamp16((int)stereo[i * 2] + s);
			stereo[i * 2 + 1] = (int16_t)CEmuIremDacClamp16((int)stereo[i * 2 + 1] + s);
		}
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return level_; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }
	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap == 0) return 0;
		buf[0] = level_;
		return 1;
	}

	unsigned Writes() const { return writes_; }

private:
	int sampleRate_;
	uint8_t level_;
	unsigned writes_;
};

CChip* CEmuChipIremDacCreate(int sampleRate)
{
	return new CChipIremDac(sampleRate);
}

void CEmuChipIremDacDestroy(CChip* c)
{
	delete c;
}
