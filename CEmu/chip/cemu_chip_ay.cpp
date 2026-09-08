#include "StdAfx.h"
#include "cemu_chip_ay.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include "types.h"
#include "psg.h"
#include <string.h>

class CChipAy : public CChip {
public:
	CChipAy(uint32_t clockHz, int sampleRate)
		: sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, clockHz_(clockHz ? clockHz : 2000000u)
		, addrLatch_(0)
		, writeCount_(0)
	{
		/* hoot ssAY8910 Initialize(clock) then SetClock(clock/2). */
		const int psgClk = (int)(clockHz_ / 2u);
		psg_.SetClock(psgClk > 0 ? psgClk : 1000000, sampleRate_);
		psg_.SetVolume(0);
		psg_.Reset();
		memset(regs_, 0, sizeof(regs_));
		memset(regWriteCount_, 0, sizeof(regWriteCount_));
	}

	void SetPortA(uint8_t v) { portA_ = v; }

	void Reset() override
	{
		addrLatch_ = 0;
		writeCount_ = 0;
		portA_ = 0xff;
		memset(regs_, 0, sizeof(regs_));
		memset(regWriteCount_, 0, sizeof(regWriteCount_));
		psg_.Reset();
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		if ((addr & 1) == 0) {
			addrLatch_ = (uint8_t)(data & 0x0f);
			return;
		}
		psg_.SetReg(addrLatch_, (uint8_t)(data & 0xff));
		regs_[addrLatch_ & 15] = (uint8_t)(data & 0xff);
		regWriteCount_[addrLatch_ & 15]++;
		writeCount_++;
		FmMonShadowWriteAyReg(addrLatch_, data);
	}

	void AdvanceClocks(uint64_t chipCycles) override
	{
		(void)chipCycles; /* sample-driven in Render */
	}

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		while (frames > 0) {
			const int n = frames > 64 ? 64 : frames;
			PSG::Sample tmp[128];
			memset(tmp, 0, (size_t)n * 2 * sizeof(PSG::Sample));
			psg_.Mix(tmp, n);
			for (int i = 0; i < n * 2; i++) {
				int32_t v = (int32_t)tmp[i];
				if (v > 32767) v = 32767;
				if (v < -32768) v = -32768;
				stereo[i] = (int16_t)v;
			}
			stereo += n * 2;
			frames -= n;
		}
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override
	{
		if (addrLatch_ == 0x0e)
			return portA_;
		return (uint8_t)psg_.GetReg(addrLatch_);
	}
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return ReadData(); }

	unsigned WriteCount() const { return writeCount_; }
	uint8_t AddrLatch() const { return addrLatch_; }
	int PeekRegs(unsigned char* out16) const
	{
		if (!out16) return 0;
		memcpy(out16, regs_, 16);
		return 1;
	}
	void PeekRegWrites(unsigned out16[16]) const
	{
		if (out16) memcpy(out16, regWriteCount_, sizeof(regWriteCount_));
	}

private:
	PSG psg_;
	int sampleRate_;
	uint32_t clockHz_;
	uint8_t addrLatch_;
	unsigned writeCount_;
	uint8_t regs_[16];
	unsigned regWriteCount_[16];
	uint8_t portA_;
};

CChip* CEmuChipAyCreate(uint32_t clockHz, int sampleRate)
{
	return new CChipAy(clockHz, sampleRate);
}

void CEmuChipAySetPortA(CChip* c, uint8_t v)
{
	if (!c) return;
	static_cast<CChipAy*>(c)->SetPortA(v);
}

void CEmuChipAyDestroy(CChip* c)
{
	delete c;
}

unsigned CEmuChipAyWriteCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipAy*>(c)->WriteCount();
}

int CEmuChipAyPeekRegs(const CChip* c, unsigned char* out16)
{
	if (!c) return 0;
	return static_cast<const CChipAy*>(c)->PeekRegs(out16);
}

void CEmuChipAyPeekRegWrites(const CChip* c, unsigned out16[16])
{
	if (!c) {
		if (out16) memset(out16, 0, 16 * sizeof(unsigned));
		return;
	}
	static_cast<const CChipAy*>(c)->PeekRegWrites(out16);
}
