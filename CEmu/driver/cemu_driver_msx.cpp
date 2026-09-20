#include "StdAfx.h"
#include "cemu_driver_msx.h"
#include "../chip/cemu_chip_ay.h"
#include "../z80/Ay_Cpu.h"
#include "../s98/device/emu2413/emu2413.h"
#include <string.h>
#include <stdlib.h>

/* MSX ドライバ: VBlank 周期は 60Hz */
CDriverMsx::CDriverMsx()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(3579545)
	, ayHz_(3579545 / 2)
	, opllHz_(3579545)
	, ayResidual_(0)
	, opllResidual_(0)
	, cpuAcc_(0)
	, cpuTarget_(0)
	, nextIrq_(0)
	, irqPeriod_(3579545 / 60)
	, sampleIndex_(0)
	, nextIrqSample_(0)
	, irqPulses_(0)
	, playing_(0)
{
}

/* 後始末 */
CDriverMsx::~CDriverMsx()
{
	Close();
}

/* AY/OPLL は Render でサンプル駆動するためここでは no-op */
void CDriverMsx::TickChips(uint64_t cpuCycles)
{
	(void)cpuCycles;
	/* AY/OPLL は Render 側でサンプル駆動 */
}

/* 出力タイムライン上の VBlank。IM2 ベクタが空なら IM1 へ落とす */
void CDriverMsx::PulseVblankIrq()
{
	if (!hw_ || !hw_->Cpu() || !playing_) return;
	Ay_Cpu* cpu = hw_->Cpu();
	/* ran2 の play LDIR/WRTPSG が page0 を壊し IFF1 を落とす。IFF1 ゲート前に
	   植え直し、次の VBlank が H.TIMI に届くようにする。 */
	hw_->KeepCompileRan2Alive();
	/* VBlank はサンプル軸のみ（RunUntil 内の CPU サイクル IRQ は使わない）。
	   二重スケジュールは Quinpl の play を 1 フレーム 2 回走らせ、Z80 スタックを
	   隣ヒープへ壊し、ドライバ破棄で落ちた。 */
	if (!cpu->r.iff1) return;
	/* EI;HALT（yosikon play）: HALT は遅延命令なので IRQ を受け付ける */
	if (cpu->get_mem() && cpu->get_mem()[cpu->r.pc] == 0x76)
		cpu->irqDelay = 0;
	/* hoot kss.cpp Interrupt: IM2 IPL 下で raise_IRQ(0xff)。IPL ISR は $0038。
	   IM2 はゲームが (I<<8)|$FF に実ベクタを書いたときだけ有効。KSS StartSong は
	   $0000-$3FFF を $C9 で埋めるため、I 未設定だと $C9C9 を踏み音源 ISR が
	   走らない（judo/replcart/labyr 無音）。 */
	if (cpu->r.im == 2) {
		const uint16_t target = Ay_CpuIm2Target(cpu, 0xff);
		uint8_t* mem = cpu->get_mem();
		int useIm2 = (target != 0);
		if (useIm2 && mem) {
			const uint8_t op = mem[target];
			if (op == 0xC9 || op == 0x00 || op == 0xFF)
				useIm2 = 0;
		}
		if (!useIm2 || !Ay_CpuIm2Interrupt(cpu, 0xff))
			Ay_CpuIm1Interrupt(cpu);
	} else {
		Ay_CpuIm1Interrupt(cpu);
	}
	irqPulses_++;
}

/* Z80 を endCycle まで進める。HALT はサンプル予算まで眠る */
void CDriverMsx::RunUntil(uint64_t endCycle)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CEmuHardMsxSetActive(hw_);
	int guard = 0;
	while ((uint64_t)cpu->time64() < endCycle && guard++ < 4000000) {
		const uint64_t now = (uint64_t)cpu->time64();
		/* HALT: このサンプルの CPU 予算まで眠る。VBlank は Render が hostRate/60
		   グリッドで注入。DI;HALT（f1sp3d / yosikon）は IFF1 が無いので起きない
		   → NOP として踏み越す。EI;HALT: irqDelay が固まる — RunUntil は HALT を
		   命令実行しないので PulseVblankIrq が落ち続ける。 */
		if (cpu->get_mem() && cpu->get_mem()[cpu->r.pc] == 0x76) {
			cpu->irqDelay = 0;
			if (hw_->GenericMode() && !cpu->r.iff1) {
				cpu->r.pc = (uint16_t)(cpu->r.pc + 1);
				cpu->adjust_time(4);
				hw_->AddCpuCycles(4);
				continue;
			}
			uint64_t delta = (endCycle > now) ? (endCycle - now) : 4;
			if (delta < 4) delta = 4;
			if (delta > 0x7fffffff) delta = 0x7fffffff;
			cpu->adjust_time((int)delta);
			hw_->AddCpuCycles(delta);
			continue;
		}
		const int cycles = Ay_CpuRunOne(cpu);
		if (cycles <= 0) break;
		hw_->AddCpuCycles((uint64_t)cycles);
		TickChips((uint64_t)cycles);
	}
}

