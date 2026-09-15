#include "StdAfx.h"
#include "cemu_chip_x1_010.h"
#include "cemu_chip.h"
#include "../fmmon/fmmon_shadow.h"
#include <string.h>

/* Seta X1-010。MAME x1_010.cpp を参考。

   チップは 8KB RAM窓。$0000-$007F が 16ch×8 制御バイト。それより上は
   制御バイトが128バイトページで索引するテーブル空間:

     reg 0  status   bit0=キーオン, bit1=0:PCM 1:波形, bit2=envワンショット
     reg 1  PCM: 音量、下位ニブル=左、上位=右
            wave: 波形ページ番号
     reg 2  PCM: 周波数（下位5bit）      wave: ピッチ下位
     reg 3  wave: ピッチ上位
     reg 4  PCM: サンプル開始（4KB単位）  wave: エンベロープ周期
     reg 5  PCM: サンプル終端 0x100-end  wave: エンベロープページ
*/
enum {
	kX1010Channels = 16,
	kX1010RamSize = 0x2000,
	kX1010FreqBits = 14,
	/* 8bitサンプル×4bit音量は ±1920 までなので、MAME VOL_BASE と同様に
	   フルレンジへスケールする。 */
	kX1010VolScale = 16
};

static int CEmuX1010Clamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

class CChipX1010 : public CChip {
public:
	CChipX1010(uint32_t clockHz, int sampleRate)
		: clockHz_(clockHz ? clockHz : 16000000u)
		, sampleRate_(sampleRate > 0 ? sampleRate : 44100)
		, rom_(NULL)
		, romSize_(0)
	{
		Reset();
	}

	void Reset() override
	{
		memset(ram_, 0, sizeof(ram_));
		memset(smpOffs_, 0, sizeof(smpOffs_));
		memset(envOffs_, 0, sizeof(envOffs_));
		memset(playing_, 0, sizeof(playing_));
	}

	uint8_t ReadRam(unsigned offset) const
	{
		return ram_[offset & (kX1010RamSize - 1)];
	}

	void WriteRam(unsigned offset, uint8_t data)
	{
		const unsigned a = offset & (kX1010RamSize - 1);
		if (a < kX1010Channels * 8u) {
			const int ch = (int)(a >> 3);
			const unsigned reg = a & 7u;
			if (reg == 0) {
				const int wasOn = ram_[a] & 1;
				const int nowOn = data & 1;
				/* 新規キーオンでサンプルとエンベロープを再開。ドライバは
				   ノート保持中に毎フレーム reg0 を書き直すので、off→on 辺
				   だけ巻き戻してよい。 */
				if (nowOn && !wasOn) {
					smpOffs_[ch] = 0;
					envOffs_[ch] = 0;
					SetPlaying(ch, 1);
				} else if (!nowOn && wasOn) {
					SetPlaying(ch, 0);
				}
			}
		}
		ram_[a] = data;
	}

	void Write(uint32_t addr, uint32_t data) override
	{
		WriteRam((unsigned)addr, (uint8_t)(data & 0xff));
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
		for (int ch = 0; ch < kX1010Channels; ch++) {
			uint8_t* reg = &ram_[ch * 8];
			if (!(reg[0] & 1)) continue;
			if (reg[0] & 2)
				MixWave(ch, reg, stereo, frames, gain);
			else
				MixPcm(ch, reg, stereo, frames, gain);
		}
	}

	uint8_t ReadStatus() override { return 0; }
	uint8_t ReadData() override { return 0; }
	uint8_t ReadStatusHi() override { return 0; }
	uint8_t ReadDataHi() override { return 0; }

	void SetPcmRom(const uint8_t* data, unsigned size) override
	{
		rom_ = data;
		romSize_ = size;
	}

	unsigned GetRegSnapshot(uint8_t* buf, unsigned cap) const override
	{
		/* レジスタパネルに意味があるのは制御ブロックだけ。後ろのテーブル
		   ページは波形バイトが流れるだけになる。 */
		if (!buf || cap == 0) return 0;
		const unsigned n = cap < kX1010Channels * 8u ? cap : kX1010Channels * 8u;
		memcpy(buf, ram_, n);
		return n;
	}

	bool Irq() const override { return false; }
	void AckIrq() override {}

private:
	void SetPlaying(int ch, int on)
	{
		if (playing_[ch] == (uint8_t)on) return;
		playing_[ch] = (uint8_t)on;
		/* FMモニタへキーオン/オフ。 */
		FmMonShadowPcmNote(ch, 60 + (ch & 15), on);
	}

