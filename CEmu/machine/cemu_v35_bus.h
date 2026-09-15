#pragma once

class CHardAc;
struct V35Cpu;

/* Irem M92 音源の共有 V35 バス。Musashi 用 cemu_m68k_bus と同様、
   アクティブは 1 ボード。コアが見るのはここに繋いだ 4 callback のみ。 */
void CEmuV35BusSetM92(CHardAc* hw);
CHardAc* CEmuV35BusGetM92();
/* cpu へ上記 callback を接続。ctx は hw。 */
void CEmuV35BusAttach(V35Cpu* cpu, CHardAc* hw);
