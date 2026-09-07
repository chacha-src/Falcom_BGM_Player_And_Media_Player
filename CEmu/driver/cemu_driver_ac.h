#pragma once
#include "cemu_driver.h"
#include "../machine/cemu_hard_ac.h"

class CDriverAc : public CDriver {
public:
	CDriverAc();
	~CDriverAc() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;

	unsigned OpmWrites() const;

private:
	CHardAc* hw_;
	int hostRate_;
	int cpuHz_;
	int opmHz_;
	int booted_;
	int triggered_;
	int pinned_; /* playlist/catalog title — inject once, do not hunt try table */
	uint8_t songCmd_;
	uint16_t songCmdWord_; /* full title code for boards with 16-bit commands */
	uint64_t opmResidual_;
	int64_t cpuAcc_;
	int cmdIndex_;
	uint64_t nextCmdAt_;
	uint64_t nextGngIrq_;
	/* Last sampled K054539 timer output, for rising-edge NMI generation. */
	int k054539TimerState_;
	uint64_t k054539Residual_;
	uint64_t nextM72Nmi_;  /* MASTER_CLOCK/8/512 = 7812.5 Hz sample pump */
	int m72FakeNmi_;       /* game has an empty NMI handler — pump host-side */
	int hasCpu_;           /* 0 for boards whose sound CPU CEmu cannot run */
	int ms1_;              /* Mega System 1 / System GX: Musashi 68000 path */
	int64_t ms1Acc_;
	int m92_;              /* Irem M92: NEC V35 path */
	int64_t m92Acc_;
	uint64_t m92OpmRes_;   /* YM2151 runs at the V35 clock / 4 */
	int deco_;             /* Data East: HuC6280 path */
	int64_t decoAcc_;
	uint64_t decoChipRes_; /* residual for YM2151 clock ratio */
	uint64_t decoNextYmIrq_; /* rate-limit HuC6280 IRQ2 pulses */
	int h8Board_;          /* Namco C352: H8/3002 path */
	int64_t h8Acc_;
	int m37702Board_;      /* Namco Sys11/NA1: M37702 path */
	int64_t m37702Acc_;
	int namcoM6809_;       /* Namco Sys1/2: mc6809 path */
	int64_t namcoAcc_;
	int sys86_;             /* Namco Sys86: HD63701 path */
	int64_t sys86Acc_;
	int m62_;              /* Irem M62: M6803 path */
	int64_t m62Acc_;
	int sega68_;           /* Model1 MultiPCM / Model2 SCSP: 68000 path */
	int64_t sega68Acc_;
	int16_t* scratch_;     /* aux-chip mix buffer (dual SN / triple AY) */
	int scratchFrames_;
	int heard_;            /* any non-zero sample since the last command */

	void RunUntil(uint64_t endCycle);
	void Ms1RunCycles(int cycles);
	int Ms1Render(int16_t* stereo, int frames);
	void M92RunCycles(int cycles);
	int M92Render(int16_t* stereo, int frames);
	void DecoRunCycles(int cycles);
	int DecoRender(int16_t* stereo, int frames);
	void H8RunCycles(int cycles);
	int H8Render(int16_t* stereo, int frames);
	void M37702RunCycles(int cycles);
	int M37702Render(int16_t* stereo, int frames);
	void NamcoM6809RunCycles(int cycles);
	int NamcoM6809Render(int16_t* stereo, int frames);
	void Sys86RunCycles(int cycles);
	int Sys86Render(int16_t* stereo, int frames);
	void M62RunCycles(int cycles);
	int M62Render(int16_t* stereo, int frames);
	void Sega68RunCycles(int cycles);
	int Sega68Render(int16_t* stereo, int frames);
	void TickOpm(uint64_t cpuCycles);
	void DeliverIrqs();
	void TryInjectCommand();
	uint16_t m92NoteOffSeen_;
	uint16_t m92ChannelPlayOff_; /* channel-BGM ptr after FB/F6/F7 skip */
	int m92NoteStuck_;
	int16_t* Scratch(int frames);
};
