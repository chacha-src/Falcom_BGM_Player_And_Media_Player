#pragma once
class CHardAc;
struct M37702Cpu;

/* Namco Sys11/NA1 用 M37702 バス: アクティブ AC は 1 枚 */
void CEmuM37702BusSetAc(CHardAc* hw);
CHardAc* CEmuM37702BusGetAc();
/* M37702 コアへ 8bit R/W callback を接続 */
void CEmuM37702BusAttach(M37702Cpu* cpu, CHardAc* hw);
