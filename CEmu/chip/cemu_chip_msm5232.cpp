#include "StdAfx.h"
#include "cemu_chip_msm5232.h"
#include "cemu_chip.h"
#include <string.h>

/* Compact MSM5232: pitch ROM + square TG + simple AR/DR envelopes.
   Enough for flstory/nycaptor melody without full MAME capacitor model. */

#define MSM_STEP_SH 16
#define MSM_VMIN 0
#define MSM_VMAX 32768

#define MSM_ROM(counter, bindiv) ((uint16_t)((counter) | ((bindiv) << 9)))

static const uint16_t kMsmPitchRom[88] = {
	MSM_ROM(506, 7),
	MSM_ROM(478, 7), MSM_ROM(451, 7), MSM_ROM(426, 7), MSM_ROM(402, 7),
	MSM_ROM(379, 7), MSM_ROM(358, 7), MSM_ROM(338, 7), MSM_ROM(319, 7),
	MSM_ROM(301, 7), MSM_ROM(284, 7), MSM_ROM(268, 7), MSM_ROM(253, 7),
	MSM_ROM(478, 6), MSM_ROM(451, 6), MSM_ROM(426, 6), MSM_ROM(402, 6),
	MSM_ROM(379, 6), MSM_ROM(358, 6), MSM_ROM(338, 6), MSM_ROM(319, 6),
	MSM_ROM(301, 6), MSM_ROM(284, 6), MSM_ROM(268, 6), MSM_ROM(253, 6),
	MSM_ROM(478, 5), MSM_ROM(451, 5), MSM_ROM(426, 5), MSM_ROM(402, 5),
	MSM_ROM(379, 5), MSM_ROM(358, 5), MSM_ROM(338, 5), MSM_ROM(319, 5),
	MSM_ROM(301, 5), MSM_ROM(284, 5), MSM_ROM(268, 5), MSM_ROM(253, 5),
	MSM_ROM(478, 4), MSM_ROM(451, 4), MSM_ROM(426, 4), MSM_ROM(402, 4),
	MSM_ROM(379, 4), MSM_ROM(358, 4), MSM_ROM(338, 4), MSM_ROM(319, 4),
	MSM_ROM(301, 4), MSM_ROM(284, 4), MSM_ROM(268, 4), MSM_ROM(253, 4),
	MSM_ROM(478, 3), MSM_ROM(451, 3), MSM_ROM(426, 3), MSM_ROM(402, 3),
	MSM_ROM(379, 3), MSM_ROM(358, 3), MSM_ROM(338, 3), MSM_ROM(319, 3),
	MSM_ROM(301, 3), MSM_ROM(284, 3), MSM_ROM(268, 3), MSM_ROM(253, 3),
	MSM_ROM(478, 2), MSM_ROM(451, 2), MSM_ROM(426, 2), MSM_ROM(402, 2),
	MSM_ROM(379, 2), MSM_ROM(358, 2), MSM_ROM(338, 2), MSM_ROM(319, 2),
	MSM_ROM(301, 2), MSM_ROM(284, 2), MSM_ROM(268, 2), MSM_ROM(253, 2),
	MSM_ROM(478, 1), MSM_ROM(451, 1), MSM_ROM(426, 1), MSM_ROM(402, 1),
	MSM_ROM(379, 1), MSM_ROM(358, 1), MSM_ROM(338, 1), MSM_ROM(319, 1),
	MSM_ROM(301, 1), MSM_ROM(284, 1), MSM_ROM(268, 1), MSM_ROM(253, 1),
	MSM_ROM(253, 1), MSM_ROM(253, 1),
	MSM_ROM(13, 7)
};

struct MsmVoice {
	int mode;
	int pitch;
	int egSect;
	int egArm;
	int eg;
	int egVol;
	int counter;
	int arRate;
	int drRate;
	int rrRate;
	uint32_t tgPeriod;
	int32_t tgCount;
	uint32_t tgCnt;
	uint32_t tgOut16;
	uint32_t tgOut8;
	uint32_t tgOut4;
	uint32_t tgOut2;
	int gf;
};

