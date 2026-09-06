#include "StdAfx.h"
#include "cemu_chip_oki6295.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <math.h>
#include <string.h>

/* Adapted from hoot ssMSM6295/ssADPCM.cpp and MAME OKI ADPCM tables. */
enum { kOkiVoices = 4, kOkiShift = 12 };

static int CEmuOkiClamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipOki6295 : public CChip {
public:
	CChipOki6295(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 1056000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
		, cmdState_(0)
		, sampleKey_(0)
		, lastCommand_(0)
	{
		BuildTables();
		Reset();
	}

	void Reset() override
	{
		memset(voice_, 0, sizeof(voice_));
		memset(snapshot_, 0, sizeof(snapshot_));
		cmdState_ = 0;
		sampleKey_ = 0;
		lastCommand_ = 0;
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		(void)addr;
		const uint8_t v = (uint8_t)(data & 0xff);
		lastCommand_ = v;
		FmMonShadowApplyOki6295(v);
		if (!cmdState_) {
			if (v & 0x80) {
				cmdState_ = 1;
				sampleKey_ = (uint8_t)(v & 0x7f);
			} else {
				if (v & 0x40) Stop(0);
				if (v & 0x20) Stop(1);
				if (v & 0x10) Stop(2);
				if (v & 0x08) Stop(3);
			}
		} else {
			cmdState_ = 0;
			const int ch = DecodeChannel(v >> 4);
			if (ch < 0 || !rom_) return;
			const uint32_t table = (uint32_t)sampleKey_ * 8u;
			if (table + 5 >= romSize_) return;
			const uint32_t start = ((rom_[table] << 16) | (rom_[table + 1] << 8) | rom_[table + 2]) & 0x3ffffu;
			const uint32_t end = ((rom_[table + 3] << 16) | (rom_[table + 4] << 8) | rom_[table + 5]) & 0x3ffffu;
			if (start > end || start >= romSize_) {
				Stop(ch);
			} else {
				Play(ch, start, end - start + 1, v & 0x0f);
			}
		}
		UpdateSnapshot();
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
		if (!stereo || frames <= 0 || !rom_) return;
		for (int i = 0; i < frames; i++) {
			int mix = 0;
			for (int ch = 0; ch < kOkiVoices; ch++) {
				Voice& vc = voice_[ch];
				if (!vc.playing) continue;
				while (vc.count >= (1 << kOkiShift)) {
					Fetch(vc);
					vc.count -= (1 << kOkiShift);
				}
				mix += vc.prevSignal * vc.volume / 16;
				vc.count += vc.incr;
			}
			const int s = mix * gain / 256;
			stereo[i * 2] = (int16_t)CEmuOkiClamp16((int)stereo[i * 2] + s);
			stereo[i * 2 + 1] = (int16_t)CEmuOkiClamp16((int)stereo[i * 2 + 1] + s);
		}
		UpdateSnapshot();
	}

	uint8_t ReadStatus() override
	{
		uint8_t d = 0;
		if (voice_[0].playing) d |= 0x08;
		if (voice_[1].playing) d |= 0x04;
		if (voice_[2].playing) d |= 0x02;
		if (voice_[3].playing) d |= 0x01;
		return d;
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override { rom_ = data; romSize_ = size; }
	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		if (!buf || cap == 0) return 0;
		const unsigned n = cap < sizeof(snapshot_) ? cap : (unsigned)sizeof(snapshot_);
		memcpy(buf, snapshot_, n);
		return n;
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return ReadStatus(); }
	uint8_t ReadDataHi() override { return 0; }

private:
	struct Voice {
		uint8_t playing;
		uint32_t start;
		uint32_t length;
		uint32_t sample;
		int signal;
		int prevSignal;
		int step;
		int count;
		int incr;
		unsigned volume;
	};

	static int DecodeChannel(int code)
	{
		static const signed char tbl[16] = { -1, 3, 2, -1, 1, -1, -1, -1, 0, -1, -1, -1, -1, -1, -1, -1 };
		return tbl[code & 15];
	}

	void BuildTables()
	{
		static const int shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
		memcpy(indexShift_, shift, sizeof(indexShift_));
		static const int nbl2bit[16][4] = {
			{ 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
			{ 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
			{-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
			{-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
		};
		for (int step = 0; step <= 48; step++) {
			const int stepval = (int)floor(16.0 * pow(11.0 / 10.0, (double)step));
			for (int nib = 0; nib < 16; nib++) {
				diffLookup_[step * 16 + nib] = nbl2bit[nib][0] *
					(stepval * nbl2bit[nib][1] + stepval / 2 * nbl2bit[nib][2] +
					 stepval / 4 * nbl2bit[nib][3] + stepval / 8);
			}
		}
		for (int vol = 0; vol < 16; vol++) {
			double out = 256.0;
			for (int i = 0; i < vol; i++) out /= 1.412537545; /* 3 dB */
			volumeTable_[vol] = (unsigned)out;
		}
	}

	void Fetch(Voice& vc)
	{
		if ((vc.sample / 2) >= vc.length || vc.start + (vc.sample / 2) >= romSize_) {
			vc.playing = 0;
			vc.step = 0;
			vc.signal = 0;
			return;
		}
		vc.prevSignal = vc.signal;
		const uint8_t b = rom_[vc.start + (vc.sample / 2)];
		const int nib = (b >> (((vc.sample & 1) << 2) ^ 4)) & 15;
		vc.sample++;
		vc.signal += diffLookup_[vc.step * 16 + nib];
		if (vc.signal > 2047) vc.signal = 2047;
		if (vc.signal < -2048) vc.signal = -2048;
		vc.step += indexShift_[nib & 7];
		if (vc.step > 48) vc.step = 48;
		if (vc.step < 0) vc.step = 0;
	}

	void Play(int ch, uint32_t start, uint32_t length, int vol)
	{
		Voice& vc = voice_[ch];
		memset(&vc, 0, sizeof(vc));
		vc.playing = 1;
		vc.start = start;
		vc.length = length;
		vc.volume = volumeTable_[vol & 15];
		vc.incr = (int)(((uint64_t)clockHz_ << kOkiShift) / (uint64_t)sampleRate_);
		if (vc.incr <= 0) vc.incr = 1;
		Fetch(vc);
	}

	void Stop(int ch)
	{
		voice_[ch].playing = 0;
		voice_[ch].sample = 0;
		voice_[ch].step = 0;
		voice_[ch].signal = 0;
	}

	void UpdateSnapshot()
	{
		snapshot_[0] = lastCommand_;
		snapshot_[1] = cmdState_;
		snapshot_[2] = sampleKey_;
		snapshot_[3] = ReadStatus();
		for (int ch = 0; ch < kOkiVoices; ch++) {
			const int o = 4 + ch * 8;
			snapshot_[o + 0] = voice_[ch].playing;
			snapshot_[o + 1] = (uint8_t)(voice_[ch].start >> 8);
			snapshot_[o + 2] = (uint8_t)voice_[ch].start;
			snapshot_[o + 3] = (uint8_t)(voice_[ch].length >> 8);
			snapshot_[o + 4] = (uint8_t)voice_[ch].length;
			snapshot_[o + 5] = (uint8_t)voice_[ch].step;
			snapshot_[o + 6] = (uint8_t)(voice_[ch].signal >> 8);
			snapshot_[o + 7] = (uint8_t)voice_[ch].signal;
		}
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint8_t cmdState_;
	uint8_t sampleKey_;
	uint8_t lastCommand_;
	Voice voice_[kOkiVoices];
	int indexShift_[8];
	int diffLookup_[49 * 16];
	unsigned volumeTable_[16];
	uint8_t snapshot_[4 + kOkiVoices * 8];
};

CChip* CEmuChipOki6295Create(uint32_t clockHz, int sampleRate)
{
	return new CChipOki6295(clockHz, sampleRate);
}

void CEmuChipOki6295Destroy(CChip* c)
{
	delete c;
}
