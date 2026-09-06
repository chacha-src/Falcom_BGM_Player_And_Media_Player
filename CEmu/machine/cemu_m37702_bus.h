#pragma once
struct CHardAc;
struct M37702Cpu;

void CEmuM37702BusSetAc(CHardAc* hw);
CHardAc* CEmuM37702BusGetAc();
void CEmuM37702BusAttach(M37702Cpu* cpu, CHardAc* hw);