class CChipMsm5232 : public CChip {
public:
	CChipMsm5232(uint32_t clockHz, int sampleRate)
		: sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, clockHz_(clockHz ? clockHz : 2000000u)
		, control1_(0)
		, control2_(0)
		, noiseCnt_(0)
		, noiseRng_(1)
		, noiseClocks_(0)
		, updateStep_(0)
		, noiseStep_(0)
	{
		for (int i = 0; i < 4; i++)
			enOut16_[i] = enOut8_[i] = enOut4_[i] = enOut2_[i] = 0;
		memset(voi_, 0, sizeof(voi_));
		InitTables();
		Reset();
	}

	void Reset() override
	{
		control1_ = control2_ = 0;
		noiseCnt_ = 0;
		noiseRng_ = 1;
		noiseClocks_ = 0;
		for (int i = 0; i < 8; i++) {
			memset(&voi_[i], 0, sizeof(voi_[i]));
			voi_[i].pitch = -1;
			voi_[i].egSect = -1;
			voi_[i].arRate = arTbl_[0];
			voi_[i].drRate = drTbl_[0];
			voi_[i].rrRate = drTbl_[0];
		}
		for (int g = 0; g < 2; g++)
			enOut16_[g] = enOut8_[g] = enOut4_[g] = enOut2_[g] = 0;
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		const unsigned off = (unsigned)(addr & 0x0fu);
		if (off > 0x0du) return;
		const uint8_t v = (uint8_t)(data & 0xffu);
		if (off < 0x08u) {
			WritePitch((int)off, v);
			return;
		}
		switch (off) {
		case 0x08:
			for (int i = 0; i < 4; i++)
				voi_[i].arRate = arTbl_[v & 7];
			break;
		case 0x09:
			for (int i = 0; i < 4; i++)
				voi_[i + 4].arRate = arTbl_[v & 7];
			break;
		case 0x0a:
			for (int i = 0; i < 4; i++)
				voi_[i].drRate = drTbl_[v & 0x0f];
			break;
		case 0x0b:
			for (int i = 0; i < 4; i++)
				voi_[i + 4].drRate = drTbl_[v & 0x0f];
			break;
		case 0x0c:
			control1_ = v;
			for (int i = 0; i < 4; i++) {
				if ((v & 0x10) && voi_[i].egSect == 1)
					voi_[i].egSect = 0;
				voi_[i].egArm = (v & 0x10) ? 1 : 0;
			}
			enOut16_[0] = (v & 1) ? ~0 : 0;
			enOut8_[0] = (v & 2) ? ~0 : 0;
			enOut4_[0] = (v & 4) ? ~0 : 0;
			enOut2_[0] = (v & 8) ? ~0 : 0;
			break;
		case 0x0d:
			control2_ = v;
			for (int i = 0; i < 4; i++) {
				if ((v & 0x10) && voi_[i + 4].egSect == 1)
					voi_[i + 4].egSect = 0;
				voi_[i + 4].egArm = (v & 0x10) ? 1 : 0;
			}
			enOut16_[1] = (v & 1) ? ~0 : 0;
			enOut8_[1] = (v & 2) ? ~0 : 0;
			enOut4_[1] = (v & 4) ? ~0 : 0;
			enOut2_[1] = (v & 8) ? ~0 : 0;
			break;
		default:
			break;
		}
	}

	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }
	bool Irq() const override { return false; }
	void AckIrq() override {}
	void AdvanceClocks(uint64_t /*clocks*/) override {}

	void Render(int16_t* stereo, int frames) override
	{
		if (!stereo || frames <= 0) return;
		for (int i = 0; i < frames; i++) {
			AdvanceEg();
			int o16 = 0, o8 = 0, o4 = 0, o2 = 0;
			GroupAdvance(0, &o2, &o4, &o8, &o16);
			int s = o2 + o4 + o8 + o16;
			GroupAdvance(1, &o2, &o4, &o8, &o16);
			s += o2 + o4 + o8 + o16;
			AdvanceNoise();
			/* GroupAdvance mid-scale is quiet vs AY residual; keep melody well
			   above PEAK_MIN (200) for flstory classify without hard clip. */
			s *= 32;
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			stereo[i * 2] = (int16_t)s;
			stereo[i * 2 + 1] = (int16_t)s;
		}
	}

	void MixAdd(int16_t* stereo, int frames, int vol) override
	{
		if (!stereo || frames <= 0) return;
		int16_t tmp[512 * 2];
		int left = frames;
		int16_t* dst = stereo;
		while (left > 0) {
			const int n = left > 512 ? 512 : left;
			Render(tmp, n);
			for (int i = 0; i < n * 2; i++) {
				int s = (int)dst[i] + ((int)tmp[i] * vol) / 256;
				if (s > 32767) s = 32767;
				if (s < -32768) s = -32768;
				dst[i] = (int16_t)s;
			}
			dst += n * 2;
			left -= n;
		}
	}

