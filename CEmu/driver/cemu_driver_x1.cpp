#include "StdAfx.h"
#include "cemu_driver_x1.h"
#include "../chip/cemu_chip_opm.h"
#include "../chip/cemu_chip_ay.h"
#include "../z80/Ay_Cpu.h"
#include <string.h>

/* IM2 先を解決。NCS gaia/hayato は I*256+N に JP <handler> を置く（コードであり表ではない）。
   ワードフェッチすると C3 xx をアドレス xxc3 と誤読する。 */
static uint16_t X1Im2Target(Ay_Cpu* cpu, uint8_t vector)
{
	if (!cpu) return 0;
	const uint8_t* mem = cpu->get_mem();
	if (!mem) return 0;
	const uint16_t slot = (uint16_t)(((uint16_t)cpu->r.i << 8) | vector);
	if (mem[slot] == 0xC3)
		return (uint16_t)mem[(uint16_t)(slot + 1)]
			| ((uint16_t)mem[(uint16_t)(slot + 2)] << 8);
	return Ay_CpuIm2Target(cpu, vector);
}

/* X1 ドライバ: 割り込み周期は CHardX1 の CTC が持つ */
CDriverX1::CDriverX1()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(4000000)
	, opmHz_(4000000)
	, ayHz_(2000000)
	, booted_(0)
	, triggered_(0)
	, songCode_(0)
	, titleCode_(0)
	, opmResidual_(0)
	, ayResidual_(0)
	, cpuAcc_(0)
	, cpuDebt_(0)
	, timerIrqs_(0)
	, vsyncIrqs_(0)
{
}

/* 後始末 */
CDriverX1::~CDriverX1()
{
	Close();
}

/* OPM 書込回数 */
unsigned CDriverX1::OpmWrites() const
{
	return hw_ ? hw_->OpmWrites() : 0;
}

/* AY 書込回数 */
unsigned CDriverX1::AyWrites() const
{
	return hw_ ? hw_->AyWrites() : 0;
}

/* OPM/OPN/AY クロックを CPU 比で進める */
void CDriverX1::TickChips(uint64_t cpuCycles)
{
	if (!hw_ || cpuCycles == 0) return;
	if (hw_->SoundChip()) {
		opmResidual_ += cpuCycles * (uint64_t)opmHz_;
		const uint64_t ticks = opmResidual_ / (uint64_t)cpuHz_;
		opmResidual_ %= (uint64_t)cpuHz_;
		if (ticks)
			hw_->SoundChip()->AdvanceClocks(ticks);
	}
	if (hw_->ChipAy()) {
		ayResidual_ += cpuCycles * (uint64_t)ayHz_;
		const uint64_t ticks = ayResidual_ / (uint64_t)cpuHz_;
		ayResidual_ %= (uint64_t)cpuHz_;
		if (ticks)
			hw_->ChipAy()->AdvanceClocks(ticks);
	}
}

