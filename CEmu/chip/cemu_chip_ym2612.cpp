#include "StdAfx.h"
#include "cemu_chip_ym2612.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include "ymfm.h"
#include "ymfm_opn.h"
#include <string.h>

/* YM2612 wrapper patterned after cemu_chip_opna.cpp; ymfm BSD-3-Clause core. */
static int CEmuYm2612Clamp16(int32_t v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return (int)v;
}

class CChipYm2612 : public CChip, public ymfm::ymfm_interface {
public:
	CChipYm2612(uint32_t clockHz, int sampleRate)
		: ym_(NULL)
		, hostRate_(sampleRate > 0 ? sampleRate : 44100)
		, clockHz_(clockHz ? clockHz : 7670454u)
		, chipRate_(0)
		, chipAcc_(0)
		, curL_(0)
		, curR_(0)
		, irq_(0)
	{
		memset(lastAddr_, 0, sizeof(lastAddr_));
		memset(reg_, 0, sizeof(reg_));
		timerLeft_[0] = timerLeft_[1] = -1;
		ym_ = new ymfm::ym2612(*this);
		chipRate_ = (int)ym_->sample_rate(clockHz_);
		if (chipRate_ <= 0) chipRate_ = (int)(clockHz_ / 72);
		/* Layout/identity come from CEmuFmMonBindFromGe (OPN2 vs OPN+SSG vs
		   Sys32 composite). Do not force OPN layout 0 here. */
		Reset();
	}

	~CChipYm2612() override { delete ym_; }

	void ymfm_update_irq(bool asserted) override { irq_ = asserted ? 1 : 0; }
	void ymfm_set_timer(uint32_t tnum, int32_t duration) override
	{
		if (tnum < 2) timerLeft_[tnum] = duration;
	}

	void Reset() override
	{
		if (ym_) ym_->reset();
		memset(lastAddr_, 0, sizeof(lastAddr_));
		memset(reg_, 0, sizeof(reg_));
		timerLeft_[0] = timerLeft_[1] = -1;
		chipAcc_ = 0;
		curL_ = curR_ = 0;
		irq_ = 0;
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		const uint8_t v = (uint8_t)(data & 0xff);
		const uint32_t off = PortOffset(addr);
		if ((off & 1) == 0) {
			lastAddr_[(off >> 1) & 1] = v;
			ym_->write(off, v);
			return;
		}
		ym_->write(off, v);
		const unsigned shadowAddr = ((off & 2) ? 0x100u : 0u) | lastAddr_[(off >> 1) & 1];
		reg_[shadowAddr & 0x1ff] = v;
		FmMonShadowWriteReg(shadowAddr, v);
	}

	void AdvanceClocks(uint64_t chipCycles) override
	{
		for (int t = 0; t < 2; t++) {
			if (timerLeft_[t] < 0) continue;
			timerLeft_[t] -= (int64_t)chipCycles;
			while (timerLeft_[t] <= 0) {
				const int64_t over = -timerLeft_[t];
				timerLeft_[t] = -1;
				if (m_engine) m_engine->engine_timer_expired((uint32_t)t);
				if (timerLeft_[t] < 0) break;
				timerLeft_[t] -= over;
			}
		}
	}

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		memset(stereo, 0, (size_t)frames * 2 * sizeof(int16_t));
		MixAdd(stereo, frames, 256);
	}

	void MixAdd(int16_t* stereo, int frames, int gain) override
	{
		if (!stereo || frames <= 0) return;
		for (int i = 0; i < frames; i++) {
			int64_t l = 0, r = 0;
			int n = 0;
			chipAcc_ += chipRate_;
			while (chipAcc_ >= hostRate_) {
				chipAcc_ -= hostRate_;
				ymfm::ym2612::output_data o;
				ym_->generate(&o, 1);
				curL_ = o.data[0];
				curR_ = o.data[1];
				l += curL_;
				r += curR_;
				n++;
			}
			if (n) {
				curL_ = (int32_t)(l / n);
				curR_ = (int32_t)(r / n);
			}
			stereo[i * 2] = (int16_t)CEmuYm2612Clamp16((int32_t)stereo[i * 2] + curL_ * gain / 256);
			stereo[i * 2 + 1] = (int16_t)CEmuYm2612Clamp16((int32_t)stereo[i * 2 + 1] + curR_ * gain / 256);
		}
	}

	bool Irq() const override { return irq_ != 0; }
	void AckIrq() override { irq_ = 0; }
	uint8_t ReadStatus() override { return ym_->read_status(); }
	uint8_t ReadData() override { return ym_->read(1); }
	uint8_t ReadStatusHi() override { return ym_->read(2); }
	uint8_t ReadDataHi() override { return ym_->read(3); }
	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap == 0) return 0;
		const unsigned n = cap < sizeof(reg_) ? cap : (unsigned)sizeof(reg_);
		memcpy(buf, reg_, n);
		return n;
	}

private:
	static uint32_t PortOffset(uint32_t addr)
	{
		if (addr == 0x100) return 2;
		if (addr == 0x101) return 3;
		return addr & 3;
	}

	ymfm::ym2612* ym_;
	int hostRate_;
	uint32_t clockHz_;
	int chipRate_;
	int64_t chipAcc_;
	int32_t curL_, curR_;
	int irq_;
	int64_t timerLeft_[2];
	uint8_t lastAddr_[2];
	uint8_t reg_[512];
};

CChip* CEmuChipYm2612Create(uint32_t clockHz, int sampleRate)
{
	return new CChipYm2612(clockHz, sampleRate);
}

void CEmuChipYm2612Destroy(CChip* c)
{
	delete c;
}
