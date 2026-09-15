#pragma once
#include <stdint.h>

class CHardX68k;

/* X68k 音源再ホスト用の Human68k 風 DOS/IOCS（$F08xxx）。
   LINE-F は約 0x400B（cmp 連鎖＋リーフ）が要る。TRAP15 は重ねてはいけない
   （旧 $F08100 がリーフを壊し ambi が死んだ）。スタックは SP=$F0FFFE の下
   （$F0C000..$F0FFFE）。DOS MALLOC をその 14KB 穴に置かない: OPMDRV.X の
   M_ALLOC はトラックバッファ $103FF（約 64KB）を要求し、そこで必ず失敗して
   KOEI 級 MML が空になった。ヒープは $A00000 の追加 RAM
   （カタログ ROM は $500000..$BFFFFF に載らない）。

   配置:
     $F08000  LINE-F DOS（約 1KB）
     $F08400  TRAP #15 IOCS
     $F08580  IOCS 本体（OPMSET/OPMINTST/FEFUNC/B_INTVCS）
     $F08600  TRAP #3 ZMUSIC
     $F08700  IRQ6 トランポリン
     $F08720  ソフト $10C
     $F08740  TRAP#1 rte / IOCS ok rts
     $F08800  DOS データ / PSP
     $A00000  ヒープ … $A40000

   薄いときだけ Install。rich-BOOT の植込はしない。 */

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
	CEMU_X68K_DOS_HEAP = 0x00A00000u,
	CEMU_X68K_DOS_HEAP_END = 0x00A40000u
};

int CEmuX68kDosInstall(CHardX68k* hw);
void CEmuX68kHookFloat2(CHardX68k* hw);
