#pragma once
#include "cemu_hard_ac.h"
struct HD63701Cpu;

void CEmuHD63701BusSetAc(CHardAc* hw);
CHardAc* CEmuHD63701BusGetAc();
void CEmuHD63701BusAttach(HD63701Cpu* cpu, CHardAc* hw);