/* CTC を now まで進め、保留中の INT を 1 本 IM2 で届ける */
void CDriverX1::DeliverIrqs(uint64_t now)
{
	if (!hw_ || !hw_->Cpu()) return;
	hw_->ArmTelenetPlayGate();
	Ay_Cpu* cpu = hw_->Cpu();
	/* 割り込み源は CTC だけ。各チャネルは自分の周期で数え、IE 付きの満了は受理されるまで
	   ctcPending_ に残る。DI 中や EI 直後の 1 命令窓に来た tick も捨てずに後で届くので、
	   長い ISR の裏で ch0 が抜けたり、ch3 と同時の ch0 が落ちたりしない。 */
	const unsigned vsyncTicks = hw_->CtcRun(now);

	/* 再生コマンド保持を VSYNC 互換線（60Hz）で減衰（90 ≈ 1.5s）。ゲストの CTC 設定に依らない */
	if (vsyncTicks && hw_->playCmdHoldIrqs_ > 0) {
		hw_->playCmdHoldIrqs_--;
		if (hw_->playCmdHoldIrqs_ == 0) {
			hw_->playCmdLatch_ = 0;
			hw_->ydosCmdSeen_ = 0;
			hw_->ydosInhibitReentry_ = 0;
			if (hw_->Mem() && hw_->Mem()[CHardX1::PLAY_FLAG] == 0x01)
				hw_->Mem()[CHardX1::PLAY_FLAG] = 0x00;
		}
	}

	/* euphory は IM 2 前に $112 で EI。page0 は JP $100 再起動スタブ。IM0 の間に RST 38 で
	   tick すると初期化を抜けない。保留中の要求も捨て、後で IM2 がそのスタブへ飛ばないようにする。 */
	if (cpu->r.im == 0) {
		memset(hw_->ctcPending_, 0, sizeof(hw_->ctcPending_));
		return;
	}
	/* hoot 互換プリセット中（ゲストが CTC 未設定）の要求は hoot の raise→lower と同じパルス。
	   この命令で受理されなければ捨てる。 */
	const int pulse = hw_->CtcHootPreset();
	if (!cpu->r.iff1) {
		if (pulse)
			memset(hw_->ctcPending_, 0, sizeof(hw_->ctcPending_));
		return;
	}
	if (cpu->r.im == 2) {
		for (int ch = 0; ch < 4; ch++) {
			if (!hw_->ctcPending_[ch]) continue;
			const uint16_t tgt = X1Im2Target(cpu, hw_->CtcVector(ch));
			/* euphory の page0 IM2 は JP $100（PROG00 再起動）に続く。<$200 全体は飛ばさない — JESUS は ISR を置く。 */
			if (tgt == 0 || tgt == 0x0100)
				hw_->ctcPending_[ch] = 0;
		}
	}

	/* Laplace 初期化は $9BC4 の duration を 0 のまま。DEC が $FF に回り、最初の F0 が 256 tick 待つ。
	   アクティブチャネルをプライムして初回 ISR がフェッチできるようにする。 */
	if (hw_->laplaceCtcF_) {
		uint8_t* mem = hw_->Mem();
		if (mem && mem[0x8709] && mem[0x80A2] == 0 && mem[0x80A3] == 0) {
			const uint8_t mask = mem[0x8709];
			for (int ch = 0; ch < 6; ch++) {
				if ((mask & (uint8_t)(1u << ch)) && mem[0x9BC4 + ch] == 0)
					mem[0x9BC4 + ch] = 1;
			}
		}
	}

	/* mars: ブートが EI してメールボックスを待つ。PATCH が PROG の play を CALL して戻るまで tick を止め、
	   最初の ch3 が $4A6B 内から $41FF に入り RETI しないのを防ぐ。 */
	if (hw_->marsHoldIrq_) {
		const uint16_t pc = cpu->r.pc;
		if (triggered_ && pc >= 0x4100u && pc < 0x6000u)
			hw_->marsSeenProg_ = 1;
		if (hw_->marsSeenProg_ && pc < 0x100u)
			hw_->marsPlayReady_ = 1;
		if (!hw_->marsPlayReady_) {
			memset(hw_->ctcPending_, 0, sizeof(hw_->ctcPending_));
			return;
		}
	}

	/* Zilog CTC のデイジーチェーン優先は ch0 > ch1 > ch2 > ch3。同時に満了した低位チャネルは
	   保留のまま、上位 ISR の RETI／EI の後で届く（ys2: ch0 の $2713 が ch1 の $2704 より先）。 */
	int take = -1;
	for (int ch = 0; ch < 4 && take < 0; ch++)
		if (hw_->ctcPending_[ch]) take = ch;
	if (take < 0)
		return;
	/* EI 直後（irqDelay）は CPU が受理しないので、要求は残して次の命令で再試行 */
	int taken;
	if (cpu->r.im == 2)
		taken = Ay_CpuIm2InterruptTo(cpu, X1Im2Target(cpu, hw_->CtcVector(take))) ? 1 : 0;
	else
		taken = Ay_CpuIm1Interrupt(cpu) ? 1 : 0;
	if (pulse)
		memset(hw_->ctcPending_, 0, sizeof(hw_->ctcPending_));
	if (!taken)
		return;
	hw_->ctcPending_[take] = 0;
	if (take == 3) vsyncIrqs_++;
	else timerIrqs_++;
}

/* Z80 を endCycle まで進める。HALT は次 IRQ まで飛ばす */
void CDriverX1::RunUntil(uint64_t endCycle)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CEmuHardX1SetActive(hw_);
	int guard = 0;
	while ((uint64_t)cpu->time64() < endCycle && guard++ < 4000000) {
		const uint64_t now = (uint64_t)cpu->time64();
		DeliverIrqs(now);
		/* HALT: 次の CTC 満了かサンプル期限へ時刻を飛ばし、IRQ を実時間に保つ */
		if (cpu->get_mem() && cpu->get_mem()[cpu->r.pc] == 0x76) {
			uint64_t wake = endCycle;
			const uint64_t ctcWake = hw_->CtcNextEvent(now);
			if (ctcWake < wake)
				wake = ctcWake;
			uint64_t delta = (wake > now) ? (wake - now) : 4;
			if (delta < 4) delta = 4;
			if (delta > 0x7fffffff) delta = 0x7fffffff;
			cpu->adjust_time((int)delta);
			hw_->AddCpuCycles(delta);
			TickChips(delta);
			continue;
		}
		const int cycles = Ay_CpuRunOne(cpu);
		if (cycles <= 0) break;
		hw_->AddCpuCycles((uint64_t)cycles);
		TickChips((uint64_t)cycles);
		/* sghost PATCH: CALL INIT ($F072) の戻り。INIT 後に $B030 音色を再読し、遅い MA00x overlay でも OPM に載せる。 */
		if (triggered_ && !hw_->psgOnly_ && cpu->r.pc == 0xF05Au)
			hw_->LoadSghostOpmPatches();
	}
}

