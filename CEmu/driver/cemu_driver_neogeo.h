#pragma once

#include "cemu_driver.h"

#include "../machine/cemu_hard_neogeo.h"



/* Neo Geo AES/MVS: Z80 + YM2610。NMI でコマンド、IM1 でタイマ */
class CDriverNeo : public CDriver {

public:

	CDriverNeo();

	~CDriverNeo() override;



	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;

	void Close() override;

	int Render(int16_t* stereo, int frames) override;

	int Seek(uint64_t sample) override;
	int OverlayTitle(unsigned titleCode) override;



private:

	CHardNeo* hw_;

	int hostRate_;

	int cpuHz_;

	int ymHz_;

	uint8_t songCmd_;

	uint8_t songCmdHi_;

	uint64_t ymResidual_;

	int64_t cpuAcc_;

	int injected_;

	uint64_t injectAt_;

	int reinjected_;

	/* YM2610 のタイマ満了。まだ IM1 として届けていない */
	int ymIrqPending_;

	/* 使い残した CPU サイクル。超過が積み上がらないよう持ち越す */
	int64_t cpuDebt_;



	void RunUntil(uint64_t endCycle);

	void TickYm(uint64_t cpuCycles);

	/* YM タイマ満了を IM1、コマンドを NMI で届ける */
	void DeliverIrqs();

	void InjectSongCommand();

	void WaitQueueIdle(int maxFrames60);

	void SendZ80Command(uint8_t cmd, int maxFrames60);

};



/* Neo Geo ドライバ生成 */
CDriver* CDriverNeoCreate();


