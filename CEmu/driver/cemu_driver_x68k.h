#pragma once
#include "../machine/cemu_hard_x68k.h"
#include "cemu_driver.h"

class CDriverX68k : public CDriver {
public:
	CDriverX68k();
	~CDriverX68k() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;

	unsigned OpmWrites() const;
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
	int locked_; /* audible lock — stop mailbox re-issue */
	int pinned_; /* playlist titleCode — do not hunt other songs */
	unsigned opmAtWindow_; /* OPM write count at dwell window start */
	int dwellExtendUsed_; /* one-shot extend when high OPM but silent */
	int64_t timerDAcc_;
	int64_t vdispAcc_;
	int softTimerBusy_;

	void TickOpm(uint64_t cpuCycles);
	void RunCycles(int cycles);
	void ServiceSoftTimers(int cycles);
	void CallUserHook(unsigned hook);
	void CallUserSubroutine(unsigned hook);
	/* Find BOOT tst.b $E00000 poll; resume there when hunting songs after a
	   dead/silent code left PC stuck outside the mailbox (aquales INTRO). */
	unsigned FindMailboxPoll() const;
	void ResumeMailboxForSong(unsigned code);
};
