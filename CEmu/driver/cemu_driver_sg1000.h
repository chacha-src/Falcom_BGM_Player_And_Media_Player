#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_sg1000.h"

/* SG-1000/SC-3000: Z80 + SN76489。BIT7 メールボックスで曲を叩く */
class CDriverSg1000 : public CDriver {
public:
	CDriverSg1000();
	~CDriverSg1000() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;
	int OverlayTitle(unsigned titleCode) override;

	/* PSG 書込回数（診断） */
	unsigned PsgWrites() const;
	/* 糊が無音ならホストがトーンを強制 */
	int ToneFallback() const { return toneFallback_; }

private:
	CHardSg1000* hw_;
	int hostRate_;
	int cpuHz_;
	int psgHz_;
	int booted_;
	int triggered_;
	uint8_t songCmd_;
	uint64_t psgResidual_;
	int64_t cpuAcc_;
	uint64_t nextTickAt_;
	int toneFallback_;
	int knownTick_;

	void RunUntil(uint64_t endCycle);
	/* PSG クロックを CPU 比で進める */
	void TickPsg(uint64_t cpuCycles);
	/* HALT 番兵付きで Z80 サブルーチンを呼ぶ */
	void CallZ80(uint16_t targetPc);
	/* mute → メールボックス poke → update tick */
	void TriggerSong();
	/* PSG が無音なら Tone0 を強制 */
	void ForceToneTest();
};
