#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_pico.h"

/* Sega Pico: 68000 + SN76489。SMPS メールボックスへ曲コードを書く。 */
class CDriverPico : public CDriver {
public:
	CDriverPico();
	~CDriverPico() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;
	int OverlayTitle(unsigned titleCode) override;

private:
	CHardPico* hw_;
	int hostRate_;
	int cpuHz_;
	int64_t cpuAcc_;
	int irqAcc_;
	int booted_;
	uint8_t songCmd_;

	void RunCycles(int cycles);
	void TriggerSong();
	void UnmaskIfStuck();
};
