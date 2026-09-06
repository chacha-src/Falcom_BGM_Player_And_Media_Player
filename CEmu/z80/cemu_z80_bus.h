#pragma once
#include "../machine/cemu_hard.h"

/* Shared Z80 port/mem bus — Ay_Cpu in/out/read/write dispatch to active hard. */
void CEmuZ80BusSetActive(CHard* hw);
CHard* CEmuZ80BusGetActive();
