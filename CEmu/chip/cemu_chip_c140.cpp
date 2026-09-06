#include "StdAfx.h"
#include "cemu_chip_c140.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>

/* Namco C140, 24-voice PCM. Ported from MAME / FBNeo c140.cpp
   (license:BSD-3-Clause, copyright-holders:R. Belmont), stripped of the
   BurnLib stream/resampler so it renders straight to the host rate like the
   other CEmu chips (see cemu_chip_c352.cpp / cemu_chip_k053260.cpp).

   16 bytes of register space per voice:
     +0 volume_right  +1 volume_left  +2 freq_msb  +3 freq_lsb
     +4 bank          +5 mode(key on = bit7)
     +6 start_msb     +7 start_lsb    +8 end_msb   +9 end_lsb
     +10 loop_msb     +11 loop_lsb    +12..15 reserved */

enum { kC140Voices = 24, kC140Regs = 0x200 };

/* Banking type (only System 2 is wired here; System 21 kept for reference). */
enum { kC140TypeSystem2 = 0, kC140TypeSystem21 = 1, kC140TypeC219 = 2 };

static int CEmuC140Clamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipC140 : public CChip {
public:
	CChipC140(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 8192000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, bankingType_(kC140TypeSystem2)
		, rom_(NULL)
		, romSize_(0)
		, baseRate_(0)
		, timerLeft_(0)
		, timerPeriod_(0)
		, irq_(false)
		, timerArmed_(0)
		, writeCount_(0)
		, voiceWriteCount_(0)
		, modeWriteCount_(0)
		, mode80WriteCount_(0)
		, mode40WriteCount_(0)
		, modeNzWriteCount_(0)
		, lastModeWrite_(0)
		, keyedPeak_(0)
	{
		/* MAME passes C140_SOUND_CLOCK (~21333) directly as baserate.
		   Older ports used 8.192 MHz / 384 — accept either. */
		if (clockHz_ >= 100000u)
			baseRate_ = clockHz_ / 384u;
		else
			baseRate_ = clockHz_;
		if (baseRate_ < 1u) baseRate_ = 21333u;
		pbase_ = (double)baseRate_ * 2.0 / (double)sampleRate_;
		BuildPcmTable();
		Reset();
	}

	void Reset() override
	{
		memset(reg_, 0, sizeof(reg_));
		memset(v_, 0, sizeof(v_));
		timerLeft_ = 0;
		timerPeriod_ = 0;
		irq_ = false;
		timerArmed_ = 0;
		writeCount_ = 0;
		voiceWriteCount_ = 0;
		modeWriteCount_ = 0;
		mode80WriteCount_ = 0;
		mode40WriteCount_ = 0;
		modeNzWriteCount_ = 0;
		lastModeWrite_ = 0;
		keyedPeak_ = 0;
	}

	void SetBankingType(int type) { bankingType_ = type; }

	void Write(uint32_t addr, uint32_t data) override
	{
		unsigned o = addr & 0x1ff;
		/* C219 mirrors odd bank regs (fixes bkrtmaq). */
		if (bankingType_ == kC140TypeC219 && o >= 0x1f8u && (o & 1u))
			o -= 8u;
		const uint8_t d = (uint8_t)(data & 0xff);
		writeCount_++;
		const unsigned voiceLimit = (bankingType_ == kC140TypeC219) ? 0x100u : 0x180u;
		if (o < voiceLimit) voiceWriteCount_++;
		if ((o & 0xfu) == 0x5u && o < voiceLimit) {
			modeWriteCount_++;
			lastModeWrite_ = d;
			if (d & 0x80u) mode80WriteCount_++;
			if (d & 0x40u) mode40WriteCount_++;
			if (d) modeNzWriteCount_++;
		}
		reg_[o] = d;
		if (o < voiceLimit) {
			const int ch = (int)(o >> 4);
			if ((o & 0xf) == 0x5) {
				/* MAME: key when bit7 set, or bit6 while already keyed (re-key). */
				if ((d & 0x80) || ((d & 0x40) && v_[ch].key)) {
					KeyOn(ch, d);
					if (keyedPeak_ < KeyedCount())
						keyedPeak_ = KeyedCount();
				} else
					KeyOff(ch);
			}
			return;
		}
		/* MAME INT1: 1f8=reload, 1fa=ack+rearm, 1fe bit0=enable.
		   Sys2 FIRQ handler STA $51FA each tick — without this the sequencer
		   only sees our soft vblank pulse and never keys YM/C140 on assault. */
		switch (o) {
		case 0x1f8:
			break;
		case 0x1fa:
			irq_ = false;
			if (reg_[0x1fe] & 1u)
				ArmTimer();
			break;
		case 0x1fe:
			if (d & 1u) {
				if (!timerArmed_) {
					irq_ = true; /* first enable → immediate INT1 */
					timerArmed_ = 1;
					ArmTimer();
				}
			} else {
				irq_ = false;
				timerArmed_ = 0;
				timerLeft_ = 0;
				timerPeriod_ = 0;
			}
			break;
		default:
			break;
		}
	}

	/* MAME c140_r: voice+5 returns in-progress in bit6 (Final Lap / Suzuka
	   poll this); raw bit7 alone leaves voices stuck "busy" forever. */
	uint8_t ReadReg(unsigned offset) const
	{
		unsigned o = offset & 0x1ffu;
		uint8_t data = reg_[o];
		if ((o & 0xfu) == 0x5u && o < 0x180u) {
			const int ch = (int)(o >> 4);
			data = (uint8_t)((v_[ch].key ? 0x40u : 0x00u) | (reg_[o] & 0x3fu));
		} else if (o == 0x1f8u) {
			data = (uint8_t)(reg_[0x1f8] + 1u);
		}
		return data;
	}

	int KeyedCount() const
	{
		int n = 0;
		for (int i = 0; i < kC140Voices; i++)
			if (v_[i].key) n++;
		return n;
	}

	unsigned WriteCount() const { return writeCount_; }
	unsigned VoiceWriteCount() const { return voiceWriteCount_; }
	unsigned ModeWriteCount() const { return modeWriteCount_; }
	unsigned Mode80WriteCount() const { return mode80WriteCount_; }
	unsigned Mode40WriteCount() const { return mode40WriteCount_; }
	unsigned ModeNzWriteCount() const { return modeNzWriteCount_; }
	uint8_t LastModeWrite() const { return lastModeWrite_; }
	int KeyedPeak() const { return keyedPeak_; }

	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

	void AdvanceClocks(uint64_t chipCycles) override
	{
		if (!timerArmed_ || timerPeriod_ == 0 || chipCycles == 0)
			return;
		/* Map CPU clocks → C140 base ticks. Fed M6809 clocks (~2.048 MHz). */
		uint64_t ticks = chipCycles * (uint64_t)baseRate_ / 2048000ull;
		if (ticks == 0) ticks = 1;
		while (ticks > 0 && timerPeriod_ > 0) {
			if (timerLeft_ > ticks) {
				timerLeft_ -= ticks;
				break;
			}
			ticks -= timerLeft_;
			timerLeft_ = timerPeriod_;
			irq_ = true;
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
		if (!stereo || frames <= 0 || !rom_ || romSize_ < 2) return;
		for (int f = 0; f < frames; f++) {
			int lsum = 0, rsum = 0;
			const int nVoices = (bankingType_ == kC140TypeC219) ? 16 : kC140Voices;
			for (int i = 0; i < nVoices; i++) {
				Voice& vc = v_[i];
				if (!vc.key) continue;
				const uint8_t* vreg = reg_ + i * 16;
				const int frequency = vreg[2] * 256 + vreg[3];
				if (frequency == 0) continue;
				const int delta = (int)((double)frequency * pbase_);
				const int lvol = (vreg[1] * 32) / kC140Voices;
				const int rvol = (vreg[0] * 32) / kC140Voices;
				const int st = vc.sampleStart;
				const int ed = vc.sampleEnd;
				const int sz = ed - st;
				if (sz <= 0) { KeyOff(i); continue; }
				const uint32_t base = FindSample(st, vc.bank, i);

				int offset = vc.ptoffset;
				int pos = vc.pos;
				int lastdt = vc.lastdt;
				int prevdt = vc.prevdt;
				int dltdt = vc.dltdt;

				offset += delta;
				int cnt = (offset >> 16) & 0x7fff;
				offset &= 0xffff;
				pos += cnt;

				if (pos >= sz) {
					if (vc.mode & 0x10) {
						pos = vc.sampleLoop - st;
						if (pos < 0) pos = 0;
					} else {
						KeyOff(i);
						continue;
					}
				}

				int dt;
				if ((vc.mode & 8) && bankingType_ != kC140TypeSystem21) {
					/* compressed 12-bit PCM */
					if (cnt) {
						const uint32_t adr = base + (uint32_t)pos;
						const int8_t raw = (adr < romSize_) ? (int8_t)rom_[adr] : 0;
						int sdt = raw >> 3;
						if (sdt < 0) sdt = (sdt << (raw & 7)) - pcmtbl_[raw & 7];
						else sdt = (sdt << (raw & 7)) + pcmtbl_[raw & 7];
						prevdt = lastdt;
						lastdt = sdt;
						dltdt = lastdt - prevdt;
					}
					dt = ((dltdt * offset) >> 16) + prevdt;
					lsum += (dt * lvol) >> (5 + 5);
					rsum += (dt * rvol) >> (5 + 5);
				} else {
					/* linear 8-bit signed PCM */
					if (cnt) {
						const uint32_t adr = base + (uint32_t)pos;
						prevdt = lastdt;
						lastdt = (adr < romSize_) ? (int8_t)rom_[adr] : 0;
						dltdt = lastdt - prevdt;
					}
					dt = ((dltdt * offset) >> 16) + prevdt;
					lsum += (dt * lvol) >> 5;
					rsum += (dt * rvol) >> 5;
				}

				vc.ptoffset = offset;
				vc.pos = pos;
				vc.lastdt = lastdt;
				vc.prevdt = prevdt;
				vc.dltdt = dltdt;
			}
			/* MAME renders lmix*8 into the output stream. */
			lsum *= 8;
			rsum *= 8;
			stereo[f * 2] = (int16_t)CEmuC140Clamp16((int)stereo[f * 2] + lsum * gain / 256);
			stereo[f * 2 + 1] = (int16_t)CEmuC140Clamp16((int)stereo[f * 2 + 1] + rsum * gain / 256);
		}
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override { rom_ = data; romSize_ = size; }

	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap == 0) return 0;
		const unsigned n = cap < sizeof(reg_) ? cap : (unsigned)sizeof(reg_);
		memcpy(buf, reg_, n);
		return n;
	}

	bool Irq() const override { return irq_; }
	void AckIrq() override { irq_ = false; }

private:
	struct Voice {
		uint8_t key;
		int ptoffset;
		int pos;
		int lastdt, prevdt, dltdt;
		int bank;
		int mode;
		int sampleStart, sampleEnd, sampleLoop;
	};

	void BuildPcmTable()
	{
		memset(pcmtbl_, 0, sizeof(pcmtbl_));
		int segbase = 0;
		for (int i = 0; i < 8; i++) {
			pcmtbl_[i] = (int16_t)segbase;
			segbase += 16 << i;
		}
	}

	/* System 2 / System 21 / C219 sample-address banking (MAME find_sample). */
	uint32_t FindSample(int adrs, int bank, int voice) const
	{
		long a = ((long)bank << 16) + adrs;
		if (bankingType_ == kC140TypeC219) {
			static const int asic219banks[4] = { 0x1f7, 0x1f1, 0x1f3, 0x1f5 };
			const int b = reg_[asic219banks[voice / 4]] & 0x3;
			return (uint32_t)((b * 0x20000) + a);
		}
		long newadr;
		if (bankingType_ == kC140TypeSystem21)
			newadr = ((a & 0x300000) >> 1) + (a & 0x7ffff);
		else
			newadr = ((a & 0x200000) >> 2) | (a & 0x7ffff);
		return (uint32_t)newadr;
	}

	void KeyOn(int ch, uint8_t modeByte)
	{
		const int maxCh = (bankingType_ == kC140TypeC219) ? 16 : kC140Voices;
		if (ch < 0 || ch >= maxCh) return;
		Voice& vc = v_[ch];
		const uint8_t* vreg = reg_ + (ch * 16);
		vc.key = 1;
		vc.ptoffset = 0;
		vc.pos = 0;
		vc.lastdt = 0;
		vc.prevdt = 0;
		vc.dltdt = 0;
		vc.bank = vreg[4];
		vc.mode = modeByte;
		vc.sampleStart = vreg[6] * 256 + vreg[7];
		vc.sampleEnd = vreg[8] * 256 + vreg[9];
		vc.sampleLoop = vreg[10] * 256 + vreg[11];
		/* C219 addresses are in words. */
		if (bankingType_ == kC140TypeC219) {
			vc.sampleStart <<= 1;
			vc.sampleEnd <<= 1;
			vc.sampleLoop <<= 1;
		}
		if (ch < 16) FmMonShadowPcmNote(ch, 48 + ch, 1);
	}

	void KeyOff(int ch)
	{
		if (ch < 0 || ch >= kC140Voices) return;
		v_[ch].key = 0;
		if (ch < 16) FmMonShadowPcmNote(ch, 48 + ch, 0);
	}

	void ArmTimer()
	{
		/* MAME: interval = (reg[1f8]+1)*2 ticks at baserate. */
		const unsigned reload = (unsigned)reg_[0x1f8] + 1u;
		timerPeriod_ = (uint64_t)reload * 2ull;
		timerLeft_ = timerPeriod_;
		timerArmed_ = 1;
	}

	uint32_t clockHz_;
	int sampleRate_;
	int bankingType_;
	double pbase_;
	uint32_t baseRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint8_t reg_[kC140Regs];
	int16_t pcmtbl_[8];
	Voice v_[kC140Voices];
	uint64_t timerLeft_;
	uint64_t timerPeriod_;
	bool irq_;
	int timerArmed_;
	unsigned writeCount_;
	unsigned voiceWriteCount_;
	unsigned modeWriteCount_;
	unsigned mode80WriteCount_;
	unsigned mode40WriteCount_;
	unsigned modeNzWriteCount_;
	uint8_t lastModeWrite_;
	int keyedPeak_;
};

CChip* CEmuChipC140Create(uint32_t clockHz, int sampleRate)
{
	return new CChipC140(clockHz, sampleRate);
}

void CEmuChipC140Destroy(CChip* c)
{
	delete c;
}

void CEmuChipC140SetType(CChip* c, int type)
{
	if (!c) return;
	static_cast<CChipC140*>(c)->SetBankingType(type);
}

uint8_t CEmuChipC140Read(CChip* c, unsigned offset)
{
	if (!c) return 0;
	return static_cast<CChipC140*>(c)->ReadReg(offset);
}

int CEmuChipC140KeyedCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipC140*>(c)->KeyedCount();
}

unsigned CEmuChipC140WriteCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipC140*>(c)->WriteCount();
}

unsigned CEmuChipC140VoiceWriteCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipC140*>(c)->VoiceWriteCount();
}

unsigned CEmuChipC140ModeWriteCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipC140*>(c)->ModeWriteCount();
}

unsigned CEmuChipC140Mode80WriteCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipC140*>(c)->Mode80WriteCount();
}

unsigned CEmuChipC140Mode40WriteCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipC140*>(c)->Mode40WriteCount();
}

unsigned CEmuChipC140ModeNzWriteCount(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipC140*>(c)->ModeNzWriteCount();
}

uint8_t CEmuChipC140LastModeWrite(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipC140*>(c)->LastModeWrite();
}

int CEmuChipC140KeyedPeak(const CChip* c)
{
	if (!c) return 0;
	return static_cast<const CChipC140*>(c)->KeyedPeak();
}
