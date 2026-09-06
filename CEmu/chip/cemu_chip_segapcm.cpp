#include "StdAfx.h"
#include "cemu_chip_segapcm.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>
#include <math.h>

/* Sega PCM — 315-5218 (16ch, banked) and discrete Hang-On/Space Harrier (8ch, no bank).
   Addressing matches MAME: sample @ get_bank(ctrl) + (addr >> 8). */
enum { kSegaPcmMaxChannels = 16 };

static int CEmuSegaClamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipSegaPcm : public CChip {
public:
	CChipSegaPcm(uint32_t clockHz, int sampleRate, unsigned bankShift, unsigned bankMask, int maxChannels)
		: clockHz_(clockHz ? clockHz : 4000000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, bankShift_(bankShift)
		, bankMask_(bankMask)
		, maxCh_(maxChannels >= 8 && maxChannels <= kSegaPcmMaxChannels ? maxChannels : 16)
		, discrete_(maxChannels <= 8 ? 1 : 0)
		, rom_(NULL)
		, romSize_(0)
	{
		/* MAME: CLOCK_DIVIDER = MaxVoices * 8 → output rate = clock / divider.
		   Discrete Hang-On/Space Harrier = 8ch → clock/64; 315-5218 = 16ch → clock/128. */
		rateMul_ = clockHz_ / ((unsigned)maxCh_ * 8u);
		if (rateMul_ == 0) rateMul_ = 1;
		rateDiv_ = (uint32_t)sampleRate_;
		if (rateDiv_ == 0) rateDiv_ = 44100;
		Reset();
	}

	void Reset() override
	{
		memset(ram_, 0xff, sizeof(ram_));
		memset(addr_, 0, sizeof(addr_));
		memset(loop_, 0, sizeof(loop_));
		memset(end_, 0xff, sizeof(end_));
		memset(freq_, 0, sizeof(freq_));
		memset(lvol_, 0, sizeof(lvol_));
		memset(rvol_, 0, sizeof(rvol_));
		memset(ctrl_, 0xff, sizeof(ctrl_));
		memset(frac_, 0, sizeof(frac_));
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		const uint8_t a = (uint8_t)(addr & 0xff);
		const uint8_t v = (uint8_t)(data & 0xff);
		ram_[a] = v;
		FmMonShadowApplySegaPcmMem(a, v);

		if (discrete_) {
			/* MAME segapcm_discrete_device::map — only these are voice regs.
			   Other bytes are scratch RAM; treating 0x02/0x86 as 315-5218
			   destroyed ch0 and caused noise / single-channel playback. */
			const int ch = (a & 0x38) >> 3;
			if (ch < 0 || ch >= maxCh_) return;
			const uint8_t base = (uint8_t)(a & (uint8_t)~0x38);
			switch (base) {
			case 0x42: lvol_[ch] = v; break;
			case 0x43: rvol_[ch] = v; break;
			case 0x44: loop_[ch] = (uint16_t)((loop_[ch] & 0xff00u) | v); break;
			case 0x45: loop_[ch] = (uint16_t)((loop_[ch] & 0x00ffu) | ((uint16_t)v << 8)); break;
			case 0x46: end_[ch] = v; break;
			case 0x47: freq_[ch] = v; break;
			case 0xc4: addr_[ch] = (addr_[ch] & 0xff00ffu) | ((uint32_t)v << 8); break;
			case 0xc5: addr_[ch] = (addr_[ch] & 0x00ffffu) | ((uint32_t)v << 16); break;
			case 0xc6:
				ctrl_[ch] = v;
				if (v & 1)
					addr_[ch] &= 0xffff00u;
				break;
			default:
				break;
			}
			return;
		}

		/* 315-5218: 16ch at 0x00+8*ch / 0x80+8*ch */
		const int ch = (a >> 3) & 0x0f;
		if (ch < 0 || ch >= maxCh_) return;
		const int r = a & 0x87;

		switch (r) {
		case 0x02:
			lvol_[ch] = v;
			break;
		case 0x03:
			rvol_[ch] = v;
			break;
		case 0x04:
			loop_[ch] = (uint16_t)((loop_[ch] & 0xff00u) | v);
			break;
		case 0x05:
			loop_[ch] = (uint16_t)((loop_[ch] & 0x00ffu) | ((uint16_t)v << 8));
			break;
		case 0x06:
			end_[ch] = v;
			break;
		case 0x07:
			freq_[ch] = v;
			break;
		case 0x84:
			addr_[ch] = (addr_[ch] & 0xff00ffu) | ((uint32_t)v << 8);
			break;
		case 0x85:
			addr_[ch] = (addr_[ch] & 0x00ffffu) | ((uint32_t)v << 16);
			break;
		case 0x86:
			ctrl_[ch] = v;
			if (v & 1)
				addr_[ch] &= 0xffff00u;
			break;
		default:
			break;
		}
	}

	void AdvanceClocks(uint64_t chipCycles) override { (void)chipCycles; }

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		memset(stereo, 0, (size_t)frames * 2 * sizeof(int16_t));
		MixAdd(stereo, frames, 200);
	}

	void MixAdd(int16_t* stereo, int frames, int gain) override
	{
		if (!stereo || frames <= 0 || !rom_ || romSize_ == 0) return;
		int g = gain;
		if (discrete_) {
			/* Driver passes 256. Old 110 clamp was for per-voice saturate;
			   accumulate-then-clamp can take AB-class gain without going thin. */
			if (g > 220) g = 200;
		} else if (g > 220) {
			/* 315-5218 (AB/OutRun): keep loud but leave room for 4+ voices. */
			g = 180;
		}
		for (int i = 0; i < frames; i++)
			TickHost(stereo + i * 2, g);
		/* Piano keys: drive from live voice state (MIDI must stay in 21..108). */
		for (int ch = 0; ch < maxCh_; ch++) {
			if (ctrl_[ch] & 1) {
				FmMonShadowPcmNote(ch, 0, 0);
				continue;
			}
			unsigned freq = freq_[ch] ? freq_[ch] : 0x28u;
			double midi = 60.0 + 12.0 * (log((double)freq / 40.0) / log(2.0));
			int m = (int)floor(midi + 0.5);
			if (m < 21) m = 21;
			if (m > 108) m = 108;
			FmMonShadowPcmNote(ch, m, 1);
		}
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		rom_ = data;
		romSize_ = size;
	}

	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap == 0) return 0;
		const unsigned n = cap < sizeof(ram_) ? cap : (unsigned)sizeof(ram_);
		memcpy(buf, ram_, n);
		/* Overlay live voice state — Z80 polls ctrl/addr for channel alloc.
		   Stale ROM/RAM here made AB/SH stop sibling voices when starting/stopping one. */
		for (int ch = 0; ch < maxCh_; ch++) {
			if (discrete_) {
				const int b = 0xc0 + ch * 8;
				if ((unsigned)(b + 6) < n) {
					buf[0x42 + ch * 8] = lvol_[ch];
					buf[0x43 + ch * 8] = rvol_[ch];
					buf[0x44 + ch * 8] = (uint8_t)(loop_[ch] & 0xff);
					buf[0x45 + ch * 8] = (uint8_t)((loop_[ch] >> 8) & 0xff);
					buf[0x46 + ch * 8] = end_[ch];
					buf[0x47 + ch * 8] = freq_[ch];
					buf[b + 4] = (uint8_t)((addr_[ch] >> 8) & 0xff);
					buf[b + 5] = (uint8_t)((addr_[ch] >> 16) & 0xff);
					buf[b + 6] = ctrl_[ch];
				}
			} else {
				const int b = ch * 8;
				if ((unsigned)(b + 0x86) < n) {
					buf[b + 0x02] = lvol_[ch];
					buf[b + 0x03] = rvol_[ch];
					buf[b + 0x04] = (uint8_t)(loop_[ch] & 0xff);
					buf[b + 0x05] = (uint8_t)((loop_[ch] >> 8) & 0xff);
					buf[b + 0x06] = end_[ch];
					buf[b + 0x07] = freq_[ch];
					buf[b + 0x84] = (uint8_t)((addr_[ch] >> 8) & 0xff);
					buf[b + 0x85] = (uint8_t)((addr_[ch] >> 16) & 0xff);
					buf[b + 0x86] = ctrl_[ch];
				}
			}
		}
		return n;
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