private:
	void InitTables()
	{
		const double rate = (double)sampleRate_;
		const double clock = (double)clockHz_;
		updateStep_ = (int)((double)(1 << MSM_STEP_SH) * rate / clock);
		noiseStep_ = (int)(((1 << MSM_STEP_SH) / 128.0) * (clock / rate));
		for (int i = 0; i < 8; i++) {
			const int rcp = 1 << ((i & 4) ? (i & ~2) : i);
			arTbl_[i] = rcp * 870;
		}
		for (int i = 0; i < 8; i++) {
			const int rcp = 1 << ((i & 4) ? (i & ~2) : i);
			drTbl_[i] = rcp * 17400;
			drTbl_[i + 8] = rcp * 101000;
		}
	}

	void WritePitch(int ch, uint8_t data)
	{
		MsmVoice* v = &voi_[ch];
		v->gf = (data & 0x80) ? 1 : 0;
		if (data & 0x80) {
			if (data >= 0xd8) {
				v->mode = 1;
				v->egSect = 0;
			} else {
				const int pitch = data & 0x7f;
				if (v->pitch != pitch) {
					v->pitch = pitch;
					const uint32_t pg = kMsmPitchRom[pitch < 88 ? pitch : 87];
					v->tgPeriod = (uint32_t)((pg & 0x1ffu) * (uint32_t)updateStep_ / 2u);
					if (v->tgPeriod < 1) v->tgPeriod = 1;
					int n = (int)((pg >> 9) & 7u);
					v->tgOut16 = 1u << n;
					n = (n > 0) ? n - 1 : 0;
					v->tgOut8 = 1u << n;
					n = (n > 0) ? n - 1 : 0;
					v->tgOut4 = 1u << n;
					n = (n > 0) ? n - 1 : 0;
					v->tgOut2 = 1u << n;
				}
				v->mode = 0;
				v->egSect = 0;
			}
			/* Firmware usually arms group outs via 0x0C/0x0D; if still zero,
			   enable the matching group's outs so keyed pitches are audible. */
			const int g = ch < 4 ? 0 : 1;
			if (!enOut16_[g] && !enOut8_[g] && !enOut4_[g] && !enOut2_[g]) {
				enOut16_[g] = enOut8_[g] = enOut4_[g] = enOut2_[g] = ~0;
			}
		} else {
			v->egSect = v->egArm ? 1 : 2;
		}
	}

	void AdvanceEg()
	{
		for (int i = 0; i < 8; i++) {
			MsmVoice* v = &voi_[i];
			switch (v->egSect) {
			case 0:
				if (v->eg < MSM_VMAX) {
					v->counter -= (MSM_VMAX - v->eg) / (v->arRate > 0 ? v->arRate : 1);
					if (v->counter <= 0) {
						const int n = -v->counter / sampleRate_ + 1;
						v->counter += n * sampleRate_;
						v->eg += n;
						if (v->eg > MSM_VMAX) v->eg = MSM_VMAX;
					}
				}
				if (!v->egArm && v->eg >= MSM_VMAX * 80 / 100)
					v->egSect = 1;
				v->egVol = v->eg / 16;
				break;
			case 1:
			case 2: {
				const int rate = (v->egSect == 1)
					? (v->drRate > 0 ? v->drRate : 1)
					: (v->rrRate > 0 ? v->rrRate : 1);
				if (v->eg > MSM_VMIN) {
					v->counter -= (v->eg - MSM_VMIN) / rate;
					if (v->counter <= 0) {
						const int n = -v->counter / sampleRate_ + 1;
						v->counter += n * sampleRate_;
						v->eg -= n;
						if (v->eg < MSM_VMIN) v->eg = MSM_VMIN;
					}
				} else {
					v->egSect = -1;
				}
				v->egVol = v->eg / 16;
				break;
			}
			default:
				break;
			}
		}
	}

	void GroupAdvance(int group, int* o2, int* o4, int* o8, int* o16)
	{
		*o2 = *o4 = *o8 = *o16 = 0;
		for (int i = 0; i < 4; i++) {
			MsmVoice* v = &voi_[group * 4 + i];
			int out2 = 0, out4 = 0, out8 = 0, out16 = 0;
			if (v->mode == 0 && v->tgPeriod > 0 && v->egSect >= 0) {
				int left = 1 << MSM_STEP_SH;
				while (left > 0) {
					const int nextevent = left;
					if (v->tgCnt & v->tgOut16) out16 += v->tgCount;
					if (v->tgCnt & v->tgOut8) out8 += v->tgCount;
					if (v->tgCnt & v->tgOut4) out4 += v->tgCount;
					if (v->tgCnt & v->tgOut2) out2 += v->tgCount;
					v->tgCount -= nextevent;
					while (v->tgCount <= 0) {
						v->tgCount += (int32_t)v->tgPeriod;
						v->tgCnt++;
						if (v->tgCnt & v->tgOut16) out16 += (int)v->tgPeriod;
						if (v->tgCnt & v->tgOut8) out8 += (int)v->tgPeriod;
						if (v->tgCnt & v->tgOut4) out4 += (int)v->tgPeriod;
						if (v->tgCnt & v->tgOut2) out2 += (int)v->tgPeriod;
						if (v->tgCount > 0) break;
						v->tgCount += (int32_t)v->tgPeriod;
						v->tgCnt++;
						if (v->tgCnt & v->tgOut16) out16 += (int)v->tgPeriod;
						if (v->tgCnt & v->tgOut8) out8 += (int)v->tgPeriod;
						if (v->tgCnt & v->tgOut4) out4 += (int)v->tgPeriod;
						if (v->tgCnt & v->tgOut2) out2 += (int)v->tgPeriod;
					}
					if (v->tgCnt & v->tgOut16) out16 -= v->tgCount;
					if (v->tgCnt & v->tgOut8) out8 -= v->tgCount;
					if (v->tgCnt & v->tgOut4) out4 -= v->tgCount;
					if (v->tgCnt & v->tgOut2) out2 -= v->tgCount;
					left -= nextevent;
				}
			} else if (v->mode == 1 && v->egSect >= 0) {
				if (noiseClocks_ & 8) out16 += (1 << MSM_STEP_SH);
				if (noiseClocks_ & 4) out8 += (1 << MSM_STEP_SH);
				if (noiseClocks_ & 2) out4 += (1 << MSM_STEP_SH);
				if (noiseClocks_ & 1) out2 += (1 << MSM_STEP_SH);
			}
			const int mid = 1 << (MSM_STEP_SH - 1);
			*o16 += ((out16 - mid) * v->egVol) >> MSM_STEP_SH;
			*o8 += ((out8 - mid) * v->egVol) >> MSM_STEP_SH;
			*o4 += ((out4 - mid) * v->egVol) >> MSM_STEP_SH;
			*o2 += ((out2 - mid) * v->egVol) >> MSM_STEP_SH;
		}
		if (!enOut16_[group]) *o16 = 0;
		if (!enOut8_[group]) *o8 = 0;
		if (!enOut4_[group]) *o4 = 0;
		if (!enOut2_[group]) *o2 = 0;
	}

	void AdvanceNoise()
	{
		int cnt = (noiseCnt_ += noiseStep_) >> MSM_STEP_SH;
		noiseCnt_ &= (1 << MSM_STEP_SH) - 1;
		while (cnt-- > 0) {
			const int tmp = noiseRng_ & (1 << 16);
			if (noiseRng_ & 1)
				noiseRng_ ^= 0x24000;
			noiseRng_ >>= 1;
			if ((noiseRng_ & (1 << 16)) != tmp)
				noiseClocks_++;
		}
	}

	int sampleRate_;
	uint32_t clockHz_;
	uint8_t control1_;
	uint8_t control2_;
	MsmVoice voi_[8];
	int enOut16_[2], enOut8_[2], enOut4_[2], enOut2_[2];
	int arTbl_[8];
	int drTbl_[16];
	int noiseCnt_;
	int noiseRng_;
	int noiseClocks_;
	int updateStep_;
	int noiseStep_;
};

CChip* CEmuChipMsm5232Create(uint32_t clockHz, int sampleRate)
{
	return new CChipMsm5232(clockHz, sampleRate);
}

void CEmuChipMsm5232Destroy(CChip* c)
{
	delete c;
}
