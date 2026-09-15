#include "StdAfx.h"
#include "cemu_driver_sg1000.h"
#include "../chip/cemu_chip_sn76489.h"
#include "../z80/Ay_Cpu.h"
#include <string.h>

/* SG-1000: Star Jacker 系メールボックスを既定に */
CDriverSg1000::CDriverSg1000()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(3579545)
	, psgHz_(3579545)
	, booted_(0)
	, triggered_(0)
	, songCmd_(0x8d)
	, psgResidual_(0)
	, cpuAcc_(0)
	, nextTickAt_(0)
	, toneFallback_(0)
	, knownTick_(0)
{
}

/* 後始末 */
CDriverSg1000::~CDriverSg1000()
{
	Close();
}

/* SN76489 書込回数 */
unsigned CDriverSg1000::PsgWrites() const
{
	return hw_ ? hw_->psgWrites_ : 0;
}

/* PSG クロックを CPU 比で進める */
void CDriverSg1000::TickPsg(uint64_t cpuCycles)
{
	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0) return;
	psgResidual_ += cpuCycles * (uint64_t)psgHz_;
	const uint64_t ticks = psgResidual_ / (uint64_t)cpuHz_;
	psgResidual_ %= (uint64_t)cpuHz_;
	if (ticks)
		hw_->SoundChip()->AdvanceClocks(ticks);
}

/* Z80 を endCycle まで進める。HALT でループを抜ける */
void CDriverSg1000::RunUntil(uint64_t endCycle)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	CEmuHardSg1000SetActive(hw_);
	int guard = 0;
	while ((uint64_t)cpu->time64() < endCycle && guard++ < 2000000) {
		const int cycles = Ay_CpuRunOne(cpu);
		if (cycles <= 0) break;
		hw_->AddCpuCycles((uint64_t)cycles);
		TickPsg((uint64_t)cycles);
		/* HALT: 時刻を進めてアイドルループを抜ける */
		if (cpu->get_mem() && cpu->get_mem()[cpu->r.pc] == 0x76) {
			cpu->adjust_time(4);
			hw_->AddCpuCycles(4);
			TickPsg(4);
			break;
		}
	}
}

/* HALT 番兵を積んで Z80 サブルーチンを呼ぶ（0.5s 上限） */
void CDriverSg1000::CallZ80(uint16_t targetPc)
{
	if (!hw_ || !hw_->Cpu()) return;
	Ay_Cpu* cpu = hw_->Cpu();
	uint8_t* mem = hw_->Mem();
	if (!mem) return;
	CEmuHardSg1000SetActive(hw_);

	const uint16_t ret = 0xFF80;
	mem[ret] = 0x76; /* HALT 番兵 */
	uint16_t sp = cpu->r.sp;
	sp -= 2;
	mem[sp] = (uint8_t)(ret & 0xff);
	mem[(uint16_t)(sp + 1)] = (uint8_t)(ret >> 8);
	cpu->r.sp = sp;
	cpu->r.pc = targetPc;

	const uint64_t start = (uint64_t)cpu->time64();
	const uint64_t limit = start + (uint64_t)cpuHz_ / 2; /* 0.5s 上限 */
	int guard = 0;
	while (guard++ < 2000000) {
		if (cpu->r.pc == ret)
			break;
		if (mem[cpu->r.pc] == 0x76 && cpu->r.pc == ret)
			break;
		if ((uint64_t)cpu->time64() >= limit)
			break;
		const int cycles = Ay_CpuRunOne(cpu);
		if (cycles <= 0) break;
		hw_->AddCpuCycles((uint64_t)cycles);
		TickPsg((uint64_t)cycles);
		if (mem[cpu->r.pc] == 0x76) {
			cpu->r.pc = ret;
			break;
		}
	}
	cpu->r.pc = ret;
}

/* 糊が無音なら Tone0 を強制（他チャネル mute） */
void CDriverSg1000::ForceToneTest()
{
	if (!hw_ || !hw_->SoundChip()) return;
	CChip* chip = hw_->SoundChip();
	/* Tone0 周期 ~0x100、音量最大。他は mute */
	chip->Write(0, 0x80 | 0x00 | 0x00); /* Tone0 fine ラッチ */
	chip->Write(0, 0x10);               /* Tone0 粗ピッチ */
	chip->Write(0, 0x90 | 0x00);         /* Tone0 音量 0=最大 */
	chip->Write(0, 0xBF);               /* Tone1 ミュート */
	chip->Write(0, 0xDF);               /* Tone2 ミュート */
	chip->Write(0, 0xFF);               /* ノイズ mute */
	toneFallback_ = 1;
	hw_->psgWrites_ = CEmuChipSn76489WriteCount(chip);
}

