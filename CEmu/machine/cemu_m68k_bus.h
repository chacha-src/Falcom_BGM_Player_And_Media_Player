#pragma once
/* Musashi 共有メモリ callback。68K ボードは同時に 1 枚だけ
   （X68k / F3 / Jaleco Mega System 1 音源）。 */
class CHardX68k;
class CHardF3;
class CHardAc;

/* アクティブ 68K ボードを切替（他は NULL 化） */
void CEmuM68kBusSetX68k(CHardX68k* hw);
void CEmuM68kBusSetF3(CHardF3* hw);
void CEmuM68kBusSetMs1(CHardAc* hw);
/* 現在のアクティブ ボード */
CHardX68k* CEmuM68kBusGetX68k();
CHardF3* CEmuM68kBusGetF3();
CHardAc* CEmuM68kBusGetMs1();