/* C010/C011 メールボックスへ曲を載せる */
void CDriverX1::TriggerSong()
{
	if (!hw_) return;
	hw_->TriggerPlay(titleCode_ ? titleCode_ : (unsigned)songCode_);
	triggered_ = 1;
}

/* ROM 読込、BGM を先載せ、PATCH ポーリングまでブート */
int CDriverX1::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardX1*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 4000000;
	opmHz_ = hw_->opmHz_ > 0 ? hw_->opmHz_ : 4000000;
	ayHz_ = hw_->ayHz_ > 0 ? hw_->ayHz_ : 2000000;
	opmResidual_ = 0;
	ayResidual_ = 0;
	cpuAcc_ = 0;
	booted_ = 0;
	triggered_ = 0;
	cpuDebt_ = 0;
	timerIrqs_ = 0;
	vsyncIrqs_ = 0;

	/* titleCode 0 は hoot の「メインテーマ」。欠番扱いしない */
	titleCode_ = titleCode;
	{
		uint8_t song = 0, bank = 0;
		CHardX1::UnpackTitle(titleCode_, &song, &bank, hw_->ydosRom_);
		(void)bank;
		songCode_ = song;
	}
	if (ge->titleCount == 0 && titleCode_ == 0)
		titleCode_ = 0; /* カタログは曲 0 を使うことがある — 0x1b を捏造しない */

	if (!hw_->LoadRoms(fs, ge, titleCode_))
		return 0;

	CEmuHardX1SetActive(hw_);
	/* ブート前に BGM を載せる。DRIVER 初期化（gaia/hayato CALL DRV）が mdata のヘッダを歩ける。
	   再生メールボックスは TriggerSong まで空。 */
	hw_->PrestageBgm(titleCode_);
	/* PATCH ポーリングまでブート（CALL 040D CTC/IM2 の後）。Play 受付に CTC tick 約 0.5s が要るドライバがある。
	   CTC は LoadRoms の CtcReset で hoot 互換プリセットになり、最初の CtcRun で今の時刻から数え始める。 */
	{
		Ay_Cpu* cpu = hw_->Cpu();
		if (cpu)
			RunUntil((uint64_t)cpu->time64() + (uint64_t)cpuHz_); /* 約 1.0s ブート */
	}
	booted_ = 1;
	hw_->CaptureBoot();
	if (!loadOnly)
		TriggerSong();
	else
		triggered_ = 1;
	return 1;
}

/* ハード参照を捨てる */
void CDriverX1::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
}

/* 同一 zip の別曲。トグル行は曲を変えずフラグだけ載せる。 */
int CDriverX1::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	if (hw_->PulseCatalogToggle(titleCode))
		return 1;
	/* 別系統が CTC とベクタを書き換えたまま Level へ戻すとテンポが倍になる。
	   ブート直後へ戻してから、その曲の再生コマンドだけ通す。 */
	hw_->RestoreBoot();
	titleCode_ = titleCode;
	songCode_ = (uint8_t)(titleCode & 0xff);
	const uint8_t saveSkip = hw_->skipTriggerStage_;
	if (overlayMix)
		hw_->skipTriggerStage_ = 1;
	hw_->TriggerPlay(titleCode_ ? titleCode_ : (unsigned)songCode_);
	if (overlayMix)
		hw_->skipTriggerStage_ = saveSkip;
	triggered_ = 1;
	return 1;
}

/* CPU＋チップを進めステレオ合成 */
int CDriverX1::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	Ay_Cpu* cpu = hw_->Cpu();
	if (!cpu || hostRate_ < 1 || cpuHz_ < 1) return 0;
	CEmuHardX1SetActive(hw_);

	for (int i = 0; i < frames; i++) {
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		/* RunUntil は期限を跨いだ命令を終えるので超過は負債として持ち越す。捨てると Z80 が約 4% 速く、CTC 同期の X1 テンポが同じだけ上がる。 */
		cpuDebt_ += cyclesPerSample;
		if (cpuDebt_ > 0) {
			const uint64_t start = (uint64_t)cpu->time64();
			RunUntil(start + (uint64_t)cpuDebt_);
			cpuDebt_ -= (int64_t)((uint64_t)cpu->time64() - start);
		}

		int16_t opmBuf[2] = { 0, 0 };
		int16_t ayBuf[2] = { 0, 0 };
		if (hw_->SoundChip())
			hw_->SoundChip()->Render(opmBuf, 1);
		if (hw_->ChipAy())
			hw_->ChipAy()->Render(ayBuf, 1);
		int32_t l = (int32_t)opmBuf[0] + (int32_t)ayBuf[0];
		int32_t r = (int32_t)opmBuf[1] + (int32_t)ayBuf[1];
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
int CDriverX1::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
