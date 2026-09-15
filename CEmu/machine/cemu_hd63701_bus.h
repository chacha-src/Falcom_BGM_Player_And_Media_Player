#pragma once
#include "cemu_hard_ac.h"
struct HD63701Cpu;

/* Namco Sys86 用 HD63701 バス: アクティブ AC は 1 枚 */
void CEmuHD63701BusSetAc(CHardAc* hw);
CHardAc* CEmuHD63701BusGetAc();
/* HD63701 コアへメモリ／ポート callback を接続 */
void CEmuHD63701BusAttach(HD63701Cpu* cpu, CHardAc* hw);
