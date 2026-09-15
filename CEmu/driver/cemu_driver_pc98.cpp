#include "StdAfx.h"
#include "cemu_driver_pc98.h"
#include "../machine/cemu_hard_pc98.h"
#include "../chip/cemu_chip_opna.h"
#include "../vendor/np2/np2ffi.h"
#include "../machine/cemu_np2ctx.h"

enum {
	/* Z80 PC-88 ウォッチドッグと同じ: レジスタが 2 秒完全静止なら演奏中ではない */
	PC98_WD_IDLE_MS = 2000
};

/* PC-98 ドライバ */
CDriverPc98::CDriverPc98()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(8000000)
	, opnHz_(3993600)
	, booted_(0)
	, triggered_(0)
	, opnResidual_(0)
	, cpuAcc_(0)
	, cpuDebt_(0)
	, titleCode_(0)
	, wdSamples_(0)
	, wdLastActive_(0)
	, wdMotion_(0)
	, wdReplays_(0)
	, wdEverActive_(0)
{
}

/* 後始末 */
CDriverPc98::~CDriverPc98()
{
	Close();
}

/* ROM 読込。bootcs 経路は約 1s settle。DOS は PumpCycles */
int CDriverPc98::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardPc98*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 8000000;
	opnHz_ = hw_->opnHz_ > 0 ? hw_->opnHz_ : 3993600;
	opnResidual_ = 0;
	cpuAcc_ = 0;
	cpuDebt_ = 0;
	booted_ = 0;
	triggered_ = 0;
	titleCode_ = titleCode;
	wdSamples_ = 0;
	wdLastActive_ = 0;
	wdMotion_ = 0;
	wdReplays_ = 0;
	wdEverActive_ = 0;

	{
		CEmuNp2Guard np2;
		CEmuHardPc98SetActive(hw_);
		if (!hw_->LoadRoms(fs, ge, titleCode))
			return 0;
		if (!hw_->isDos_) {
			/* bootcs: 約 1s settle */
			const uint64_t bootCycles = (uint64_t)cpuHz_;
			RunUntil(hw_->cpuCycles_ + bootCycles);
		}
	}

	booted_ = 1;
	return 1;
}

/* ハード参照を捨てる */
void CDriverPc98::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
}

/* 同一 zip の別曲を TriggerPlay で切替 */
int CDriverPc98::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	CEmuNp2Guard np2;
	CEmuHardPc98SetActive(hw_);
	titleCode_ = titleCode;
	const int ok = hw_->TriggerPlay(titleCode_) ? 1 : 0;
	if (ok) triggered_ = 1;
	return ok;
}

/* OPN クロックを CPU 比で進める */
void CDriverPc98::TickOpn(uint64_t cpuCycles)
{
	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0 || cpuHz_ <= 0) return;
	opnResidual_ += cpuCycles * (uint64_t)opnHz_;
	const uint64_t opnTicks = opnResidual_ / (uint64_t)cpuHz_;
	opnResidual_ %= (uint64_t)cpuHz_;
	if (opnTicks)
		hw_->SoundChip()->AdvanceClocks(opnTicks);
}

/* i286 を endCycle まで進める。DOS は PumpCycles */
void CDriverPc98::RunUntil(uint64_t endCycle)
{
	if (!hw_) return;
	CEmuNp2Guard np2;
	CEmuHardPc98SetActive(hw_);
	if (hw_->isDos_) {
		hw_->PumpCycles(endCycle);
		return;
	}
	CEmuHardPc98SetActive(hw_);
	while (hw_->cpuCycles_ < endCycle) {
		hw_->ProfSample();
		const int32_t cyc = np2_step();
		const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
		hw_->cpuCycles_ += u;
		hw_->TickSide(u);
		TickOpn(u);
		hw_->DeliverIrqs();
	}
}

/* Z80 PC-88 と同じ停滞ウォッチドッグ。IRQ 源の修復は無し。V30 の再生トリガは
   BIOS 風の 1 コールなので、止まったリップは曲の再キックが唯一の手当て。
   生存信号はキーオン／F-num／SSG 周期。アイドルポーリングは演奏ではない。 */
