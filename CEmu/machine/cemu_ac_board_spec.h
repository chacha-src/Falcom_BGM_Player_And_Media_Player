#pragma once
/* Fixed arcade sound-board specifications.
   Catalog XML supplies ROM names/offsets and title codes; this table supplies
   the hardware constants (CPU, regions, chips, IRQ lines, latch). */

#include "cemu_hard_ac.h"

struct CEmuGameEntry;

enum CEmuAcCpuKind {
	CEMU_AC_CPU_NONE = 0,
	CEMU_AC_CPU_Z80,
	CEMU_AC_CPU_V35,
	CEMU_AC_CPU_M68000,
	CEMU_AC_CPU_H6280,
	CEMU_AC_CPU_M6502,
	CEMU_AC_CPU_M6809,
	CEMU_AC_CPU_HD63701,
	CEMU_AC_CPU_H8,
	CEMU_AC_CPU_M6803,
	CEMU_AC_CPU_M37702
};

enum CEmuAcIrqKind {
	CEMU_AC_IRQ_NONE = 0,
	CEMU_AC_IRQ_Z80_IM1,
	CEMU_AC_IRQ_Z80_NMI,
	CEMU_AC_IRQ_V35_INTP0,
	CEMU_AC_IRQ_V35_INTP1,
	CEMU_AC_IRQ_M6809_FIRQ,
	CEMU_AC_IRQ_M6809_IRQ
};

enum CEmuAcChipKind {
	CEMU_AC_CHIP_NONE = 0,
	CEMU_AC_CHIP_YM2151,
	CEMU_AC_CHIP_YM2203,
	CEMU_AC_CHIP_YM2610,
	CEMU_AC_CHIP_YM2612,
	CEMU_AC_CHIP_YM3812,
	CEMU_AC_CHIP_OKI6295,
	CEMU_AC_CHIP_QSOUND,
	CEMU_AC_CHIP_C352,
	CEMU_AC_CHIP_C140,
	CEMU_AC_CHIP_C30,
	CEMU_AC_CHIP_SEGAPCM,
	CEMU_AC_CHIP_GA20,
	CEMU_AC_CHIP_SN76489,
	CEMU_AC_CHIP_AY8910,
	CEMU_AC_CHIP_K054539,
	CEMU_AC_CHIP_RF5C68,
	CEMU_AC_CHIP_RF5C400,
	CEMU_AC_CHIP_SCSP
};

struct CEmuAcBoardSpec {
	CEmuAcBoard board;
	const char* name;
	CEmuAcCpuKind cpu;
	int cpuHz;
	unsigned codeRomBytes;
	unsigned workRamBytes;
	unsigned latchAddr;
	CEmuAcIrqKind chipIrq;
	CEmuAcIrqKind latchIrq;
	CEmuAcChipKind chip0;
	unsigned chip0Addr;
	int chip0Hz;
	CEmuAcChipKind chip1;
	unsigned chip1Addr;
	int chip1Hz;
	unsigned cmdAdd;
	const char* mameRef;
};

const CEmuAcBoardSpec* CEmuAcBoardSpecById(CEmuAcBoard board);
CEmuAcBoard CEmuAcResolveBoard(const CEmuGameEntry* ge);
