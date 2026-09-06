#pragma once
/* Shared Musashi memory callbacks — one live 68K board (X68k, F3 or the
   Jaleco Mega System 1 sound board). */
class CHardX68k;
class CHardF3;
class CHardAc;

void CEmuM68kBusSetX68k(CHardX68k* hw);
void CEmuM68kBusSetF3(CHardF3* hw);
void CEmuM68kBusSetMs1(CHardAc* hw);
CHardX68k* CEmuM68kBusGetX68k();
CHardF3* CEmuM68kBusGetF3();
CHardAc* CEmuM68kBusGetMs1();
