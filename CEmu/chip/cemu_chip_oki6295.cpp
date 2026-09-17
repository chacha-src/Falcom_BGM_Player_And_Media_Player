#include "StdAfx.h"
#include "cemu_chip_oki6295.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <math.h>
#include <string.h>

/* hoot ssMSM6295/ssADPCM.cpp と MAME OKI ADPCM テーブルを参考。 */
enum { kOkiVoices = 4, kOkiShift = 12 };

/* 鍵盤は A0–C8（21–108）。sf2 のドラム番号 0x0B は o0b で範囲外→点灯しない。 */
static int CEmuOkiMonMidi(int sampleKey)
{
	int m = sampleKey & 127;
	if (m < 21) m += 36;
	if (m > 108) m = 108;
	return m;
}

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
		, bank_(NULL)
	{
		memset(monOn_, 0, sizeof(monOn_));
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
		memset(monOn_, 0, sizeof(monOn_));
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		(void)addr;
		const uint8_t v = (uint8_t)(data & 0xff);
		lastCommand_ = v;
		/* FMモニタへOKIコマンドをシャドウ。 */
		FmMonShadowApplyOki6295(v);
		if (!cmdState_) {
			if (v & 0x80) {
				cmdState_ = 1;
				sampleKey_ = (uint8_t)(v & 0x7f);
			} else {
				/* MAME: bits 6-3 がボイス（bit3=ch0）。複数可。 */
				unsigned mask = (unsigned)v >> 3;
				for (int ch = 0; ch < kOkiVoices; ch++, mask >>= 1) {
					if (mask & 1)
						Stop(ch);
				}
			}
		} else {
			cmdState_ = 0;
			if (!rom_) return;
			const uint32_t table = (uint32_t)sampleKey_ * 8u;
			uint8_t hdr[6];
			for (int k = 0; k < 6; k++) {
				const uint32_t phys = Xlat(table + (uint32_t)k);
				if (phys >= romSize_) return;
				hdr[k] = rom_[phys];
			}
			const uint32_t start = ((hdr[0] << 16) | (hdr[1] << 8) | hdr[2]) & 0x3ffffu;
			const uint32_t end = ((hdr[3] << 16) | (hdr[4] << 8) | hdr[5]) & 0x3ffffu;
			/* 再生スロットは MAME（bit4=voice0）。同じビットへの再キーは hoot 同様に置き換え
			   （sf2 は Ch1 に BD とシンバルを重ね、無視するとシンバルが欠ける）。 */
			unsigned mask = (unsigned)v >> 4;
			for (int ch = 0; ch < kOkiVoices; ch++, mask >>= 1) {
				if (!(mask & 1)) continue;
				if (start >= end || Xlat(start) >= romSize_)
					Stop(ch);
				else
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
		if (gain <= 0) return;
		for (int i = 0; i < frames; i++) {
			int mix = 0;
			for (int ch = 0; ch < kOkiVoices; ch++) {
				Voice& vc = voice_[ch];
				if (!vc.playing) continue;
				while (vc.count >= (1 << kOkiShift)) {
					Fetch(vc);
					vc.count -= (1 << kOkiShift);
				}
				/* hoot ssADPCM: ニブル間を線形補間。ZOH だと 7.5kHz がざらつく。
				   乗算は 12bit×位相×volume が int32 上限に触るので 64bit。 */
				const int64_t dat = ((int64_t)vc.prevSignal * vc.count
					+ (int64_t)vc.signal * ((1 << kOkiShift) - vc.count))
					* (int64_t)vc.volume / ((int64_t)16 << kOkiShift);
				mix += (int)dat;
				vc.count += vc.incr;
			}
			/* gain は 256=unity。hoot pcm_mix を生で渡して /256 を外すと溢れてノイズになる。 */
			const int s = (int)((int64_t)mix * gain / 256);
			/* モノラルADPCMを L/R へ同じ値。 */
			stereo[i * 2] = (int16_t)CEmuOkiClamp16((int)stereo[i * 2] + s);
			stereo[i * 2 + 1] = (int16_t)CEmuOkiClamp16((int)stereo[i * 2 + 1] + s);
		}
		/* サンプル終了でキーオフ。PDX1 = ファームの voice bit0（MAME bit4）。 */
		for (int ch = 0; ch < kOkiVoices; ch++) {
			const int on = voice_[ch].playing ? 1 : 0;
			if (monOn_[ch] && !on)
				FmMonShadowPcmNote(ch, 0, 0);
			monOn_[ch] = (uint8_t)on;
		}
		UpdateSnapshot();
	}

	uint8_t ReadStatus() override
	{
		/* MAME: 上位は 1 固定（naname）。bit0=ch0 … bit3=ch3。 */
		uint8_t d = 0xf0;
		if (voice_[0].playing) d |= 0x01;
		if (voice_[1].playing) d |= 0x02;
		if (voice_[2].playing) d |= 0x04;
		if (voice_[3].playing) d |= 0x08;
		return d;
	}

	void SetPcmRom(const uint8_t* data, unsigned size) override { rom_ = data; romSize_ = size; }
	void SetBankTable(const unsigned* entries) { bank_ = entries; }
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
			for (int i = 0; i < vol; i++) out /= 1.412537545; /* 3dB ステップ */
			volumeTable_[vol] = (unsigned)out;
		}
	}

	/* 論理チップアドレス → サンプルROMオフセット。バンク表が無ければ恒等。
	   （CEmuChipOki6295SetBankTable 参照）。各窓は entry*0x10000+(addr&0xFFFF)。
	   NMK112 のページ基底と窓基底が打ち消し合うため。 */
	uint32_t Xlat(uint32_t addr) const
	{
		if (!bank_) return addr;
		addr &= 0x3ffffu;
		unsigned slot;
		if (addr < 0x400u) slot = addr >> 8;
		else if (addr < 0x10000u) slot = 4;
		else slot = 4 + (addr >> 16);
		return bank_[slot] * 0x10000u + (addr & 0xffffu);
	}

	void Fetch(Voice& vc)
	{
		const uint32_t phys = Xlat(vc.start + (vc.sample / 2));
		if ((vc.sample / 2) >= vc.length || phys >= romSize_) {
			vc.playing = 0;
			vc.step = 0;
			vc.signal = 0;
			return;
		}
		vc.prevSignal = vc.signal;
		const uint8_t b = rom_[phys];
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
		vc.playing = 1; /* キーオン */
		vc.start = start;
		vc.length = length;
		vc.volume = volumeTable_[vol & 15];
		vc.incr = (int)(((uint64_t)clockHz_ << kOkiShift) / (uint64_t)sampleRate_); /* クロック→ホスト */
		if (vc.incr <= 0) vc.incr = 1;
		Fetch(vc);
		FmMonShadowPcmNote(ch, CEmuOkiMonMidi(sampleKey_), 1);
		monOn_[ch] = 1;
	}

	void Stop(int ch)
	{
		if (ch < 0 || ch >= kOkiVoices) return;
		if (monOn_[ch] || voice_[ch].playing)
			FmMonShadowPcmNote(ch, 0, 0);
		monOn_[ch] = 0;
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
			unsigned vol = voice_[ch].playing ? voice_[ch].volume : 0u;
			if (vol > 255u) vol = 255u;
			snapshot_[o + 0] = (uint8_t)vol;
			snapshot_[o + 1] = (uint8_t)(voice_[ch].start >> 8);
			snapshot_[o + 2] = (uint8_t)voice_[ch].start;
			snapshot_[o + 3] = (uint8_t)(voice_[ch].length >> 8);
			snapshot_[o + 4] = (uint8_t)voice_[ch].length;
			snapshot_[o + 5] = (uint8_t)voice_[ch].step;
			snapshot_[o + 6] = (uint8_t)(voice_[ch].signal >> 8);
			snapshot_[o + 7] = (uint8_t)voice_[ch].signal;
		}
		FmMonShadowSetCompanionRegs(snapshot_, (unsigned)sizeof(snapshot_));
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint8_t cmdState_;
	uint8_t sampleKey_;
	uint8_t lastCommand_;
	const unsigned* bank_;
	uint8_t monOn_[kOkiVoices];
	Voice voice_[kOkiVoices];
	int indexShift_[8];
	int diffLookup_[49 * 16];
	unsigned volumeTable_[16];
	uint8_t snapshot_[4 + kOkiVoices * 8];
};

/* MSM6295 ラッパ生成。 */
CChip* CEmuChipOki6295Create(uint32_t clockHz, int sampleRate)
{
	return new CChipOki6295(clockHz, sampleRate);
}

void CEmuChipOki6295Destroy(CChip* c)
{
	delete c;
}

void CEmuChipOki6295SetBankTable(CChip* c, const unsigned* entries)
{
	CChipOki6295* oki = dynamic_cast<CChipOki6295*>(c);
	if (oki) oki->SetBankTable(entries);
}
