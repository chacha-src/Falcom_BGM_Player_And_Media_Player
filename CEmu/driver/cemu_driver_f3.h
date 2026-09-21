#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_f3.h"

/* Taito F3: 68000 + ES5505。DPRAM リングへコマンド注入 */
class CDriverF3 : public CDriver {
public:
	CDriverF3();
	~CDriverF3() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;
	int OverlayTitle(unsigned titleCode) override;

private:
	/* Musashi を cycles 進める */
	void RunCycles(int cycles);
	/* 試行テーブルから曲コードを注入 */
	void TryInjectCommand();
	void WakeMailboxIfQueued();
	void KickMailboxOnce();
	void PostTypeE();
	void ArmKeyOnGates();
	void TickSeqHost();
	void RestoreMailboxSleep();
	void ExpireHeadWaitOnce();
	void PunchMediumWaits();
	void SnapChainOnce();
	void RestoreChain();
	unsigned MapSongCode(unsigned code);
	void LogState(const char* tag);

	CHardF3* hw_;
	int hostRate_;
	int cpuHz_;
	int64_t cpuAcc_;
	int booted_;
	unsigned songCode_;
	unsigned tryCodes_[64];
	int tryCount_;
	int cmdIndex_;
	int dwellLeft_;
	int dwellFrames_;
	int bestPeak_;
	int windowPeak_;
	unsigned bestSongCode_;
	int locked_;
	int irqPhase_;
	int kickedMail_;
	unsigned hitIdle_;
	unsigned hitIrq_;
	unsigned hitTask0_;
	unsigned hitMail_;
	unsigned hitPlay_;
	unsigned hitDisp_;
	unsigned hitTick_;
	int seqTickAcc_;
	unsigned seqCalls_;
	unsigned irq6Vec_;
	int delayGated_;
	int expiredHead_;
	unsigned lastHeadWait_;
	unsigned waitDecs_;
	unsigned typeEPosts_;
	int f3Arabianm_;
	unsigned idlePark_;
	unsigned mbDisp_;
	unsigned strm0_;
	int demoRestart_;
	unsigned restartEvery_;
	uint16_t chainSnap_[48];
	int chainSnapN_;
	unsigned chainLoopEvery_;
	unsigned tblOffs_;
	int chainPark_;
};

/* F3 ドライバ生成 */
CDriver* CDriverF3Create();
