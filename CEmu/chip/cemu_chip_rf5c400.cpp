#include "StdAfx.h"
#include "cemu_chip_rf5c400.h"
#include "cemu_chip.h"
#include <string.h>
#include <stdlib.h>

/* RF5C400 stub: accept host register/PCM ROM setup for a future host.
   MixAdd/Render always produce silence (no soft wave playback). */
class CChipRf5c400 : public CChip {
public:
	CChipRf5c400(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 18432000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
	{
		memset(reg_, 0, sizeof(reg_));
		(void)clockHz_;
		(void)sampleRate_;
	}
	void Reset() override
	{
		memset(reg_, 0, sizeof(reg_));
	}
	void Write(uint32_t addr, uint32_t data) override
	{
		const unsigned a = addr & 0xffu;
		reg_[a] = (uint16_t)(data & 0xffff);
	}
	void AdvanceClocks(uint64_t) override {}
	void Render(int16_t* stereo, int frames) override
	{
		if (stereo && frames > 0)
			memset(stereo, 0, (size_t)frames * 2 * sizeof(int16_t));
	}
	void MixAdd(int16_t* /*stereo*/, int /*frames*/, int /*gain*/) override
	{
		/* Silence — no soft wave from host-poked regs. */
	}
	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		rom_ = data;
		romSize_ = size;
	}
	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

private:
	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint16_t reg_[0x100];
};

CChip* CEmuChipRf5c400Create(uint32_t clockHz, int sampleRate)
{
	return new CChipRf5c400(clockHz, sampleRate);
}
void CEmuChipRf5c400Destroy(CChip* c) { delete c; }
