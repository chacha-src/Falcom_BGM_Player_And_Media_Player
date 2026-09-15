#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_pc88.h"

/* 停滞ウォッチドッグの再キック回数。ドライバ自身のループと強制ループを
   プローブが区別する用。同時に 1 曲しか描かないのでグローバル。 */
unsigned CEmuPc88WatchdogReplays();
void CEmuPc88WatchdogResetCount();
void CEmuPc88WatchdogSetEnabled(int on);

/* 直近の再生キックが拾ったオープニング音声と、先頭の初期無音を切った
   フレーム数。プローブ専用。 */
unsigned CEmuPc88LeadFrames();
unsigned CEmuPc88LeadTrimmedFrames();

/* PC-88: Z80 + YM2203/YM2608。RTC/VRTC と OPN タイマで IRQ */
class CDriverPc88 : public CDriver {
public:
	CDriverPc88();
	~CDriverPc88() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;
	int OverlayTitle(unsigned titleCode) override;

private:
	CHardPc88* hw_;
	int sampleRate_;
	int hostRate_;
	int cpuHz_;
	int opnHz_;
	int booted_;
	int triggered_;
	int forceEiBoot_; /* Wing MCM1/DRIVER1 初期化パルス中は 1 */
	uint64_t nextRtc_;
	uint64_t nextVrtc_;
	uint64_t rtcPeriod_;
	uint64_t vrtcPeriod_;
	uint64_t opnResidual_;
	int64_t cpuAcc_; /* cpuHz/hostRate の端数アキュムレータ */
	int64_t cpuCycleBudget_; /* 直前命令の余り。約 +6% テンポを防ぐ */
	/* リードイン: TriggerPlay が PATCH コマンドを抜く間に OPN が出した
	   曲頭。BeginLeadCapture 参照。 */
	int16_t* lead_;
	int leadCap_;   /* int16_t スロット */
	int leadLen_;   /* 使用中スロット */
	int leadPos_;   /* 読出カーソル */
	int capturing_;
	int64_t capAcc_; /* キャプチャ歩進用 cycles*hostRate */
	/* 停滞ウォッチドッグ: リップを再生・ループさせる（WatchdogTick） */
	uint64_t wdSamples_;
	uint64_t wdLastActive_;
	unsigned wdMotion_;
	unsigned wdTimerFires_;
	unsigned wdReplays_;
	int wdEverActive_;
	int wdArmedTick_;
	int replayPending_;

	void RunUntil(uint64_t endCycle);
	/* RTC/VRTC/OPN タイマ IRQ を届ける */
	void DeliverIrqs(uint64_t now);
	void TickOpn(uint64_t cpuCycles);
	/* PATCH のコマンド待ちループ PC */
	int FindPollLoop() const;
	/* HALT/DI で固まったら起こす */
	void Unwedge();
	void TriggerPlay();
	void WatchdogTick();
	void BeginLeadCapture();
	void CaptureLead(uint64_t cpuCycles);
	void EndLeadCapture();
	int DrainLead(int16_t* stereo, int frames);
};