/* 無音が続くリップを再キックする（一度鳴った後は触らない） */
void CDriverPc98::WatchdogTick()
{
	CChip* chip = hw_ ? hw_->SoundChip() : NULL;
	const int rate = hostRate_ > 0 ? hostRate_ : 44100;
	if (!chip) return;
	unsigned w = 0, k = 0, f = 0, s = 0, m = 0;
	CEmuChipYm2608GetPlayMetrics(chip, &w, &k, &f, &s, &m);
	const unsigned motion = k + f + s + hw_->BeepActivity()
		+ hw_->MidiByteCount() + hw_->MidiNoteOnCount();
	if (motion != wdMotion_) {
		wdMotion_ = motion;
		wdLastActive_ = wdSamples_;
		wdEverActive_ = 1;
		return;
	}
	if (wdSamples_ - wdLastActive_ < (uint64_t)rate * PC98_WD_IDLE_MS / 1000u)
		return;
	wdLastActive_ = wdSamples_;
	/* 一度も鳴っていないときだけ — CDriverPc88::WatchdogTick 参照。
	   既に動いているプレーヤを再キックすると RAM の途中状態から再開し、
	   直そうとした無音より耳障りになる。 */
	if (wdEverActive_ || wdReplays_ >= 4)
		return;
	wdReplays_++;
	hw_->TriggerPlay(titleCode_);
}

/* int16 へ飽和 */
static int CEmuPc98Clamp16(int v)
{
	if (v > 32767) return 32767;
	if (v < -32768) return -32768;
	return v;
}

/* CPU＋OPN を進め、BEEP/OPL を混成。定期的にウォッチドッグ */
int CDriverPc98::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0 || !booted_) return 0;
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	CEmuNp2Guard np2;
	CChip* const opl = hw_->OplChip();
	CEmuHardPc98SetActive(hw_);
	if (!triggered_) {
		hw_->TriggerPlay(titleCode_);
		triggered_ = 1;
	}
	const int rate = hostRate_ > 0 ? hostRate_ : 44100;
	if (cpuHz_ < 1 || rate < 1) return 0;
	for (int i = 0; i < frames; i++) {
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)rate);
		cpuAcc_ %= (int64_t)rate;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		cpuDebt_ += cyclesPerSample;
		if (cpuDebt_ > 0) {
			const uint64_t start = hw_->cpuCycles_;
			const uint64_t end = start + (uint64_t)cpuDebt_;
			if (hw_->isDos_) {
				hw_->PumpCycles(end);
			} else {
				while (hw_->cpuCycles_ < end) {
					hw_->ProfSample();
					const int32_t cyc = np2_step();
					const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
					hw_->cpuCycles_ += u;
					hw_->TickSide(u);
					TickOpn(u);
					hw_->DeliverIrqs();
				}
			}
			cpuDebt_ -= (int64_t)(hw_->cpuCycles_ - start);
		}
		chip->Render(stereo + i * 2, 1);
		hw_->MixBeep(stereo + i * 2, 1);
		if (opl) {
			/* SOUND ORCHESTRA の売りは疑似ステレオ: OPL が左、YM2203 が右
			   （マニュアルは左右逆）。上の OPN 合成は両 ch モノなので、
			   OPN を右寄り・OPL を左に振ると分離を再現できる。 */
			int16_t o[2] = { 0, 0 };
			opl->Render(o, 1);
			int16_t* p = stereo + i * 2;
			const int oL = o[0], pL = p[0], pR = p[1];
			p[0] = (int16_t)CEmuPc98Clamp16(pL / 4 + oL);
			p[1] = (int16_t)CEmuPc98Clamp16(pR - pR / 4 + o[1] / 4);
		}
		if ((++wdSamples_ & 511) == 0)
			WatchdogTick();
	}
	return frames;
}

/* Seek は未対応 */
int CDriverPc98::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
