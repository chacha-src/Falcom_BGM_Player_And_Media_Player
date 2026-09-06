#pragma once
#include "cemu_hard_ac.h"
struct H8Cpu;

void CEmuH8BusSetAc(CHardAc* hw);
CHardAc* CEmuH8BusGetAc();
void CEmuH8BusAttach(H8Cpu* cpu, CHardAc* hw);