/* KSS/カートリッジを読み、曲を開始する */
int CDriverMsx::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardMsx*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 3579545;
	ayHz_ = hw_->ayHz_ > 0 ? hw_->ayHz_ : cpuHz_ / 2;
	opllHz_ = hw_->opllHz_ > 0 ? hw_->opllHz_ : cpuHz_;
	irqPeriod_ = (uint64_t)cpuHz_ / 60;
	ayResidual_ = 0;
	opllResidual_ = 0;
	cpuAcc_ = 0;
	cpuTarget_ = 0;
	sampleIndex_ = 0;
	nextIrqSample_ = (uint64_t)hostRate_ / 60u;
	nextIrq_ = 0;
	irqPulses_ = 0;
	playing_ = 0;

	if (!hw_->LoadKss(fs, ge, titleCode))
		return 0;
	/* コード 0 も通す（fmpac sample 00、ds4 track 0）。0→1 強制は 2 曲が潰れた */
	if (!hw_->StartSong(titleCode))
		return 0;

	Ay_Cpu* cpu = hw_->Cpu();
	cpuTarget_ = cpu ? (uint64_t)cpu->time64() : 0;
	playing_ = 1;
	return 1;
}

/* ハード参照を捨てる */
void CDriverMsx::Close()
{
	hw_ = NULL;
	playing_ = 0;
}

/* 同一 zip の別曲を StartSong で切替。トグル行は曲を変えずフラグだけ載せる。 */
int CDriverMsx::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	if (hw_->ApplyCatalogToggle(titleCode))
		return 1;
	return hw_->StartSong(titleCode) ? 1 : 0;
}

/* サンプル軸で Z80 を進め、AY/SCC/SN/OPLL/OPL を混成 */
int CDriverMsx::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	Ay_Cpu* cpu = hw_->Cpu();
	if (!cpu || hostRate_ < 1 || cpuHz_ < 1) return 0;
	CEmuHardMsxSetActive(hw_);

	for (int i = 0; i < frames; i++) {
		/* サンプル→CPU の絶対写像。VBlank は出力タイムライン (hostRate/60) 上。
		   Z80 命令の超過には載せない。Quinpl のテンポ揺れを消す。 */
		sampleIndex_++;
		const uint64_t want = (sampleIndex_ * (uint64_t)cpuHz_) / (uint64_t)hostRate_;
		if (want > cpuTarget_)
			cpuTarget_ = want;
		RunUntil(cpuTarget_);
		if (sampleIndex_ >= nextIrqSample_) {
			/* DI 中も 60Hz グリッドを進める。EI 時に取りこぼし端がまとめて来ないように
			   （Quinpl が約 2 倍速になった）。 */
			const uint64_t step = (uint64_t)hostRate_ / 60u;
			nextIrqSample_ += step ? step : 1u;
			PulseVblankIrq();
			/* 帯域外の ISR 予算は足さない。次の出力サンプルが同じ絶対タイムラインで
			   ハンドラを実行する。VBlank 毎 200us ボーナスは CPU をバーストさせて
			   タイムライン待ちのアイドルを作り、Quinpl のテンポ揺れになった。 */
		}

		int16_t ayBuf[2] = { 0, 0 };
		int16_t opllS = 0;
		if (hw_->ChipAy())
			hw_->ChipAy()->Render(ayBuf, 1);
		if (hw_->ChipScc())
			hw_->ChipScc()->MixAdd(ayBuf, 1, 256);
		if (hw_->ChipSng()) {
			int16_t snBuf[2] = { 0, 0 };
			hw_->ChipSng()->Render(snBuf, 1);
			int32_t sl = (int32_t)ayBuf[0] + (int32_t)snBuf[0];
			int32_t sr = (int32_t)ayBuf[1] + (int32_t)snBuf[1];
			if (sl > 32767) sl = 32767;
			if (sl < -32768) sl = -32768;
			if (sr > 32767) sr = 32767;
			if (sr < -32768) sr = -32768;
			ayBuf[0] = (int16_t)sl;
			ayBuf[1] = (int16_t)sr;
		}
		if (hw_->Opll()) {
			OPLL* ochip = (OPLL*)hw_->Opll();
			int32_t o = (int32_t)OPLL_calc(ochip) * 5;
			if (o > 32767) o = 32767;
			if (o < -32768) o = -32768;
			opllS = (int16_t)o;
		}
		if (hw_->ChipOpl()) {
			int16_t oplBuf[2] = { 0, 0 };
			hw_->ChipOpl()->Render(oplBuf, 1);
			int32_t ol = (int32_t)opllS + (int32_t)oplBuf[0];
			if (ol > 32767) ol = 32767;
			if (ol < -32768) ol = -32768;
			opllS = (int16_t)ol;
		}
		int32_t l = (int32_t)ayBuf[0] + (int32_t)opllS;
		int32_t r = (int32_t)ayBuf[1] + (int32_t)opllS;
		if (l > 32767) l = 32767;
		if (l < -32768) l = -32768;
		if (r > 32767) r = 32767;
		if (r < -32768) r = -32768;
		stereo[i * 2] = (int16_t)l;
		stereo[i * 2 + 1] = (int16_t)r;
	}
	return frames;
}

/* Seek は未対応 */
int CDriverMsx::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