	/* PCM: サンプルROMから符号付き8bitを直読み。reg2 下位5bitはマスタクロックの
	   固定 1/8192 分周に掛かる。Meta Fox はレジスタ0のままその既定レートを使う。 */
	void MixPcm(int ch, const uint8_t* reg, int16_t* stereo, int frames, int gain)
	{
		if (!rom_ || !romSize_) return;
		/* reg5 はサンプル長を 0x100-ページで、reg4 の開始ページから数える。
		   絶対終端アドレスではない。 */
		const uint32_t base = (uint32_t)reg[4] * 0x1000u;
		const uint32_t len = (uint32_t)(0x100u - reg[5]) * 0x1000u;
		if (!len) return;
		const unsigned freq = reg[2] & 0x1fu;
		const double rate = (double)clockHz_ / 8192.0
			* (double)(freq ? freq : 4u);
		uint32_t step = (uint32_t)(rate * (double)(1u << kX1010FreqBits)
			/ (double)sampleRate_);
		if (!step) step = 1;
		const int volL = (reg[1] & 0x0f) * kX1010VolScale;
		const int volR = ((reg[1] >> 4) & 0x0f) * kX1010VolScale; /* パン=L/Rニブル */
		uint32_t offs = smpOffs_[ch];
		for (int i = 0; i < frames; i++) {
			const uint32_t pos = offs >> kX1010FreqBits;
			if (pos >= len) {
				/* サンプル尽きた: チップ自身がキーオンbitを落とす。 */
				ram_[ch * 8] &= (uint8_t)~1u;
				SetPlaying(ch, 0);
				break;
			}
			const uint32_t adr = base + pos;
			const int8_t s = (adr < romSize_) ? (int8_t)rom_[adr] : 0;
			AddSample(stereo, i, s * volL, s * volR, gain);
			offs += step;
		}
		smpOffs_[ch] = offs;
	}

	/* 波形: reg1 が選ぶ128バイト符号付きページ。振幅は reg5 の
	   128バイトステレオエンベロープページで整形。 */
	void MixWave(int ch, const uint8_t* reg, int16_t* stereo, int frames, int gain)
	{
		const int8_t* wave = (const int8_t*)&ram_[((unsigned)reg[1] * 0x80u)
			& (kX1010RamSize - 1)];
		const uint8_t* env = &ram_[((unsigned)reg[5] * 0x80u)
			& (kX1010RamSize - 1)];
		/* 1つの基底ステップが波形とエンベロープの両方を駆動。reg2 は波形側の
		   単なる乗数、reg4 はエンベロープを割るので、ピッチと減衰は独立。 */
		const double ebase = (double)clockHz_ / 128.0 / 1024.0 / 4.0;
		const uint32_t unit = (uint32_t)(ebase * (double)(1u << kX1010FreqBits)
			/ (double)sampleRate_);
		if (!reg[2]) return;
		uint32_t step = unit * reg[2];
		if (!step) step = 1;
		uint32_t estep = unit / (reg[4] ? reg[4] : 1u);
		if (!estep) estep = 1;
		uint32_t offs = smpOffs_[ch];
		uint32_t eoffs = envOffs_[ch];
		for (int i = 0; i < frames; i++) {
			const unsigned ei = (eoffs >> kX1010FreqBits);
			if ((reg[0] & 4) && ei >= 0x80u) {
				/* ワンショットエンベロープ終了。 */
				ram_[ch * 8] &= (uint8_t)~1u;
				SetPlaying(ch, 0);
				break;
			}
			const int8_t s = wave[(offs >> kX1010FreqBits) & 0x7f];
			const uint8_t e = env[ei & 0x7f];
			const int volL = ((e >> 4) & 0x0f) * kX1010VolScale;
			const int volR = (e & 0x0f) * kX1010VolScale;
			AddSample(stereo, i, s * volL, s * volR, gain);
			offs += step;
			eoffs += estep;
		}
		smpOffs_[ch] = offs;
		envOffs_[ch] = eoffs;
	}

	void AddSample(int16_t* stereo, int i, int l, int r, int gain)
	{
		/* ステレオMix: L/R を既存バッファへ加算して飽和。 */
		const int sl = (int)stereo[i * 2] + (l * gain >> 8);
		const int sr = (int)stereo[i * 2 + 1] + (r * gain >> 8);
		stereo[i * 2] = (int16_t)CEmuX1010Clamp16(sl);
		stereo[i * 2 + 1] = (int16_t)CEmuX1010Clamp16(sr);
	}

	uint32_t clockHz_;
	int sampleRate_;
	const uint8_t* rom_;
	unsigned romSize_;
	uint8_t ram_[kX1010RamSize];
	uint32_t smpOffs_[kX1010Channels];
	uint32_t envOffs_[kX1010Channels];
	uint8_t playing_[kX1010Channels];
};

/* X1-010 ラッパ生成。 */
CChip* CEmuChipX1010Create(uint32_t clockHz, int sampleRate)
{
	return new CChipX1010(clockHz, sampleRate);
}

void CEmuChipX1010Destroy(CChip* c)
{
	delete c;
}

uint8_t CEmuChipX1010Read(CChip* c, unsigned offset)
{
	return c ? static_cast<CChipX1010*>(c)->ReadRam(offset) : 0;
}

void CEmuChipX1010Write(CChip* c, unsigned offset, uint8_t data)
{
	if (c) static_cast<CChipX1010*>(c)->WriteRam(offset, data);
}
