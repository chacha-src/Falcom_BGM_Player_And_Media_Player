#pragma once

struct H6280Cpu;
class CHardAc;

void CEmuH6280BusSetDeco(CHardAc* hw);
CHardAc* CEmuH6280BusGetDeco();
void CEmuH6280BusAttach(H6280Cpu* cpu, CHardAc* hw);
