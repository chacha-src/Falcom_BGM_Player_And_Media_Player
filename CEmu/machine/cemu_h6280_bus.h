#pragma once

struct H6280Cpu;
class CHardAc;

/* Data East HuC6280 バス: アクティブ DECO ボードは 1 枚 */
void CEmuH6280BusSetDeco(CHardAc* hw);
CHardAc* CEmuH6280BusGetDeco();
/* HuC6280 コアへ 8bit R/W callback を接続 */
void CEmuH6280BusAttach(H6280Cpu* cpu, CHardAc* hw);
