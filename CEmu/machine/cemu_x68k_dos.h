#pragma once
#include <stdint.h>

class CHardX68k;

/* Human68k-ish DOS/IOCS for X68k sound rehost in $F08xxx.
   LINE-F needs ~0x400B (cmp chain + leaves); TRAP15 must NOT overlap it
   (old $F08100 smashed leaves and broke ambi). HEAP_END $F0C000 → ≥16KB
   stack under SP=$F0FFFE.

   Layout:
     $F08000  LINE-F DOS (~1KB)
     $F08400  TRAP #15 IOCS
     $F08580  IOCS bodies (OPMSET/OPMINTST/FEFUNC/B_INTVCS)
     $F08600  TRAP #3 ZMUSIC
     $F08700  IRQ6 trampoline
     $F08720  soft $10C
     $F08740  TRAP#1 rte / IOCS ok rts
     $F08800  DOS data / PSP
     $F08900  heap … $F0C000

   Install when thin. No rich-BOOT plants. */

enum {
	CEMU_X68K_DOS_BASE = 0x00F08000u,
	CEMU_X68K_DOS_LINEF = 0x00F08000u,
	CEMU_X68K_DOS_TRAP15 = 0x00F08400u,
	CEMU_X68K_DOS_OPMSET = 0x00F08580u,
	CEMU_X68K_DOS_OPMINTST = 0x00F08590u,
	CEMU_X68K_DOS_FEFUNC = 0x00F085A2u,
	CEMU_X68K_DOS_B_INTVCS = 0x00F085A6u,
	CEMU_X68K_DOS_TRAP3 = 0x00F08600u,
	CEMU_X68K_DOS_IRQ6 = 0x00F08700u,
	CEMU_X68K_DOS_SOFT10C = 0x00F08720u,
	CEMU_X68K_DOS_TRAP1 = 0x00F08740u,
	CEMU_X68K_DOS_IOCS_OK = 0x00F08744u,
	CEMU_X68K_DOS_DATA = 0x00F08800u,
	CEMU_X68K_DOS_HEAP = 0x00F08900u,
	CEMU_X68K_DOS_HEAP_END = 0x00F0C000u
};

int CEmuX68kDosInstall(CHardX68k* hw);
