#include "StdAfx.h"
#include "cemu_driver_pico.h"
#include "../chip/cemu_chip_sn76489.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>
#include <stdlib.h>

CDriverPico::CDriverPico()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(7670454)
	, cpuAcc_(0)
	, irqAcc_(0)
	, booted_(0)
	, songCmd_(0x81)
{
}

CDriverPico::~CDriverPico()
{
	Close();
}

void CDriverPico::UnmaskIfStuck()
{
	if (!hw_ || !hw_->MusashiReady()) return;
	const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR);
	const unsigned ipl = (sr >> 8) & 7u;
	if (ipl >= 6u)
		m68k_set_reg(M68K_REG_SR, (sr | 0x2000u) & ~0x0700u);
}

void CDriverPico::RunCycles(int cycles)
{
	if (!hw_ || !hw_->MusashiReady() || cycles <= 0) return;
	CEmuHardPicoSetActive(hw_);
	const int vblank = cpuHz_ / 60;
	while (cycles > 0) {
		int slice = cycles;
		if (slice > 4000) slice = 4000;
		const int ran = m68k_execute(slice);
		const int used = (ran > 0) ? ran : slice;
		irqAcc_ += used;
		hw_->TickPcm(used);
		if (irqAcc_ >= vblank) {
			irqAcc_ -= vblank;
			hw_->PulseVint();
			if (ran < 64)
				UnmaskIfStuck();
		}
		cycles -= used;
	}
}

void CDriverPico::TriggerSong()
{
	if (!hw_) return;
	const unsigned box = hw_->MailboxAddr();
	const unsigned ram = hw_->SmpsRamBase();
	if (box >= 2u) {
		const uint8_t f = (uint8_t)(hw_->Read8(box - 2u) & 0x9F);
		hw_->Write8(box - 2u, f);
	}
	if (box)
		hw_->Write8(box, songCmd_);
	if (ram)
		hw_->Write8((ram + 9u) & 0xFFFFFFu, songCmd_);
	/* 旧 SMPS は FFxx09 固定も見る */
	hw_->Write8(0xFF0009u, songCmd_);
	hw_->Write8(0xFFF009u, songCmd_);
	hw_->PulseVint();
}

int CDriverPico::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs || hw->hardKind != CHard::KIND_PICO) return 0;
	hw_ = (CHardPico*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 7670454;
	cpuAcc_ = 0;
	irqAcc_ = 0;
	booted_ = 0;
	songCmd_ = 0x81;
	if (titleCode && (titleCode & 0xff) != 0)
		songCmd_ = (uint8_t)(titleCode & 0xff);
	else if (ge->titleCount > 0 && ge->title[0].code)
		songCmd_ = (uint8_t)(ge->title[0].code & 0xff);

	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;
	CEmuHardPicoSetActive(hw_);
	/* VDP 初期化と SMPS コピー待ち */
	RunCycles(cpuHz_);
	UnmaskIfStuck();
	TriggerSong();
	booted_ = 1;
	return 1;
}

void CDriverPico::Close()
{
	hw_ = NULL;
	booted_ = 0;
}

int CDriverPico::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	songCmd_ = (uint8_t)(titleCode & 0xff);
	if (!songCmd_)
		songCmd_ = 0x81;
	TriggerSong();
	return 1;
}

int CDriverPico::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	CChip* chip = hw_->SoundChip();
	if (!chip || hostRate_ < 1 || cpuHz_ < 1) return 0;
	CEmuHardPicoSetActive(hw_);
	for (int i = 0; i < frames; i++) {
		cpuAcc_ += cpuHz_;
		const int cyc = (int)(cpuAcc_ / hostRate_);
		cpuAcc_ -= (int64_t)cyc * hostRate_;
		if (cyc > 0)
			RunCycles(cyc);
		int16_t tmp[2];
		chip->Render(tmp, 1);
		stereo[i * 2] = tmp[0];
		stereo[i * 2 + 1] = tmp[1];
	}
	return frames;
}

int CDriverPico::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