private:
	uint32_t BankOf(uint8_t ctrl) const
	{
		if (discrete_) return 0;
		return (uint32_t)(ctrl & bankMask_) << bankShift_;
	}

	void TickHost(int16_t* lr, int gain)
	{
		/* Sum all voices in 32-bit first. Saturating after each channel
		   crushed multi-PCM (SH PCM1+2+3) into thin clipped mush. */
		int32_t accL = (int32_t)lr[0];
		int32_t accR = (int32_t)lr[1];

		for (int ch = 0; ch < maxCh_; ch++) {
			if (ctrl_[ch] & 1)
				continue;

			if ((addr_[ch] >> 16) == (uint32_t)((end_[ch] + 1) & 0xff)) {
				if (ctrl_[ch] & 2) {
					ctrl_[ch] |= 1;
					/* Discrete ctrl lives at 0xC6+8*ch — NOT 0x46 (end). */
					const int ramOff = discrete_ ? (0xc0 + ch * 8 + 6) : (ch * 8 + 0x86);
					if (ramOff >= 0 && ramOff < 256)
						ram_[ramOff] = ctrl_[ch];
					continue;
				}
				addr_[ch] = ((uint32_t)loop_[ch] << 8);
				frac_[ch] = 0;
			}

			const uint32_t bank = BankOf(ctrl_[ch]);
			uint32_t romAdr = bank + (addr_[ch] >> 8);
			int sample = 0;
			if (romSize_ && romAdr < romSize_) {
				sample = (int)rom_[romAdr] - 0x80;
			} else if (!discrete_ && romSize_ > 0) {
				/* 315-5218: brief bank overrun → wrap in ROM (hard 0 = dropouts). */
				romAdr %= romSize_;
				sample = (int)rom_[romAdr] - 0x80;
			}

			accL += sample * (lvol_[ch] & 0x7f) * gain / 128;
			accR += sample * (rvol_[ch] & 0x7f) * gain / 128;

			frac_[ch] += (uint32_t)freq_[ch] * rateMul_;
			const uint32_t add = frac_[ch] / rateDiv_;
			frac_[ch] %= rateDiv_;
			addr_[ch] = (addr_[ch] + add) & 0xffffffu;
			/* Keep RAM mirror live — Z80 / FM mon may read addr/ctrl. */
			if (discrete_) {
				ram_[0xc0 + ch * 8 + 4] = (uint8_t)((addr_[ch] >> 8) & 0xff);
				ram_[0xc0 + ch * 8 + 5] = (uint8_t)((addr_[ch] >> 16) & 0xff);
			} else {
				ram_[ch * 8 + 0x84] = (uint8_t)((addr_[ch] >> 8) & 0xff);
				ram_[ch * 8 + 0x85] = (uint8_t)((addr_[ch] >> 16) & 0xff);
			}
		}

		lr[0] = (int16_t)CEmuSegaClamp16(accL);
		lr[1] = (int16_t)CEmuSegaClamp16(accR);
	}

	uint32_t clockHz_;
	int sampleRate_;
	unsigned bankShift_;
	unsigned bankMask_;
	int maxCh_;
	int discrete_;
	uint32_t rateMul_;
	uint32_t rateDiv_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint8_t ram_[256];
	uint32_t addr_[kSegaPcmMaxChannels];
	uint32_t frac_[kSegaPcmMaxChannels];
	uint16_t loop_[kSegaPcmMaxChannels];
	uint8_t end_[kSegaPcmMaxChannels];
	uint8_t freq_[kSegaPcmMaxChannels];
	uint8_t lvol_[kSegaPcmMaxChannels];
	uint8_t rvol_[kSegaPcmMaxChannels];
	uint8_t ctrl_[kSegaPcmMaxChannels];
};

CChip* CEmuChipSegaPcmCreate(uint32_t clockHz, int sampleRate, unsigned bankShift, unsigned bankMask)
{
	return new CChipSegaPcm(clockHz, sampleRate, bankShift, bankMask, 16);
}

CChip* CEmuChipSegaPcmCreateDiscrete(uint32_t clockHz, int sampleRate)
{
	/* Hang-On / Space Harrier: 8ch, no bank switch */
	return new CChipSegaPcm(clockHz, sampleRate, 0, 0, 8);
}

void CEmuChipSegaPcmDestroy(CChip* c)
{
	delete c;
}
