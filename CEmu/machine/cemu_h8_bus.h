#pragma once
#include "cemu_hard_ac.h"
struct H8Cpu;

/* Namco C352 用 H8 バス: アクティブ AC ボードを 1 枚だけ保持 */
void CEmuH8BusSetAc(CHardAc* hw);
CHardAc* CEmuH8BusGetAc();
/* H8 コアへ 8bit R/W callback を接続 */
void CEmuH8BusAttach(H8Cpu* cpu, CHardAc* hw);