/* mute → メールボックス poke → update tick。無音ならトーン強制 */
void CDriverSg1000::TriggerSong()
{
	if (!hw_ || !hw_->Cpu() || !hw_->Mem()) return;
	uint8_t* mem = hw_->Mem();
	const uint16_t box = hw_->mailboxAddr_;
	const uint16_t upd = hw_->soundUpdatePc_;
	const uint16_t mute = hw_->soundMutePc_;

	/* Sega PSG ドライバのワーク RAM をクリア（C000-C3FF は Congo C1E6 と
	   Mikie C300 のメールボックス／チャネルブロック） */
	memset(mem + 0xC000, 0, 0x400);

	/* BIT7 メールボックス糊: mute → コマンド poke → update tick */
	const int haveGlue = (box >= 0xC000 && box <= 0xC3FF
		&& upd > 0 && upd < 0xC000 && mute > 0 && mute < 0xC000
		&& mem[upd] != 0x00);
	if (haveGlue) {
		CallZ80(mute);
		mem[box] = songCmd_;
		if ((songCmd_ & 0x80) == 0)
			mem[box] = (uint8_t)(0x80 | (songCmd_ & 0x7f));
		CallZ80(upd);
		/* 2 回目 tick で Congo/Mikie シーケンサが初期化を抜ける */
		CallZ80(upd);
		knownTick_ = 1;
	}

	triggered_ = 1;
	/* 実 PSG 通信を優先。糊が無音のときだけトーン強制 */
	if (hw_->psgWrites_ == 0)
		ForceToneTest();
}

/* ROM 読込後に短い settle、BIT7 糊で曲を叩く */
int CDriverSg1000::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs) return 0;
	hw_ = (CHardSg1000*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 3579545;
	psgHz_ = hw_->psgHz_ > 0 ? hw_->psgHz_ : 3579545;
	psgResidual_ = 0;
	cpuAcc_ = 0;
	booted_ = 0;
	triggered_ = 0;
	toneFallback_ = 0;
	knownTick_ = 0;

	uint8_t code = 0x0d; /* Star Jacker タイトル BGM */
	if (titleCode && (titleCode & 0xff) != 0)
		code = (uint8_t)(titleCode & 0xff);
	else if (ge->titleCount > 0 && ge->title[0].code)
		code = (uint8_t)(ge->title[0].code & 0xff);
	songCmd_ = (uint8_t)(0x80 | (code & 0x7f));

	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;

	CEmuHardSg1000SetActive(hw_);
	/* リセットベクタが乗る程度に短く settle。ゲーム本体は走らせない */
	RunUntil((uint64_t)cpuHz_ / 120);
	booted_ = 1;
	TriggerSong();
	nextTickAt_ = (uint64_t)hw_->Cpu()->time64() + (uint64_t)cpuHz_ / 60;
	return 1;
}

/* ハード参照を捨てる */
void CDriverSg1000::Close()
{
	hw_ = NULL;
	booted_ = 0;
	triggered_ = 0;
}

/* 同一 zip の別曲をメールボックス経由で切替 */
int CDriverSg1000::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	songCmd_ = (uint8_t)(titleCode & 0xff);
	if (!songCmd_)
		songCmd_ = 1;
	TriggerSong();
	triggered_ = 1;
	return 1;
}

/* 60Hz tick で update を呼び、PSG をサンプル合成 */
int CDriverSg1000::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	Ay_Cpu* cpu = hw_->Cpu();
	CChip* chip = hw_->SoundChip();
	if (!cpu || !chip) return 0;
	CEmuHardSg1000SetActive(hw_);
	if (hostRate_ < 1 || cpuHz_ < 1) return 0;

	for (int i = 0; i < frames; i++) {
		const uint64_t now = (uint64_t)cpu->time64();
		if (!toneFallback_ && knownTick_ && now >= nextTickAt_) {
			if (hw_->soundUpdatePc_)
				CallZ80(hw_->soundUpdatePc_);
			nextTickAt_ = (uint64_t)cpu->time64() + (uint64_t)cpuHz_ / 60;
		}
		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		/* トーン強制: Z80 不要。チップ時刻だけ進める */
		if (toneFallback_) {
			TickPsg((uint64_t)cyclesPerSample);
			cpu->adjust_time(cyclesPerSample);
			hw_->AddCpuCycles((uint64_t)cyclesPerSample);
		} else {
			const uint64_t end = (uint64_t)cpu->time64() + (uint64_t)cyclesPerSample;
			/* tick 間はアイドル。PSG クロックだけ進める */
			const uint64_t cur = (uint64_t)cpu->time64();
			if (end > cur) {
				cpu->adjust_time((int)(end - cur));
				hw_->AddCpuCycles(end - cur);
				TickPsg(end - cur);
			}
		}
		chip->Render(stereo + i * 2, 1);
	}
	return frames;
}

/* Seek は未対応 */
int CDriverSg1000::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
