#pragma once

class CHardAc;
struct V35Cpu;

/* Irem M92 sound board bus for the shared V35 core, mirroring the split
   cemu_m68k_bus.cpp uses for Musashi: exactly one board is active, and the
   core sees only the four callbacks installed here. */
void CEmuV35BusSetM92(CHardAc* hw);
CHardAc* CEmuV35BusGetM92();
/* Installs the callbacks above on `cpu` with `hw` as the context. */
void CEmuV35BusAttach(V35Cpu* cpu, CHardAc* hw);
