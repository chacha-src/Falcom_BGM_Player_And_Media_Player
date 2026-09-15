#pragma once
#include "../machine/cemu_hard_x68k.h"
#include "cemu_driver.h"

/* X68000: 68000 + YM2151 @$E90001 + 曲メールボックス @$E00000 */
class CDriverX68k : public CDriver {
public:
	CDriverX68k();
	~CDriverX68k() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;
	int OverlayTitle(unsigned titleCode) override;

	unsigned OpmWrites() const;
	/* 現在 PC（診断） */
	unsigned Pc() const;

private:
	CHardX68k* hw_;
	int hostRate_;
	int cpuHz_;
	int opmHz_;
	int booted_;
	uint64_t opmResidual_;
	int64_t cpuAcc_;
	uint64_t nextCmdAt_;
	int cmdIndex_;
	unsigned songCode_;
	unsigned bestSongCode_;
	int bestPeak_;
	int windowPeak_;
	int dwellFrames_;
	int dwellLeft_;
	unsigned tryCodes_[128];
	int tryCount_;
	int irqWas_;
	int locked_; /* 可聴ロック — メールボックス再発行を止める */
	int pinned_; /* プレイリスト titleCode — 他曲を探さない */
	unsigned opmAtWindow_; /* dwell 開始時の OPM 書込数 */
	int dwellExtendUsed_; /* OPM 多いが無音のとき一度だけ延長 */
	int64_t timerDAcc_;
	int64_t vdispAcc_;
	int softTimerBusy_;
	int opmSpinRescue_; /* 初期化後 $94A → メールボックスの一回救済 */

	void TickOpm(uint64_t cpuCycles);
	void RunCycles(int cycles);
	/* ソフトタイマ（MFP 非武装時の $10C 相当） */
	void ServiceSoftTimers(int cycles);
	void CallUserHook(unsigned hook, int tickOpmDuring = 1);
	void CallUserSubroutine(unsigned hook);
	/* BOOT の tst.b $E00000 ポーリングを探す。無音コードで PC が
	   メールボックス外に残ったとき再開する（aquales INTRO）。 */
	unsigned FindMailboxPoll() const;
	void ResumeMailboxForSong(unsigned code);
};
